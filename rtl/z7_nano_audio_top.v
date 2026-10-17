`timescale 1ns/1ps
// Z7-Nano audio loopback top.
//
// The accepted display design is reused as-is (same PS/VDMA/framebuffer block
// design, same HDMI pins and timing). The audio register file lives inside that
// block design on the third AXI port; this top adds:
//   * WM8731 control-port initialisation over I2C (needed before the module
//     drives BCLK/ADCLRC/DACLRC at all),
//   * the I2S slave loopback (ADCDAT -> DACDAT) with its L1 frame timestamps,
//   * a small readout drawn over the top-right corner of the existing picture.
//
// The AN831 module carries the WM8731's 12.288 MHz crystal and is configured as
// the I2S clock master, exactly like the vendor demo: BCLK (64 x Fs = 3.072 MHz)
// and both LRCLKs are FPGA inputs, so the audio runs at exactly 48 kHz and is
// asynchronous to the 50 MHz display clock.
//
// Audio pins come from hardware/z7_nano/source.json ("AudioPins") and are
// generated into output/z7_nano/config/audio.xdc by scripts/prepare_z7_nano.py.
module z7_nano_audio_top (
    inout wire [14:0] DDR_addr,
    inout wire [2:0]  DDR_ba,
    inout wire DDR_cas_n, DDR_ck_n, DDR_ck_p, DDR_cke, DDR_cs_n,
    inout wire [3:0]  DDR_dm,
    inout wire [31:0] DDR_dq,
    inout wire [3:0]  DDR_dqs_n, DDR_dqs_p,
    inout wire DDR_odt, DDR_ras_n, DDR_reset_n, DDR_we_n,
    inout wire FIXED_IO_ddr_vrn, FIXED_IO_ddr_vrp,
    inout wire [53:0] FIXED_IO_mio,
    inout wire FIXED_IO_ps_clk, FIXED_IO_ps_porb, FIXED_IO_ps_srstb,
    input  wire sys_clk, sys_rst_n,
    output wire led_n, tmds_clk_p, tmds_clk_n,
    output wire [2:0] tmds_data_p, tmds_data_n,
    // Audio module (PL is the I2S slave: the codec drives BCLK and both LRCLKs
    // from its own crystal; only DACDAT is driven by the PL. I2C pads are open
    // drain.)
    input  wire i2s_bclk,
    input  wire i2s_dadlrc,
    input  wire i2s_adclrc,
    input  wire i2s_adcdat,
    output wire i2s_dacdat,
    inout  wire i2c_scl,
    inout  wire i2c_sda,
    // ESP8266 four-wire link (JP1 pins 1/2 = N17/P18): PL UART mailbox to the
    // AT firmware, used by the PS for the cloud WebSocket session.
    output wire esp_tx,
    input  wire esp_rx
);
    wire pixel_clk, serial_clk_5x, locked, video_reset;
    wire video_hs, video_vs, video_de;
    wire [23:0] video_rgb, video_rgb_overlay;
    wire [31:0] frame_count;
    wire        overlay_pixel;
    wire [15:0] overlay_pixels;

    // --- audio status and control between the register file (block design)
    //     and the PL audio chain -------------------------------------------
    wire        init_busy, init_done, init_error, codec_found;
    wire [1:0]  codec_addr_sel;
    wire [7:0]  regs_written;
    wire        init_trigger, clear_extremes, freeze_display;
    wire [8:0]  cfg_r0, cfg_r1, cfg_r2, cfg_r3, cfg_r4, cfg_r5, cfg_r7, cfg_r8;
    wire [15:0] occ_frames, latency_cycles;
    wire [31:0] adc_frames, dac_frames, bclk_cycles;
    wire [15:0] overruns, underruns;
    wire        audio_live;
    wire [19:0] latency_us, min_us, max_us;
    wire        max_valid;
    wire [31:0] pin_count0, pin_count1, pin_count2, pin_count3;
    // Codec control-port readback (proves the I2C writes landed) and bus levels.
    wire [8:0]  rb_r4, rb_r7, rb_r8, rb_r9;
    wire        rb_valid, sda_idle, scl_idle;
    // Microphone PCM tap (BCLK domain -> system domain).
    wire [15:0] adc_left_sample;
    wire        adc_sample_toggle;
    wire [15:0] sample_data;
    wire        sample_toggle;
    wire [31:0] sample_count;



    // --- I2C open-drain pads ----------------------------------------------
    wire scl_low, sda_low, sda_raw, sda_in;
    reg  sda_sync1, sda_sync2;

    IOBUF u_i2c_scl (.O(), .IO(i2c_scl), .I(1'b0), .T(~scl_low));
    IOBUF u_i2c_sda (.O(sda_raw), .IO(i2c_sda), .I(1'b0), .T(~sda_low));

    // Two-flop synchroniser: the master samples SDA on the 50 MHz clock.
    always @(posedge pixel_clk) begin
        if (!(sys_rst_n & locked)) begin sda_sync1 <= 1'b1; sda_sync2 <= 1'b1; end
        else begin sda_sync1 <= sda_raw; sda_sync2 <= sda_sync1; end
    end
    assign sda_in = sda_sync2;

    // --- clocks and reset --------------------------------------------------
    hdmi_clock u_clock(.sys_clk(sys_clk), .reset(~sys_rst_n), .pixel_clk(pixel_clk),
        .serial_clk_5x(serial_clk_5x), .locked(locked));
    async_rst_sync u_video_reset(.clk(pixel_clk), .reset_n(sys_rst_n & locked),
        .syn_reset(video_reset));

    // --- display block design (unchanged picture, plus the audio registers) -
    nano_audio_wrapper u_system(
        .DDR_0_addr(DDR_addr), .DDR_0_ba(DDR_ba), .DDR_0_cas_n(DDR_cas_n),
        .DDR_0_ck_n(DDR_ck_n), .DDR_0_ck_p(DDR_ck_p), .DDR_0_cke(DDR_cke), .DDR_0_cs_n(DDR_cs_n),
        .DDR_0_dm(DDR_dm), .DDR_0_dq(DDR_dq), .DDR_0_dqs_n(DDR_dqs_n), .DDR_0_dqs_p(DDR_dqs_p),
        .DDR_0_odt(DDR_odt), .DDR_0_ras_n(DDR_ras_n), .DDR_0_reset_n(DDR_reset_n), .DDR_0_we_n(DDR_we_n),
        .FIXED_IO_0_ddr_vrn(FIXED_IO_ddr_vrn), .FIXED_IO_0_ddr_vrp(FIXED_IO_ddr_vrp),
        .FIXED_IO_0_mio(FIXED_IO_mio), .FIXED_IO_0_ps_clk(FIXED_IO_ps_clk),
        .FIXED_IO_0_ps_porb(FIXED_IO_ps_porb), .FIXED_IO_0_ps_srstb(FIXED_IO_ps_srstb),
        .pixel_clk(pixel_clk), .pixel_reset(video_reset), .frame_count(frame_count),
        .video_hs(video_hs), .video_vs(video_vs), .video_de(video_de), .video_rgb(video_rgb),
        .sample_data(sample_data), .sample_toggle(sample_toggle),
        .sample_count(sample_count),
        .esp_tx(esp_tx), .esp_rx(esp_rx),
        .live(audio_live), .codec_found(codec_found), .codec_addr_sel(codec_addr_sel),
        .init_busy(init_busy), .init_done(init_done), .init_error(init_error),
        .regs_written(regs_written),
        .occ_frames(occ_frames), .latency_cycles(latency_cycles),
        .adc_frames(adc_frames), .dac_frames(dac_frames),
        .overruns(overruns), .underruns(underruns), .bclk_cycles(bclk_cycles),
        .latency_us(latency_us), .min_us(min_us), .max_us(max_us), .max_valid(max_valid),
        .pin_count0(pin_count0), .pin_count1(pin_count1),
        .pin_count2(pin_count2), .pin_count3(pin_count3),
        .rb_r4(rb_r4), .rb_r7(rb_r7), .rb_r8(rb_r8), .rb_r9(rb_r9),
        .rb_valid(rb_valid), .sda_idle(sda_idle), .scl_idle(scl_idle),
        .init_trigger(init_trigger), .clear_extremes(clear_extremes),
        .freeze_display(freeze_display),
        .cfg_r0(cfg_r0), .cfg_r1(cfg_r1), .cfg_r2(cfg_r2), .cfg_r3(cfg_r3),
        .cfg_r4(cfg_r4), .cfg_r5(cfg_r5), .cfg_r7(cfg_r7), .cfg_r8(cfg_r8));

    // --- codec control port ------------------------------------------------
    // The codec can NACK during power-up transients, so a failed initialisation
    // is retried from hardware every 0.5 s until it succeeds.
    reg [24:0] retry_timer;
    reg        retry_pulse;
    always @(posedge pixel_clk) begin
        if (!(sys_rst_n & locked)) begin
            retry_timer <= 25'd0;
            retry_pulse <= 1'b0;
        end else begin
            retry_pulse <= 1'b0;
            if (init_error && !init_busy) begin
                if (retry_timer == 25'd24_999_999) begin
                    retry_timer <= 25'd0;
                    retry_pulse <= 1'b1;
                end else begin
                    retry_timer <= retry_timer + 1'b1;
                end
            end else begin
                retry_timer <= 25'd0;
            end
        end
    end

    wm8731_init u_codec_init(
        .clk(pixel_clk), .reset_n(sys_rst_n & locked),
        .trigger(init_trigger | retry_pulse),
        .cfg_r0(cfg_r0), .cfg_r1(cfg_r1), .cfg_r2(cfg_r2), .cfg_r3(cfg_r3),
        .cfg_r4(cfg_r4), .cfg_r5(cfg_r5), .cfg_r7(cfg_r7), .cfg_r8(cfg_r8),
        .busy(init_busy), .done(init_done), .error(init_error),
        .codec_found(codec_found), .addr_sel(codec_addr_sel), .regs_written(regs_written),
        .rb_r4(rb_r4), .rb_r7(rb_r7), .rb_r8(rb_r8), .rb_r9(rb_r9), .rb_valid(rb_valid),
        .sda_idle(sda_idle), .scl_idle(scl_idle),
        .scl_low(scl_low), .sda_low(sda_low), .sda_in(sda_in));

    // --- I2S slave loopback ------------------------------------------------
    // The codec is the clock master (R7.MS = 1): BCLK = 3.072 MHz, 64 BCLK per
    // stereo frame = 48 kHz, derived from the module's own 12.288 MHz crystal.
    // The loopback FIFO buffers two frames; the measured L1 latency is three
    // frames = 192 BCLK = 62.5 us.
    i2s_loopback #(.FIFO_DEPTH(2), .AW(1)) u_loopback(
        .clk_sys(pixel_clk), .reset_n_sys(sys_rst_n & locked),
        .bclk(i2s_bclk), .adclrc(i2s_adclrc), .dadlrc(i2s_dadlrc),
        .adcdat(i2s_adcdat), .dacdat(i2s_dacdat),
        .adc_left_sample(adc_left_sample), .adc_sample_toggle(adc_sample_toggle),
        .occ_frames(occ_frames), .latency_cycles(latency_cycles),
        .adc_frames(adc_frames), .dac_frames(dac_frames),
        .overruns(overruns), .underruns(underruns), .bclk_cycles(bclk_cycles),
        .live(audio_live));

    // --- microphone sample tap ---------------------------------------------
    // Publish the latest ADC left sample to the register file at 0x80/0x84 so
    // the PS can read 48 kHz PCM and decimate it to the cloud's 16 kHz.
    audio_sample_tap u_sample_tap(
        .reset_n(sys_rst_n & locked),
        .bclk(i2s_bclk), .adc_sample(adc_left_sample), .adc_toggle(adc_sample_toggle),
        .clk(pixel_clk), .sample_data(sample_data), .sample_toggle(sample_toggle),
        .sample_count(sample_count));


    // --- L1 latency presentation -------------------------------------------
    // The measured cycles come from the loopback itself, so only the unit
    // conversion depends on this constant (3.072 MHz for 48 kHz / 64 BCLK).
    audio_latency #(.BCLK_HZ(3072000)) u_latency(
        .clk(pixel_clk), .reset_n(sys_rst_n & locked),
        .live(audio_live), .cycles(latency_cycles), .clear_extremes(clear_extremes),
        .latency_us(latency_us), .min_us(min_us), .max_us(max_us), .max_valid(max_valid));

    // Pin activity counters: a ball that carries a clock shows a large count,
    // which identifies swapped or unconnected audio wires without a scope.
    audio_pin_activity u_pin_activity(
        .clk(pixel_clk), .reset_n(sys_rst_n & locked),
        .pins({i2s_adcdat, i2s_dadlrc, i2s_adclrc, i2s_bclk}),
        .count0(pin_count0), .count1(pin_count1),
        .count2(pin_count2), .count3(pin_count3));

    // --- latency readout over the existing picture -------------------------
    audio_overlay u_overlay(
        .pixel_clk(pixel_clk), .reset_n(sys_rst_n & locked),
        .video_vs(video_vs), .video_de(video_de), .video_rgb_in(video_rgb),
        .latency_us(latency_us), .live(audio_live), .freeze(freeze_display),
        .video_rgb_out(video_rgb_overlay), .overlay_pixel(overlay_pixel),
        .text_pixels(overlay_pixels));

    // --- TMDS output (same transmitter and timing as the accepted design) --
    // The overlay registers the RGB passthrough, so DE/HS/VS get the same
    // one-pixel delay; otherwise every line would shift by one pixel.
    reg video_de_d, video_hs_d, video_vs_d;
    always @(posedge pixel_clk) begin
        if (video_reset) begin
            video_de_d <= 1'b0; video_hs_d <= 1'b0; video_vs_d <= 1'b0;
        end else begin
            video_de_d <= video_de; video_hs_d <= video_hs; video_vs_d <= video_vs;
        end
    end

    dvi_transmitter_top u_transmitter(.pclk(pixel_clk), .pclk_x5(serial_clk_5x),
        .reset_n(sys_rst_n & locked), .video_in(video_rgb_overlay), .video_h_sync(video_hs_d),
        .video_v_sync(video_vs_d), .video_de(video_de_d),
        .tmds_clk_p(tmds_clk_p), .tmds_clk_n(tmds_clk_n),
        .tmds_data_p(tmds_data_p), .tmds_data_n(tmds_data_n), .tmds_oen());

    // Heartbeat: locked + frames advancing; a codec init error forces it low.
    assign led_n = ~(locked & frame_count[5] & !init_error);
endmodule
