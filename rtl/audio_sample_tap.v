`timescale 1ns/1ps
// Cross the codec ADC sample from the BCLK domain into the 50 MHz system domain.
//
// The producer toggles `sample_toggle` once per captured stereo frame and holds
// `sample_data` stable for the whole BCLK period, so the consumer can capture
// the data whenever it sees the synchronised toggle change. No FIFO is needed:
// the PS polls at far above 48 kHz and picks every third new sample for 16 kHz.
module audio_sample_tap (
    input  wire        reset_n,
    // BCLK domain (from i2s_loopback).
    input  wire        bclk,
    input  wire [15:0] adc_sample,
    input  wire        adc_toggle,
    // System domain.
    input  wire        clk,
    output reg  [15:0] sample_data,
    output reg         sample_toggle,   // toggles once per new sample in `clk`
    output reg  [31:0] sample_count     // captured frames seen by the system side
);
    reg t1, t2, t3;
    reg [15:0] data1, data2;

    // Synchronise the toggle (2 FF) and sample the data through 2 pipelines so
    // the data captured at the toggle edge is the value that was stable then.
    always @(posedge clk) begin
        if (!reset_n) begin
            t1 <= 1'b0; t2 <= 1'b0; t3 <= 1'b0;
            data1 <= 16'h0000; data2 <= 16'h0000;
            sample_data <= 16'h0000; sample_toggle <= 1'b0; sample_count <= 32'd0;
        end else begin
            t1 <= adc_toggle;
            t2 <= t1;
            t3 <= t2;
            data1 <= adc_sample;
            data2 <= data1;
            if (t2 != t3) begin
                sample_data   <= data2;
                sample_toggle <= ~sample_toggle;
                sample_count  <= sample_count + 1'b1;
            end
        end
    end
endmodule
