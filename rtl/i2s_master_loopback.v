`timescale 1ns/1ps
// I2S master loopback for the WM8731 module.
//
// The AN831 module carries its own 12.288 MHz crystal for the codec's MCLK but
// expects the host to provide BCLK/LRCLK, so the FPGA is the I2S master here.
// Everything is derived from clk (50 MHz): BCLK = clk/16 = 3.125 MHz and the
// frame is 64 BCLK (two 32-bit slots), giving 48.83 kHz. Both directions are in
// the same clock domain, so there is no clock-domain crossing at all.
//
// The codec's ADC data is buffered for DEPTH frames before being played back on
// its DAC, which is what makes the round trip work without a sample-rate
// converter. The buffered frames also give the measured L1 latency.
module i2s_master_loopback #(
    parameter integer DIV   = 16,       // clk cycles per BCLK period
    parameter integer SLOT  = 32,       // BCLK per channel slot
    parameter integer DEPTH = 2         // frames buffered before playback
) (
    input  wire        clk,
    input  wire        reset_n,
    input  wire        enable,   // 0 = hold the clocks while the codec is configured
    output reg         bclk,            // to the codec
    output reg         lrclk,           // to the codec (both LRCLK pins)
    output reg         dacdat,          // to the codec
    input  wire        adcdat,          // from the codec
    output reg  [15:0] occ_frames,
    output reg  [15:0] latency_cycles,
    output reg  [31:0] adc_frames,
    output reg  [31:0] dac_frames,
    output reg  [15:0] overruns,
    output reg  [15:0] underruns,
    output reg  [31:0] bclk_cycles,
    output wire        live
);
    localparam integer FRAME = 2 * SLOT;            // 64 BCLK per frame
    localparam integer HALF  = DIV / 2;            // BCLK high phase, in clk cycles
    localparam integer AW    = (DEPTH > 1) ? $clog2(DEPTH) : 1;

    reg [3:0]  cyc;            // position inside one BCLK period
    reg [5:0]  bitpos;         // 0..FRAME-1
    reg [63:0] rx_frame;       // received bits, bit 63 = first bit of the frame
    reg [63:0] tx_frame;       // bits to transmit this frame
    reg [31:0] cycle_cnt;      // BCLK periods since reset

    reg [15:0] fifo_l [0:DEPTH-1];
    reg [15:0] fifo_r [0:DEPTH-1];
    reg [15:0] stamp  [0:DEPTH-1];
    reg [AW-1:0] wp, rp;
    reg [1:0]  occ;
    reg        primed;

    wire [5:0] nxt = bitpos + 1'b1;
    wire       pop_ok  = primed && (occ != 2'd0);
    // The slot freed by this frame's playback can be reused immediately, which
    // keeps the FIFO two frames deep without counting a spurious overrun.
    wire       push_ok = (occ < DEPTH) || pop_ok;

    // The bit to drive for the upcoming bit period. I2S puts the MSB of each
    // sample one BCLK AFTER its LRCLK transition, so the frame is shifted by
    // one extra bit (the leading zero is that mandatory delay bit).
    wire [64:0] tx_shift = {tx_frame, 1'b0};
    wire [64:0] tx_aligned = tx_shift >> (7'd64 - nxt);

    assign live = primed;

    always @(posedge clk) begin
        if (!reset_n) begin
            cyc <= 4'd0; bitpos <= 6'd0; bclk <= 1'b0; lrclk <= 1'b0; dacdat <= 1'b0;
            rx_frame <= 64'd0; tx_frame <= 64'd0; cycle_cnt <= 32'd0;
            wp <= {AW{1'b0}}; rp <= {AW{1'b0}}; occ <= 2'd0; primed <= 1'b0;
            occ_frames <= 16'd0; latency_cycles <= 16'd0;
            adc_frames <= 32'd0; dac_frames <= 32'd0;
            overruns <= 16'd0; underruns <= 16'd0; bclk_cycles <= 32'd0;
        end else begin
            if (!enable) begin
                // Quiet bus during codec initialisation: no I2S clocks means no
                // return-current noise coupling into the I2C lines.
                cyc <= 4'd0; bitpos <= 6'd0; bclk <= 1'b0; lrclk <= 1'b0; dacdat <= 1'b0;
            end else begin
            cyc  <= cyc + 1'b1;
            bclk <= (cyc < HALF);

            // ---- falling edge: drive LRCLK and the next data bit ----------
            if (cyc == HALF) begin
                lrclk  <= (nxt >= SLOT[5:0]);
                dacdat <= tx_aligned[0];
            end

            // ---- sample the ADC data inside the high phase ---------------
            if (cyc == 4'd2) begin
                rx_frame[6'd63 - bitpos] <= adcdat;
            end

            // ---- end of a bit period ------------------------------------
            if (cyc == DIV - 1) begin
                bitpos      <= nxt;
                cycle_cnt   <= cycle_cnt + 1'b1;
                bclk_cycles <= bclk_cycles + 1'b1;

                if (nxt == 6'd0 && enable) begin
                    // Frame boundary: one frame is captured and one is played,
                    // so the occupancy only changes by the difference.
                    if (push_ok) begin
                        fifo_l[wp] <= rx_frame[62:47];
                        fifo_r[wp] <= rx_frame[30:15];
                        stamp[wp]  <= cycle_cnt[15:0];
                        wp         <= wp + 1'b1;
                        if (occ + 1'b1 >= DEPTH) primed <= 1'b1;
                    end else begin
                        overruns <= overruns + 1'b1;
                    end
                    if (pop_ok) begin
                        tx_frame       <= {fifo_l[rp], 16'd0, fifo_r[rp], 16'd0};
                        rp             <= rp + 1'b1;
                        latency_cycles <= cycle_cnt[15:0] - stamp[rp];
                        dac_frames     <= dac_frames + 1'b1;
                    end else if (primed) begin
                        underruns <= underruns + 1'b1;
                    end
                    occ        <= occ + (push_ok ? 1'b1 : 1'b0) - (pop_ok ? 1'b1 : 1'b0);
                    occ_frames <= {14'd0, occ + (push_ok ? 1'b1 : 1'b0) - (pop_ok ? 1'b1 : 1'b0)};
                    adc_frames <= adc_frames + 1'b1;
                end
            end
            end
        end
    end
endmodule
