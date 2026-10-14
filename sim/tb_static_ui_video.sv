`timescale 1ns / 1ps
module tb_static_ui_video;
    reg clk = 0, reset = 1;
    always #10 clk = ~clk;
    wire hs, vs, de;
    wire [23:0] rgb;
    wire [11:0] h, v;
    wire [31:0] frame;
    reg [3:0] expected_indices [0:614399];
    reg [23:0] expected_palette [0:15];
    integer eh, ev, pixel_index, active_pixels = 0, completed_frames = 0;
    integer hs_low = 0, vs_low = 0, cycles = 0;
    reg expected_de;
    static_ui_video dut(clk, reset, hs, vs, de, rgb, h, v, frame);

    initial begin
        $readmemh("static_ui_expected.mem", expected_indices);
        $readmemh("static_ui_palette.mem", expected_palette);
        repeat (6) @(posedge clk);
        @(negedge clk); reset = 0;
        // Ignore initial pipeline duplicates in blanking, then verify raster
        // progression independently rather than accepting DUT coordinates.
        wait (h == 1 && v == 0);
        eh = 1; ev = 0;
        while (completed_frames < 2) begin
            @(negedge clk);
            if (h !== eh || v !== ev) $fatal(1,"Raster counter mismatch expected %0d,%0d got %0d,%0d",eh,ev,h,v);
            expected_de = eh >= 160 && eh < 1184 && ev >= 23 && ev < 623;
            if (hs !== (eh >= 20) || vs !== (ev >= 3) || de !== expected_de)
                $fatal(1,"HS/VS/DE mismatch at %0d,%0d",eh,ev);
            if (frame !== completed_frames) $fatal(1,"Frame count mismatch");
            if (completed_frames == 1) begin
                if (!hs) hs_low++;
                if (!vs) vs_low++;
                cycles++;
            end
            if (expected_de) begin
                pixel_index = (ev-23)*1024 + (eh-160);
                if (rgb !== expected_palette[expected_indices[pixel_index]])
                    $fatal(1,"Pixel mismatch at index=%0d (%0d,%0d): expected %06h got %06h",pixel_index,eh-160,ev-23,expected_palette[expected_indices[pixel_index]],rgb);
                active_pixels++;
            end else if (rgb !== 24'h0) begin
                $fatal(1,"Non-black blanking pixel");
            end
            if (eh == 1311) begin
                eh = 0;
                if (ev == 634) begin
                    ev = 0;
                    if (active_pixels != 614400) $fatal(1,"Wrong active pixel count %0d",active_pixels);
                    active_pixels = 0;
                    completed_frames++;
                end else ev++;
            end else eh++;
        end
        if (cycles != 833120 || hs_low != 12700 || vs_low != 3936)
            $fatal(1,"Wrong full-frame control counts: cycles=%0d hs=%0d vs=%0d",cycles,hs_low,vs_low);
        @(negedge clk); reset = 1;
        #1;
        if (de !== 0 || rgb !== 24'h0 || h !== 0 || v !== 0 || frame !== 0)
            $fatal(1,"Reset did not blank output and reset raster");
        $display("PASS_STATIC_UI_VIDEO: 1228800 active pixels match full-image golden data; raster, sync, blanking and reset verified");
        $finish;
    end
    initial begin #40000000; $fatal(1,"Static video test timeout"); end
endmodule
