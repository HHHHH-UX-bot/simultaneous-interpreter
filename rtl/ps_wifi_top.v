`timescale 1ns/1ps
module ps_wifi_top (
    inout wire [14:0] DDR_addr,
    inout wire [2:0] DDR_ba,
    inout wire DDR_cas_n,DDR_ck_n,DDR_ck_p,DDR_cke,DDR_cs_n,
    inout wire [3:0] DDR_dm,
    inout wire [31:0] DDR_dq,
    inout wire [3:0] DDR_dqs_n,DDR_dqs_p,
    inout wire DDR_odt,DDR_ras_n,DDR_reset_n,DDR_we_n,
    inout wire FIXED_IO_ddr_vrn,FIXED_IO_ddr_vrp,
    inout wire [53:0] FIXED_IO_mio,
    inout wire FIXED_IO_ps_clk,FIXED_IO_ps_porb,FIXED_IO_ps_srstb,
    input wire sys_clk,sys_rst_n,esp_rx,
    output wire esp_tx,hdmi_en,tmds_clk_p,tmds_clk_n,
    output wire [2:0] tmds_data_p,tmds_data_n,
    output wire led_locked,led_heartbeat
);
    wire pixel_clk,serial_clk_5x,locked,video_reset;
    wire video_hs,video_vs,video_de;
    wire [23:0] video_rgb;
    wire [31:0] frame_count;
    assign hdmi_en=1'b1;
    assign led_locked=locked;
    assign led_heartbeat=frame_count[5];
    hdmi_clock u_clock(.sys_clk(sys_clk),.reset(~sys_rst_n),.pixel_clk(pixel_clk),
        .serial_clk_5x(serial_clk_5x),.locked(locked));
    async_rst_sync u_video_reset(.clk(pixel_clk),.reset_n(sys_rst_n & locked),.syn_reset(video_reset));
    ps_display_wrapper u_system(
        .DDR_0_addr(DDR_addr),.DDR_0_ba(DDR_ba),.DDR_0_cas_n(DDR_cas_n),
        .DDR_0_ck_n(DDR_ck_n),.DDR_0_ck_p(DDR_ck_p),.DDR_0_cke(DDR_cke),.DDR_0_cs_n(DDR_cs_n),
        .DDR_0_dm(DDR_dm),.DDR_0_dq(DDR_dq),.DDR_0_dqs_n(DDR_dqs_n),.DDR_0_dqs_p(DDR_dqs_p),
        .DDR_0_odt(DDR_odt),.DDR_0_ras_n(DDR_ras_n),.DDR_0_reset_n(DDR_reset_n),.DDR_0_we_n(DDR_we_n),
        .FIXED_IO_0_ddr_vrn(FIXED_IO_ddr_vrn),.FIXED_IO_0_ddr_vrp(FIXED_IO_ddr_vrp),
        .FIXED_IO_0_mio(FIXED_IO_mio),.FIXED_IO_0_ps_clk(FIXED_IO_ps_clk),
        .FIXED_IO_0_ps_porb(FIXED_IO_ps_porb),.FIXED_IO_0_ps_srstb(FIXED_IO_ps_srstb),
        .pixel_clk(pixel_clk),.pixel_reset(video_reset),.frame_count(frame_count),
        .esp_rx(esp_rx),.esp_tx(esp_tx),
        .video_hs(video_hs),.video_vs(video_vs),.video_de(video_de),.video_rgb(video_rgb));
    dvi_transmitter_top u_transmitter(.pclk(pixel_clk),.pclk_x5(serial_clk_5x),
        .reset_n(sys_rst_n & locked),.video_in(video_rgb),.video_h_sync(video_hs),
        .video_v_sync(video_vs),.video_de(video_de),.tmds_clk_p(tmds_clk_p),.tmds_clk_n(tmds_clk_n),
        .tmds_data_p(tmds_data_p),.tmds_data_n(tmds_data_n),.tmds_oen());
endmodule
