`timescale 1ns / 1ps

module static_ui_top (
    input wire sys_clk,
    input wire sys_rst_n,
    output wire hdmi_en,
    output wire tmds_clk_p,
    output wire tmds_clk_n,
    output wire [2:0] tmds_data_p,
    output wire [2:0] tmds_data_n,
    output wire led_locked,
    output wire led_heartbeat
);
    wire pixel_clk, serial_clk_5x, locked, video_reset;
    wire video_hs, video_vs, video_de;
    wire [23:0] video_rgb;
    wire [11:0] h_count, v_count;
    wire [31:0] frame_count;
    assign hdmi_en = 1'b1;
    assign led_locked = locked;
    assign led_heartbeat = frame_count[5];

    hdmi_clock u_clock (
        .sys_clk(sys_clk), .reset(~sys_rst_n), .pixel_clk(pixel_clk),
        .serial_clk_5x(serial_clk_5x), .locked(locked)
    );
    async_rst_sync u_video_reset (
        .clk(pixel_clk), .reset_n(sys_rst_n & locked), .syn_reset(video_reset)
    );
    static_ui_video u_video (
        .pixel_clk(pixel_clk), .reset(video_reset),
        .video_hs(video_hs), .video_vs(video_vs), .video_de(video_de),
        .video_rgb(video_rgb), .h_count(h_count), .v_count(v_count),
        .frame_count(frame_count)
    );
    dvi_transmitter_top u_transmitter (
        .pclk(pixel_clk), .pclk_x5(serial_clk_5x),
        .reset_n(sys_rst_n & locked), .video_in(video_rgb),
        .video_h_sync(video_hs), .video_v_sync(video_vs), .video_de(video_de),
        .tmds_clk_p(tmds_clk_p), .tmds_clk_n(tmds_clk_n),
        .tmds_data_p(tmds_data_p), .tmds_data_n(tmds_data_n), .tmds_oen()
    );
endmodule
