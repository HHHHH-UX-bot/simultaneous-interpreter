module wifi_ui_top (
    input wire sys_clk, sys_rst_n,
    input wire esp_rx,
    output wire esp_tx,
    output wire hdmi_en, tmds_clk_p, tmds_clk_n,
    output wire [2:0] tmds_data_p, tmds_data_n,
    output wire led_locked, led_heartbeat
);
    // Share the existing buffered 50 MHz pixel clock. The package clock input
    // must connect only to hdmi_clock's IBUF, never directly to fabric FFs.
    wire pixel_clk,serial_clk_5x,locked,video_reset;
    wire video_hs,video_vs,video_de;
    wire [23:0] video_rgb;
    wire [11:0] h_count,v_count;
    wire [31:0] frame_count;
    assign hdmi_en=1'b1;
    assign led_locked=locked;
    assign led_heartbeat=frame_count[5];
    hdmi_clock u_clock(.sys_clk(sys_clk),.reset(~sys_rst_n),
        .pixel_clk(pixel_clk),.serial_clk_5x(serial_clk_5x),.locked(locked));
    async_rst_sync u_video_reset(.clk(pixel_clk),.reset_n(sys_rst_n & locked),.syn_reset(video_reset));
    static_ui_video u_video(.pixel_clk(pixel_clk),.reset(video_reset),
        .video_hs(video_hs),.video_vs(video_vs),.video_de(video_de),.video_rgb(video_rgb),
        .h_count(h_count),.v_count(v_count),.frame_count(frame_count));
    dvi_transmitter_top u_transmitter(.pclk(pixel_clk),.pclk_x5(serial_clk_5x),
        .reset_n(sys_rst_n & locked),.video_in(video_rgb),
        .video_h_sync(video_hs),.video_v_sync(video_vs),.video_de(video_de),
        .tmds_clk_p(tmds_clk_p),.tmds_clk_n(tmds_clk_n),
        .tmds_data_p(tmds_data_p),.tmds_data_n(tmds_data_n),.tmds_oen());
    reg [15:0] startup_reset = 16'hffff;
    always @(posedge pixel_clk) startup_reset <= {startup_reset[14:0],1'b0};
    wire uart_reset = video_reset || startup_reset[15];
    wire [31:0] awaddr,wdata,araddr,rdata;
    wire [3:0] wstrb;
    wire awvalid,awready,wvalid,wready,bvalid,bready,arvalid,arready,rvalid,rready;
    wire [1:0] bresp,rresp;
    wifi_jtag_axi u_jtag(
        .aclk(pixel_clk),.aresetn(~uart_reset),
        .m_axi_awaddr(awaddr),.m_axi_awprot(),.m_axi_awvalid(awvalid),.m_axi_awready(awready),
        .m_axi_wdata(wdata),.m_axi_wstrb(wstrb),.m_axi_wvalid(wvalid),.m_axi_wready(wready),
        .m_axi_bresp(bresp),.m_axi_bvalid(bvalid),.m_axi_bready(bready),
        .m_axi_araddr(araddr),.m_axi_arprot(),.m_axi_arvalid(arvalid),.m_axi_arready(arready),
        .m_axi_rdata(rdata),.m_axi_rresp(rresp),.m_axi_rvalid(rvalid),.m_axi_rready(rready));
    wifi_uart_axi u_uart(
        .aclk(pixel_clk),.aresetn(~uart_reset),
        .s_axi_awaddr(awaddr),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_araddr(araddr),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
        .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
        .esp_rx(esp_rx),.esp_tx(esp_tx));
endmodule
