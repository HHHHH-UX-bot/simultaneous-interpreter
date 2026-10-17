`timescale 1ns/1ps
// Self-checking testbench for audio_latency: BCLK-cycle to microsecond
// conversion, live gating and min/max tracking.
//
// BCLK = 3.072 MHz -> LAT_FACTOR = round(1e6*256/3072000) = 83
//   us = (cycles * 83) >> 8
//   64 cycles   (1 frame at 48 kHz) -> 20 us
//   128 cycles  (2 frames)          -> 41 us
//   3072 cycles (1 ms)              -> 996 us
//   65535 cycles                    -> 21247 us
module tb_audio_latency;
    reg clk = 0, reset_n = 0;
    always #10 clk = ~clk;

    reg         live = 0;
    reg  [15:0] cycles = 0;
    reg         clear_extremes = 0;
    wire [19:0] latency_us, min_us, max_us;
    wire        max_valid;

    audio_latency #(.BCLK_HZ(3072000)) dut (
        .clk(clk), .reset_n(reset_n), .live(live), .cycles(cycles),
        .clear_extremes(clear_extremes), .latency_us(latency_us),
        .min_us(min_us), .max_us(max_us), .max_valid(max_valid));

    integer errors = 0;

    task check_us;
        input [15:0]  c;
        input [19:0]  expect;
        input [127:0] name;
        begin
            cycles = c;
            @(negedge clk); @(negedge clk);
            if (latency_us !== expect) begin
                $display("FAIL %0s: cycles=%0d -> %0d us, expected %0d",
                         name, c, latency_us, expect);
                errors = errors + 1;
            end
        end
    endtask

    initial begin
        reset_n = 0;
        repeat (4) @(negedge clk);
        reset_n = 1;
        live = 1;
        cycles = 16'd64;
        @(negedge clk); @(negedge clk);
        clear_extremes = 1;
        @(negedge clk);
        clear_extremes = 0;
        @(negedge clk);

        check_us(16'd1,     20'd0,     "one cycle");
        check_us(16'd64,    20'd20,    "one frame");
        check_us(16'd128,   20'd41,    "two frames");
        check_us(16'd192,   20'd62,    "three frames");
        check_us(16'd3072,  20'd996,   "one millisecond");
        check_us(16'd65535, 20'd21247, "large value");

        if (min_us !== 20'd0 || max_us !== 20'd21247 || !max_valid) begin
            $display("FAIL extremes: min=%0d max=%0d valid=%0b", min_us, max_us, max_valid);
            errors = errors + 1;
        end

        clear_extremes = 1;
        cycles = 16'd192;
        @(negedge clk);
        clear_extremes = 0;
        @(negedge clk);
        @(negedge clk);
        if (min_us !== 20'd62 || max_us !== 20'd62 || !max_valid) begin
            $display("FAIL clear: min=%0d max=%0d valid=%0b", min_us, max_us, max_valid);
            errors = errors + 1;
        end

        // Track a different value after the clear.
        check_us(16'd64, 20'd20, "after clear");

        live = 0;
        @(negedge clk); @(negedge clk);
        if (latency_us !== 20'd0) begin
            $display("FAIL live=0: latency_us=%0d", latency_us);
            errors = errors + 1;
        end

        if (errors == 0) $display("PASS_AUDIO_LATENCY");
        else             $display("FAIL_AUDIO_LATENCY errors=%0d", errors);
        $finish;
    end
endmodule
