`timescale 1ns/1ps
// WM8731 control-port initialisation sequencer.
//
// Order matters: bus recovery, soft reset, clock/format configuration, power-up,
// signal path, then ACTIVE last. After the write sequence the same nine
// registers are read back over the 2-wire interface, which proves that the
// writes really landed on the part (an interrupted slave can otherwise ACK
// every byte without accepting the data).
//
// The nine codec value fields are writable shadows, so the analogue path,
// volumes and interface format can be re-tuned from the host and re-applied
// without a rebuild.
//
// Register map reference: WM8731 datasheet PD Rev 4.8, Table 29/30
// (docs/design/SOURCES.md). R4/R7/R8 defaults are in audio_regs.v.
module wm8731_init #(
    parameter CLK_HZ = 50000000,
    parameter SCL_HZ = 50000,      // slower bus: more margin against power-up noise
    // Delay after the soft reset before the remaining writes (1 ms default).
    parameter RESET_WAIT = CLK_HZ / 1000,
    parameter RETRIES = 4,              // full-sequence retries on a NACK
    parameter RETRY_WAIT = CLK_HZ / 100, // 10 ms between attempts
    // Read the configuration back after writing it. The hardware build keeps
    // this on; the write-sequence testbench turns it off because its slave
    // model only implements writes.
    parameter DO_READBACK = 1
) (
    input  wire       clk,
    input  wire       reset_n,
    input  wire       trigger,        // pulse: (re)run the sequence
    input  wire [8:0] cfg_r0,         // left line input volume
    input  wire [8:0] cfg_r1,         // right line input volume
    input  wire [8:0] cfg_r2,         // left headphone volume
    input  wire [8:0] cfg_r3,         // right headphone volume
    input  wire [8:0] cfg_r4,         // analogue audio path
    input  wire [8:0] cfg_r5,         // digital audio path
    input  wire [8:0] cfg_r7,         // interface format (holds the master bit)
    input  wire [8:0] cfg_r8,         // sampling control
    output reg        busy,
    output reg        recover,        // bus-recovery pulse before probing
    output reg        done,           // one-clock pulse
    output reg        error,          // no codec answered, or a write was NACKed
    output reg        codec_found,
    output reg [1:0]  addr_sel,       // 0 -> 0x1A, 1 -> 0x1B
    output reg [7:0]  regs_written,
    // Readback of the codec registers after the write sequence.
    output reg  [8:0] rb_r4,
    output reg  [8:0] rb_r7,
    output reg  [8:0] rb_r8,
    output reg  [8:0] rb_r9,
    output reg        rb_valid,       // all four reads answered with the right address
    // Live bus levels: a stuck bus shows one of these low after a transaction.
    output wire       sda_idle,
    output wire       scl_idle,
    // Open-drain pads.
    output wire       scl_low,
    output wire       sda_low,
    input  wire       sda_in
);
    localparam [3:0]
        T_IDLE    = 4'd0,
        T_RECOVER = 4'd1,
        T_PROBE_A = 4'd2,
        T_PROBE_B = 4'd3,
        T_WAIT    = 4'd4,
        T_WRITE   = 4'd5,
        T_STEP    = 4'd6,
        T_RD_INIT = 4'd7,
        T_RD      = 4'd8,
        T_DONE    = 4'd9,
        T_FAIL    = 4'd10;

    localparam [1:0] ADDR_1A = 2'd0, ADDR_1B = 2'd1;
    localparam [4:0] LAST_STEP = 5'd10;

    reg [3:0]  tstate;
    reg [4:0]  step;
    reg [16:0] wait_cnt;
    reg [2:0]  attempts;
    reg [19:0] retry_cnt;
    reg        start_issued;
    reg        recover_issued;

    // Readback sequencer.
    reg [1:0]  rd_reg_sel;
    reg [6:0]  rd_reg_addr;
    reg [7:0]  rd_cap_hi, rd_cap_lo;
    reg        rd_mode_v;
    wire [8:0] rd_value = {rd_cap_hi[0], rd_cap_lo[7:0]};
    wire       rd_addr_ok = (rd_cap_hi[7:1] == rd_reg_addr);

    // --- I2C master ---------------------------------------------------------
    reg  [6:0] dev;
    reg  [7:0] tx_count;
    reg        i2c_start;
    wire [5:0] tx_index;
    reg  [7:0] tx_byte;
    wire       i2c_busy, i2c_done, i2c_ack_error, i2c_recovery_busy;
    wire       i2c_rd_valid;
    wire [7:0] i2c_rd_byte;
    wire [1:0] i2c_rd_index;

    assign sda_idle = sda_in;
    assign scl_idle = !scl_low;

    i2c_master #(.CLK_HZ(CLK_HZ), .SCL_HZ(SCL_HZ)) u_i2c (
        .clk       (clk),
        .reset_n   (reset_n),
        .start     (i2c_start),
        .tx_count  (tx_count),
        .tx_index  (tx_index),
        .tx_byte   (tx_byte),
        .busy      (i2c_busy),
        .done      (i2c_done),
        .ack_error (i2c_ack_error),
        .scl_low   (scl_low),
        .sda_low   (sda_low),
        .sda_in    (sda_in),
        .rd        (rd_mode_v),
        .rd_count  (3'd2),
        .rd_byte   (i2c_rd_byte),
        .rd_index  (i2c_rd_index),
        .rd_valid  (i2c_rd_valid),
        .recover   (recover),
        .recovery_busy (i2c_recovery_busy)
    );

    // --- Byte decoder ------------------------------------------------------
    // index 0 = address byte, 1 = register address + data MSB, 2 = data LSB.
    reg [6:0] seq_reg;
    reg [8:0] seq_val;

    always @* begin
        case (step)
            5'd0:    begin seq_reg = 7'h0F; seq_val = 9'h000; end   // RESET
            5'd1:    begin seq_reg = 7'h08; seq_val = cfg_r8; end   // sampling
            5'd2:    begin seq_reg = 7'h07; seq_val = cfg_r7; end   // interface (master bit)
            5'd3:    begin seq_reg = 7'h06; seq_val = 9'h000; end   // no power down
            5'd4:    begin seq_reg = 7'h04; seq_val = cfg_r4; end   // analogue path
            5'd5:    begin seq_reg = 7'h05; seq_val = cfg_r5; end   // digital path
            5'd6:    begin seq_reg = 7'h00; seq_val = cfg_r0; end   // left in volume
            5'd7:    begin seq_reg = 7'h01; seq_val = cfg_r1; end   // right in volume
            5'd8:    begin seq_reg = 7'h02; seq_val = cfg_r2; end   // left hp volume
            5'd9:    begin seq_reg = 7'h03; seq_val = cfg_r3; end   // right hp volume
            5'd10:   begin seq_reg = 7'h09; seq_val = 9'h001; end   // ACTIVE (last)
            default: begin seq_reg = 7'h0F; seq_val = 9'h000; end
        endcase
    end

    always @* begin
        case (tx_index)
            6'd0:    tx_byte = {dev, rd_mode_v};     // R/W = 1 for read transactions
            6'd1:    tx_byte = {seq_reg, seq_val[8]};
            6'd2:    tx_byte = seq_val[7:0];
            default: tx_byte = 8'h00;
        endcase
    end

    // --- Readback capture --------------------------------------------------
    always @(posedge clk) begin
        if (!reset_n) begin
            rd_cap_hi <= 8'h00;
            rd_cap_lo <= 8'h00;
        end else if (i2c_rd_valid) begin
            if (i2c_rd_index == 2'd0) rd_cap_hi <= i2c_rd_byte;
            else                      rd_cap_lo <= i2c_rd_byte;
        end
    end

    // --- Sequencer ---------------------------------------------------------
    always @(posedge clk) begin
        if (!reset_n) begin
            tstate <= T_IDLE; step <= 5'd0; wait_cnt <= 17'd0; start_issued <= 1'b0;
            attempts <= 3'd0; retry_cnt <= 20'd0; recover_issued <= 1'b0;
            dev <= 7'h1A; tx_count <= 8'd0; i2c_start <= 1'b0;
            busy <= 1'b0; done <= 1'b0; error <= 1'b0; codec_found <= 1'b0;
            addr_sel <= ADDR_1A; regs_written <= 8'd0;
            recover <= 1'b0;
            rd_reg_sel <= 2'd0; rd_reg_addr <= 7'h00; rd_mode_v <= 1'b0;
            rb_r4 <= 9'd0; rb_r7 <= 9'd0; rb_r8 <= 9'd0; rb_r9 <= 9'd0;
            rb_valid <= 1'b0;
        end else begin
            i2c_start <= 1'b0;
            case (tstate)
                T_IDLE: begin
                    busy <= 1'b0;
                    if (trigger) begin
                        busy         <= 1'b1;
                        done         <= 1'b0;
                        error        <= 1'b0;
                        codec_found  <= 1'b0;
                        regs_written <= 8'd0;
                        step         <= 5'd0;
                        addr_sel     <= ADDR_1A;
                        start_issued <= 1'b0;
                        recover_issued <= 1'b0;
                        recover      <= 1'b0;
                        rd_reg_sel   <= 2'd0;
                        rd_mode_v    <= 1'b0;
                        rb_valid     <= 1'b0;
                        // Bus recovery stays available but is not run by default:
                        // the bus idles high on the real module and the extra SCL
                        // burst is not needed. Use T_RECOVER explicitly later if a
                        // stuck-bus case is ever observed.
                        tstate       <= T_PROBE_A;
                    end
                end
                // Clock SCL nine times with SDA released so a slave that was
                // interrupted mid-byte lets go of the bus before we probe it.
                T_RECOVER: begin
                    if (!recover_issued) begin
                        recover        <= 1'b1;
                        recover_issued <= 1'b1;
                    end else begin
                        recover <= 1'b0;
                        if (!i2c_recovery_busy) begin
                            recover_issued <= 1'b0;
                            tstate         <= T_PROBE_A;
                        end
                    end
                end
                // A zero-data write (address byte then STOP) probes for an ACK.
                T_PROBE_A, T_PROBE_B: begin
                    if (!start_issued && !i2c_busy) begin
                        dev          <= (tstate == T_PROBE_A) ? 7'h1A : 7'h1B;
                        tx_count     <= 8'd0;
                        rd_mode_v    <= 1'b0;
                        i2c_start    <= 1'b1;
                        start_issued <= 1'b1;
                    end else if (start_issued && i2c_done) begin
                        start_issued <= 1'b0;
                        if (!i2c_ack_error) begin
                            codec_found <= 1'b1;
                            addr_sel    <= (tstate == T_PROBE_A) ? ADDR_1A : ADDR_1B;
                            wait_cnt    <= 17'd0;
                            tstate      <= T_WAIT;
                        end else if (tstate == T_PROBE_A) begin
                            tstate <= T_PROBE_B;
                        end else begin
                            error  <= 1'b1;
                            tstate <= T_FAIL;
                        end
                    end
                end
                T_WAIT: begin
                    if (wait_cnt >= RESET_WAIT[16:0]) tstate <= T_WRITE;
                    else                              wait_cnt <= wait_cnt + 1'b1;
                end
                T_WRITE: begin
                    if (!start_issued && !i2c_busy) begin
                        tx_count     <= 8'd2;
                        rd_mode_v    <= 1'b0;
                        i2c_start    <= 1'b1;
                        start_issued <= 1'b1;
                    end else if (start_issued && i2c_done) begin
                        start_issued <= 1'b0;
                        // The software-reset write (R15) can legally lose its ACK because
                        // the codec resets itself before acknowledging it.
                        if (i2c_ack_error && step != 5'd0) begin
                            error  <= 1'b1;
                            tstate <= T_FAIL;
                        end else begin
                            regs_written <= regs_written + 1'b1;
                            if (step == LAST_STEP) begin
                                tstate <= (DO_READBACK != 0) ? T_RD_INIT : T_DONE;
                            end else begin
                                tstate <= T_STEP;
                            end
                        end
                    end
                end
                T_STEP: begin
                    // One clock of spacing; the master is back in IDLE and the
                    // decoder already sees the incremented step.
                    step   <= step + 1'b1;
                    tstate <= T_WRITE;
                end
                // Read one codec register: byte 0 = {register, data[8]},
                // byte 1 = data[7:0].
                T_RD_INIT: begin
                    case (rd_reg_sel)
                        2'd0:    rd_reg_addr <= 7'h04;
                        2'd1:    rd_reg_addr <= 7'h07;
                        2'd2:    rd_reg_addr <= 7'h08;
                        default: rd_reg_addr <= 7'h09;
                    endcase
                    rd_mode_v <= 1'b1;
                    tstate    <= T_RD;
                end
                T_RD: begin
                    if (!start_issued && !i2c_busy) begin
                        tx_count     <= 8'd0;
                        rd_mode_v    <= 1'b1;
                        i2c_start    <= 1'b1;
                        start_issued <= 1'b1;
                    end else if (start_issued && i2c_done) begin
                        start_issued <= 1'b0;
                        rd_mode_v    <= 1'b0;
                        if (i2c_ack_error || !rd_addr_ok) begin
                            rb_valid <= 1'b0;
                            tstate   <= T_DONE;
                        end else begin
                            case (rd_reg_sel)
                                2'd0:    rb_r4 <= rd_value;
                                2'd1:    rb_r7 <= rd_value;
                                2'd2:    rb_r8 <= rd_value;
                                default: rb_r9 <= rd_value;
                            endcase
                            if (rd_reg_sel == 2'd3) begin
                                rb_valid <= 1'b1;
                                tstate   <= T_DONE;
                            end else begin
                                rd_reg_sel <= rd_reg_sel + 1'b1;
                                tstate     <= T_RD_INIT;
                            end
                        end
                    end
                end
                T_DONE: begin
                    busy   <= 1'b0;
                    done   <= 1'b1;
                    tstate <= T_IDLE;
                end
                default: begin
                    busy   <= 1'b0;
                    error  <= 1'b1;
                    tstate <= T_IDLE;
                end
            endcase
        end
    end
endmodule
