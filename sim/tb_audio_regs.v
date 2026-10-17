`timescale 1ns/1ps
// Self-checking testbench for the audio AXI4-Lite register file: identity,
// codec shadows, control pulses, status aggregation and the read-only
// measurement registers.
module tb_audio_regs;
    reg clk = 0, reset_n = 0;
    always #10 clk = ~clk;

    reg  [7:0]  awaddr = 0;
    reg         awvalid = 0;
    wire        awready;
    reg  [31:0] wdata = 0;
    reg  [3:0]  wstrb = 4'hf;
    reg         wvalid = 0;
    wire        wready;
    wire [1:0]  bresp;
    wire        bvalid;
    reg         bready = 0;
    reg  [7:0]  araddr = 0;
    reg         arvalid = 0;
    wire        arready;
    wire [31:0] rdata;
    wire [1:0]  rresp;
    wire        rvalid;
    reg         rready = 0;

    // status stubs
    reg        live = 1;
    reg        codec_found = 1;
    reg  [1:0] codec_addr_sel = 2'd1;
    reg        init_busy = 0;
    reg        init_done = 1;
    reg        init_error = 0;
    reg  [7:0] regs_written = 8'd11;
    reg  [15:0] occ_frames = 16'd2;
    reg  [15:0] latency_cycles = 16'd192;
    reg  [31:0] bclk_hz = 32'd3072000;
    reg  [31:0] adc_frames = 32'd123, dac_frames = 32'd121;
    reg  [15:0] overruns = 16'd1, underruns = 16'd2;
    reg  [31:0] bclk_cycles = 32'h00010000;
    reg  [19:0] latency_us = 20'd62, min_us = 20'd62, max_us = 20'd70;
    reg         max_valid = 1;

    wire        init_trigger, clear_extremes;
    wire [8:0]  cfg_r0, cfg_r1, cfg_r2, cfg_r3, cfg_r4, cfg_r5, cfg_r7, cfg_r8;

    audio_regs dut (
        .clk(clk), .reset_n(reset_n),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(wstrb), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .live(live), .codec_found(codec_found), .codec_addr_sel(codec_addr_sel),
        .init_busy(init_busy), .init_done(init_done), .init_error(init_error),
        .regs_written(regs_written),
        .occ_frames(occ_frames), .latency_cycles(latency_cycles), .bclk_hz(bclk_hz),
        .adc_frames(adc_frames), .dac_frames(dac_frames),
        .overruns(overruns), .underruns(underruns), .bclk_cycles(bclk_cycles),
        .latency_us(latency_us), .min_us(min_us), .max_us(max_us), .max_valid(max_valid),
        .pin_count0(32'd11), .pin_count1(32'd22),
        .pin_count2(32'd33), .pin_count3(32'd44),
        .init_trigger(init_trigger), .clear_extremes(clear_extremes), .freeze_display(),
        .cfg_r0(cfg_r0), .cfg_r1(cfg_r1), .cfg_r2(cfg_r2), .cfg_r3(cfg_r3),
        .cfg_r4(cfg_r4), .cfg_r5(cfg_r5), .cfg_r7(cfg_r7), .cfg_r8(cfg_r8));

    integer errors = 0;
    reg [31:0] rd;

    task axi_write;
        input [7:0] addr;
        input [31:0] data;
        begin
            @(negedge clk);
            awaddr = addr; awvalid = 1; wdata = data; wvalid = 1; bready = 1;
            @(negedge clk);
            while (!(awready && wready)) @(negedge clk);
            @(negedge clk);
            awvalid = 0; wvalid = 0;
            while (!bvalid) @(negedge clk);
            @(negedge clk);
            bready = 0;
        end
    endtask

    task axi_read;
        input [7:0] addr;
        output [31:0] data;
        begin
            @(negedge clk);
            araddr = addr; arvalid = 1; rready = 1;
            @(negedge clk);
            while (!arready) @(negedge clk);
            @(negedge clk);
            arvalid = 0;
            while (!rvalid) @(negedge clk);
            data = rdata;
            @(negedge clk);
            rready = 0;
        end
    endtask

    task expect_read;
        input [7:0] addr;
        input [31:0] expect;
        input [127:0] name;
        begin
            axi_read(addr, rd);
            if (rd !== expect) begin
                $display("FAIL %0s: read %02h = %08h, expected %08h", name, addr, rd, expect);
                errors = errors + 1;
            end
        end
    endtask

    reg trig_seen, clr_seen;

    initial begin
        reset_n = 0;
        repeat (5) @(negedge clk);
        reset_n = 1;
        repeat (2) @(negedge clk);

        expect_read(8'h00, 32'h41554431, "id");
        expect_read(8'h08, 32'd62,       "latency_us");
        expect_read(8'h0c, 32'd192,      "latency_cycles");
        expect_read(8'h10, 32'd123,      "adc_frames");
        expect_read(8'h14, 32'd121,      "dac_frames");
        expect_read(8'h18, 32'd1,        "overruns");
        expect_read(8'h1c, 32'd2,        "underruns");
        expect_read(8'h20, 32'h00010000, "bclk_cycles");
        expect_read(8'h24, 32'd62,       "min_us");
        expect_read(8'h28, 32'd70,       "max_us");
        expect_read(8'h50, 32'd2,        "occupancy");
        expect_read(8'h54, 32'd3072000,  "bclk_hz");
        expect_read(8'h60, 32'd11,       "bclk pin edges");
        expect_read(8'h64, 32'd22,       "adclrc pin edges");
        expect_read(8'h68, 32'd33,       "dadlrc pin edges");
        expect_read(8'h6c, 32'd44,       "adcdat pin edges");
        expect_read(8'h2c, 32'd1,        "ctrl default");

        // Default shadows.
        expect_read(8'h40, 32'h010, "default r4");
        expect_read(8'h48, 32'h042, "default r7 (master)");
        expect_read(8'h4c, 32'h000, "default r8");

        // Status word: [7:0] flags (live, codec_found, addr_sel[1:0], init_busy,
        // init_done, init_error, max_valid) and [31:24] regs_written.
        // live=1 codec_found=1 addr_sel=1 init_done=1 max_valid=1 -> 0xA7
        expect_read(8'h04, 32'h0B00_00A7, "status");

        // Shadow write/read-back plus the control pulses.
        axi_write(8'h40, 32'h015);
        expect_read(8'h40, 32'h015, "r4 write");

        trig_seen = 0; clr_seen = 0;
        // CTRL bit 0 is edge sensitive: clear it, then set it to pulse. The
        // pulses last one clock, so watch every cycle instead of sampling once.
        axi_write(8'h2c, 32'h0000_0000);
        fork
            begin : watch
                integer n;
                for (n = 0; n < 24; n = n + 1) begin
                    @(negedge clk);
                    if (init_trigger)  trig_seen = 1;
                    if (clear_extremes) clr_seen = 1;
                end
            end
            begin
                axi_write(8'h2c, 32'h0000_0003);
            end
        join
        if (!trig_seen) begin $display("FAIL init_trigger not pulsed"); errors = errors + 1; end
        if (!clr_seen)  begin $display("FAIL clear_extremes not pulsed"); errors = errors + 1; end

        // Unknown address must answer with a slave error on both channels.
        axi_read(8'h7c, rd);
        if (rresp !== 2'b10) begin $display("FAIL read error response: %0b", rresp); errors = errors + 1; end
        axi_write(8'h7c, 32'd0);
        if (bresp !== 2'b10) begin $display("FAIL write error response: %0b", bresp); errors = errors + 1; end

        if (errors == 0) $display("PASS_AUDIO_REGS");
        else             $display("FAIL_AUDIO_REGS errors=%0d", errors);
        $finish;
    end

    initial begin
        #200000;
        $display("FAIL_AUDIO_REGS timeout");
        $finish;
    end
endmodule
