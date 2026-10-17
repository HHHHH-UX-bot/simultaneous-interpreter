`timescale 1ns/1ps
// Byte-level I2C master for the WM8731 control port (100 kHz standard mode).
//
// The pads are open drain: the core drives them low or releases them, and the
// pull-ups live on the audio module (VW_I2C_SCLK / VM_I2C_SDAT) or the board.
// Bytes are supplied combinationally by the caller, indexed by the registered
// tx_index: index 0 is the address byte, 1..tx_count are the data bytes. Each
// byte is latched in S_LOAD one clock after tx_index changes, so the caller
// only needs a combinational decoder.
//
// Timing: every quarter of an SCL period lasts QUARTER clocks. START and STOP
// walk three quarters (qn), the byte engine walks four quarters per bit (k)
// over nine bits: eight data bits plus the ACK bit.
module i2c_master #(
    parameter CLK_HZ  = 50000000,
    parameter SCL_HZ  = 100000,
    // Clocks per quarter of an SCL period (>= 2).
    parameter QUARTER = CLK_HZ / (4 * SCL_HZ)
) (
    input  wire       clk,
    input  wire       reset_n,
    input  wire       start,       // pulse to begin a transaction
    input  wire [7:0] tx_count,    // data bytes after the address byte (0..8)
    output reg  [5:0] tx_index,
    input  wire [7:0] tx_byte,     // combinationally valid for tx_index
    output wire       busy,
    output reg        done,        // one-clock pulse
    output reg        ack_error,   // sticky until the next transaction
    output wire       scl_low,     // 1 = pull SCL low (open drain)
    output wire       sda_low,     // 1 = pull SDA low (open drain)
    input  wire       sda_in,
    // Bus recovery: a slave interrupted mid-transfer can hold SDA low, which
    // blocks every later transaction until SCL is clocked nine times.
    input  wire       recover,
    output wire       recovery_busy
);
    localparam [2:0]
        S_IDLE  = 3'd0,
        S_START = 3'd1,
        S_LOAD  = 3'd2,
        S_BYTE  = 3'd3,
        S_NEXT  = 3'd4,
        S_STOP  = 3'd5,
        S_GAP   = 3'd6;

    localparam integer QW = (QUARTER < 2) ? 2 : QUARTER;

    reg [2:0]  state;
    reg [1:0]  qn;         // quarter index for START/STOP
    reg [15:0] qphase;     // clocks inside the current quarter
    reg [5:0]  sub;        // 36 phases per byte: 9 bits * 4 quarters
    reg [7:0]  shreg;
    reg [7:0]  sent;       // completed data bytes
    reg [7:0]  count;
    reg        nack;
    reg        scl_low_i, sda_low_i;

    wire [3:0] bitpos   = sub[5:2];
    wire [1:0] k        = sub[1:0];
    wire       is_ack   = (bitpos == 4'd8);
    wire       qdone    = (qphase >= QW - 1);
    wire       byte_end = (sub == 6'd35) && (k == 2'd3);

    assign busy = (state != S_IDLE);

    always @(posedge clk) begin
        if (!reset_n) begin
            state <= S_IDLE; qn <= 2'd0; qphase <= 0; sub <= 0; shreg <= 0;
            sent <= 0; count <= 0; nack <= 0; tx_index <= 0;
            done <= 0; ack_error <= 0; scl_low_i <= 0; sda_low_i <= 0;
        end else begin
            done <= 1'b0;
            case (state)
                S_IDLE: begin
                    scl_low_i <= 1'b0;
                    sda_low_i <= 1'b0;
                    if (start) begin
                        count     <= tx_count;
                        sent      <= 8'd0;
                        tx_index  <= 6'd0;
                        nack      <= 1'b0;
                        ack_error <= 1'b0;
                        qphase    <= 16'd0;
                        qn        <= 2'd0;
                        state     <= S_START;
                    end
                end
                // START: SDA falls while SCL is high.
                S_START: begin
                    case (qn)
                        2'd0:    begin scl_low_i <= 1'b0; sda_low_i <= 1'b0; end
                        2'd1:    begin scl_low_i <= 1'b0; sda_low_i <= 1'b1; end
                        default: begin scl_low_i <= 1'b1; sda_low_i <= 1'b1; end
                    endcase
                    if (qdone) begin
                        qphase <= 16'd0;
                        if (qn == 2'd2) begin qn <= 2'd0; state <= S_LOAD; end
                        else                qn <= qn + 1'b1;
                    end else qphase <= qphase + 1'b1;
                end
                // Latch the byte the caller presents for the current tx_index.
                S_LOAD: begin
                    shreg  <= tx_byte;
                    sub    <= 6'd0;
                    qphase <= 16'd0;
                    state  <= S_BYTE;
                end
                // Eight data bits (MSB first, SDA changes while SCL is low)
                // plus the ACK bit.
                S_BYTE: begin
                    case (k)
                        2'd0: begin
                            scl_low_i <= 1'b1;
                            sda_low_i <= is_ack ? 1'b0 : ~shreg[7];
                        end
                        2'd1: begin
                            scl_low_i <= 1'b0;
                            if (is_ack && qdone && sda_in) nack <= 1'b1;
                        end
                        2'd2: begin
                            scl_low_i <= 1'b0;
                        end
                        default: begin
                            // Sample/prepare only; the shift happens at the end
                            // of this quarter, once per bit.
                            scl_low_i <= 1'b1;
                        end
                    endcase
                    if (qdone) begin
                        qphase <= 16'd0;
                        if (byte_end) state <= S_NEXT;
                        else begin
                            sub <= sub + 1'b1;
                            if (k == 2'd3 && !is_ack) shreg <= {shreg[6:0], 1'b0};
                        end
                    end else qphase <= qphase + 1'b1;
                end
                S_NEXT: begin
                    if (nack) begin
                        ack_error <= 1'b1;
                        state     <= S_STOP;
                    end else if (sent >= count) begin
                        state <= S_STOP;
                    end else begin
                        sent     <= sent + 1'b1;
                        tx_index <= tx_index + 1'b1;
                        state    <= S_LOAD;
                    end
                end
                // STOP: SDA rises while SCL is high.
                S_STOP: begin
                    case (qn)
                        2'd0:    begin scl_low_i <= 1'b1; sda_low_i <= 1'b1; end
                        2'd1:    begin scl_low_i <= 1'b0; sda_low_i <= 1'b1; end
                        default: begin scl_low_i <= 1'b0; sda_low_i <= 1'b0; end
                    endcase
                    if (qdone) begin
                        qphase <= 16'd0;
                        if (qn == 2'd2) begin qn <= 2'd0; state <= S_GAP; end
                        else                qn <= qn + 1'b1;
                    end else qphase <= qphase + 1'b1;
                end
                S_GAP: begin
                    scl_low_i <= 1'b0;
                    sda_low_i <= 1'b0;
                    if (qdone) begin
                        qphase <= 16'd0;
                        done   <= 1'b1;
                        state  <= S_IDLE;
                    end else qphase <= qphase + 1'b1;
                end
                default: state <= S_IDLE;
            endcase
        end
    end
    // ---- bus recovery -----------------------------------------------------
    // While recovering, SDA is released and SCL is clocked nine times so a
    // slave that was interrupted mid-byte shifts out and frees the bus.
    reg [3:0]  rec_cnt;
    reg [15:0] rec_div;
    reg        rec_active;

    assign recovery_busy = rec_active;
    assign scl_low = rec_active ? ~rec_cnt[0] : scl_low_i;
    assign sda_low = rec_active ? 1'b0       : sda_low_i;

    always @(posedge clk) begin
        if (!reset_n) begin
            rec_active <= 1'b0;
            rec_cnt    <= 4'd0;
            rec_div    <= 16'd0;
        end else if (recover && !rec_active) begin
            rec_active <= 1'b1;
            rec_cnt    <= 4'd0;
            rec_div    <= 16'd0;
        end else if (rec_active) begin
            if (rec_div == QW[15:0] - 1'b1) begin
                rec_div <= 16'd0;
                if (rec_cnt == 4'd17) rec_active <= 1'b0;   // 9 low + 9 high phases
                else                  rec_cnt    <= rec_cnt + 1'b1;
            end else begin
                rec_div <= rec_div + 1'b1;
            end
        end
    end
endmodule
