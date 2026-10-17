`timescale 1ns/1ps
// Latency readout drawn on top of the existing picture.
//
// The accepted display chain (VDMA -> framebuffer_video -> DVI) is untouched:
// this block only replaces pixels inside a fixed box in the top-right corner of
// the 1024x600 active area, drawing "L1 nnnn us" in a 5x7 font scaled 2x
// (10 cells * 12 px = 120 px wide, 16 px tall). Pixels outside the box pass
// through unchanged, so the existing picture and controls keep working
// (see docs/design/UI_SPEC.md#时延读数).
module audio_overlay #(
    parameter H_ACTIVE     = 1024,
    parameter X_ORIGIN     = 896,      // H_ACTIVE - 8 - 120
    parameter Y_ORIGIN     = 8,
    parameter MAX_SHOWN_US = 9999,
    parameter [23:0] TEXT_RGB = 24'hFFFFFF
) (
    input  wire        pixel_clk,
    input  wire        reset_n,
    input  wire        video_vs,
    input  wire        video_de,
    input  wire [23:0] video_rgb_in,
    input  wire [19:0] latency_us,
    input  wire        live,
    output reg  [23:0] video_rgb_out,
    output reg         overlay_pixel,     // 1 while a text pixel is drawn
    output reg  [15:0] text_pixels        // drawn pixels in the current frame
);
    localparam integer CELL_W = 12;
    localparam integer BOX_W  = 120;      // 10 cells
    localparam integer BOX_H  = 16;

    // character codes
    localparam [3:0] CH_L = 4'd10, CH_1 = 4'd11, CH_U = 4'd12, CH_S = 4'd13,
                     CH_DASH = 4'd14, CH_SP = 4'd15;

    reg [10:0] x;                 // active pixel index in the line
    reg [9:0]  y;                 // active line index in the frame
    reg        de_d, vs_d;
    reg [19:0] shown_us;
    reg        shown_live;

    wire in_h = video_de && (x >= X_ORIGIN[10:0]) && (x < (X_ORIGIN + BOX_W));
    wire in_v = (y >= Y_ORIGIN[9:0]) && (y < (Y_ORIGIN + BOX_H));
    wire [6:0] col_off = x[6:0] - X_ORIGIN[6:0];
    wire [3:0] cell_ix = in_h ? (col_off / 7'd12) : 4'd0;
    wire [3:0] cell_x  = in_h ? (col_off % 7'd12) : 4'd0;
    wire [3:0] row_off = y[3:0] - Y_ORIGIN[3:0];

    // saturating display value
    wire [19:0] value = (latency_us > MAX_SHOWN_US) ? MAX_SHOWN_US[19:0] : latency_us;
    wire [3:0]  d3 = value / 20'd1000;
    wire [3:0]  d2 = (value / 20'd100) % 10;
    wire [3:0]  d1 = (value / 20'd10) % 10;
    wire [3:0]  d0 = value % 10;

    reg [3:0] ch;
    always @* begin
        case (cell_ix)
            4'd0: ch = CH_L;
            4'd1: ch = CH_1;
            4'd2: ch = CH_SP;
            4'd3: ch = shown_live ? d3 : CH_DASH;
            4'd4: ch = shown_live ? d2 : CH_DASH;
            4'd5: ch = shown_live ? d1 : CH_DASH;
            4'd6: ch = shown_live ? d0 : CH_DASH;
            4'd7: ch = CH_SP;
            4'd8: ch = CH_U;
            4'd9: ch = CH_S;
            default: ch = CH_SP;
        endcase
    end

    // 5x7 font, one five-column row per lookup.
    function [4:0] glyph_row;
        input [3:0] c;
        input [2:0] r;
        begin
            case (c)
                CH_L: case (r)
                    3'd0: glyph_row = 5'b10000; 3'd1: glyph_row = 5'b10000;
                    3'd2: glyph_row = 5'b10000; 3'd3: glyph_row = 5'b10000;
                    3'd4: glyph_row = 5'b10000; 3'd5: glyph_row = 5'b10000;
                    default: glyph_row = 5'b11111;
                endcase
                CH_1: case (r)
                    3'd0: glyph_row = 5'b00100; 3'd1: glyph_row = 5'b01100;
                    3'd2: glyph_row = 5'b00100; 3'd3: glyph_row = 5'b00100;
                    3'd4: glyph_row = 5'b00100; 3'd5: glyph_row = 5'b00100;
                    default: glyph_row = 5'b01110;
                endcase
                CH_U: case (r)
                    3'd0: glyph_row = 5'b10001; 3'd1: glyph_row = 5'b10001;
                    3'd2: glyph_row = 5'b10001; 3'd3: glyph_row = 5'b10001;
                    3'd4: glyph_row = 5'b10001; 3'd5: glyph_row = 5'b10001;
                    default: glyph_row = 5'b01110;
                endcase
                CH_S: case (r)
                    3'd0: glyph_row = 5'b01111; 3'd1: glyph_row = 5'b10000;
                    3'd2: glyph_row = 5'b10000; 3'd3: glyph_row = 5'b01110;
                    3'd4: glyph_row = 5'b00001; 3'd5: glyph_row = 5'b00001;
                    default: glyph_row = 5'b11110;
                endcase
                CH_DASH: case (r)
                    3'd3: glyph_row = 5'b11111;
                    default: glyph_row = 5'b00000;
                endcase
                4'd0: case (r)
                    3'd0: glyph_row = 5'b01110; 3'd1: glyph_row = 5'b10001;
                    3'd2: glyph_row = 5'b10011; 3'd3: glyph_row = 5'b10101;
                    3'd4: glyph_row = 5'b11001; 3'd5: glyph_row = 5'b10001;
                    default: glyph_row = 5'b01110;
                endcase
                4'd1: case (r)
                    3'd0: glyph_row = 5'b00100; 3'd1: glyph_row = 5'b01100;
                    3'd2: glyph_row = 5'b00100; 3'd3: glyph_row = 5'b00100;
                    3'd4: glyph_row = 5'b00100; 3'd5: glyph_row = 5'b00100;
                    default: glyph_row = 5'b01110;
                endcase
                4'd2: case (r)
                    3'd0: glyph_row = 5'b01110; 3'd1: glyph_row = 5'b10001;
                    3'd2: glyph_row = 5'b00001; 3'd3: glyph_row = 5'b00110;
                    3'd4: glyph_row = 5'b01000; 3'd5: glyph_row = 5'b10000;
                    default: glyph_row = 5'b11111;
                endcase
                4'd3: case (r)
                    3'd0: glyph_row = 5'b11111; 3'd1: glyph_row = 5'b00010;
                    3'd2: glyph_row = 5'b00100; 3'd3: glyph_row = 5'b00010;
                    3'd4: glyph_row = 5'b00001; 3'd5: glyph_row = 5'b10001;
                    default: glyph_row = 5'b01110;
                endcase
                4'd4: case (r)
                    3'd0: glyph_row = 5'b00010; 3'd1: glyph_row = 5'b00110;
                    3'd2: glyph_row = 5'b01010; 3'd3: glyph_row = 5'b10010;
                    3'd4: glyph_row = 5'b11111; 3'd5: glyph_row = 5'b00010;
                    default: glyph_row = 5'b00010;
                endcase
                4'd5: case (r)
                    3'd0: glyph_row = 5'b11111; 3'd1: glyph_row = 5'b10000;
                    3'd2: glyph_row = 5'b11110; 3'd3: glyph_row = 5'b00001;
                    3'd4: glyph_row = 5'b00001; 3'd5: glyph_row = 5'b10001;
                    default: glyph_row = 5'b01110;
                endcase
                4'd6: case (r)
                    3'd0: glyph_row = 5'b00110; 3'd1: glyph_row = 5'b01000;
                    3'd2: glyph_row = 5'b10000; 3'd3: glyph_row = 5'b11110;
                    3'd4: glyph_row = 5'b10001; 3'd5: glyph_row = 5'b10001;
                    default: glyph_row = 5'b01110;
                endcase
                4'd7: case (r)
                    3'd0: glyph_row = 5'b11111; 3'd1: glyph_row = 5'b00001;
                    3'd2: glyph_row = 5'b00010; 3'd3: glyph_row = 5'b00100;
                    3'd4: glyph_row = 5'b01000; 3'd5: glyph_row = 5'b01000;
                    default: glyph_row = 5'b01000;
                endcase
                4'd8: case (r)
                    3'd0: glyph_row = 5'b01110; 3'd1: glyph_row = 5'b10001;
                    3'd2: glyph_row = 5'b10001; 3'd3: glyph_row = 5'b01110;
                    3'd4: glyph_row = 5'b10001; 3'd5: glyph_row = 5'b10001;
                    default: glyph_row = 5'b01110;
                endcase
                4'd9: case (r)
                    3'd0: glyph_row = 5'b01110; 3'd1: glyph_row = 5'b10001;
                    3'd2: glyph_row = 5'b10001; 3'd3: glyph_row = 5'b01111;
                    3'd4: glyph_row = 5'b00001; 3'd5: glyph_row = 5'b00010;
                    default: glyph_row = 5'b01100;
                endcase
                default: glyph_row = 5'b00000;
            endcase
        end
    endfunction

    wire [2:0] grow = row_off[3:1];        // 0..7
    wire [2:0] gcol = cell_x[3:1];         // 0..5 (column 5 blank)
    wire [4:0] glyph = glyph_row(ch, grow);
    wire       glyph_bit = (gcol < 3'd5) ? glyph[3'd4 - gcol] : 1'b0;
    wire       draw = in_h && in_v && glyph_bit;

    always @(posedge pixel_clk) begin
        if (!reset_n) begin
            x <= 0; y <= 0; de_d <= 0; vs_d <= 0;
            shown_us <= 0; shown_live <= 0;
            video_rgb_out <= 0; overlay_pixel <= 0; text_pixels <= 0;
        end else begin
            de_d <= video_de;
            vs_d <= video_vs;

            if (!video_de) x <= 0;
            else           x <= x + 1'b1;

            if (video_vs && !vs_d) begin
                y <= 0;
                shown_us    <= latency_us;     // latch once per frame
                shown_live  <= live;
                text_pixels <= 0;
            end else if (de_d && !video_de) begin
                y <= y + 1'b1;
            end

            if (draw) begin
                video_rgb_out <= TEXT_RGB;
                overlay_pixel <= 1'b1;
                text_pixels   <= text_pixels + 1'b1;
            end else begin
                video_rgb_out <= video_rgb_in;
                overlay_pixel <= 1'b0;
            end
        end
    end
endmodule
