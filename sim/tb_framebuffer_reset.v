`timescale 1ns/1ps
// Exercise the generated reset IP, including the previously incorrect idle level.
module tb_framebuffer_reset;
    reg clk=0, ext_reset=1, aux_reset=0, locked=0;
    always #10 clk=~clk;
    wire peripheral_n, interconnect_n;
    ps_display_reset_0 reset_ip(.slowest_sync_clk(clk),.ext_reset_in(ext_reset),
        .aux_reset_in(aux_reset),.mb_debug_sys_rst(1'b0),.dcm_locked(locked),
        .peripheral_aresetn(peripheral_n),.interconnect_aresetn(interconnect_n));
    reg [31:0] araddr=0;
    reg arvalid=0;
    wire arready,rvalid;
    wire [31:0] rdata;
    wire [1:0] rresp;
    framebuffer_video video(.clk(clk),.reset_n(peripheral_n),
        .s_axis_tdata(32'd0),.s_axis_tkeep(4'hf),.s_axis_tuser(1'b0),
        .s_axis_tlast(1'b0),.s_axis_tvalid(1'b0),
        .s_axi_awaddr(32'd0),.s_axi_awprot(3'd0),.s_axi_awvalid(1'b0),
        .s_axi_wdata(32'd0),.s_axi_wstrb(4'd0),.s_axi_wvalid(1'b0),.s_axi_bready(1'b1),
        .s_axi_araddr(araddr),.s_axi_arprot(3'd0),.s_axi_arvalid(arvalid),
        .s_axi_arready(arready),.s_axi_rdata(rdata),.s_axi_rresp(rresp),
        .s_axi_rvalid(rvalid),.s_axi_rready(1'b1));
    task await_release;
        integer n;
        begin
            n=0;
            while (peripheral_n!==1'b1 || interconnect_n!==1'b1) begin
                @(posedge clk); #1;
                n=n+1;
                if(n>500) $fatal(1,"Generated reset IP never released");
            end
        end
    endtask
    task read_register(input [31:0] address, input [31:0] expected);
        integer n;
        begin
            @(negedge clk); araddr=address; arvalid=1;
            n=0;
            do begin
                @(posedge clk);
                n=n+1;
                if(n>30) $fatal(1,"AXI address not accepted after reset release");
            end while (!arready);
            @(negedge clk); arvalid=0;
            n=0;
            while(!rvalid) begin
                @(negedge clk); n=n+1;
                if(n>30) $fatal(1,"AXI read timed out after reset release");
            end
            if(rresp!==0 || rdata!==expected) $fatal(1,"AXI reset-path read mismatch at %h",address);
            @(negedge clk);
        end
    endtask
    initial begin
        repeat(10) @(negedge clk);
        locked=1; ext_reset=0;
        repeat(200) @(negedge clk);
        if(peripheral_n!==0 || interconnect_n!==0) $fatal(1,"Low auxiliary input did not hold active-low reset");
        $display("PASS_RESET_NEGATIVE_CONTROL");
        aux_reset=1;
        await_release;
        read_register(32'h43c10000,32'h46564231);
        read_register(32'h43c10024,32'h02580400);
        ext_reset=1;
        repeat(40) @(negedge clk);
        if(peripheral_n!==0 || interconnect_n!==0) $fatal(1,"External reset not asserted");
        ext_reset=0;
        await_release;
        read_register(32'h43c10004,32'd0);
        locked=0;
        repeat(40) @(negedge clk);
        if(peripheral_n!==0 || interconnect_n!==0) $fatal(1,"Clock loss not asserted");
        locked=1;
        await_release;
        read_register(32'h43c10000,32'h46564231);
        $display("PASS_FRAMEBUFFER_RESET_INTEGRATION");
        $finish;
    end
    initial begin #100000; $fatal(1,"Reset integration watchdog"); end
endmodule
