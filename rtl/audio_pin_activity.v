`timescale 1ns/1ps
// Edge counter for the audio pins. Used to find out which ball actually carries
// a signal when a module is wired by hand: the counters are read over AXI and a
// ball with clock-like activity stands out immediately.
module audio_pin_activity #(
    parameter integer W = 32
) (
    input  wire         clk,
    input  wire         reset_n,
    input  wire [3:0]   pins,      // {adcdat, dadlrc, adclrc, bclk}
    output reg  [W-1:0] count0,
    output reg  [W-1:0] count1,
    output reg  [W-1:0] count2,
    output reg  [W-1:0] count3
);
    reg [3:0] s1, s2, s3;

    always @(posedge clk) begin
        if (!reset_n) begin
            s1 <= 4'd0; s2 <= 4'd0; s3 <= 4'd0;
            count0 <= {W{1'b0}}; count1 <= {W{1'b0}};
            count2 <= {W{1'b0}}; count3 <= {W{1'b0}};
        end else begin
            s1 <= pins;
            s2 <= s1;
            s3 <= s2;
            if (s2[0] != s3[0]) count0 <= count0 + 1'b1;
            if (s2[1] != s3[1]) count1 <= count1 + 1'b1;
            if (s2[2] != s3[2]) count2 <= count2 + 1'b1;
            if (s2[3] != s3[3]) count3 <= count3 + 1'b1;
        end
    end
endmodule
