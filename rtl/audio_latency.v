`timescale 1ns/1ps
// L1 latency presentation: measured BCLK cycles -> microseconds.
//
//   us = cycles * 1e6 / BCLK_HZ
//
// evaluated in hardware as (cycles * LAT_FACTOR) >> 8 with
// LAT_FACTOR = round(1e6 * 256 / BCLK_HZ)  (83 for the module's 3.072 MHz BCLK),
// which needs one multiplier and one shift. Values saturate so a stalled stream
// cannot wrap the display.
module audio_latency #(
    parameter BCLK_HZ    = 3072000,
    parameter MAX_US     = 999999,
    parameter LAT_FACTOR = (1000000 * 256) / BCLK_HZ
) (
    input  wire        clk,
    input  wire        reset_n,
    input  wire        live,             // BCLK domain is producing snapshots
    input  wire [15:0] cycles,           // capture -> emission, in BCLK cycles
    input  wire        clear_extremes,   // pulse: reset min/max
    output reg  [19:0] latency_us,
    output reg  [19:0] min_us,
    output reg  [19:0] max_us,
    output reg         max_valid
);
    wire [37:0] scaled  = cycles * LAT_FACTOR[20:0];
    wire [29:0] us_calc = scaled[37:8];
    wire [19:0] us_sat  = (us_calc > MAX_US) ? MAX_US[19:0] : us_calc[19:0];

    always @(posedge clk) begin
        if (!reset_n) begin
            latency_us <= 20'd0;
            min_us     <= 20'hFFFFF;
            max_us     <= 20'd0;
            max_valid  <= 1'b0;
        end else begin
            latency_us <= live ? us_sat : 20'd0;
            if (clear_extremes) begin
                min_us    <= 20'hFFFFF;
                max_us    <= 20'd0;
                max_valid <= 1'b0;
            end else if (live) begin
                if (us_sat < min_us) min_us <= us_sat;
                if (!max_valid || us_sat > max_us) begin
                    max_us    <= us_sat;
                    max_valid <= 1'b1;
                end
            end
        end
    end
endmodule
