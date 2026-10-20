`timescale 1ns/1ps
// Bundled-data CDC: toggle and data can take different routes to clk.
// A new toggle is visible at the first receiving edge, while data bits settle
// just after it. The published sample must use a later, coherent bus value.
module tb_audio_sample_tap;
    reg clk=0, bclk=0, reset_n=0;
    always #10 clk=~clk;
    always #162.76 bclk=~bclk;
    reg [15:0] adc_sample=0;
    reg adc_toggle=0;
    wire [15:0] sample_data;
    wire sample_toggle;
    wire [31:0] sample_count;
    audio_sample_tap dut(.reset_n(reset_n),.bclk(bclk),.adc_sample(adc_sample),
        .adc_toggle(adc_toggle),.clk(clk),.sample_data(sample_data),
        .sample_toggle(sample_toggle),.sample_count(sample_count));

    integer i;
    reg [15:0] expected;
    initial begin
        repeat(4) @(negedge clk);
        reset_n=1;
        for(i=0;i<128;i=i+1) begin
            case(i%4)
                0: expected=16'h7fff;
                1: expected=16'h8000;
                2: expected=16'hffff-i;
                3: expected=16'h0001+i;
            endcase
            // Control arrives before the next rising edge. Low data bits
            // follow it, but the sign/upper bits arrive 4 ns after that edge.
            @(negedge clk);
            #(1+3*(i%3)) adc_toggle=~adc_toggle;
            adc_sample[7:0]=expected[7:0];
            #(3*(i%5)) adc_sample[15:8]=expected[15:8];
            repeat(6) @(negedge clk);
            if(sample_count!==i+1 || sample_data!==expected || sample_toggle!==adc_toggle) begin
                $display("FAIL_AUDIO_SAMPLE_TAP i=%0d expected=%04x actual=%04x count=%0d",i,expected,sample_data,sample_count);
                $finish;
            end
            #20000; // Source sample is held for the rest of the 48 kHz frame.
        end
        $display("PASS_AUDIO_SAMPLE_TAP 128 coherent samples with control/data skew");
        $finish;
    end
    initial begin
        #3000000;
        $display("FAIL_AUDIO_SAMPLE_TAP timeout");$finish;
    end
endmodule
