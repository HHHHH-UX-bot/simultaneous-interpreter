`timescale 1ns/1ps
// AXI4-Lite register file for the audio loopback (same handshake style as
// framebuffer_video.v so both blocks behave identically on the PS bus).
//
// The codec value shadows are the single knob for the analogue path, volumes
// and interface format: change them over AXI, then pulse CTRL.init_trigger to
// rewrite the whole WM8731 sequence without rebuilding the bitstream.
module audio_regs #(
    parameter [8:0] DEF_R0 = 9'h017,   // left line input volume, 0 dB
    parameter [8:0] DEF_R1 = 9'h017,   // right line input volume, 0 dB
    parameter [8:0] DEF_R2 = 9'h079,   // left headphone volume, 0 dB
    parameter [8:0] DEF_R3 = 9'h079,   // right headphone volume, 0 dB
    // R4 bits (datasheet Table 30): [0] MICBOOST, [1] MUTEMIC, [2] INSEL
    // (1 = microphone to ADC), [3] BYPASS, [4] DACSEL (1 = DAC to the outputs).
    // 0x015 = MICBOOST + microphone input + DAC selected: the DAC drives LINE OUT
    // and HP OUT, and the ADC digitises the microphone (same value as the AN831
    // demo configuration, R4 = 0x15).
    parameter [8:0] DEF_R4 = 9'h015,
    parameter [8:0] DEF_R5 = 9'h000,   // digital path: DAC unmuted, de-emphasis off
    // R7 bits: [1:0] FORMAT (10 = I2S), [3:2] IWL (00 = 16 bit), [6] MS
    // (1 = the codec is the clock master and drives BCLK/ADCLRC/DACLRC from its
    // own 12.288 MHz crystal), [7] BCLKINV.
    parameter [8:0] DEF_R7 = 9'h042,
    // R8: [0] USB/NORMAL (0 = normal), [1] BOSR (0 = 256 fs), [5:2] SR
    // (0000 = 48 kHz with a 12.288 MHz MCLK), [6] CLKIDIV2, [7] CLKODIV2.
    parameter [8:0] DEF_R8 = 9'h000
) (
    input  wire        clk,
    input  wire        reset_n,
    // AXI4-Lite slave
    input  wire [7:0]  s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output reg  [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [7:0]  s_axi_araddr,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output reg  [31:0] s_axi_rdata,
    output reg  [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready,
    // Status inputs
    input  wire        live,
    input  wire        codec_found,
    input  wire [1:0]  codec_addr_sel,
    input  wire        init_busy,
    input  wire        init_done,
    input  wire        init_error,
    input  wire [7:0]  regs_written,
    input  wire [15:0] occ_frames,
    input  wire [15:0] latency_cycles,
    input  wire [31:0] bclk_hz,
    // Pin activity counters (BCLK, ADCLRC, DADLRC, ADCDAT edges) for wiring checks.
    input  wire [31:0] pin_count0,
    input  wire [31:0] pin_count1,
    input  wire [31:0] pin_count2,
    input  wire [31:0] pin_count3,
    input  wire [31:0] adc_frames,
    input  wire [31:0] dac_frames,
    input  wire [15:0] overruns,
    input  wire [15:0] underruns,
    input  wire [31:0] bclk_cycles,
    input  wire [19:0] latency_us,
    input  wire [19:0] min_us,
    input  wire [19:0] max_us,
    input  wire        max_valid,
    // Codec control-port readback and live bus levels (diagnostics).
    input  wire [8:0]  rb_r4,
    input  wire [8:0]  rb_r7,
    input  wire [8:0]  rb_r8,
    input  wire [8:0]  rb_r9,
    input  wire        rb_valid,
    input  wire        sda_idle,
    input  wire        scl_idle,
    // Microphone sample tap (system domain, see audio_sample_tap.v).
    input  wire [15:0] sample_data,
    input  wire        sample_toggle,
    input  wire [31:0] sample_count,
    // Controls out
    output reg         init_trigger,
    output reg         clear_extremes,
    output reg         freeze_display,
    output reg  [8:0]  cfg_r0,
    output reg  [8:0]  cfg_r1,
    output reg  [8:0]  cfg_r2,
    output reg  [8:0]  cfg_r3,
    output reg  [8:0]  cfg_r4,
    output reg  [8:0]  cfg_r5,
    output reg  [8:0]  cfg_r7,
    output reg  [8:0]  cfg_r8
);
    localparam [31:0] ID_VALUE = 32'h41554431;   // "AUD1"

    reg        aw_hold, w_hold;
    reg [7:0]  aw_address;
    reg [31:0] w_data;
    reg [3:0]  w_strobe;
    reg [31:0] ctrl_shadow;
    reg        ctrl_bit0_d;

    assign s_axi_awready = !aw_hold && !s_axi_bvalid;
    assign s_axi_wready  = !w_hold  && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid;

    // [31:24] registers written, [23:11] reserved, [10] scl_idle, [9] sda_idle,
    // [8] readback valid, [7] max_valid, [6] init_error, [5] init_done,
    // [4] init_busy, [3:2] addr_sel, [1] codec_found, [0] live.
    wire [31:0] status = {regs_written, 13'd0, scl_idle, sda_idle, rb_valid,
                          max_valid, init_error, init_done, init_busy,
                          codec_addr_sel, codec_found, live};

    always @(posedge clk) begin
        if (!reset_n) begin
            aw_hold <= 0; w_hold <= 0; aw_address <= 0; w_data <= 0; w_strobe <= 0;
            s_axi_bresp <= 0; s_axi_bvalid <= 0;
            s_axi_rdata <= 0; s_axi_rresp <= 0; s_axi_rvalid <= 0;
            init_trigger <= 0; ctrl_bit0_d <= 0; clear_extremes <= 0; freeze_display <= 0;
            ctrl_shadow <= 32'h1;                 // run the codec init after reset
            cfg_r0 <= DEF_R0; cfg_r1 <= DEF_R1; cfg_r2 <= DEF_R2; cfg_r3 <= DEF_R3;
            cfg_r4 <= DEF_R4; cfg_r5 <= DEF_R5; cfg_r7 <= DEF_R7; cfg_r8 <= DEF_R8;
        end else begin
            // CTRL bit 0 is edge sensitive: the reset value 1 starts the codec
            // initialisation without any software write, and clearing then
            // setting the bit re-runs the sequence.
            init_trigger   <= ctrl_shadow[0] && !ctrl_bit0_d;
            ctrl_bit0_d    <= ctrl_shadow[0];
            clear_extremes <= 1'b0;

            if (s_axi_awvalid && s_axi_awready) begin aw_hold <= 1; aw_address <= s_axi_awaddr; end
            if (s_axi_wvalid && s_axi_wready)   begin w_hold  <= 1; w_data <= s_axi_wdata; w_strobe <= s_axi_wstrb; end
            if (s_axi_bvalid && s_axi_bready)   s_axi_bvalid <= 0;

            if (aw_hold && w_hold && !s_axi_bvalid) begin
                aw_hold <= 0; w_hold <= 0; s_axi_bvalid <= 1; s_axi_bresp <= 0;
                case (aw_address)
                    8'h30: if (w_strobe[1:0] == 2'b11) cfg_r0 <= w_data[8:0];
                    8'h34: if (w_strobe[1:0] == 2'b11) cfg_r1 <= w_data[8:0];
                    8'h38: if (w_strobe[1:0] == 2'b11) cfg_r2 <= w_data[8:0];
                    8'h3c: if (w_strobe[1:0] == 2'b11) cfg_r3 <= w_data[8:0];
                    8'h40: if (w_strobe[1:0] == 2'b11) cfg_r4 <= w_data[8:0];
                    8'h44: if (w_strobe[1:0] == 2'b11) cfg_r5 <= w_data[8:0];
                    8'h48: if (w_strobe[1:0] == 2'b11) cfg_r7 <= w_data[8:0];
                    8'h4c: if (w_strobe[1:0] == 2'b11) cfg_r8 <= w_data[8:0];
                    8'h2c: begin
                        ctrl_shadow <= w_data;
                        clear_extremes <= w_data[1];
                        freeze_display <= w_data[2];
                    end
                    default: s_axi_bresp <= 2'b10;
                endcase
            end

            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 0;
            if (s_axi_arvalid && s_axi_arready) begin
                s_axi_rvalid <= 1; s_axi_rresp <= 0;
                case (s_axi_araddr)
                    8'h00: s_axi_rdata <= ID_VALUE;
                    8'h04: s_axi_rdata <= status;
                    8'h08: s_axi_rdata <= {12'd0, latency_us};
                    8'h0c: s_axi_rdata <= {16'd0, latency_cycles};
                    8'h10: s_axi_rdata <= adc_frames;
                    8'h14: s_axi_rdata <= dac_frames;
                    8'h18: s_axi_rdata <= {16'd0, overruns};
                    8'h1c: s_axi_rdata <= {16'd0, underruns};
                    8'h20: s_axi_rdata <= bclk_cycles;
                    8'h24: s_axi_rdata <= (min_us == 20'hFFFFF) ? 32'hFFFFFFFF : {12'd0, min_us};
                    8'h28: s_axi_rdata <= {12'd0, max_us};
                    8'h2c: s_axi_rdata <= ctrl_shadow;
                    8'h30: s_axi_rdata <= {23'd0, cfg_r0};
                    8'h34: s_axi_rdata <= {23'd0, cfg_r1};
                    8'h38: s_axi_rdata <= {23'd0, cfg_r2};
                    8'h3c: s_axi_rdata <= {23'd0, cfg_r3};
                    8'h40: s_axi_rdata <= {23'd0, cfg_r4};
                    8'h44: s_axi_rdata <= {23'd0, cfg_r5};
                    8'h48: s_axi_rdata <= {23'd0, cfg_r7};
                    8'h4c: s_axi_rdata <= {23'd0, cfg_r8};
                    8'h50: s_axi_rdata <= {16'd0, occ_frames};
                    8'h54: s_axi_rdata <= bclk_hz;
                    8'h60: s_axi_rdata <= pin_count0;   // BCLK pin edges
                    8'h64: s_axi_rdata <= pin_count1;   // ADCLRC pin edges
                    8'h68: s_axi_rdata <= pin_count2;   // DADLRC pin edges
                    8'h6c: s_axi_rdata <= pin_count3;   // ADCDAT pin edges
                    8'h70: s_axi_rdata <= {23'd0, rb_r4};   // codec R4 readback
                    8'h74: s_axi_rdata <= {23'd0, rb_r7};   // codec R7 readback
                    8'h78: s_axi_rdata <= {23'd0, rb_r8};   // codec R8 readback
                    8'h7c: s_axi_rdata <= {23'd0, rb_r9};   // codec R9 readback
                    // Microphone PCM tap: [16] toggles once per captured frame,
                    // [15:0] is the latest left sample (signed 16-bit).
                    8'h80: s_axi_rdata <= {15'd0, sample_toggle, sample_data[15:0]};
                    8'h84: s_axi_rdata <= sample_count;
                    default: begin s_axi_rdata <= 32'd0; s_axi_rresp <= 2'b10; end
                endcase
            end
        end
    end
endmodule
