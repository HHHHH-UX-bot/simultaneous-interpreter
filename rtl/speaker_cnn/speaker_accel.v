`timescale 1ns/1ps
// AXI-Lite control, verified local model RAM, and an independent microphone branch.
module speaker_accel(
 input wire clk,input wire reset_n,input wire [15:0] sample_data,input wire sample_toggle,
 input wire [15:0] s_axi_awaddr,input wire s_axi_awvalid,output wire s_axi_awready,
 input wire [31:0] s_axi_wdata,input wire [3:0] s_axi_wstrb,input wire s_axi_wvalid,output wire s_axi_wready,
 output reg [1:0] s_axi_bresp,output reg s_axi_bvalid,input wire s_axi_bready,
 input wire [15:0] s_axi_araddr,input wire s_axi_arvalid,output wire s_axi_arready,
 output reg [31:0] s_axi_rdata,output reg [1:0] s_axi_rresp,output reg s_axi_rvalid,input wire s_axi_rready
);
 reg aw_pending,w_pending;
 reg [15:0] write_addr;reg [31:0] write_data;reg [3:0] write_strobe;
 wire write_commit=aw_pending&&w_pending&&!s_axi_bvalid;
 assign s_axi_awready=!aw_pending&&!s_axi_bvalid;
 assign s_axi_wready=!w_pending&&!s_axi_bvalid;
 reg [1:0] control;reg pipeline_clear;
 reg [31:0] threshold,model_hash,model_count,model_identity,model_magic,model_version,model_words;
 reg model_valid,model_fault;
 wire reset_pipeline=!reset_n||!control[0]||pipeline_clear;
 (* ram_style="block" *) reg [31:0] model_ram[0:8191];
 wire [12:0] model_addr0,model_addr1;wire model_ce0,model_ce1;
 reg [31:0] model_q0,model_q1;
 wire mfcc_busy,cnn_busy,snapshot_busy,pipe_ready;
 wire model_write=write_commit&&write_addr[15]&&write_addr[1:0]==0&&write_strobe==4'hf&&!control[0];
 wire [12:0] model_write_index=write_addr[14:2];
 reg read_pending,read_ready;reg [1:0] read_kind;reg [15:0] read_addr;
 assign s_axi_arready=!s_axi_rvalid&&!read_pending&&!read_ready;
 wire model_read=read_pending&&read_kind==1;
 wire feature_read=read_pending&&read_kind==2;
 wire [15:0] feature_q,embedding_value,mfcc_value;
 wire [10:0] debug_feature_index=(read_addr-16'h1000)>>2;
 (* ram_style="distributed" *) reg [15:0] input_fifo[0:511];
 reg [8:0] fifo_write,fifo_read;reg [9:0] fifo_count;
 wire fifo_push=write_commit&&write_addr==16'h20&&write_strobe==4'hf&&control==2'b11&&fifo_count<512;
 wire fifo_pop=control==2'b11&&fifo_count!=0&&pipe_ready;
 wire [15:0] live_pcm;wire live_valid;wire [31:0] capture_overruns;
 wire pcm_valid=control[0]&&(control[1]?fifo_pop:live_valid);
 wire [15:0] pcm_data=control[1]?input_fifo[fifo_read]:live_pcm;
 wire [31:0] samples,frames,sequence,frame_drops,cnn_drops,collisions,voice_frames;
 wire [31:0] energy,window_sample,mfcc_cycles,cnn_cycles,cnn_error;wire [7:0] window_voice;
 speaker_capture capture(
  .clk(clk),.reset(reset_pipeline),.enable(control[0]&&!control[1]),
  .sample_data(sample_data),.sample_toggle(sample_toggle),
  .pcm_data(live_pcm),.pcm_valid(live_valid),.overruns(capture_overruns));
 speaker_pipeline pipeline(
  .clk(clk),.reset(reset_pipeline),.pcm_data(pcm_data),.pcm_valid(pcm_valid),
  .model_ready(model_valid),.energy_threshold(threshold),.test_ready(pipe_ready),
  .model_addr0(model_addr0),.model_ce0(model_ce0),.model_q0(model_q0),
  .model_addr1(model_addr1),.model_ce1(model_ce1),.model_q1(model_q1),
  .embedding_read_index(s_axi_araddr[7:2]),.embedding_read_data(embedding_value),
  .mfcc_read_index(s_axi_araddr[5:2]),.mfcc_read_data(mfcc_value),
  .debug_feature_ce(feature_read&&!cnn_busy),.debug_feature_addr(debug_feature_index),.debug_feature_q(feature_q),
  .sample_count(samples),.frame_count(frames),.embedding_sequence(sequence),.frame_drops(frame_drops),.cnn_drops(cnn_drops),
  .sample_collisions(collisions),.voice_frames(voice_frames),.last_energy(energy),.last_window_sample(window_sample),
  .mfcc_cycles(mfcc_cycles),.cnn_cycles(cnn_cycles),.window_voice_count(window_voice),.cnn_error(cnn_error),
  .mfcc_busy(mfcc_busy),.cnn_busy(cnn_busy),.snapshot_busy(snapshot_busy));
 // During inference both physical BRAM ports serve the CNN. Weight updates
 // and host readback are accepted only while inference is disabled.
 wire [12:0] model_port_a_addr=cnn_busy?model_addr0:model_write_index;
 wire [12:0] model_port_b_addr=cnn_busy?model_addr1:read_addr[14:2];
 wire model_port_a_ce=model_write||(cnn_busy&&model_ce0);
 wire model_port_b_ce=cnn_busy?model_ce1:model_read;
 always @(posedge clk)begin
  if(model_port_a_ce)begin
   if(model_write)model_ram[model_port_a_addr]<=write_data;
   model_q0<=model_ram[model_port_a_addr];
  end
  if(model_port_b_ce)model_q1<=model_ram[model_port_b_addr];
  if(fifo_push)input_fifo[fifo_write]<=write_data[15:0];
 end
 always @(posedge clk)begin
  if(reset_pipeline)begin fifo_write<=0;fifo_read<=0;fifo_count<=0;end
  else begin
   if(fifo_push)fifo_write<=fifo_write+1'b1;
   if(fifo_pop)fifo_read<=fifo_read+1'b1;
   case({fifo_push,fifo_pop})
    2'b10:fifo_count<=fifo_count+1'b1;
    2'b01:fifo_count<=fifo_count-1'b1;
    default:fifo_count<=fifo_count;
   endcase
  end
 end
 wire [31:0] hash_seed=model_write_index==0?32'h811c9dc5:model_hash;
 wire [31:0] hash_xor=hash_seed^write_data;
 wire [31:0] hash_next=hash_xor*32'd16777619;
 always @(posedge clk)begin
  pipeline_clear<=0;
  if(!reset_n)begin
   aw_pending<=0;w_pending<=0;write_addr<=0;write_data<=0;write_strobe<=0;
   s_axi_bvalid<=0;s_axi_bresp<=0;control<=0;threshold<=2000;
   model_hash<=32'h811c9dc5;model_count<=0;model_identity<=0;
   model_magic<=0;model_version<=0;model_words<=0;model_valid<=0;model_fault<=0;
  end else begin
   if(s_axi_bvalid&&s_axi_bready)s_axi_bvalid<=0;
   if(s_axi_awvalid&&s_axi_awready)begin write_addr<=s_axi_awaddr;aw_pending<=1;end
   if(s_axi_wvalid&&s_axi_wready)begin write_data<=s_axi_wdata;write_strobe<=s_axi_wstrb;w_pending<=1;end
   if(write_commit)begin
    aw_pending<=0;w_pending<=0;s_axi_bvalid<=1;s_axi_bresp<=0;
    if(write_strobe!=4'hf || write_addr[1:0]!=0)s_axi_bresp<=2;
    else if(write_addr[15])begin
     if(control[0])s_axi_bresp<=2;
     else begin
      model_valid<=0;
      if(model_write_index==0)begin model_count<=1;model_hash<=hash_next;model_fault<=0;end
      else if(model_write_index==model_count&&model_count<7076)begin
       model_count<=model_count+1'b1;model_hash<=hash_next;
      end else begin model_fault<=1;s_axi_bresp<=2;end
      case(model_write_index)
       0:model_magic<=write_data;1:model_version<=write_data;
       2:model_words<=write_data;3:model_identity<=write_data;
      endcase
     end
    end else case(write_addr)
     16'h08:begin
      if(write_data[31:3]!=0 || (write_data[0]&&!model_valid))s_axi_bresp<=2;
      else begin
       control<=write_data[1:0];
       if(write_data[2]||control!=write_data[1:0])pipeline_clear<=1;
      end
     end
     16'h1c:begin
      if(control[0]||model_fault||model_count!=7076||model_magic!=32'h314b5053||
         model_version!=1||model_words!=7076||model_hash!=write_data)begin
       model_valid<=0;model_fault<=1;s_axi_bresp<=2;
      end else model_valid<=1;
     end
     16'h20:if(!fifo_push)s_axi_bresp<=2;
     16'h28:threshold<=write_data;
     default:s_axi_bresp<=2;
    endcase
   end
  end
 end
 always @(posedge clk)begin
  if(!reset_n)begin
   read_pending<=0;read_ready<=0;read_kind<=0;read_addr<=0;
   s_axi_rvalid<=0;s_axi_rresp<=0;s_axi_rdata<=0;
  end else begin
   if(s_axi_rvalid&&s_axi_rready)s_axi_rvalid<=0;
   if(read_pending)begin read_pending<=0;read_ready<=1;end
   if(read_ready)begin
    read_ready<=0;s_axi_rvalid<=1;s_axi_rresp<=0;
    if(read_kind==1)begin
     if(cnn_busy)begin s_axi_rresp<=2;s_axi_rdata<=0;end else s_axi_rdata<=model_q1;
    end else if(cnn_busy||snapshot_busy)begin s_axi_rresp<=2;s_axi_rdata<=0;end
    else s_axi_rdata<={{16{feature_q[15]}},feature_q};
   end
   if(s_axi_arvalid&&s_axi_arready)begin
    read_addr<=s_axi_araddr;s_axi_rvalid<=1;s_axi_rresp<=0;s_axi_rdata<=0;
    if(s_axi_araddr[1:0]!=0)s_axi_rresp<=2;
    else if(s_axi_araddr[15])begin
     if(cnn_busy)s_axi_rresp<=2;
     else begin read_kind<=1;read_pending<=1;s_axi_rvalid<=0;end
    end else if(s_axi_araddr>=16'h1000&&s_axi_araddr<16'h3000)begin
     if(!control[1]||cnn_busy||snapshot_busy)s_axi_rresp<=2;
     else begin read_kind<=2;read_pending<=1;s_axi_rvalid<=0;end
    end else if(s_axi_araddr>=16'h100&&s_axi_araddr<16'h200)
     s_axi_rdata<=sequence?{{16{embedding_value[15]}},embedding_value}:0;
    else if(s_axi_araddr>=16'h200&&s_axi_araddr<16'h240)
     s_axi_rdata<=frames?{{16{mfcc_value[15]}},mfcc_value}:0;
    else case(s_axi_araddr)
     16'h00:s_axi_rdata<=32'h314b5053;
     16'h04:s_axi_rdata<=1;
     16'h08:s_axi_rdata<={30'd0,control};
     16'h0c:s_axi_rdata<={23'd0,control[0],control[1],model_fault,(fifo_count!=0),pipe_ready,snapshot_busy,cnn_busy,mfcc_busy,model_valid};
     16'h10:s_axi_rdata<=model_count;
     16'h14:s_axi_rdata<=model_hash;
     16'h18:s_axi_rdata<=model_identity;
     16'h1c:s_axi_rdata<=model_valid;
     16'h24:s_axi_rdata<={22'd0,fifo_count};
     16'h28:s_axi_rdata<=threshold;
     16'h2c:s_axi_rdata<=samples;
     16'h30:s_axi_rdata<=frames;
     16'h34:s_axi_rdata<=sequence;
     16'h38:s_axi_rdata<=frame_drops;
     16'h3c:s_axi_rdata<=cnn_drops;
     16'h40:s_axi_rdata<=collisions;
     16'h44:s_axi_rdata<=capture_overruns;
     16'h48:s_axi_rdata<=voice_frames;
     16'h4c:s_axi_rdata<=energy;
     16'h50:s_axi_rdata<=window_sample;
     16'h54:s_axi_rdata<={24'd0,window_voice};
     16'h58:s_axi_rdata<=mfcc_cycles;
     16'h5c:s_axi_rdata<=cnn_cycles;
     16'h60:s_axi_rdata<=cnn_error;
     default:s_axi_rresp<=2;
    endcase
   end
  end
 end
endmodule
