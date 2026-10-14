`timescale 1ns / 1ps

// XRGB8888 stream -> the same raster used by the accepted static display.
// All stream, register and video logic shares the 50 MHz pixel clock.
module framebuffer_video #(
    parameter H_SYNC=20, H_BACK=140, H_ACTIVE=1024, H_TOTAL=1312,
    parameter V_SYNC=3, V_BACK=20, V_ACTIVE=600, V_TOTAL=635
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK", X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS:S_AXI, ASSOCIATED_RESET reset_n, FREQ_HZ 50000000" *)
    input wire clk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 reset_n RST", X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input wire reset_n,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TDATA" *) input wire [31:0] s_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TKEEP" *) input wire [3:0] s_axis_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TUSER" *) input wire s_axis_tuser,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TLAST" *) input wire s_axis_tlast,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TVALID" *) input wire s_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TREADY" *) output wire s_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWADDR", X_INTERFACE_PARAMETER = "PROTOCOL AXI4LITE, DATA_WIDTH 32, ADDR_WIDTH 32" *) input wire [31:0] s_axi_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWPROT" *) input wire [2:0] s_axi_awprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *) input wire s_axi_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *) output wire s_axi_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WDATA" *) input wire [31:0] s_axi_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WSTRB" *) input wire [3:0] s_axi_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WVALID" *) input wire s_axi_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WREADY" *) output wire s_axi_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BRESP" *) output reg [1:0] s_axi_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BVALID" *) output reg s_axi_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BREADY" *) input wire s_axi_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARADDR" *) input wire [31:0] s_axi_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARPROT" *) input wire [2:0] s_axi_arprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *) input wire s_axi_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *) output wire s_axi_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RDATA" *) output reg [31:0] s_axi_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RRESP" *) output reg [1:0] s_axi_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RVALID" *) output reg s_axi_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RREADY" *) input wire s_axi_rready,
    output reg video_hs, video_vs, video_de,
    output reg [23:0] video_rgb,
    output wire dma_fsync,
    output reg [11:0] h_count,v_count,
    output reg [31:0] frame_count
);
    reg [11:0] raw_h,raw_v;
    reg enable,running,frame_bad;
    reg [31:0] good_frames,underflows,format_errors,frame_pixels,last_frame_pixels;
    reg [63:0] pixel_count;
    localparam [31:0] DIMENSIONS=V_ACTIVE*65536+H_ACTIVE;
    wire raw_hs=raw_h>=H_SYNC;
    wire raw_vs=raw_v>=V_SYNC;
    wire raw_de=raw_h>=H_SYNC+H_BACK && raw_h<H_SYNC+H_BACK+H_ACTIVE &&
                raw_v>=V_SYNC+V_BACK && raw_v<V_SYNC+V_BACK+V_ACTIVE;
    wire [11:0] x=raw_h-(H_SYNC+H_BACK);
    wire [11:0] y=raw_v-(V_SYNC+V_BACK);
    wire first_pixel=raw_de && x==0 && y==0;
    wire last_pixel=raw_de && x==H_ACTIVE-1 && y==V_ACTIVE-1;
    wire format_ok=(s_axis_tuser==first_pixel) &&
                   (s_axis_tlast==(x==H_ACTIVE-1)) && s_axis_tkeep==4'hf;
    // A bad frame is drained until the next SOF; HDMI timing keeps running.
    assign s_axis_tready=running && (frame_bad ? !s_axis_tuser : raw_de);
    // VDMA starts on a falling edge. Hold high until a frame-aligned enable.
    assign dma_fsync=!running || raw_vs;
    always @(posedge clk) begin
        if (!reset_n) begin
            raw_h<=0; raw_v<=0; running<=0; frame_bad<=1;
            video_hs<=0; video_vs<=0; video_de<=0; video_rgb<=0;
            h_count<=0; v_count<=0; frame_count<=0;
            good_frames<=0; underflows<=0; format_errors<=0;
            pixel_count<=0; frame_pixels<=0; last_frame_pixels<=0;
        end else begin
            if (raw_h==H_TOTAL-1) begin
                raw_h<=0;
                if (raw_v==V_TOTAL-1) begin raw_v<=0; frame_count<=frame_count+1'b1; end
                else raw_v<=raw_v+1'b1;
            end else raw_h<=raw_h+1'b1;
            video_hs<=raw_hs; video_vs<=raw_vs; video_de<=raw_de;
            video_rgb<=0; h_count<=raw_h; v_count<=raw_v;
            // Apply enable before the wrap creates a falling VS/fsync edge.
            if (raw_h==H_TOTAL-1 && raw_v==V_TOTAL-1) begin
                running<=enable; frame_bad<=!enable; frame_pixels<=0;
            end
            if (running && raw_de && !frame_bad) begin
                if (!s_axis_tvalid) begin
                    underflows<=underflows+1'b1; frame_bad<=1;
                end else if (!format_ok) begin
                    format_errors<=format_errors+1'b1; frame_bad<=1;
                end else begin
                    video_rgb<=s_axis_tdata[23:0];
                    pixel_count<=pixel_count+1'b1;
                    frame_pixels<=frame_pixels+1'b1;
                    if (last_pixel) begin
                        good_frames<=good_frames+1'b1;
                        last_frame_pixels<=frame_pixels+1'b1;
                    end
                end
            end
        end
    end

    reg aw_hold,w_hold;
    reg [7:0] aw_address;
    reg [31:0] w_data;
    reg [3:0] w_strobe;
    assign s_axi_awready=!aw_hold && !s_axi_bvalid;
    assign s_axi_wready=!w_hold && !s_axi_bvalid;
    assign s_axi_arready=!s_axi_rvalid;
    always @(posedge clk) begin
        if (!reset_n) begin
            enable<=0; aw_hold<=0; w_hold<=0; aw_address<=0; w_data<=0; w_strobe<=0;
            s_axi_bresp<=0; s_axi_bvalid<=0; s_axi_rresp<=0; s_axi_rdata<=0; s_axi_rvalid<=0;
        end else begin
            if (s_axi_awvalid && s_axi_awready) begin aw_hold<=1; aw_address<=s_axi_awaddr[7:0]; end
            if (s_axi_wvalid && s_axi_wready) begin w_hold<=1; w_data<=s_axi_wdata; w_strobe<=s_axi_wstrb; end
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid<=0;
            if (aw_hold && w_hold && !s_axi_bvalid) begin
                aw_hold<=0; w_hold<=0; s_axi_bvalid<=1; s_axi_bresp<=0;
                if (aw_address==8'h04) begin
                    if (w_strobe[0]) enable<=w_data[0];
                end else s_axi_bresp<=2;
            end
            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid<=0;
            if (s_axi_arvalid && s_axi_arready) begin
                s_axi_rvalid<=1; s_axi_rresp<=0;
                case (s_axi_araddr[7:0])
                    8'h00: s_axi_rdata<=32'h46564231;
                    8'h04: s_axi_rdata<={30'd0,running,enable};
                    8'h08: s_axi_rdata<=frame_count;
                    8'h0c: s_axi_rdata<=good_frames;
                    8'h10: s_axi_rdata<=underflows;
                    8'h14: s_axi_rdata<=format_errors;
                    8'h18: s_axi_rdata<=pixel_count[31:0];
                    8'h1c: s_axi_rdata<=pixel_count[63:32];
                    8'h20: s_axi_rdata<=last_frame_pixels;
                    8'h24: s_axi_rdata<=DIMENSIONS;
                    8'h28: s_axi_rdata<={7'd0,frame_bad,raw_v,raw_h};
                    default: begin s_axi_rdata<=0; s_axi_rresp<=2; end
                endcase
            end
        end
    end
endmodule
