// Reference: https://gitcode.com/max1337/Zynq7010-project
// Commit: 43aef7ba1311fd122379098361c2ddb0e11ca87a, FPGA/hdmi_block/rtl/dvi_transmitter_top.v
module dvi_transmitter_top (
    input        pclk,
    input        pclk_x5,
    input        reset_n,

    input [23:0] video_in,
    input        video_h_sync, //行同步信号
    input        video_v_sync, //场同步信号
    input        video_de,     //使能

    output       tmds_clk_p,
    output       tmds_clk_n,
    output [2:0] tmds_data_p,
    output [2:0] tmds_data_n,
    output       tmds_oen
);

wire reset;
wire [9:0] red_10bit;
wire [9:0] green_10bit;
wire [9:0] blue_10bit;
wire [9:0] clk_10bit;
wire [2:0] tmds_data_serial; //单端信号,通过OBUFDS原语实现单端转差分
wire       tmds_clk_serial;

assign clk_10bit = 10'b1111100000;

async_rst_sync rst_sync(
    .clk         (pclk),
    .reset_n     (reset_n),
    .syn_reset   (reset)
);

dvi_encoder encoder_b(
    .clkin(pclk),
    .rstin(reset),
    .din  (video_in[7:0]),
    .c0   (video_h_sync),
    .c1   (video_v_sync),
    .de   (video_de),
    .dout (blue_10bit)
);

dvi_encoder encoder_g(
    .clkin(pclk),
    .rstin(reset),
    .din  (video_in[15:8]),
    .c0   (1'b0),
    .c1   (1'b0),
    .de   (video_de),
    .dout (green_10bit)
);

dvi_encoder encoder_r(
    .clkin(pclk),
    .rstin(reset),
    .din  (video_in[23:16]),
    .c0   (1'b0),
    .c1   (1'b0),
    .de   (video_de),
    .dout (red_10bit)
);

serializer_10_to_1 serializer_b(
    .parallel_clk   (pclk),
    .serial_clk_5x  (pclk_x5),
    .reset          (reset),
    .parallel_data  (blue_10bit),
    .serial_data_out(tmds_data_serial[0])
);

serializer_10_to_1 serializer_g(
    .parallel_clk   (pclk),
    .serial_clk_5x  (pclk_x5),
    .reset          (reset),
    .parallel_data  (green_10bit),
    .serial_data_out(tmds_data_serial[1])
);

serializer_10_to_1 serializer_r(
    .parallel_clk   (pclk),
    .serial_clk_5x  (pclk_x5),
    .reset          (reset),
    .parallel_data  (red_10bit),
    .serial_data_out(tmds_data_serial[2])
);

serializer_10_to_1 serializer_clk(
    .parallel_clk   (pclk),
    .serial_clk_5x  (pclk_x5),
    .reset          (reset),
    .parallel_data  (clk_10bit),
    .serial_data_out(tmds_clk_serial)
);

OBUFDS #(
    .IOSTANDARD("TMDS_33"), // Specify the output I/O standard
    .SLEW("SLOW")           // Specify the output slew rate
) TMDS0 (
    .O(tmds_data_p[0]),     // Diff_p output (connect directly to top-level port)
    .OB(tmds_data_n[0]),   // Diff_n output (connect directly to top-level port)
    .I(tmds_data_serial[0])      // Buffer input
);

OBUFDS #(
    .IOSTANDARD("TMDS_33"), // Specify the output I/O standard
    .SLEW("SLOW")           // Specify the output slew rate
) TMDS1 (
    .O(tmds_data_p[1]),     // Diff_p output (connect directly to top-level port)
    .OB(tmds_data_n[1]),   // Diff_n output (connect directly to top-level port)
    .I(tmds_data_serial[1])      // Buffer input
);

OBUFDS #(
    .IOSTANDARD("TMDS_33"), // Specify the output I/O standard
    .SLEW("SLOW")           // Specify the output slew rate
) TMDS2 (
    .O(tmds_data_p[2]),     // Diff_p output (connect directly to top-level port)
    .OB(tmds_data_n[2]),   // Diff_n output (connect directly to top-level port)
    .I(tmds_data_serial[2])      // Buffer input
);

OBUFDS #(
    .IOSTANDARD("TMDS_33"), // Specify the output I/O standard
    .SLEW("SLOW")           // Specify the output slew rate
) TMDS3 (
    .O(tmds_clk_p),     // Diff_p output (connect directly to top-level port)
    .OB(tmds_clk_n),   // Diff_n output (connect directly to top-level port)
    .I(tmds_clk_serial)      // Buffer input
);

endmodule //dvi_transmitter_top