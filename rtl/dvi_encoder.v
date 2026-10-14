// Reference: https://gitcode.com/max1337/Zynq7010-project
// Commit: 43aef7ba1311fd122379098361c2ddb0e11ca87a, FPGA/hdmi_block/rtl/dvi_encoder.v
module dvi_encoder (
    input clkin,        // 像素时钟输入，DVI/HDMI 编码器的工作时钟
    input rstin,        // 异步复位输入，高电平有效（复位编码器内部状态）
    input [7:0] din,     // 8 位像素数据输入（待编码的原始数据）
    input c0,            // 控制信号 0（在 DE=0 消隐期间与 c1 一起编码为 2 位控制码）
    input c1,            // 控制信号 1（在 DE=0 消隐期间与 c0 一起编码为 2 位控制码）
    input de,            // 数据使能信号：DE=1 表示有效像素期，编码 din；DE=0 表示消隐期，编码 {c1,c0}
    output reg [9:0] dout  // 10 位编码输出（8b/10b 编码结果，含最小化跳变的 DC 平衡码流）
);

reg [3:0] n1d;
reg [7:0] din_q;

always @(posedge clkin) begin
    n1d <= din[0] + din[1] + din[2] + din[3] + din[4] + din[5] + din[6] + din[7]; //统计输入数据1的个数
    din_q <= din;//打一拍,保持与n1d同步
end

wire decision1;

assign decision1 = (n1d > 4'h4) | ((n1d == 4'h4) && (din_q[0] == 4'h0));

wire [8:0] q_m;

assign q_m[0] = din_q[0];
assign q_m[1] = (decision1) ? (q_m[0] ^~ din_q[1]) : (q_m[0] ^ din_q[1]);
assign q_m[2] = (decision1) ? (q_m[1] ^~ din_q[2]) : (q_m[1] ^ din_q[2]);
assign q_m[3] = (decision1) ? (q_m[2] ^~ din_q[3]) : (q_m[2] ^ din_q[3]);
assign q_m[4] = (decision1) ? (q_m[3] ^~ din_q[4]) : (q_m[3] ^ din_q[4]);
assign q_m[5] = (decision1) ? (q_m[4] ^~ din_q[5]) : (q_m[4] ^ din_q[5]);
assign q_m[6] = (decision1) ? (q_m[5] ^~ din_q[6]) : (q_m[5] ^ din_q[6]);
assign q_m[7] = (decision1) ? (q_m[6] ^~ din_q[7]) : (q_m[6] ^ din_q[7]);
assign q_m[8] = (decision1) ? 0 : 1;

reg [3:0] n1q_m;
reg [3:0] n0q_m;

always @(posedge clkin) begin
    n1q_m <= q_m[0] + q_m[1] + q_m[2] + q_m[3] + q_m[4] + q_m[5] + q_m[6] + q_m[7];
    n0q_m <= 8 - (q_m[0] + q_m[1] + q_m[2] + q_m[3] + q_m[4] + q_m[5] + q_m[6] + q_m[7]); //统计0的个数
end

parameter CTRLOKEN0 = 10'b1101010100;
parameter CTRLOKEN1 = 10'b0010101011;
parameter CTRLOKEN2 = 10'b0101010100;
parameter CTRLOKEN3 = 10'b1010101011;

wire decision2;
wire decision3;
reg [4:0] cnt;
reg [8:0] q_m_reg;
reg de_q;
reg de_reg;
reg c0_q;
reg c0_reg;
reg c1_q;
reg c1_reg;

assign decision2 = (cnt == 5'd0) | (n1q_m == n0q_m);
assign decision3 = ((~cnt[4]) && (n1q_m > n0q_m)) | ((cnt[4]) && (n0q_m > n1q_m));

always @(posedge clkin)begin
    q_m_reg <= q_m;
    de_q    <= de;
    de_reg  <= de_q;
    c0_q    <= c0;
    c0_reg  <= c0_q;
    c1_q    <= c1;
    c1_reg  <= c1_q;
end

always @(posedge clkin or posedge rstin)begin
    if(rstin)begin
        dout <= 10'd0;
        cnt  <= 5'd0;
    end
    else begin
        if(de_reg)begin
            if(decision2)begin
                dout[9]   <= ~q_m_reg[8];
                dout[8]   <= q_m_reg[8];
                dout[7:0] <= (q_m_reg[8]) ? q_m_reg[7:0] : ~q_m_reg[7:0];
                cnt       <= (~q_m_reg[8]) ? (cnt + n0q_m - n1q_m) : (cnt + n1q_m - n0q_m);
            end
            else begin
                if(decision3)begin
                    dout[9]   <= 1'b1;
                    dout[8]   <= q_m_reg[8];
                    dout[7:0] <= ~q_m_reg[7:0];
                    cnt       <= cnt + {q_m_reg[8],1'b0} + (n0q_m - n1q_m);
                end
                else begin
                    dout[9]   <= 1'b0;
                    dout[8]   <= q_m_reg[8];
                    dout[7:0] <= q_m_reg[7:0];
                    cnt       <= cnt - {~q_m_reg[8],1'b0} + (n1q_m - n0q_m);
                end
            end
        end
        else begin
            case({c1_reg,c0_reg})
                2'b00: dout <= CTRLOKEN0;
                2'b01: dout <= CTRLOKEN1;
                2'b10: dout <= CTRLOKEN2;
                default:dout <= CTRLOKEN3;
            endcase
            cnt <= 5'd0;
        end
    end
end

endmodule //dvi_encoder