`timescale 1ns/1ps
module tb_z7_fast_uart;
    reg clk=0,reset=1,rx=1,tx_valid=0;
    reg [7:0] tx_data=0;
    wire tx,tx_ready,rx_valid,framing_error,rx_busy,rx_level;
    wire [7:0] rx_data;
    integer received=0,decoded=0;
    localparam real PEER_BIT_NS=1000000000.0/921600.0;
    always #10 clk=~clk;
    wifi_uart_phy dut(.clk(clk),.reset(reset),.baud_divider(16'd54),.rx(rx),.tx(tx),
        .tx_data(tx_data),.tx_valid(tx_valid),.tx_ready(tx_ready),.rx_data(rx_data),
        .rx_valid(rx_valid),.framing_error(framing_error),.rx_busy(rx_busy),.rx_level(rx_level));
    function automatic [7:0] pattern(input integer index);
        pattern=(index*37+93)&255;
    endfunction
    always @(posedge clk) if(!reset) begin
        if(framing_error) $fatal(1,"Framing error with nominal 921600 peer");
        if(rx_valid) begin
            if(received>=64 || rx_data!==pattern(received)) $fatal(1,"RX mismatch at %0d",received);
            received=received+1;
        end
    end
    task automatic send_rx(input [7:0] value);
        rx=0;#(PEER_BIT_NS);
        for(integer bit_index=0;bit_index<8;bit_index=bit_index+1) begin
            rx=value[bit_index];#(PEER_BIT_NS);
        end
        rx=1;#(PEER_BIT_NS);
    endtask
    initial begin
        reg [7:0] value;
        wait(!reset);
        for(integer index=0;index<64;index=index+1) begin
            @(negedge tx);#(PEER_BIT_NS*1.5);
            for(integer bit_index=0;bit_index<8;bit_index=bit_index+1) begin
                value[bit_index]=tx;#(PEER_BIT_NS);
            end
            if(value!==pattern(index) || tx!==1'b1) $fatal(1,"TX mismatch/stop at %0d",index);
            decoded=decoded+1;
        end
    end
    initial begin
        repeat(5) @(negedge clk);reset=0;
        repeat(2) @(negedge clk);
        fork
            begin
                for(integer index=0;index<64;index=index+1) send_rx(pattern(index));
            end
            begin
                for(integer index=0;index<64;index=index+1) begin
                    wait(tx_ready);@(negedge clk);tx_data=pattern(index);tx_valid=1;
                    @(negedge clk);tx_valid=0;wait(tx_ready);
                end
            end
        join
        #(PEER_BIT_NS*2);
        if(received!=64 || decoded!=64 || rx_busy || !rx_level) $fatal(1,"Incomplete fast UART waveform test");
        $display("PASS_Z7_UART_921600_WIRE rx=64 tx=64 nominal_peer=921600 divisor=54");$finish;
    end
    initial begin #2000000;$fatal(1,"Fast UART simulation timeout");end
endmodule
