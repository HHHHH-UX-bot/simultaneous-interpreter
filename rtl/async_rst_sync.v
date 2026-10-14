// Reference: https://gitcode.com/max1337/Zynq7010-project
// Commit: 43aef7ba1311fd122379098361c2ddb0e11ca87a, FPGA/hdmi_block/rtl/async_rst_sync.v
module async_rst_sync ( //异步复位同步释放
    input  clk,
    input  reset_n,
    output syn_reset
);

reg reset_1;
reg reset_2;

assign syn_reset = reset_2;

always@(posedge clk or negedge reset_n)begin
    if(!reset_n)begin
        reset_1 <= 1'b1;
        reset_2 <= 1'b1;
    end
    else begin
        reset_1 <= 1'b0;
        reset_2 <= reset_1;
    end
end

endmodule //async_rst_sync