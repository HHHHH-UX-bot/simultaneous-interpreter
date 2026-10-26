`timescale 1ns/1ps
// Streaming frame/window controller around the real, independently verified HLS cores.
module speaker_pipeline(
 input wire clk,input wire reset,input wire [15:0] pcm_data,input wire pcm_valid,
 input wire model_ready,input wire [31:0] energy_threshold,
 output wire test_ready,
 output wire [12:0] model_addr0,output wire model_ce0,input wire [31:0] model_q0,
 output wire [12:0] model_addr1,output wire model_ce1,input wire [31:0] model_q1,
 input wire [5:0] embedding_read_index,output wire [15:0] embedding_read_data,
 input wire [3:0] mfcc_read_index,output wire [15:0] mfcc_read_data,
 input wire debug_feature_ce,input wire [10:0] debug_feature_addr,output wire [15:0] debug_feature_q,
 output reg [31:0] sample_count,output reg [31:0] frame_count,
 output reg [31:0] embedding_sequence,output reg [31:0] frame_drops,output reg [31:0] cnn_drops,
 output reg [31:0] sample_collisions,output reg [31:0] voice_frames,
 output reg [31:0] last_energy,output reg [31:0] last_window_sample,
 output reg [31:0] mfcc_cycles,output reg [31:0] cnn_cycles,
 output reg [7:0] window_voice_count,output reg [31:0] cnn_error,
 output reg mfcc_busy,output reg cnn_busy,output wire snapshot_busy
);
 (* ram_style="block" *) reg [15:0] ring[0:399],frame_ram[0:399];
 (* ram_style="block" *) reg [15:0] history[0:2047],cnn_input[0:2047];
 (* ram_style="distributed" *) reg [15:0] embeddings[0:127];
 reg [15:0] last_mfcc[0:15];
 reg [8:0] ring_write,filled,copy_read,copy_issue,copy_dest;
 reg [7:0] hop;
 reg copy_busy,copy_valid,mfcc_start;
 reg [15:0] copy_q,pcm_q;
 reg [31:0] pending_frame_sample,mfcc_timer,cnn_timer;
 wire [8:0] pcm_addr;wire pcm_ce,mfcc_done,mfcc_idle,mfcc_ready;
 wire [3:0] mfcc_addr;wire mfcc_ce,mfcc_we;wire [15:0] mfcc_data;
 wire [31:0] mfcc_energy;wire mfcc_energy_valid;
 reg [127:0] voice_bits;reg [7:0] voiced;
 reg [5:0] cnn_hop;
 reg cnn_copy_busy,cnn_copy_valid,cnn_start,published_bank;
 reg [11:0] cnn_copy_issue;
 reg [10:0] cnn_copy_read,cnn_copy_dest;
 reg [15:0] cnn_copy_q,feature_q;
 reg [31:0] pending_cnn_sample,pending_cnn_status;
 reg [7:0] pending_cnn_voiced;
 wire [10:0] feature_addr;wire feature_ce;
 wire [5:0] embed_addr;wire embed_ce,embed_we;wire [15:0] embed_data;
 wire cnn_done,cnn_idle,cnn_ready,status_valid;wire [31:0] status;
 wire voice_now=last_energy>energy_threshold;
 wire [7:0] voiced_next=voiced+(voice_now?8'd1:8'd0)-
                  ((frame_count>=128 && voice_bits[frame_count[6:0]])?8'd1:8'd0);
 wire [6:0] oldest_row=frame_count[6:0]+7'd1;
 assign test_ready=!copy_busy&&!mfcc_busy&&!cnn_copy_busy&&!cnn_busy;
 assign snapshot_busy=copy_busy||cnn_copy_busy;
 assign embedding_read_data=embeddings[{published_bank,embedding_read_index}];
 assign mfcc_read_data=last_mfcc[mfcc_read_index];
 assign debug_feature_q=feature_q;
 wire feature_read_enable=cnn_busy?feature_ce:debug_feature_ce;
 wire [10:0] feature_read_address=cnn_busy?feature_addr:debug_feature_addr;
 speaker_mfcc_core mfcc(
  .ap_clk(clk),.ap_rst(reset),.ap_start(mfcc_start),.ap_done(mfcc_done),.ap_idle(mfcc_idle),.ap_ready(mfcc_ready),
  .pcm_address0(pcm_addr),.pcm_ce0(pcm_ce),.pcm_q0(pcm_q),
  .mfcc_address0(mfcc_addr),.mfcc_ce0(mfcc_ce),.mfcc_we0(mfcc_we),.mfcc_d0(mfcc_data),
  .energy(mfcc_energy),.energy_ap_vld(mfcc_energy_valid));
 speaker_cnn_core cnn(
  .ap_clk(clk),.ap_rst(reset),.ap_start(cnn_start),.ap_done(cnn_done),.ap_idle(cnn_idle),.ap_ready(cnn_ready),
  .features_address0(feature_addr),.features_ce0(feature_ce),.features_q0(feature_q),
  .model_address0(model_addr0),.model_ce0(model_ce0),.model_q0(model_q0),
  .model_address1(model_addr1),.model_ce1(model_ce1),.model_q1(model_q1),
  .embedding_address0(embed_addr),.embedding_ce0(embed_ce),.embedding_we0(embed_we),.embedding_d0(embed_data),
  .status(status),.status_ap_vld(status_valid));
 // Synchronous memory ports match ap_memory's one-cycle read latency.
 always @(posedge clk) begin
  if(!reset) begin
   if(pcm_valid)ring[ring_write]<=pcm_data;
   if(copy_busy && copy_issue<400)copy_q<=ring[copy_read];
   if(copy_valid)frame_ram[copy_dest]<=copy_q;
   if(pcm_ce)pcm_q<=frame_ram[pcm_addr];
   if(mfcc_ce && mfcc_we)begin
    history[{frame_count[6:0],mfcc_addr}]<=mfcc_data;
    last_mfcc[mfcc_addr]<=mfcc_data;
   end
   if(cnn_copy_busy && cnn_copy_issue<2048)cnn_copy_q<=history[cnn_copy_read];
   if(cnn_copy_valid)cnn_input[cnn_copy_dest]<=cnn_copy_q;
   if(feature_read_enable)feature_q<=cnn_input[feature_read_address];
   if(embed_ce && embed_we)embeddings[{!published_bank,embed_addr}]<=embed_data;
  end
 end
 always @(posedge clk) begin
  mfcc_start<=0;cnn_start<=0;copy_valid<=0;cnn_copy_valid<=0;
  if(reset) begin
   sample_count<=0;frame_count<=0;embedding_sequence<=0;frame_drops<=0;cnn_drops<=0;
   sample_collisions<=0;voice_frames<=0;last_energy<=0;last_window_sample<=0;
   mfcc_cycles<=0;cnn_cycles<=0;window_voice_count<=0;cnn_error<=0;
   ring_write<=0;filled<=0;hop<=0;copy_busy<=0;mfcc_busy<=0;cnn_busy<=0;
   copy_read<=0;copy_issue<=0;copy_dest<=0;pending_frame_sample<=0;
   mfcc_timer<=0;cnn_timer<=0;voice_bits<=0;voiced<=0;cnn_hop<=0;
   cnn_copy_busy<=0;cnn_copy_issue<=0;cnn_copy_read<=0;cnn_copy_dest<=0;
   published_bank<=0;pending_cnn_sample<=0;pending_cnn_voiced<=0;pending_cnn_status<=0;
  end else begin
   if(pcm_valid) begin
    sample_count<=sample_count+1'b1;ring_write<=ring_write==399?0:ring_write+1'b1;
    if(copy_busy)sample_collisions<=sample_collisions+1'b1;
    if(filled<400)filled<=filled+1'b1;
    if(filled>=400)hop<=hop==159?0:hop+1'b1;
    if(filled==399 || (filled>=400 && hop==159)) begin
     if(copy_busy || mfcc_busy)frame_drops<=frame_drops+1'b1;
     else begin
      copy_busy<=1;copy_issue<=0;copy_read<=ring_write==399?0:ring_write+1'b1;
      pending_frame_sample<=sample_count+1'b1;
     end
    end
   end
   if(copy_busy && copy_issue<400)begin
    copy_valid<=1;copy_dest<=copy_issue;copy_issue<=copy_issue+1'b1;
    copy_read<=copy_read==399?0:copy_read+1'b1;
   end
   if(copy_valid && copy_dest==399)begin
    copy_busy<=0;mfcc_start<=1;mfcc_busy<=1;mfcc_timer<=0;
   end
   if(mfcc_busy)mfcc_timer<=mfcc_timer+1'b1;
   if(mfcc_energy_valid)last_energy<=mfcc_energy;
   if(mfcc_done)begin
    mfcc_busy<=0;mfcc_cycles<=mfcc_timer;frame_count<=frame_count+1'b1;
    voice_bits[frame_count[6:0]]<=voice_now;voiced<=voiced_next;
    if(voice_now)voice_frames<=voice_frames+1'b1;
    if(frame_count>=127)begin
     cnn_hop<=cnn_hop+1'b1;
     if(frame_count==127 || cnn_hop==63)begin
      cnn_hop<=0;
      if(model_ready && voiced_next>=64)begin
       if(cnn_busy || cnn_copy_busy)cnn_drops<=cnn_drops+1'b1;
       else begin
        cnn_copy_busy<=1;cnn_copy_issue<=0;cnn_copy_read<={oldest_row,4'b0};
        pending_cnn_sample<=pending_frame_sample;pending_cnn_voiced<=voiced_next;
       end
      end
     end
    end
   end
   if(cnn_copy_busy && cnn_copy_issue<2048)begin
    cnn_copy_valid<=1;cnn_copy_dest<=cnn_copy_issue[10:0];cnn_copy_issue<=cnn_copy_issue+1'b1;
    cnn_copy_read<=cnn_copy_read+1'b1;
   end
   if(cnn_copy_valid && cnn_copy_dest==2047)begin
    cnn_copy_busy<=0;cnn_start<=1;cnn_busy<=1;cnn_timer<=0;pending_cnn_status<=0;
   end
   if(cnn_busy)cnn_timer<=cnn_timer+1'b1;
   if(status_valid)pending_cnn_status<=status;
   if(cnn_done)begin
    cnn_busy<=0;cnn_cycles<=cnn_timer;cnn_error<=status_valid?status:pending_cnn_status;
    published_bank<=!published_bank;embedding_sequence<=embedding_sequence+1'b1;
    last_window_sample<=pending_cnn_sample;window_voice_count<=pending_cnn_voiced;
   end
  end
 end
endmodule
