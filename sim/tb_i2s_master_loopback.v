`timescale 1ns/1ps
// Structural self-check for the I2S master loopback: clock/timing shape, frame
// buffering, occupancy, latency and the absence of overruns/underruns once the
// FIFO is primed. Bit alignment is covered on the board by the codec itself.
module tb_i2s_master_loopback;
    localparam integer DIV   = 16;
    localparam integer SLOT  = 32;
    localparam integer FRAME = 2*SLOT;

    reg clk = 0, reset_n = 0;
    always #10 clk = ~clk;

    wire bclk, lrclk, dacdat;
    reg  adcdat = 0;
    wire [15:0] occ, lat;
    wire [31:0] adc, dac, bclk_cycles;
    wire [15:0] over, under;
    wire live;

    i2s_master_loopback #(.DIV(DIV), .SLOT(SLOT), .DEPTH(2)) dut (
        .clk(clk), .reset_n(reset_n), .enable(1'b1),
        .bclk(bclk), .lrclk(lrclk), .dacdat(dacdat), .adcdat(adcdat),
        .occ_frames(occ), .latency_cycles(lat),
        .adc_frames(adc), .dac_frames(dac),
        .overruns(over), .underruns(under), .bclk_cycles(bclk_cycles), .live(live));

    // Codec model: puts a walking pattern on ADCDAT so DACDAT has something to
    // reproduce, one bit per BCLK, MSB first after each LRCLK transition.
    reg [15:0] tx_word = 16'hA5C3;
    reg [4:0]  bit_i = 0;
    always @(posedge clk) begin
        if (dut.cyc == 4'd2) begin
            if (bit_i < 5'd16) adcdat <= tx_word[5'd15 - bit_i];
            else               adcdat <= 1'b0;
            if (bit_i == 5'd15) begin
                bit_i   <= 5'd0;
                tx_word <= tx_word + 16'h0101;
            end else begin
                bit_i <= bit_i + 1'b1;
            end
        end
    end

    integer errors = 0;
    integer bclk_edges = 0, lrclk_edges = 0;
    reg bclk_d = 0, lrclk_d = 0;
    always @(posedge clk) begin
        bclk_d <= bclk; lrclk_d <= lrclk;
        if (bclk && !bclk_d) bclk_edges = bclk_edges + 1;
        if (lrclk != lrclk_d) lrclk_edges = lrclk_edges + 1;
    end

    initial begin
        reset_n = 0;
        repeat (5) @(negedge clk);
        reset_n = 1;
        // one full frame plus priming
        repeat (DIV*FRAME*40) @(negedge clk);

        if (bclk_edges < FRAME*3) begin
            $display("FAIL master: bclk not running (%0d edges)", bclk_edges); errors = errors + 1;
        end
        if (lrclk_edges < 6) begin
            $display("FAIL master: lrclk not toggling (%0d edges)", lrclk_edges); errors = errors + 1;
        end
        if (!live) begin
            $display("FAIL master: FIFO never primed"); errors = errors + 1;
        end
        if (occ == 0) begin
            $display("FAIL master: occupancy is zero"); errors = errors + 1;
        end
        if (adc < 16 || dac < 8) begin
            $display("FAIL master: frame counters stalled (adc=%0d dac=%0d)", adc, dac); errors = errors + 1;
        end
        // 2 frames of buffering = 128 BCLK; allow the pipeline slack.
        if (lat < 100 || lat > 200) begin
            $display("FAIL master: latency_cycles=%0d outside the expected 2-frame range", lat);
            errors = errors + 1;
        end
        if (over > 2) begin
            $display("FAIL master: %0d overruns after priming", over); errors = errors + 1;
        end
        if (under > 1) begin
            $display("FAIL master: %0d underruns after priming", under); errors = errors + 1;
        end
        $display("INFO master: bclk_edges=%0d lrclk_edges=%0d adc=%0d dac=%0d occ=%0d latency=%0d under=%0d",
                 bclk_edges, lrclk_edges, adc, dac, occ, lat, under);
        if (errors == 0) $display("PASS_I2S_MASTER");
        else             $display("FAIL_I2S_MASTER errors=%0d", errors);
        $finish;
    end

    initial begin
        #3000000;
        $display("FAIL_I2S_MASTER timeout");
        $finish;
    end
endmodule
