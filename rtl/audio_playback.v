`timescale 1ns/1ps
// 24 kHz mono PCM -> both channels of the existing 48 kHz/16-bit I2S DAC.
// All state is in the 50 MHz domain; delayed synchronised falling edges allow
// for the codec's LRCLK propagation. Samples stay buffered across chunks.
module audio_playback(
    input wire clk, reset_n, clear,
    input wire bclk, lrclk, run, ending,
    input wire push,
    input wire [15:0] data,
    output wire [13:0] count,
    output reg dacdat,
    output reg [31:0] underruns, played
);
    reg [15:0] memory [0:8191];
    reg [12:0] wr, rd;
    reg [13:0] used;
    reg [15:0] head, sample, shift;
    (* ASYNC_REG = "TRUE" *) reg [2:0] bc, lr;
    reg bc_seen, falling_pending, lr_seen, repeat_frame;
    reg [5:0] bit_index;
    wire falling = bc_seen && !bc[2];
    // WM8731 LRCLK follows BCLK's falling edge by up to 10 ns. Give its
    // synchroniser one extra 50 MHz cycle before interpreting the boundary;
    // otherwise that delay sometimes shifts the sign bit by one BCLK.
    wire left_edge = falling_pending && (lr[2] != lr_seen) && !lr[2];
    wire pop = left_edge && run && !repeat_frame && used != 0;
    wire write_sample = push && used < 8192;
    assign count = used;
    always @(posedge clk) begin
        bc <= {bc[1:0],bclk}; lr <= {lr[1:0],lrclk};bc_seen<=bc[2];falling_pending<=falling;
        head <= memory[rd];
        if(write_sample) memory[wr] <= data;
        if(!reset_n || clear) begin
            wr<=0; rd<=0; used<=0; bc<=0; bc_seen<=0;falling_pending<=0;lr<=0; lr_seen<=0;
            repeat_frame<=0; bit_index<=0; sample<=0; shift<=0;
            dacdat<=0; underruns<=0; played<=0;
        end else begin
            if(write_sample) wr <= wr+1'b1;
            if(pop) rd <= rd+1'b1;
            case ({write_sample,pop})
                2'b10: used <= used+1'b1;
                2'b01: used <= used-1'b1;
                default: ;
            endcase
            if(falling_pending) begin
                if(lr[2] != lr_seen) begin
                    lr_seen <= lr[2]; bit_index<=0; dacdat<=0;
                    if(!lr[2]) begin
                        if(!run) begin sample<=0; shift<=0; repeat_frame<=0; end
                        else if(!repeat_frame) begin
                            repeat_frame<=1;
                            if(used!=0) begin sample<=head; shift<=head; played<=played+1'b1; end
                            else begin
                                sample<=0; shift<=0;
                                if(!ending) underruns<=underruns+1'b1;
                            end
                        end else begin repeat_frame<=0; shift<=sample; end
                    end else shift<=sample;
                end else begin
                    if(bit_index<16) begin dacdat<=shift[15]; shift<={shift[14:0],1'b0}; end
                    else dacdat<=0;
                    if(bit_index<32) bit_index<=bit_index+1'b1;
                end
            end
        end
    end
endmodule
