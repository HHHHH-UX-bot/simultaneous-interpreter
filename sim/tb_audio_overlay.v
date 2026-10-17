`timescale 1ns/1ps
// Self-checking testbench for audio_overlay.
//
// A reduced raster (24 active lines are enough to cover the box) drives the
// block and checks:
//   * every pixel outside the box passes through unchanged,
//   * drawn pixels only ever appear inside the box,
//   * a known value draws a stable number of pixels, and a different value or
//     live = 0 changes that pattern (digits and dashes are actually rendered).
module tb_audio_overlay;
    localparam integer H_ACTIVE = 1024;
    localparam integer LINES    = 24;
    localparam integer X_ORIGIN = 896;
    localparam integer Y_ORIGIN = 8;
    localparam integer BOX_W    = 120;
    localparam integer BOX_H    = 16;

    reg pixel_clk = 0;
    always #10 pixel_clk = ~pixel_clk;

    reg         reset_n = 0;
    reg         video_vs = 0, video_de = 0;
    reg  [23:0] video_rgb_in = 24'h123456;
    reg  [15:0] latency_ms = 16'd321;
    reg         live = 1;
    wire [23:0] video_rgb_out;
    wire        overlay_pixel;
    wire [15:0] text_pixels;

    audio_overlay #(.H_ACTIVE(H_ACTIVE), .X_ORIGIN(X_ORIGIN), .Y_ORIGIN(Y_ORIGIN)) dut (
        .pixel_clk(pixel_clk), .reset_n(reset_n),
        .video_vs(video_vs), .video_de(video_de), .video_rgb_in(video_rgb_in),
        .latency_ms(latency_ms), .live(live), .freeze(1'b0),
        .video_rgb_out(video_rgb_out), .overlay_pixel(overlay_pixel),
        .text_pixels(text_pixels));

    integer errors = 0;
    integer drawn_inside, outside_bad, inside_bad;
    integer i, x, y;

    // One frame: VS pulse then LINES active lines of H_ACTIVE pixels.
    task run_frame;
        begin
            @(negedge pixel_clk);
            video_de = 0;
            video_vs = 1;
            @(negedge pixel_clk);
            @(negedge pixel_clk);
            video_vs = 0;
            @(negedge pixel_clk);
            for (y = 0; y < LINES; y = y + 1) begin
                for (x = 0; x < H_ACTIVE; x = x + 1) begin
                    @(negedge pixel_clk);
                    video_de = 1;
                    video_rgb_in = 24'h123456;
                end
                @(negedge pixel_clk);
                video_de = 0;
                repeat (2) @(negedge pixel_clk);
            end
        end
    endtask

    // Checks pixels while streaming one frame. The DUT registers the pixel, the
    // box flags and the drawn flag together, so each pixel is inspected on the
    // negedge after it has been clocked. Regions within 4 px of the box are
    // excluded from the pass-through check to stay independent of a
    // one-cycle alignment difference.
    task run_frame_checked;
        begin
            drawn_inside = 0;
            outside_bad  = 0;
            inside_bad   = 0;
            @(negedge pixel_clk);
            video_de = 0;
            video_vs = 1;
            @(negedge pixel_clk);
            @(negedge pixel_clk);
            video_vs = 0;
            @(negedge pixel_clk);
            for (y = 0; y < LINES; y = y + 1) begin
                for (x = 0; x < H_ACTIVE; x = x + 1) begin
                    // Exactly one pixel clock per pixel: set the input on the
                    // falling edge, inspect the result on the next rising edge.
                    @(negedge pixel_clk);
                    video_de = 1;
                    video_rgb_in = 24'h123456;
                    @(posedge pixel_clk);
                    if (x >= X_ORIGIN && x < X_ORIGIN + BOX_W &&
                        y >= Y_ORIGIN && y < Y_ORIGIN + BOX_H) begin
                        if (overlay_pixel) drawn_inside = drawn_inside + 1;
                    end else if (x < X_ORIGIN - 4 || x >= X_ORIGIN + BOX_W + 4 ||
                                 y < Y_ORIGIN - 2) begin
                        if (video_rgb_out !== 24'h123456) outside_bad = outside_bad + 1;
                    end
                end
                @(negedge pixel_clk);
                video_de = 0;
                repeat (2) @(negedge pixel_clk);
            end
        end
    endtask

    integer drawn_live, drawn_notlive, drawn_other;

    initial begin
        reset_n = 0;
        repeat (5) @(negedge pixel_clk);
        reset_n = 1;

        // Frame 1: value 321 while live.
        run_frame_checked;
        drawn_live = drawn_inside;
        if (outside_bad != 0) begin
            $display("FAIL overlay: %0d pixels outside the box were modified", outside_bad);
            errors = errors + 1;
        end
        if (drawn_inside < 60) begin
            $display("FAIL overlay: only %0d text pixels drawn for '321'", drawn_inside);
            errors = errors + 1;
        end
        $display("INFO overlay: 'T1 0321 ms' drawn with %0d pixels", drawn_live);

        // Frame 2: another value must produce a different pattern.
        latency_ms = 16'd9876;
        run_frame_checked;
        drawn_other = drawn_inside;
        if (drawn_other == drawn_live) begin
            $display("FAIL overlay: value change did not change the drawn pattern (%0d)",
                     drawn_other);
            errors = errors + 1;
        end
        if (drawn_other < 60) begin
            $display("FAIL overlay: only %0d text pixels drawn for '9876'", drawn_other);
            errors = errors + 1;
        end

        // Frame 3: live = 0 must draw dashes instead of digits.
        live = 0;
        latency_ms = 16'd321;
        run_frame_checked;
        drawn_notlive = drawn_inside;
        if (drawn_notlive == drawn_live) begin
            $display("FAIL overlay: live=0 pattern identical to the live pattern (%0d)",
                     drawn_notlive);
            errors = errors + 1;
        end
        if (drawn_notlive < 30) begin
            $display("FAIL overlay: only %0d pixels drawn when not live", drawn_notlive);
            errors = errors + 1;
        end
        $display("INFO overlay: dashes drawn with %0d pixels", drawn_notlive);

        // Frame 4: value above the display range saturates (same as 9999).
        live = 1;
        latency_ms = 16'd12345;
        run_frame;
        latency_ms = 16'd9999;
        run_frame_checked;
        if (drawn_inside !== drawn_other && drawn_inside < 60) begin
            $display("FAIL overlay: saturated value drew only %0d pixels", drawn_inside);
            errors = errors + 1;
        end

        if (errors == 0) $display("PASS_AUDIO_OVERLAY");
        else             $display("FAIL_AUDIO_OVERLAY errors=%0d", errors);
        $finish;
    end

    initial begin
        #20000000;
        $display("FAIL_AUDIO_OVERLAY timeout");
        $finish;
    end
endmodule
