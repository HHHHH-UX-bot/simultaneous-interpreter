`timescale 1ns/1ps
module tb_wifi_control;
    reg clk=0,resetn=0,rx=1;
    always #10 clk=~clk;
    reg [31:0] awaddr=0,wdata=0;
    reg awvalid=0,wvalid=0;
    wire awready,wready,bvalid,tx;
    wire [1:0] bresp;
    integer tested=0;
    integer busy_prefetch_checks=0;
    wifi_uart_axi #(.FIFO_ADDR_BITS(4)) dut(
        .aclk(clk),.aresetn(resetn),
        .s_axi_awaddr(awaddr),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(4'hf),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(1'b1),
        .s_axi_araddr(32'd0),.s_axi_arvalid(1'b0),.s_axi_arready(),
        .s_axi_rdata(),.s_axi_rresp(),.s_axi_rvalid(),.s_axi_rready(1'b1),.esp_rx(rx),.esp_tx(tx));
    always @(negedge clk) if(resetn && (dut.tx_pending || dut.tx_offer)) begin
        if(!dut.status_word[2]) $fatal(1,"STATUS falsely reports idle during TX prefetch");
        busy_prefetch_checks++;
    end
    task automatic write_reg(input [31:0] address,data,input [1:0] response);
        begin
            @(negedge clk); awaddr=address; wdata=data; awvalid=1; wvalid=1;
            @(posedge clk); if(!awready || !wready) $fatal(1,"Test write was not accepted");
            @(negedge clk); awvalid=0; wvalid=0;
            wait(bvalid); #1;
            if(bresp!==response) $fatal(1,"Unexpected control write response");
            @(posedge clk); #1; // Pulse took effect on this edge.
        end
    endtask
    task automatic prepare;
        begin
            @(negedge clk); resetn=0; rx=1;
            repeat(5) @(negedge clk); resetn=1;
            write_reg(8,100,0);
            write_reg(16,8'h55,0);
            write_reg(16,8'haa,0);
            write_reg(16,8'h33,0);
            wait(dut.u_phy.tx_bits==1 && dut.u_phy.tx_timer==5);
        end
    endtask
    task automatic align_pulse(input integer phase,input [3:0] pulse);
        begin
            // At timer1, an AXI write pulse applies on the first possible FIFO
            // pop. Delaying1/2 clocks targets pending/offer respectively.
            wait(dut.u_phy.tx_bits==1 && dut.u_phy.tx_timer==1);
            repeat(phase) @(posedge clk);
            write_reg(12,pulse,0);
        end
    endtask
    initial begin
        // PHY-only reset on FIFO-pop boundary must leave both queued bytes.
        prepare(); align_pulse(0,4);
        if(dut.tx_count!==2 || dut.sent_bytes!==1) $fatal(1,"PHY reset dropped a queued byte or counted false TX");
        tested++;
        // Flush on prefetched offer boundary must not start another frame.
        prepare(); align_pulse(2,2);
        if(!dut.tx_ready || dut.tx_count!==0 || dut.tx_offer || dut.tx_pending || dut.sent_bytes!==1 || tx!==1)
            $fatal(1,"TX flush allowed discarded prefetched byte onto wire");
        tested++;
        // PHY-only reset at pending/offer discards staging but preserves FIFO.
        prepare(); align_pulse(1,4);
        if(dut.tx_count!==1 || dut.sent_bytes!==1 || !dut.tx_ready) $fatal(1,"PHY reset pending-stage mismatch");
        tested++;
        prepare(); align_pulse(2,4);
        if(dut.tx_count!==1 || dut.sent_bytes!==1 || !dut.tx_ready) $fatal(1,"PHY reset counted aborted offer");
        tested++;
        // RX goes low one cycle before AW/W. Commit collides exactly with
        // state0 start detection after the two-flop synchronizer.
        @(negedge clk); resetn=0;
        repeat(5) @(negedge clk); resetn=1;
        write_reg(8,100,0);
        @(negedge clk); rx=0;
        write_reg(8,200,2);
        if(dut.baud_divider!==100 || dut.u_phy.rx_state!==1) $fatal(1,"RX-start/divider collision was not protected");
        tested++;
        if(busy_prefetch_checks<4) $fatal(1,"Missing busy/prefetch coverage");
        $display("PASS_WIFI_CONTROL: %0d directed reset/flush/prefetch and RX-start divider collisions through AXI; atomic busy checked %0d times",tested,busy_prefetch_checks);
        $finish;
    end
    initial begin #2000000; $fatal(1,"Control collision test timeout"); end
endmodule
