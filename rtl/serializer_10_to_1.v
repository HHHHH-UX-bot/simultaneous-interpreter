// Reference: https://gitcode.com/max1337/Zynq7010-project
// Commit: 43aef7ba1311fd122379098361c2ddb0e11ca87a, FPGA/hdmi_block/rtl/serializer_10_to_1.v
module serializer_10_to_1 (
    input        parallel_clk,   //并行时钟
    input        serial_clk_5x,  //5倍串行时钟
    input        reset, 
    input  [9:0] parallel_data,  //10位并行数据
    output       serial_data_out //1位串行数据
);

wire cascade1; //级联串联1
wire cascade2; //级联串联2

//OSERDESE2原语，相当于8转1串并转换模块底层汇编
OSERDESE2 #(
      .DATA_RATE_OQ("DDR"),   // DDR, SDR
      .DATA_RATE_TQ("BUF"),   // Unused tristate channel: width 1 requires BUF/SDR.
      .DATA_WIDTH(10),        // Parallel data width (2-8,10,14)
      .INIT_OQ(1'b0),         // Initial value of OQ output (1'b0,1'b1)
      .INIT_TQ(1'b0),         // Initial value of TQ output (1'b0,1'b1)
      .SERDES_MODE("MASTER"), // MASTER, SLAVE
      .SRVAL_OQ(1'b0),        // OQ output value when SR is used (1'b0,1'b1)
      .SRVAL_TQ(1'b0),        // TQ output value when SR is used (1'b0,1'b1)
      .TBYTE_CTL("FALSE"),    // Enable tristate byte operation (FALSE, TRUE)
      .TBYTE_SRC("FALSE"),    // Tristate byte source (FALSE, TRUE)
      .TRISTATE_WIDTH(1)      // 3-state converter width (1,4)
   )
OSERDESE2_Master (
    .OFB(),                // 1-bit output: Feedback path for data
    .OQ(serial_data_out),  // 1-bit output: Data path output
    // SHIFTOUT1 / SHIFTOUT2: 1-bit (each) output: Data output expansion (1-bit each)
    .SHIFTOUT1(),          
    .SHIFTOUT2(),
    .TBYTEOUT(),   // 1-bit output: Byte group tristate
    .TFB(),             // 1-bit output: 3-state control
    .TQ(),               // 1-bit output: 3-state control
    .CLK(serial_clk_5x),   // 1-bit input: High speed clock
    .CLKDIV(parallel_clk), // 1-bit input: Divided clock
    // D1 - D8: 1-bit (each) input: Parallel data inputs (1-bit each)
    .D1(parallel_data[0]),
    .D2(parallel_data[1]),
    .D3(parallel_data[2]),
    .D4(parallel_data[3]),
    .D5(parallel_data[4]),
    .D6(parallel_data[5]),
    .D7(parallel_data[6]),
    .D8(parallel_data[7]),
    .OCE(1'b1),             // 1-bit input: Output data clock enable
    .RST(reset),            // 1-bit input: Reset
    // SHIFTIN1 / SHIFTIN2: 1-bit (each) input: Data input expansion (1-bit each)
    .SHIFTIN1(cascade1),    //输入有10位,需要两个8转1并转串级联
    .SHIFTIN2(cascade2),
    // T1 - T4: 1-bit (each) input: Parallel 3-state inputs
    .T1(1'b0),
    .T2(1'b0),
    .T3(1'b0),
    .T4(1'b0),
    .TBYTEIN(1'b0),         // 1-bit input: Byte group tristate
    .TCE(1'b0)              // 1-bit input: 3-state clock enable
);

OSERDESE2 #(
      .DATA_RATE_OQ("DDR"),   // DDR, SDR
      .DATA_RATE_TQ("BUF"),   // Unused tristate channel: width 1 requires BUF/SDR.
      .DATA_WIDTH(10),        // Parallel data width (2-8,10,14)
      .INIT_OQ(1'b0),         // Initial value of OQ output (1'b0,1'b1)
      .INIT_TQ(1'b0),         // Initial value of TQ output (1'b0,1'b1)
      .SERDES_MODE("SLAVE"),  // MASTER, SLAVE
      .SRVAL_OQ(1'b0),        // OQ output value when SR is used (1'b0,1'b1)
      .SRVAL_TQ(1'b0),        // TQ output value when SR is used (1'b0,1'b1)
      .TBYTE_CTL("FALSE"),    // Enable tristate byte operation (FALSE, TRUE)
      .TBYTE_SRC("FALSE"),    // Tristate byte source (FALSE, TRUE)
      .TRISTATE_WIDTH(1)      // 3-state converter width (1,4)
   )
OSERDESE2_Slave (
    .OFB(),             // 1-bit output: Feedback path for data
    .OQ(),              // 1-bit output: Data path output,MASTER模式接过了,这里不需要了
    // SHIFTOUT1 / SHIFTOUT2: 1-bit (each) output: Data output expansion (1-bit each)
    .SHIFTOUT1(cascade1),
    .SHIFTOUT2(cascade2),
    .TBYTEOUT(),   // 1-bit output: Byte group tristate
    .TFB(),             // 1-bit output: 3-state control
    .TQ(),               // 1-bit output: 3-state control
    .CLK(serial_clk_5x),   // 1-bit input: High speed clock
    .CLKDIV(parallel_clk), // 1-bit input: Divided clock
    // D1 - D8: 1-bit (each) input: Parallel data inputs (1-bit each)
    .D1(),
    .D2(),
    .D3(parallel_data[8]), //8+2,这两位手册规定接D3D4
    .D4(parallel_data[9]),
    .D5(),
    .D6(),
    .D7(),
    .D8(),
    .OCE(1'b1),             // 1-bit input: Output data clock enable
    .RST(reset),            // 1-bit input: Reset
    // SHIFTIN1 / SHIFTIN2: 1-bit (each) input: Data input expansion (1-bit each)
    .SHIFTIN1(),
    .SHIFTIN2(),
    // T1 - T4: 1-bit (each) input: Parallel 3-state inputs
    .T1(1'b0),
    .T2(1'b0),
    .T3(1'b0),
    .T4(1'b0),
    .TBYTEIN(1'b0),         // 1-bit input: Byte group tristate
    .TCE(1'b0)              // 1-bit input: 3-state clock enable
);

endmodule //serializer_10_to_1
