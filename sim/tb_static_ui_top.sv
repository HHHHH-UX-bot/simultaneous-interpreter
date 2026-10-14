`timescale 1ns / 1ps
module tb_static_ui_top;
    reg sys_clk = 0, sys_rst_n = 0;
    always #10 sys_clk = ~sys_clk;
    wire en, cp, cn, locked, heartbeat;
    wire [2:0] dp, dn;
    realtime previous_pixel, previous_serial;
    integer pixel_edges = 0, serial_edges = 0;
    // Independent TMDS reference at the RGB interface. The receiver below
    // recovers actual 10-bit symbols from the output pins, LSB first.
    reg [29:0] expected [0:160000];
    reg expected_de [0:160000];
    reg expected_asymmetric [0:160000];
    integer written = 0, next_word = -1, checked = 0;
    integer active_checked = 0, blank_checked = 0, asymmetric_checked = 0;
    integer rd_b = 0, rd_g = 0, rd_r = 0;
    reg [9:0] ref_b, ref_g, ref_r, rx_b, rx_g, rx_r, rx_clk;
    reg [29:0] received;
    integer bit_number, candidate, alignment_count;

    task automatic encode_reference(
        input [7:0] value, input enabled, input [1:0] control,
        inout integer disparity, output reg [9:0] symbol
    );
        integer ones, bit_index, qm, previous, qm8, balance, invert_xor;
        begin
            if (!enabled) begin
                case (control)
                    0: symbol = 10'b1101010100;
                    1: symbol = 10'b0010101011;
                    2: symbol = 10'b0101010100;
                    3: symbol = 10'b1010101011;
                endcase
                disparity = 0;
            end else begin
                ones = 0;
                for (bit_index=0; bit_index<8; bit_index++) ones += value[bit_index];
                invert_xor = ones > 4 || (ones == 4 && !value[0]);
                qm = value[0]; previous = qm;
                for (bit_index=1; bit_index<8; bit_index++) begin
                    previous = previous ^ value[bit_index] ^ invert_xor;
                    qm |= previous << bit_index;
                end
                qm8 = !invert_xor;
                ones = 0;
                for (bit_index=0; bit_index<8; bit_index++) ones += (qm >> bit_index) & 1;
                balance = 2*ones-8;
                if (disparity == 0 || balance == 0) begin
                    symbol = ((1-qm8)<<9) | (qm8<<8) | (qm8 ? qm : qm^255);
                    disparity += qm8 ? balance : -balance;
                end else if ((disparity>0 && balance>0) || (disparity<0 && balance<0)) begin
                    symbol = 512 | (qm8<<8) | (qm^255);
                    disparity += 2*qm8-balance;
                end else begin
                    symbol = (qm8<<8) | qm;
                    disparity += balance-2*(1-qm8);
                end
                if (disparity < -8 || disparity > 8) $fatal(1,"Reference disparity outside TMDS limits");
            end
        end
    endtask
    static_ui_top dut(sys_clk, sys_rst_n, en, cp, cn, dp, dn, locked, heartbeat);
    initial begin
        #500; sys_rst_n = 1;
        wait (locked === 1);
        wait (checked == 140000);
        if (active_checked < 80000 || blank_checked < 30000 || asymmetric_checked < 100)
            $fatal(1,"Insufficient active/blanking/color-lane coverage: %0d/%0d/%0d",active_checked,blank_checked,asymmetric_checked);
        if (serial_edges < 5*pixel_edges-10) $fatal(1,"Serial clock not running at 5x");
        if (en !== 1 || ^{cp,cn,dp,dn} === 1'bx) $fatal(1,"Unknown TMDS or missing HDMI enable");
        if (cp !== ~cn || dp !== ~dn) $fatal(1,"Differential TMDS not complementary");
        if (dut.video_reset !== 0 || ^dut.video_rgb === 1'bx) $fatal(1,"Static video unknown after reset");
        $display("PASS_STATIC_UI_TOP: 140000 recovered 10-bit symbols on all three lanes plus clock; active=%0d blank=%0d asymmetric=%0d; MMCM 50/250 MHz and differential outputs verified",active_checked,blank_checked,asymmetric_checked);
        $finish;
    end
    always @(posedge dut.pixel_clk) begin
        if (dut.u_transmitter.reset === 0) begin
            if (pixel_edges > 0 && ($realtime-previous_pixel < 19.99 || $realtime-previous_pixel > 20.01))
                $fatal(1,"Pixel clock is not 50 MHz");
            previous_pixel = $realtime; pixel_edges++;
            if (written > 160000) $fatal(1,"Reference buffer overflow");
            if (^{dut.video_rgb,dut.video_de,dut.video_hs,dut.video_vs} === 1'bx)
                $fatal(1,"Unknown RGB/control at transmitter input");
            encode_reference(dut.video_rgb[7:0],dut.video_de,{dut.video_vs,dut.video_hs},rd_b,ref_b);
            encode_reference(dut.video_rgb[15:8],dut.video_de,0,rd_g,ref_g);
            encode_reference(dut.video_rgb[23:16],dut.video_de,0,rd_r,ref_r);
            expected[written] = {ref_r,ref_g,ref_b};
            expected_de[written] = dut.video_de;
            expected_asymmetric[written] = dut.video_rgb[7:0] != dut.video_rgb[15:8] || dut.video_rgb[15:8] != dut.video_rgb[23:16];
            written++;
        end
    end
    initial begin
        wait (dut.u_transmitter.reset === 0);
        repeat (10) @(posedge dut.pixel_clk);
        forever begin
            // Clock lane is 0000011111 in wire order. Its falling edge
            // provides the word boundary independently of internal data wires.
            @(negedge cp);
            for (bit_number=0; bit_number<10; bit_number++) begin
                if (bit_number != 0) @(posedge dut.serial_clk_5x or negedge dut.serial_clk_5x);
                #0.3;
                if (^{cp,cn,dp,dn} === 1'bx || cp !== ~cn || dp !== ~dn)
                    $fatal(1,"Unknown/noncomplementary serialized bit");
                rx_clk[bit_number] = cp;
                rx_b[bit_number] = dp[0]; rx_g[bit_number] = dp[1]; rx_r[bit_number] = dp[2];
            end
            if (rx_clk !== 10'b1111100000) $fatal(1,"Serialized clock word/bit order incorrect: %h",rx_clk);
            received = {rx_r,rx_g,rx_b};
            if (next_word < 0) begin
                // Synchronize once at the first HS control-token transition.
                // The bounded search tolerates primitive pipeline latency,
                // never skipped/reordered symbols after this boundary.
                if (rx_b == 10'b0010101011) begin
                    alignment_count = 0;
                    for (candidate=written-10; candidate<written; candidate++) begin
                        if (candidate > 0 && expected[candidate] === received && expected[candidate-1] !== received) begin
                            next_word = candidate; alignment_count++;
                        end
                    end
                    if (alignment_count != 1) $fatal(1,"Cannot uniquely align serialized TMDS reference: candidates=%0d",alignment_count);
                end
            end
            if (next_word >= 0) begin
                if (next_word >= written || received !== expected[next_word])
                    $fatal(1,"Serial TMDS mismatch word=%0d received=%h expected=%h",next_word,received,expected[next_word]);
                if (expected_de[next_word]) active_checked++; else blank_checked++;
                if (expected_de[next_word] && expected_asymmetric[next_word]) asymmetric_checked++;
                next_word++; checked++;
            end
        end
    end
    always @(posedge dut.serial_clk_5x) begin
        if (pixel_edges > 0) begin
            if (serial_edges > 0 && ($realtime-previous_serial < 3.99 || $realtime-previous_serial > 4.01))
                $fatal(1,"Serializer clock is not 250 MHz");
            previous_serial = $realtime; serial_edges++;
        end
    end
    initial begin #6000000; $fatal(1,"MMCM/static serial integration test timeout"); end
endmodule
