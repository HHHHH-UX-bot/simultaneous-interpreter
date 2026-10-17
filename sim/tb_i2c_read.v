`timescale 1ns/1ps
// Self-checking testbench for the 2-wire read path of i2c_master.
//
// Models the WM8731 control port answering one register read: the address
// byte (0x35 = 0x1A with R/W = 1) is ACKed, then two bytes are clocked in
// (register 0x07 = {0x07, data[8]} and data[7:0]), the master ACKs the first
// byte and NACKs the last one. The test checks the two received bytes, their
// indices, and that no ACK error is reported.
module tb_i2c_read;
    localparam integer CLK_HZ  = 50000000;
    localparam integer SCL_HZ  = 1000000;
    localparam integer QUARTER = CLK_HZ / (4 * SCL_HZ);

    reg clk = 0, reset_n = 0;
    always #10 clk = ~clk;

    reg        start = 0;
    reg        rd = 0;
    reg  [7:0] tx_byte = 8'h35;
    wire [5:0] tx_index;
    wire       busy, done, ack_error;
    wire       scl_low, sda_low, sda_in;
    wire [7:0] rd_byte;
    wire [1:0] rd_index;
    wire       rd_valid;
    wire       recovery_busy;

    // Wired-AND bus.
    reg  slave_low = 0;
    wire scl = scl_low ? 1'b0 : 1'b1;
    wire sda = (sda_low | slave_low) ? 1'b0 : 1'b1;
    assign sda_in = sda;

    // ---- read slave model -------------------------------------------------
    // Register 0x07 contains 0x042: byte0 = {7'h07, 1'b0} = 0x0E, byte1 = 0x42.
    localparam [7:0] BYTE0 = 8'h0E;
    localparam [7:0] BYTE1 = 8'h42;
    localparam [7:0] ADDR_BYTE = 8'h35;

    reg        active = 0;
    integer    edges = 0;        // SCL rising edges seen since START
    integer    next_edge;
    reg  [7:0] rx_addr = 8'h00;  // address byte shifted in from the master

    always @(negedge sda) if (scl) begin active <= 1'b1; edges <= 0; slave_low <= 1'b0; rx_addr <= 8'h00; end
    always @(posedge sda) if (scl) active <= 1'b0;

    always @(posedge scl) if (active) begin
        edges <= edges + 1;
        if (edges < 8) rx_addr <= {rx_addr[6:0], sda};
    end

    // Drive the next read bit while SCL is low (SDA changes only while SCL low).
    // edges counts the rising edges already seen, so the next rising edge is
    // edges+1: address bits 1..8, address ACK 9, byte0 bits 10..17, master ACK
    // 18, byte1 bits 19..26, master NACK 27.
    always @(negedge scl) begin
        if (!active) begin
            slave_low <= 1'b0;
        end else begin
            next_edge = edges + 1;
            if (next_edge == 9)
                slave_low <= (rx_addr == ADDR_BYTE) ? 1'b1 : 1'b0;   // ACK address
            else if (next_edge >= 10 && next_edge <= 17)
                slave_low <= ~BYTE0[7 - (next_edge - 10)];
            else if (next_edge >= 19 && next_edge <= 26)
                slave_low <= ~BYTE1[7 - (next_edge - 19)];
            else
                slave_low <= 1'b0;
        end
    end

    // ---- DUT --------------------------------------------------------------
    i2c_master #(.CLK_HZ(CLK_HZ), .SCL_HZ(SCL_HZ)) dut (
        .clk(clk), .reset_n(reset_n),
        .start(start), .tx_count(8'd0), .tx_index(tx_index), .tx_byte(tx_byte),
        .busy(busy), .done(done), .ack_error(ack_error),
        .scl_low(scl_low), .sda_low(sda_low), .sda_in(sda_in),
        .rd(rd), .rd_count(3'd2), .rd_byte(rd_byte), .rd_index(rd_index),
        .rd_valid(rd_valid),
        .recover(1'b0), .recovery_busy(recovery_busy));

    integer errors = 0;
    reg [7:0] got0, got1;
    integer   got_count = 0;

    always @(posedge clk) begin
        if (rd_valid) begin
            got_count = got_count + 1;
            if (got_count == 1) begin
                got0 = rd_byte;
                if (rd_index !== 2'd0) begin
                    $display("FAIL first byte index %0d", rd_index); errors = errors + 1;
                end
            end else if (got_count == 2) begin
                got1 = rd_byte;
                if (rd_index !== 2'd1) begin
                    $display("FAIL second byte index %0d", rd_index); errors = errors + 1;
                end
            end
        end
    end

    initial begin
        repeat (8) @(negedge clk);
        reset_n = 1;
        repeat (4) @(negedge clk);

        // One read transaction: address byte with R/W = 1, two data bytes.
        @(negedge clk);
        rd = 1'b1; start = 1'b1;
        @(negedge clk);
        start = 1'b0;
        while (!done) @(negedge clk);
        @(negedge clk);

        if (ack_error) begin $display("FAIL unexpected ACK error"); errors = errors + 1; end
        if (got_count != 2) begin $display("FAIL %0d read bytes, expected 2", got_count); errors = errors + 1; end
        if (got0 !== BYTE0) begin $display("FAIL byte0 = %02h, expected %02h", got0, BYTE0); errors = errors + 1; end
        if (got1 !== BYTE1) begin $display("FAIL byte1 = %02h, expected %02h", got1, BYTE1); errors = errors + 1; end
        if (edges < 27) begin $display("FAIL only %0d SCL edges", edges); errors = errors + 1; end

        if (errors == 0) $display("PASS_I2C_READ");
        else             $display("FAIL_I2C_READ errors=%0d", errors);
        $finish;
    end

    initial begin
        #2_000_000;
        $display("FAIL_I2C_READ timeout (edges=%0d bytes=%0d)", edges, got_count);
        $finish;
    end
endmodule
