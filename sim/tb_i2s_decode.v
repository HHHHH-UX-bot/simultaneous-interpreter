`timescale 1ns/1ps
// Verifies the serial I2S stream the way a codec decodes it: on every BCLK
// rising edge, MSB first, one bit after each LRCLK transition, 32-bit words.
// The decoded sample must match the tone generator inside the DUT.
module tb_i2s_declode;
    localparam integer DIV  = 16;
    localparam integer SLOT = 32;

    reg clk = 0, reset_n = 0;
    always #10 clk = ~clk;

    wire bclk, lrclk, dacdat;
    wire [15:0] occ, lat;
    wire [31:0] adc, dac, bclk_cycles;
    wire [15:0] over, under;
    wire live;

    i2s_master_loopback #(.DIV(DIV), .SLOT(SLOT), .DEPTH(2), .TONE_TEST(1)) dut (
        .clk(clk), .reset_n(reset_n), .enable(1'b1),
        .bclk(bclk), .lrclk(lrclk), .dacdat(dacdat), .adcdat(1'b0),
        .occ_frames(occ), .latency_cycles(lat),
        .adc_frames(adc), .dac_frames(dac),
        .overruns(over), .underruns(under), .bclk_cycles(bclk_cycles), .live(live));

    // Decoder: count BCLK edges since the last LRCLK transition, shift data.
    reg        bclk_d = 0, lrclk_d = 0;
    integer    slot_bit = 0;
    reg [31:0] shift = 0;
    reg [31:0] left_word = 0, right_word = 0;
    integer    words = 0, mismatches = 0;
    reg [31:0] expect_word = 0;

    always @(posedge clk) begin
        bclk_d  <= bclk;
        lrclk_d <= lrclk;
        if (lrclk != lrclk_d) begin
            // A slot just ended: 'shift' holds its 32 bits, first bit = delay.
            if (lrclk_d == 1'b0) begin
                left_word <= shift;
            end else begin
                right_word <= shift;
                words <= words + 1;
                expect_word = {dut.tone_sample, 16'd0};
                // shift[31] is the I2S delay bit, shift[30:1] the word MSBs.
                if (shift[31] !== 1'b0 || shift[30:0] !== expect_word[31:1]) begin
                    mismatches <= mismatches + 1;
                end
                shift <= 32'd0;
            end
            shift <= 32'd0;
        end else if (bclk && !bclk_d) begin
            shift <= {shift[30:0], dacdat};
        end
    end

    initial begin
        reset_n = 0;
        repeat (5) @(negedge clk);
        reset_n = 1;
        repeat (DIV*64*20) @(negedge clk);

        $display("INFO decode: words=%0d mismatches=%0d left=0x%04x right=0x%04x tone=0x%04x",
                 words, mismatches, left_word[31:16], right_word[31:16], dut.tone_sample);
        if (words < 5) begin
            $display("FAIL decode: not enough frames decoded"); $finish;
        end
        if (mismatches != 0) begin
            $display("FAIL decode: %0d of %0d words do not match the generated sample",
                     mismatches, words);
        end else if (left_word[31:16] !== dut.tone_sample ||
                     right_word[31:16] !== dut.tone_sample) begin
            $display("FAIL decode: decoded sample 0x%04x/0x%04x != tone 0x%04x",
                     left_word[31:16], right_word[31:16], dut.tone_sample);
        end else begin
            $display("PASS_I2S_DECODE");
        end
        $finish;
    end

    initial begin
        #2000000;
        $display("FAIL decode: timeout");
        $finish;
    end
endmodule
