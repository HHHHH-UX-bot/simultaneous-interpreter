`timescale 1ns/1ps
// Self-checking testbench for the WM8731 control port:
//   * i2c_master START/byte/ACK/STOP sequencing,
//   * address probe on 0x1A, fallback to 0x1B, and the no-codec error path,
//   * the exact register/value sequence written by wm8731_init, including the
//     master-mode interface value and ACTIVE last.
module tb_i2c_wm8731;
    localparam integer CLK_HZ  = 50000000;
    localparam integer SCL_HZ  = 1000000;      // faster than the board, same logic
    localparam integer QUARTER = CLK_HZ / (4 * SCL_HZ);

    reg clk = 0, reset_n = 0;
    always #10 clk = ~clk;                     // 50 MHz

    reg        trigger = 0;
    wire [8:0] cfg_r0 = 9'h017, cfg_r1 = 9'h017, cfg_r2 = 9'h079, cfg_r3 = 9'h079;
    wire [8:0] cfg_r4 = 9'h010, cfg_r5 = 9'h000, cfg_r7 = 9'h042, cfg_r8 = 9'h000;
    wire       busy, done, error, codec_found;
    wire [1:0] addr_sel;
    wire [7:0] regs_written;
    wire       scl_low, sda_low;
    wire       sda_in;
    wire [8:0] rb_r4, rb_r7, rb_r8, rb_r9;
    wire       rb_valid, sda_idle, scl_idle;

    // ---- wired-AND bus with pull-ups -------------------------------------
    reg  slave_low = 0;
    wire scl = scl_low ? 1'b0 : 1'b1;
    wire sda = (sda_low | slave_low) ? 1'b0 : 1'b1;
    assign sda_in = sda;

    // ---- I2C slave model --------------------------------------------------
    reg [7:0]  ack_addrs [0:1];               // addresses that answer
    integer    n_ack;
    reg [7:0]  sh;
    reg [3:0]  bitc;
    reg        active;
    integer    bytec;
    reg [7:0]  byte1;
    reg        ack_this_byte;
    integer    wr_count;
    reg [6:0]  wr_reg [0:31];
    reg [8:0]  wr_val [0:31];
    integer    i;
    reg        saw_ack_for_addr;

    function integer addr_answered;
        input [7:0] byte_in;      // address byte: bits [7:1] are the 7-bit address
        integer k;
        begin
            addr_answered = 0;
            for (k = 0; k < n_ack; k = k + 1)
                if (ack_addrs[k][6:0] == byte_in[7:1]) addr_answered = 1;
        end
    endfunction

    // START: SDA falls while SCL is high.
    always @(negedge sda) begin
        if (scl) begin
            active <= 1'b1;
            bitc   <= 4'd0;
            bytec  <= 0;

        end
    end


    // STOP: SDA rises while SCL is high.
    always @(posedge sda) begin
        if (scl) active <= 1'b0;
    end

    always @(posedge scl) begin

        if (active) begin
            if (bitc < 4'd8) begin
                sh   <= {sh[6:0], sda};
                bitc <= bitc + 1'b1;
            end else begin
                // End of a byte: bytec 0 = address, 1 = register, 2 = data.
                if (bytec == 0) begin
                    saw_ack_for_addr <= addr_answered(sh);
                end else if (bytec == 1) begin
                    byte1 <= sh;
                end else if (bytec == 2) begin
                    if (wr_count < 32) begin
                        wr_reg[wr_count] = byte1[7:1];
                        wr_val[wr_count] = {byte1[0], sh};
                        wr_count = wr_count + 1;
                    end
                end
                bytec <= bytec + 1;
                bitc  <= 4'd0;
            end
        end
    end

    always @(negedge scl) begin
        if (active && bitc == 4'd8) begin
            // ACK phase: answer when the byte just received was addressed to us.
            if (bytec == 0) slave_low <= addr_answered(sh);
            else            slave_low <= saw_ack_for_addr;
        end else begin
            slave_low <= 1'b0;
        end
    end

    // ---- DUT --------------------------------------------------------------
    wm8731_init #(.CLK_HZ(CLK_HZ), .SCL_HZ(SCL_HZ), .RESET_WAIT(2000),
                  .DO_READBACK(0)) dut (
        .clk(clk), .reset_n(reset_n), .trigger(trigger),
        .cfg_r0(cfg_r0), .cfg_r1(cfg_r1), .cfg_r2(cfg_r2), .cfg_r3(cfg_r3),
        .cfg_r4(cfg_r4), .cfg_r5(cfg_r5), .cfg_r7(cfg_r7), .cfg_r8(cfg_r8),
        .busy(busy), .done(done), .error(error), .codec_found(codec_found),
        .addr_sel(addr_sel), .regs_written(regs_written),
        .rb_r4(rb_r4), .rb_r7(rb_r7), .rb_r8(rb_r8), .rb_r9(rb_r9),
        .rb_valid(rb_valid), .sda_idle(sda_idle), .scl_idle(scl_idle),
        .scl_low(scl_low), .sda_low(sda_low), .sda_in(sda_in));

    integer errors = 0;

    task expect_reg;
        input integer ix;
        input [6:0]   reg_addr;
        input [8:0]   value;
        input [127:0] name;
        begin
            if (wr_count <= ix) begin
                $display("FAIL %0s: write %0d missing (only %0d writes)", name, ix, wr_count);
                errors = errors + 1;
            end else if (wr_reg[ix] !== reg_addr || wr_val[ix] !== value) begin
                $display("FAIL %0s: write %0d = R%0d/%03h, expected R%0d/%03h",
                         name, ix, wr_reg[ix], wr_val[ix], reg_addr, value);
                errors = errors + 1;
            end
        end
    endtask

    task run_init;
        input [127:0] name;
        begin
            @(negedge clk);
            wr_count = 0;
            slave_low = 0;
            ack_this_byte = 0;
            trigger = 1'b1;
            @(negedge clk);
            trigger = 1'b0;
            // Wait for done or error.
            while (!done && !error) @(negedge clk);
        end
    endtask

    initial begin
        // ---- run 1: codec answers on 0x1A --------------------------------
        ack_addrs[0] = 8'h1A; ack_addrs[1] = 8'h1A; n_ack = 1;
        reset_n = 0;
        repeat (8) @(negedge clk);
        reset_n = 1;

        run_init("0x1A");
        if (!codec_found) begin $display("FAIL 0x1A: codec_found not set"); errors = errors + 1; end
        if (addr_sel !== 2'd0) begin $display("FAIL 0x1A: addr_sel=%0d", addr_sel); errors = errors + 1; end
        if (error) begin $display("FAIL 0x1A: unexpected error"); errors = errors + 1; end
        if (regs_written !== 8'd11) begin
            $display("FAIL 0x1A: regs_written=%0d, expected 11", regs_written); errors = errors + 1;
        end
        expect_reg(0,  7'h0F, 9'h000, "run1");
        expect_reg(1,  7'h08, 9'h000, "run1");
        expect_reg(2,  7'h07, 9'h042, "run1");     // master mode + I2S + 16 bit
        expect_reg(3,  7'h06, 9'h000, "run1");
        expect_reg(4,  7'h04, 9'h010, "run1");
        expect_reg(5,  7'h05, 9'h000, "run1");
        expect_reg(6,  7'h00, 9'h017, "run1");
        expect_reg(7,  7'h01, 9'h017, "run1");
        expect_reg(8,  7'h02, 9'h079, "run1");
        expect_reg(9,  7'h03, 9'h079, "run1");
        expect_reg(10, 7'h09, 9'h001, "run1");     // ACTIVE last
        if (wr_count !== 11) begin
            $display("FAIL run1: %0d writes, expected 11", wr_count); errors = errors + 1;
        end

        // ---- run 2: only 0x1B answers (address fallback) ------------------
        ack_addrs[0] = 8'h1B; n_ack = 1;
        run_init("0x1B");
        if (!codec_found || addr_sel !== 2'd1) begin
            $display("FAIL 0x1B fallback: codec_found=%0b addr_sel=%0d", codec_found, addr_sel);
            errors = errors + 1;
        end
        if (error) begin $display("FAIL 0x1B: unexpected error"); errors = errors + 1; end
        if (wr_count !== 11) begin
            $display("FAIL 0x1B: %0d writes, expected 11", wr_count); errors = errors + 1;
        end
        expect_reg(0, 7'h0F, 9'h000, "run2");

        // ---- run 3: nobody answers -> error -------------------------------
        n_ack = 0;
        run_init("none");
        if (!error) begin $display("FAIL none: error not raised"); errors = errors + 1; end
        if (codec_found) begin $display("FAIL none: codec_found set"); errors = errors + 1; end

        if (errors == 0) $display("PASS_I2C_WM8731");
        else             $display("FAIL_I2C_WM8731 errors=%0d", errors);
        $finish;
    end

    // Watchdog
    initial begin
        #4_000_000;
        $display("FAIL_I2C_WM8731 timeout (writes=%0d)", wr_count);
        $finish;
    end
endmodule
