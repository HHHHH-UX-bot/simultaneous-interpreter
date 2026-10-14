`timescale 1ns/1ps
// Z7-Nano has one active-low PL LED and no HDMI output-enable pin.
module z7_nano_static_top (
    input wire sys_clk, sys_rst_n,
    output wire led_n, tmds_clk_p, tmds_clk_n,
    output wire [2:0] tmds_data_p, tmds_data_n
);
    wire locked, heartbeat;
    assign led_n = ~(locked & heartbeat);
    static_ui_top u_ui (
        .sys_clk(sys_clk), .sys_rst_n(sys_rst_n), .hdmi_en(),
        .led_locked(locked), .led_heartbeat(heartbeat),
        .tmds_clk_p(tmds_clk_p), .tmds_clk_n(tmds_clk_n),
        .tmds_data_p(tmds_data_p), .tmds_data_n(tmds_data_n)
    );
endmodule
