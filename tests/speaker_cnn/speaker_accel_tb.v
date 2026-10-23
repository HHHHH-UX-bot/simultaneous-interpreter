`timescale 1ns/1ps
module speaker_accel_tb;
 reg clk=0;always #10 clk=~clk;
 reg reset_n=0;reg [15:0] awaddr=0,araddr=0;
 reg awvalid=0,wvalid=0,arvalid=0;reg [31:0] wdata=0;reg [3:0] wstrb=15;
 wire awready,wready,bvalid,arready,rvalid;wire [1:0] bresp,rresp;wire [31:0] rdata;
 reg [31:0] model[0:8191];reg [15:0] pcm[0:20719],features[0:2047],embedding[0:63];
 `include "fixture_config.vh"
 speaker_accel dut(.clk(clk),.reset_n(reset_n),.sample_data(16'd0),.sample_toggle(1'b0),
  .s_axi_awaddr(awaddr),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
  .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
  .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(1'b1),
  .s_axi_araddr(araddr),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
  .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(1'b1));
 task write32;
  input [15:0] addr;input [31:0] value;input [3:0] strobe;input integer order;input [1:0] expected;
  integer timeout;reg adone,wdone;
  begin
   @(negedge clk);awaddr=addr;wdata=value;wstrb=strobe;adone=0;wdone=0;
   awvalid=order!=2;wvalid=order!=1;timeout=0;
   while(!(adone&&wdone)&&timeout<1000)begin
    @(posedge clk);if(awvalid&&awready)adone=1;if(wvalid&&wready)wdone=1;
    @(negedge clk);if(adone)awvalid=0;if(wdone)wvalid=0;
    if(order==1&&adone&&timeout>=3)wvalid=1;
    if(order==2&&wdone&&timeout>=3)awvalid=1;
    if(wdone)wvalid=0;if(adone)awvalid=0;
    timeout=timeout+1;
   end
   if(timeout>=1000)$fatal(1,"AXI write channels timed out");
   while(!bvalid&&timeout<2000)begin @(negedge clk);timeout=timeout+1;end
   if(!bvalid||bresp!==expected)$fatal(1,"AXI BRESP addr=%h actual=%d expected=%d",addr,bresp,expected);
   @(negedge clk);
  end
 endtask
 task read32;
  input [15:0] addr;output [31:0] value;integer timeout;
  begin
   @(negedge clk);araddr=addr;arvalid=1;timeout=0;
   while(!arready&&timeout<1000)begin @(negedge clk);timeout=timeout+1;end
   @(posedge clk);@(negedge clk);arvalid=0;
   while(!rvalid&&timeout<2000)begin @(negedge clk);timeout=timeout+1;end
   if(!rvalid||rresp!==0)$fatal(1,"AXI read failed addr=%h resp=%d",addr,rresp);
   value=rdata;@(negedge clk);
  end
 endtask
 integer i,j,count,batch;reg [31:0] value;
 initial begin
  $readmemh("model.mem",model);$readmemh("pcm.mem",pcm);
  $readmemh("features.mem",features);$readmemh("embedding.mem",embedding);
  repeat(10)@(negedge clk);reset_n=1;repeat(5)@(negedge clk);
  read32(0,value);if(value!==32'h314b5053)$fatal(1,"Wrong core ID");
  write32(8,1,15,1,2); // Inference cannot run with an unverified model.
  write32(16'h8001,32'h12345678,15,2,2);
  read32(16'h10,value);if(value!==0)$fatal(1,"Unaligned model write changed count");
  for(i=0;i<7076;i=i+1)write32(16'h8000+i*4,model[i],15,0,0);
  read32(16'h14,value);if(value!==EXPECTED_HASH)$fatal(1,"Model FNV differs");
  for(i=0;i<7076;i=i+1)begin
   read32(16'h8000+i*4,value);if(value!==model[i])$fatal(1,"Model readback differs %d",i);
  end
  write32(16'h1c,EXPECTED_HASH,15,2,0);
  write32(16'h28,0,15,1,0); // Numerical fixture: include every nonzero energy frame.
  write32(8,3,15,0,0);
  i=0;
  while(i<20720)begin
   read32(16'h24,value);
   if(value>448)repeat(128)@(negedge clk);
   else begin
    batch=20720-i;if(batch>64)batch=64;
    for(j=0;j<batch;j=j+1)write32(16'h20,{16'd0,pcm[i+j]},15,0,0);
    i=i+batch;
   end
  end
  count=0;value=0;
  while(value==0&&count<100000)begin repeat(128)@(negedge clk);read32(16'h34,value);count=count+1;end
  if(value!==1)$fatal(1,"CNN did not finish exactly one window");
  read32(16'h2c,value);if(value!==20720)$fatal(1,"Sample count wrong %d",value);
  read32(16'h30,value);if(value!==128)$fatal(1,"Frame count wrong %d",value);
  read32(16'h50,value);if(value!==20720)$fatal(1,"Window source alignment wrong");
  for(i=0;i<64;i=i+1)begin
   read32(16'h100+i*4,value);if(value[15:0]!==embedding[i])$fatal(1,"Embedding differs %d %h %h",i,value,embedding[i]);
  end
  for(i=0;i<2048;i=i+1)begin
   read32(16'h1000+i*4,value);if(value[15:0]!==features[i])$fatal(1,"Feature window differs %d %h %h",i,value,features[i]);
  end
  for(i=0;i<16;i=i+1)begin
   read32(16'h200+i*4,value);if(value[15:0]!==features[2032+i])$fatal(1,"Latest MFCC differs");
  end
  for(i=16'h38;i<=16'h44;i=i+4)begin read32(i,value);if(value!==0)$fatal(1,"Stream drop/collision %h %d",i,value);end
  read32(16'h60,value);if(value!==0)$fatal(1,"CNN error");
  write32(8,7,15,0,0);repeat(5)@(negedge clk);
  read32(16'h34,value);if(value!==0)$fatal(1,"Pipeline reset did not clear result");
  read32(16'h1c,value);if(value!==1)$fatal(1,"Pipeline reset erased verified model");
  $display("PASS_SPEAKER_FULL_AXI_MFCC_CNN");$finish;
 end
 initial begin #1000000000;$fatal(1,"Speaker simulation deadline");end
endmodule
