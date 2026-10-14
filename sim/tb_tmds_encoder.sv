`timescale 1ns / 1ps
module tb_tmds_encoder;
    localparam N = 2344;
    reg clk = 0, reset = 1;
    always #10 clk = ~clk;
    reg [10:0] inputs [0:N-1];
    reg [9:0] expected [0:N-1];
    reg [7:0] data = 0;
    reg c0 = 0, c1 = 0, de = 0;
    wire [9:0] encoded;
    integer i;
    dvi_encoder dut(clk, reset, data, c0, c1, de, encoded);
    initial begin
        $readmemh("tmds_input.mem", inputs);
        $readmemh("tmds_expected.mem", expected);
        repeat (6) @(posedge clk);
        @(negedge clk); reset = 0;
        for (i = 0; i < N+2; i++) begin
            if (i < N) {de,c1,c0,data} = inputs[i];
            else {de,c1,c0,data} = 0;
            @(posedge clk); #1;
            if (i >= 2 && encoded !== expected[i-2])
                $fatal(1, "TMDS mismatch at %0d: expected %03h got %03h", i-2, expected[i-2], encoded);
            @(negedge clk);
        end
        $display("PASS_TMDS_ENCODER: 2344 symbols match independent model");
        $finish;
    end
    initial begin #100000; $fatal(1, "TMDS test timeout"); end
endmodule
