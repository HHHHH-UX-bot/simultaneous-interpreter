`timescale 1ns/1ps
// Dedicated local speaker branch. Existing STT FIFO and codec controls are untouched.
module speaker_capture(
 input wire clk,input wire reset,input wire enable,
 input wire [15:0] sample_data,input wire sample_toggle,
 output reg [15:0] pcm_data,output reg pcm_valid,
 output reg [31:0] overruns
);
 `include "speaker_fir_rom.vh"
 (* ram_style="distributed" *) reg signed [15:0] taps[0:62];
 reg toggle_d,initialized,busy;
 reg [5:0] init_at,write_at,read_at,index;
 reg [1:0] decimate;
 reg signed [47:0] accumulator;
 wire sample_edge=sample_toggle!=toggle_d;
 wire signed [31:0] product=$signed(taps[read_at])*$signed(fir_coeff(index));
 wire signed [47:0] added=accumulator+{{16{product[31]}},product};
 wire signed [47:0] scaled=added>>>15;
 always @(posedge clk) begin
  pcm_valid<=0;
  if(reset) begin
   toggle_d<=sample_toggle;initialized<=0;busy<=0;init_at<=0;
   write_at<=0;read_at<=0;index<=0;decimate<=0;accumulator<=0;
   pcm_data<=0;overruns<=0;
  end else begin
   toggle_d<=sample_toggle;
   if(!initialized) begin
    taps[init_at]<=0;
    if(init_at==62)initialized<=1;else init_at<=init_at+1'b1;
   end else if(sample_edge && enable) begin
    if(busy)overruns<=overruns+1'b1;
    else begin
     taps[write_at]<=sample_data;read_at<=write_at;
     write_at<=write_at==62?0:write_at+1'b1;
     busy<=1;index<=0;accumulator<=0;
    end
   end
   if(busy) begin
    accumulator<=added;
    if(index==62) begin
     busy<=0;
     if(decimate==2) begin
      decimate<=0;pcm_valid<=1;
      if(scaled>32767)pcm_data<=16'h7fff;
      else if(scaled< -32768)pcm_data<=16'h8000;
      else pcm_data<=scaled[15:0];
     end else decimate<=decimate+1'b1;
    end else begin index<=index+1'b1;read_at<=read_at==0?62:read_at-1'b1;end
   end
  end
 end
endmodule
