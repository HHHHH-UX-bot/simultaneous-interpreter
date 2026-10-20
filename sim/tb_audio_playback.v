`timescale 1ns/1ps
module tb_audio_playback;
    reg clk=0, reset_n=0, clear=0, bclk=0, lrclk=0, run=0, ending=0, push=0;
    reg [15:0] data=0;
    wire [13:0] count;
    wire dacdat;
    wire [31:0] underruns,played;
    integer lr_delay=0,sweep;
    always #10 clk=~clk;
    audio_playback dut(clk,reset_n,clear,bclk,lrclk,run,ending,push,data,count,dacdat,underruns,played);
    task put(input [15:0] value);
        begin @(negedge clk);data=value;push=1;@(negedge clk);push=0;end
    endtask
    task slot(input channel,input [15:0] expected);
        integer i;reg [15:0] value;
        begin
            value=0;
            for(i=0;i<32;i=i+1) begin
                bclk=0;#(lr_delay);lrclk=channel;#(163-lr_delay);bclk=1;
                if(i>=1 && i<=16) value={value[14:0],dacdat};
                #163;
            end
            if(value!==expected) $fatal(1,"I2S channel=%d got=%h expected=%h",channel,value,expected);
        end
    endtask
    task frame(input [15:0] expected);
        begin slot(0,expected);slot(1,expected);end
    endtask
    initial begin
      for(sweep=0;sweep<=10;sweep=sweep+1) begin
        lr_delay=sweep;reset_n=0;run=0;ending=0;push=0;clear=0;
        #100;reset_n=1;#100;
        put(16'h1234);put(16'hc567);put(16'h0000);
        if(count!=3) $fatal(1,"FIFO push failed");
        // Synchronise the first left boundary with a silent right slot.
        slot(1,0);run=1;
        frame(16'h1234);frame(16'h1234);
        // Fill while serial playback is active; preserve sample order.
        put(16'h7abc);
        frame(16'hc567);frame(16'hc567);
        frame(0);frame(0);ending=1;
        frame(16'h7abc);frame(16'h7abc);frame(0);frame(0);
        if(count!=0 || played!=4 || underruns!=0) $fatal(1,"drain accounting failed");
        ending=0;frame(0);frame(0);
        if(underruns==0) $fatal(1,"starvation not reported");
        @(negedge clk);clear=1;@(negedge clk);clear=0;run=0;
        if(count!=0 || played!=0 || underruns!=0) $fatal(1,"clear failed");
      end
        $display("PASS_AUDIO_PLAYBACK 24k mono, 48k stereo I2S, FIFO order, silence/drain, LRCLK skew 0..10 ns");$finish;
    end
    initial begin #5000000;$fatal(1,"timeout");end
endmodule
