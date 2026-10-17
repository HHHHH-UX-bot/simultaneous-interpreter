`timescale 1ns/1ps
// I2S loopback between the module's ADC and DAC paths.
//
// The WM8731 module is the I2S clock master, so BCLK, ADCLRC and DACLRC are PL
// inputs. The whole audio path therefore lives in the BCLK domain and needs no
// clock-domain crossing:
//   ADCDAT --(rx, posedge bclk)--> frame FIFO --(tx, posedge bclk)--> DACDAT
//
// I2S timing used here (Philips convention, one BCLK of delay between the LRCLK
// edge and the data bits):
//   * receive : the LRCLK-change edge is skipped, the MSB is taken on the next
//               rising edge and 16 bits are shifted in.
//   * transmit: the MSB is loaded when the LRCLK change is seen and emitted on
//               the following falling edge, so the codec samples it one BCLK
//               after the LRCLK edge.
//
// The L1 latency is the number of stereo frames sitting in the loopback FIFO;
// counters are snapshotted into the 50 MHz system domain with a toggle
// handshake (data is latched while stable, so multi-bit CDC is safe).
module i2s_loopback #(
    parameter FIFO_DEPTH   = 2,        // stereo frames buffered (>= 2)
    parameter AW           = 1,        // log2(FIFO_DEPTH)
    parameter STALE_CYCLES = 250000    // 5 ms at 50 MHz without BCLK -> not live
) (
    input  wire        clk_sys,
    input  wire        reset_n_sys,
    // Module side (PL is the I2S slave).
    input  wire        bclk,
    input  wire        adclrc,
    input  wire        adcdat,
    input  wire        dadlrc,
    output wire        dacdat,
    // Snapshots for the system domain.
    output reg  [15:0] occ_frames,
    output reg  [15:0] latency_cycles,   // capture -> emission, in BCLK cycles
    output reg  [31:0] adc_frames,
    output reg  [31:0] dac_frames,
    output reg  [15:0] overruns,
    output reg  [15:0] underruns,
    output reg  [31:0] bclk_cycles,
    output reg         live
);
    localparam integer DEPTH = FIFO_DEPTH;

    // ------------------------------------------------------------------
    // BCLK domain registers
    // ------------------------------------------------------------------
    reg [31:0] fifo_mem [0:DEPTH-1];
    reg [15:0] stamp_mem [0:DEPTH-1];    // capture timestamp per buffered frame
    reg [AW:0] wr_ptr, rd_ptr;
    reg [31:0] wr_total, rd_total, cycle_cnt;
    reg [15:0] over_cnt, under_cnt;
    reg [15:0] lat_cycles;
    // Frame timestamp: stamp_cur is the BCLK cycle at which the frame being
    // captured started (its left-slot change). The frame that just completed
    // carried that value, so the measured L1 covers capture -> emission.
    reg [15:0] stamp_cur, push_stamp;

    reg        adclrc_d;
    reg [4:0]  rx_bit;
    reg [15:0] rx_shift, rx_left, rx_push_left, rx_push_right;
    reg        rx_push;

    reg        dadlrc_d;
    reg [4:0]  tx_bit;
    reg [15:0] tx_l, tx_r, tx_shift;
    reg        primed;          // hold off until the FIFO holds a full depth

    reg        req_t;
    reg        ack_sync1, ack_sync2;
    reg [15:0] snap_occ, snap_over, snap_under, snap_lat;
    reg [31:0] snap_adc, snap_dac, snap_cyc;

    // ------------------------------------------------------------------
    // System domain registers
    // ------------------------------------------------------------------
    reg        req_sync1, req_sync2, req_seen, ack_t_sys;
    reg [19:0] stale_cnt;

    wire [AW:0] occ_calc   = wr_ptr - rd_ptr;
    wire        fifo_full  = (occ_calc == DEPTH[AW:0]);
    wire        fifo_empty = (occ_calc == 0);

    // Asynchronous assert, no release requirement: the loopback only runs when
    // BCLK exists, and the system side reports live = 0 until snapshots arrive.
    wire bclk_reset = !reset_n_sys;

    // ------------------------------------------------------------------
    // Receive + FIFO + transmit sequencing (posedge BCLK)
    // ------------------------------------------------------------------
    always @(posedge bclk or posedge bclk_reset) begin
        if (bclk_reset) begin
            wr_ptr <= 0; rd_ptr <= 0; wr_total <= 0; rd_total <= 0; cycle_cnt <= 0;
            over_cnt <= 0; under_cnt <= 0;
            adclrc_d <= 1'b0; rx_bit <= 5'd0; rx_shift <= 16'h0000; rx_left <= 16'h0000;
            rx_push <= 1'b0; rx_push_left <= 16'h0000; rx_push_right <= 16'h0000;
            dadlrc_d <= 1'b0; tx_bit <= 5'd0; tx_l <= 16'h0000; tx_r <= 16'h0000;
            tx_shift <= 16'h0000; primed <= 1'b0; lat_cycles <= 16'd0;
            stamp_cur <= 16'd0; push_stamp <= 16'd0;
            req_t <= 1'b0; ack_sync1 <= 1'b0; ack_sync2 <= 1'b0;
            snap_occ <= 16'd0; snap_over <= 16'd0; snap_under <= 16'd0;
            snap_adc <= 32'd0; snap_dac <= 32'd0; snap_cyc <= 32'd0; snap_lat <= 16'd0;
        end else begin
            cycle_cnt <= cycle_cnt + 1'b1;
            rx_push   <= 1'b0;

            // ---- receive -------------------------------------------------
            if (adclrc != adclrc_d) begin
                if (adclrc_d == 1'b0) begin
                    rx_left <= rx_shift;                  // left word complete
                end else begin
                    rx_push_left  <= rx_left;             // right word complete
                    rx_push_right <= rx_shift;
                    rx_push       <= 1'b1;
                    // The frame that just completed started at the previous
                    // left-slot change, which is the current stamp_cur.
                    push_stamp    <= stamp_cur;
                end
                adclrc_d <= adclrc;
                rx_bit   <= 5'd0;                         // change edge itself is skipped
                rx_shift <= 16'h0000;
                if (adclrc == 1'b0) stamp_cur <= cycle_cnt[15:0];   // new frame starts
            end else if (rx_bit < 5'd16) begin
                rx_shift <= {rx_shift[14:0], adcdat};
                rx_bit   <= rx_bit + 1'b1;
            end

            // ---- push one stereo frame ----------------------------------
            if (rx_push) begin
                if (fifo_full) begin
                    over_cnt <= over_cnt + 1'b1;          // keep the stream, drop oldest-free slot
                end else begin
                    fifo_mem[wr_ptr[AW-1:0]]  <= {rx_push_left, rx_push_right};
                    stamp_mem[wr_ptr[AW-1:0]] <= push_stamp;
                    wr_ptr   <= wr_ptr + 1'b1;
                    wr_total <= wr_total + 1'b1;
                end
            end

            // ---- transmit -----------------------------------------------
            // The DAC stays silent until the FIFO holds a full depth, so the
            // start-up transient is not counted as an underrun. After that the
            // occupancy (and therefore the L1 latency) stays at FIFO_DEPTH.
            if (occ_calc >= DEPTH[AW:0]) primed <= 1'b1;
            if (dadlrc != dadlrc_d) begin
                dadlrc_d <= dadlrc;
                tx_bit   <= 5'd0;
                if (dadlrc == 1'b0) begin
                    // Start of a new frame: take the oldest buffered frame.
                    if (!primed || fifo_empty) begin
                        if (primed) under_cnt <= under_cnt + 1'b1;
                        tx_l     <= 16'h0000;
                        tx_r     <= 16'h0000;
                        tx_shift <= 16'h0000;
                    end else begin
                        tx_l     <= fifo_mem[rd_ptr[AW-1:0]][31:16];
                        tx_r     <= fifo_mem[rd_ptr[AW-1:0]][15:0];
                        tx_shift <= fifo_mem[rd_ptr[AW-1:0]][31:16];
                        // L1: BCLK cycles between capture and emission. The
                        // subtraction wraps safely because the depth is small.
                        lat_cycles <= cycle_cnt[15:0] - stamp_mem[rd_ptr[AW-1:0]];
                        rd_ptr   <= rd_ptr + 1'b1;
                        rd_total <= rd_total + 1'b1;
                    end
                end else begin
                    tx_shift <= tx_r;                     // right channel word
                end
            end else begin
                tx_bit <= tx_bit + 1'b1;
                if (tx_bit < 5'd15) tx_shift <= {tx_shift[14:0], 1'b0};
            end

            // ---- snapshot handshake --------------------------------------
            ack_sync1 <= ack_t_sys;
            ack_sync2 <= ack_sync1;
            if (req_t == ack_sync2) begin
                snap_occ   <= occ_calc;
                snap_lat   <= lat_cycles;
                snap_adc   <= wr_total;
                snap_dac   <= rd_total;
                snap_over  <= over_cnt;
                snap_under <= under_cnt;
                snap_cyc   <= cycle_cnt;
                req_t      <= ~req_t;
            end
        end
    end

    // Bit currently presented to the codec (MSB first for 16 BCLK, then zeros).
    wire tx_bit_c = (tx_bit <= 5'd15) ? tx_shift[15] : 1'b0;

    // Emitted on the falling edge: the codec samples it on the next rising edge,
    // which is exactly the I2S one-BCLK delay after the LRCLK change.
    reg dacdat_r;
    always @(negedge bclk or posedge bclk_reset) begin
        if (bclk_reset) dacdat_r <= 1'b0;
        else            dacdat_r <= tx_bit_c;
    end
    assign dacdat = dacdat_r;

    // ------------------------------------------------------------------
    // Snapshot consumption (system domain)
    // ------------------------------------------------------------------
    always @(posedge clk_sys) begin
        if (!reset_n_sys) begin
            req_sync1 <= 1'b0; req_sync2 <= 1'b0; req_seen <= 1'b0; ack_t_sys <= 1'b0;
            stale_cnt <= 20'd0; live <= 1'b0;
            occ_frames <= 16'd0; latency_cycles <= 16'd0;
            adc_frames <= 32'd0; dac_frames <= 32'd0;
            overruns <= 16'd0; underruns <= 16'd0; bclk_cycles <= 32'd0;
        end else begin
            req_sync1 <= req_t;
            req_sync2 <= req_sync1;
            if (req_sync2 != req_seen) begin
                req_seen    <= req_sync2;
                ack_t_sys   <= req_sync2;
                occ_frames  <= snap_occ;
                latency_cycles <= snap_lat;
                adc_frames  <= snap_adc;
                dac_frames  <= snap_dac;
                overruns    <= snap_over;
                underruns   <= snap_under;
                bclk_cycles <= snap_cyc;
                stale_cnt   <= 20'd0;
                live        <= 1'b1;
            end else if (stale_cnt >= STALE_CYCLES[19:0]) begin
                live <= 1'b0;
            end else begin
                stale_cnt <= stale_cnt + 1'b1;
            end
        end
    end
endmodule
