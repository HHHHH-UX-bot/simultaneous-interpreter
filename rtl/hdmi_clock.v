`timescale 1ns / 1ps

// 50 MHz -> VCO 1000 MHz -> pixel 50 MHz and DDR serial clock 250 MHz.
// Direct 7-series primitives avoid migrating a clock IP from Vivado 2018.3.
module hdmi_clock (
    input wire sys_clk,
    input wire reset,
    output wire pixel_clk,
    output wire serial_clk_5x,
    output wire locked
);
    wire input_clk, feedback, feedback_buf, pixel_raw, serial_raw;
    IBUF u_input (.I(sys_clk), .O(input_clk));
    BUFG u_feedback (.I(feedback), .O(feedback_buf));
    BUFG u_pixel (.I(pixel_raw), .O(pixel_clk));
    BUFG u_serial (.I(serial_raw), .O(serial_clk_5x));
    MMCME2_BASE #(
        .BANDWIDTH("OPTIMIZED"),
        .CLKIN1_PERIOD(20.0),
        .DIVCLK_DIVIDE(1),
        .CLKFBOUT_MULT_F(20.0),
        .CLKOUT0_DIVIDE_F(20.0),
        .CLKOUT1_DIVIDE(4),
        .CLKOUT0_DUTY_CYCLE(0.5),
        .CLKOUT1_DUTY_CYCLE(0.5),
        .STARTUP_WAIT("FALSE")
    ) u_mmcm (
        .CLKIN1(input_clk), .CLKFBIN(feedback_buf), .CLKFBOUT(feedback),
        .CLKOUT0(pixel_raw), .CLKOUT1(serial_raw), .LOCKED(locked),
        .RST(reset), .PWRDWN(1'b0),
        .CLKFBOUTB(), .CLKOUT0B(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(),
        .CLKOUT3(), .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6()
    );
endmodule
