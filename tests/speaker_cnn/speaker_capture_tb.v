`timescale 1ns/1ps
module speaker_capture_tb;
 reg clk=0;always #10 clk=~clk;
 reg reset=1,enable=1,toggle=0;reg [15:0] sample=0;
 wire [15:0] pcm;wire valid;wire [31:0] overruns;
 reg [15:0] input_values[0:2048],expected_values[0:682];
 integer i,seen=0;reg checking=1;
 speaker_capture dut(.clk(clk),.reset(reset),.enable(enable),.sample_data(sample),.sample_toggle(toggle),.pcm_data(pcm),.pcm_valid(valid),.overruns(overruns));
 always @(posedge clk)if(valid&&checking)begin
  if(seen>=683||pcm!==expected_values[seen])$fatal(1,"FIR mismatch %d actual=%h expected=%h",seen,pcm,expected_values[seen]);
  seen=seen+1;
 end
 initial begin
  $readmemh("input.mem",input_values);$readmemh("expected.mem",expected_values);
  repeat(10)@(negedge clk);reset=0;repeat(100)@(negedge clk);
  for(i=0;i<2049;i=i+1)begin
   sample=input_values[i];toggle=~toggle;repeat(100)@(negedge clk);
  end
  repeat(100)@(negedge clk);
  if(seen!=683||overruns!=0)$fatal(1,"Capture output count/overrun wrong");
  checking=0;
  for(i=0;i<20;i=i+1)begin toggle=~toggle;repeat(2)@(negedge clk);end
  repeat(100)@(negedge clk);if(overruns==0)$fatal(1,"Overrun was not detected");
  $display("PASS_SPEAKER_FIR_CAPTURE");$finish;
 end
 initial begin #10000000;$fatal(1,"FIR simulation timeout");end
endmodule
