`timescale 1ns/1ps
module framebuffer_case #(
    parameter W=1024,H=600,HS=20,HB=140,HT=1312,VS=3,VB=20,VT=635,
    parameter FAULTS=0
)(input wire clk,output reg done=0);
    reg reset_n=0;
    reg [31:0] awaddr=0,wdata=0,araddr=0;
    reg [3:0] wstrb=0;
    reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
    wire awready,wready,bvalid,arready,rvalid;
    wire [1:0] bresp,rresp;
    wire [31:0] rdata;
    reg source_active=0,previous_fsync=1,injected=0;
    integer source_index=0,epoch=0,observed=0,ox,oy,out_index;
    wire ready,fsync;
    wire [31:0] frame_count;
    wire hs,vs,de;
    wire [23:0] rgb;
    wire [11:0] hc,vc;
    wire drop_cycle=FAULTS && epoch==2 && source_index==W+5 && !injected;
    wire wrong_last=FAULTS && epoch==3 && source_index==2*W+3;
    wire valid=source_active && !drop_cycle;
    function [23:0] reference_pixel;
        input integer px,py,page;
        begin
            if (px<W/3) reference_pixel=24'hff0000;
            else if (px<2*W/3) reference_pixel=24'h00ff00;
            else reference_pixel=24'h0000ff;
            if (((px/16)+(py/16)+page)%2) reference_pixel=reference_pixel^24'h5a5a5a;
        end
    endfunction
    wire [31:0] stream_data={8'h00,reference_pixel(source_index%W,source_index/W,epoch%2)};
    framebuffer_video #(.H_SYNC(HS),.H_BACK(HB),.H_ACTIVE(W),.H_TOTAL(HT),
        .V_SYNC(VS),.V_BACK(VB),.V_ACTIVE(H),.V_TOTAL(VT)) dut(
        .clk(clk),.reset_n(reset_n),.s_axis_tdata(stream_data),.s_axis_tkeep(4'hf),
        .s_axis_tuser(source_index==0),.s_axis_tlast((source_index%W==W-1)^wrong_last),
        .s_axis_tvalid(valid),.s_axis_tready(ready),
        .s_axi_awaddr(awaddr),.s_axi_awprot(3'd0),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_araddr(araddr),.s_axi_arprot(3'd0),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
        .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
        .video_hs(hs),.video_vs(vs),.video_de(de),.video_rgb(rgb),.dma_fsync(fsync),
        .h_count(hc),.v_count(vc),.frame_count(frame_count));
    always @(posedge clk) begin
        previous_fsync<=fsync;
        if (!reset_n) begin source_active<=0; source_index<=0; epoch<=0; injected<=0; end
        else if (previous_fsync && !fsync) begin
            if (source_active) $fatal(1,"New fsync before previous frame completed");
            source_active<=1; source_index<=0; epoch<=epoch+1; injected<=0;
        end else begin
            if (drop_cycle && dut.raw_de) injected<=1;
            if (valid && ready) begin
                if (source_index==W*H-1) source_active<=0;
                else source_index<=source_index+1;
            end
        end
    end
    always @(posedge clk) begin
        #1;
        if (reset_n && !done) begin
            if (hs !== (hc>=HS) || vs !== (vc>=VS)) $fatal(1,"Sync coordinate mismatch");
            if (de !== (hc>=HS+HB && hc<HS+HB+W && vc>=VS+VB && vc<VS+VB+H))
                $fatal(1,"DE coordinate mismatch");
            if ((!de || !dut.running) && rgb!==0) $fatal(1,"Blanking/disabled output not black");
            if (dut.running && de) begin
                ox=hc-HS-HB; oy=vc-VS-VB; out_index=oy*W+ox;
                if ((FAULTS && epoch==2 && out_index>=W+5) || (FAULTS && epoch==3 && out_index>=2*W+3)) begin
                    if (rgb!==0) $fatal(1,"Bad frame must remain black");
                end else begin
                    if (rgb!==reference_pixel(ox,oy,epoch%2))
                        $fatal(1,"RGB mismatch page%0d (%0d,%0d): %h",epoch,ox,oy,rgb);
                    observed=observed+1;
                end
            end
        end
    end
    task write_reg;
        input [31:0] address,value;
        input [3:0] strobes;
        input [1:0] expected_resp;
        begin
            @(negedge clk); awaddr=address; awvalid=1;
            do @(posedge clk); while (!awready);
            @(negedge clk); awvalid=0;
            repeat(3) @(negedge clk);
            wdata=value; wstrb=strobes; wvalid=1;
            do @(posedge clk); while (!wready);
            @(negedge clk); wvalid=0;
            wait(bvalid); #1;
            if (bresp!==expected_resp) $fatal(1,"Write response mismatch");
            repeat(3) begin @(negedge clk); if (!bvalid || bresp!==expected_resp) $fatal(1,"B response not held"); end
            bready=1; @(negedge clk); bready=0;
        end
    endtask
    task read_reg;
        input [31:0] address,expected;
        input [1:0] expected_resp;
        reg [31:0] saved;
        begin
            @(negedge clk); araddr=address; arvalid=1;
            do @(posedge clk); while (!arready);
            @(negedge clk); arvalid=0;
            wait(rvalid); #1; saved=rdata;
            if (rdata!==expected || rresp!==expected_resp) $fatal(1,"Read %h got %h expected %h",address,rdata,expected);
            repeat(3) begin @(negedge clk); if (!rvalid || rdata!==saved || rresp!==expected_resp) $fatal(1,"R response not held"); end
            rready=1; @(negedge clk); rready=0;
        end
    endtask
    initial begin
        repeat(6) @(negedge clk); reset_n=1;
        read_reg(32'h43c10000,32'h46564231,0);
        read_reg(32'h43c10024,H*65536+W,0);
        write_reg(32'h43c10010,32'hffffffff,4'hf,2);
        write_reg(32'h43c10004,1,4'h0,0);
        read_reg(32'h43c10004,0,0);
        write_reg(32'h43c10004,1,4'h1,0);
        if (FAULTS) begin
            wait(epoch>=4 && dut.good_frames>=2); @(negedge clk);
            if (dut.underflows!=1 || dut.format_errors!=1 || dut.last_frame_pixels!=W*H)
                $fatal(1,"Failure/recovery counters wrong");
            $display("PASS_FRAMEBUFFER_FAULT_RECOVERY underflows=1 format_errors=1");
        end else begin
            wait(observed>=2*W*H); @(negedge clk);
            if (dut.good_frames!=2 || dut.underflows!=0 || dut.format_errors!=0 || dut.pixel_count!=2*W*H)
                $fatal(1,"Full raster counters wrong");
            read_reg(32'h43c10020,W*H,0);
            $display("PASS_FRAMEBUFFER_RASTER pixels=%0d frames=2",observed);
            write_reg(32'h43c10004,0,4'h1,0);
            wait(!dut.running);
            repeat(HT*VT+7) @(negedge clk);
            if (epoch!=2 || source_active) $fatal(1,"Disable issued an extra DMA frame sync");
            read_reg(32'h43c10004,0,0);
            $display("PASS_FRAMEBUFFER_DISABLE no_extra_fsync=1");
        end
        done=1;
    end
endmodule

module tb_framebuffer_video;
    reg clk=0;
    always #10 clk=~clk;
    wire full_done,fault_done;
    framebuffer_case full(.clk(clk),.done(full_done));
    framebuffer_case #(.W(8),.H(4),.HS(2),.HB(2),.HT(15),.VS(1),.VB(1),.VT(7),.FAULTS(1)) fault(.clk(clk),.done(fault_done));
    initial begin
        wait(full_done && fault_done);
        $display("PASS_FRAMEBUFFER_SIMULATION"); $finish;
    end
    initial begin #100000000; $fatal(1,"Simulation timeout"); end
endmodule
