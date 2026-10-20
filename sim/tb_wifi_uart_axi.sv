`timescale 1ns/1ps
module tb_wifi_uart_axi #(parameter TRACE=0);
    reg clk=0, resetn=0, rx=1;
    always #10 clk=~clk;
    wire tx;
    reg [31:0] awaddr=0,wdata=0,araddr=0;
    reg [3:0] wstrb=0;
    reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
    wire awready,wready,bvalid,arready,rvalid;
    wire [1:0] bresp,rresp;
    wire [31:0] rdata;
    integer transmitted=0, i,j, writes=0;
    reg [7:0] wire_bytes[0:34000];
    reg [31:0] value;
    reg [31:0] expected_hash,expected_first,expected_blocks[0:127];
    reg [7:0] stimulus;
    realtime bit_period=2000.0;
    wifi_uart_axi #(.FIFO_ADDR_BITS(4),.TX_TRACE(TRACE)) dut(
        .aclk(clk),.aresetn(resetn),
        .s_axi_awaddr(awaddr),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_araddr(araddr),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
        .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
        .esp_rx(rx),.esp_tx(tx));
    task automatic put_aw(input [31:0] address,input integer delay_cycles);
        begin
            repeat(delay_cycles) @(negedge clk);
            @(negedge clk); awaddr=address; awvalid=1;
            do @(posedge clk); while(!awready);
            @(negedge clk); awvalid=0;
        end
    endtask
    task automatic put_w(input [31:0] data,input [3:0] mask,input integer delay_cycles);
        begin
            repeat(delay_cycles) @(negedge clk);
            @(negedge clk); wdata=data; wstrb=mask; wvalid=1;
            do @(posedge clk); while(!wready);
            @(negedge clk); wvalid=0;
        end
    endtask
    task automatic write_reg(input [31:0] address,data,input [3:0] mask,input [1:0] response);
        begin
            // AW before W, W before AW, and simultaneous independent channels.
            fork
                put_aw(address,(writes%3==1)?3:0);
                put_w(data,mask,(writes%3==0)?4:0);
            join
            wait(bvalid); #1;
            if(bresp!==response) $fatal(1,"Write response %h at %h expected %h",bresp,address,response);
            repeat(3) begin @(negedge clk); if(!bvalid || bresp!==response) $fatal(1,"B response lost under backpressure"); end
            bready=1; @(negedge clk); bready=0; writes++;
        end
    endtask
    task automatic read_reg(input [31:0] address,output [31:0] data,input [1:0] response);
        reg [31:0] held;
        begin
            @(negedge clk); araddr=address; arvalid=1;
            do @(posedge clk); while(!arready);
            @(negedge clk); arvalid=0;
            wait(rvalid); #1; held=rdata;
            if(rresp!==response) $fatal(1,"Read response error at %h",address);
            repeat(3) begin
                @(negedge clk);
                if(!rvalid || rdata!==held || rresp!==response) $fatal(1,"R response changed under backpressure");
            end
            data=held; rready=1; @(negedge clk); rready=0;
        end
    endtask
    task automatic send_byte(input [7:0] byte_value,input realtime period,input stop_ok);
        integer bit_index;
        begin
            #317; rx=0; #(period);
            for(bit_index=0;bit_index<8;bit_index++) begin rx=byte_value[bit_index]; #(period); end
            rx=stop_ok; #(period); rx=1; #(period);
        end
    endtask
    // Decode the TX wire independently at bit centers; do not inspect DUT data.
    initial forever begin : tx_receiver
        reg [7:0] decoded;
        integer bit_index;
        @(negedge tx); #(bit_period/2);
        if(tx!==0) $fatal(1,"TX start bit too short");
        for(bit_index=0;bit_index<8;bit_index++) begin #(bit_period); decoded[bit_index]=tx; end
        #(bit_period);
        if(tx!==1) $fatal(1,"TX stop bit invalid");
        wire_bytes[transmitted]=decoded; transmitted++;
    end
    initial begin
        repeat(10) @(negedge clk); resetn=1;
        read_reg(0,value,0); if(value!==32'h57494631) $fatal(1,"Identity");
        read_reg(8,value,0); if(value!==434) $fatal(1,"Default divider");
        write_reg(8,100,15,0);
        write_reg(8,0,15,2);
        write_reg(8,32'hffffffff,0,0);
        read_reg(8,value,0); if(value!==100) $fatal(1,"Byte strobe or invalid-divider write");
        write_reg(32'h80000008,100,15,2);
        read_reg(32'h100,value,TRACE?0:2);
        read_reg(32'h14,value,0); if(value!==0) $fatal(1,"Empty read must be invalid");
        #211; rx=0; #400; rx=1; #4000;
        read_reg(32'h18,value,0); if(value!==0) $fatal(1,"Start glitch accepted");
        for(i=0;i<256;i++) begin
            read_reg(4,value,0);
            while(!value[1]) read_reg(4,value,0);
            write_reg(32'h10,i,1,0);
        end
        wait(transmitted==256); #3000;
        for(i=0;i<256;i++) if(wire_bytes[i]!==i[7:0]) $fatal(1,"TX order/data mismatch %0d",i);
        read_reg(32'h28,value,0); if(value!==256) $fatal(1,"TX duplicated or dropped");
        // Multiple FIFO wraparounds, all 256 byte values, +/-0.5% source baud.
        for(i=0;i<32;i++) begin
            for(j=0;j<8;j++) send_byte(((i*8+j)*37+13)&255,(j%2)?1990.0:2010.0,1);
            read_reg(32'h18,value,0); if(value!==8) $fatal(1,"RX count mismatch");
            for(j=0;j<8;j++) begin
                read_reg(32'h14,value,0);
                if(!value[31] || value[7:0]!==(((i*8+j)*37+13)&255)) $fatal(1,"RX wrap/order/data mismatch");
            end
            read_reg(32'h18,value,0); if(value!==0) $fatal(1,"RX double-pop under backpressure");
        end
        for(i=0;i<20;i++) send_byte(i,2000.0,1);
        read_reg(32'h18,value,0); if(value!==16) $fatal(1,"RX full count");
        read_reg(32'h20,value,0); if(value!==4) $fatal(1,"Overflow not counted");
        for(i=0;i<16;i++) begin
            read_reg(32'h14,value,0);
            if(value!==(32'h80000000|i)) $fatal(1,"Full FIFO contents changed");
        end
        write_reg(12,9,1,0); #4000;
        send_byte(8'h5a,2000.0,0); #5000;
        read_reg(32'h18,value,0); if(value!==0) $fatal(1,"Bad-stop byte accepted");
        read_reg(32'h24,value,0); if(value!==1) $fatal(1,"Framing error not counted");
        write_reg(12,15,1,0); #4000;
        write_reg(8,434,15,0); bit_period=8680.0;
        write_reg(32'h10,8'h96,1,0);
        write_reg(8,100,15,2); // Changing divider during TX must fail.
        wait(transmitted==257); #10000;
        if(wire_bytes[256]!==8'h96) $fatal(1,"115200 baud TX mismatch");
        send_byte(8'ha5,8680.0,1);
        read_reg(32'h14,value,0); if(value!==32'h800000a5) $fatal(1,"115200 baud RX mismatch");
        if(TRACE) begin
            write_reg(8,50,15,0);bit_period=1000.0;
            read_reg(32'h30,value,0);if(value!==32'h31544857) $fatal(1,"TX trace identity");
            write_reg(32'h34,15,15,0); // Start diagnostic session 7.
            expected_hash=32'h811c9dc5;
            for(i=0;i<33041;i++) begin
                stimulus=(i*73+(i>>8)*17+31)&255;
                read_reg(4,value,0);while(!value[1]) read_reg(4,value,0);
                write_reg(32'h10,stimulus,1,0);
                expected_hash=(expected_hash^stimulus)*32'h01000193;
                if((i&255)==255) begin
                    expected_blocks[(i>>8)&127]=expected_hash;
                    if(i==255) expected_first=expected_hash;
                    expected_hash=32'h811c9dc5;
                end
            end
            wait(transmitted==257+33041);#2000;
            write_reg(32'h34,0,15,0);
            read_reg(32'h38,value,0);if(value!==33041) $fatal(1,"TX wire trace byte count");
            read_reg(32'h3c,value,0);if(value!==expected_hash) $fatal(1,"TX wire partial fingerprint");
            read_reg(32'h40,value,0);if(value!==expected_first) $fatal(1,"TX first block overwritten after ring wrap");
            read_reg(32'h44,value,0);if(value!==7) $fatal(1,"TX trace epoch");
            read_reg(32'h48,value,0);if(value!==0) $fatal(1,"TX trace framing errors");
            for(i=0;i<128;i++) begin
                read_reg(32'h100+4*i,value,0);
                if(value!==expected_blocks[i]) $fatal(1,"TX wire fingerprint mismatch at slot %0d",i);
            end
            for(i=0;i<33041;i++)
                if(wire_bytes[257+i]!==((i*73+(i>>8)*17+31)&255)) $fatal(1,"Long TX wire byte mismatch %0d",i);
            write_reg(32'h10,8'haa,1,0);wait(transmitted==257+33042);#2000;
            read_reg(32'h38,value,0);if(value!==33041) $fatal(1,"Frozen TX trace changed");
            write_reg(32'h34,17,15,0);
            read_reg(32'h38,value,0);if(value!==0) $fatal(1,"TX trace reset count");
            read_reg(32'h44,value,0);if(value!==8) $fatal(1,"TX trace new epoch");
            $display("PASS_WIFI_TX_TRACE: 33041 independently decoded bytes, ring wrap, first identity, partial block, freeze and restart");
        end
        // Reset an incomplete AW transaction; no stale write may survive.
        put_aw(32'h10,0); @(negedge clk); resetn=0;
        repeat(5) @(negedge clk); resetn=1;
        repeat(5) @(negedge clk);
        if(bvalid || rvalid || tx!==1) $fatal(1,"Reset did not clear protocol/UART state");
        read_reg(32'h1c,value,0); if(value!==0) $fatal(1,"Stale TX after reset");
        $display("PASS_WIFI_UART: all 256 TX/RX values, FIFO wrap/full/overflow, AXI AW/W ordering/backpressure, strobes, reset, bad framing, start glitches and 115200 timing");
        $finish;
    end
    initial begin
        if(TRACE) #400000000;else #30000000;
        $fatal(1,"UART/AXI test timeout");
    end
endmodule
