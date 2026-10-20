`timescale 1ns/1ps
// Two-line latency readout drawn on top of the existing picture.
//
// The accepted display chain (VDMA -> framebuffer_video -> DVI) is untouched:
// this block only replaces pixels inside a fixed box in the top-right corner of
// the 1024x600 active area, drawing
//     L1 nnnn us     (audio loopback latency, audio_regs 0x08)
//     T1 nnnn ms     (cloud translation latency, audio_regs 0x88)
// in a 5x7 font scaled 2x (10 cells * 12 px = 120 px wide, two 16 px lines).
// Pixels outside the box pass through unchanged.
//
// RGB and DE/HS/VS are all registered here in one block, exactly like
// framebuffer_video.v, so the sync pulses stay aligned with the delayed pixel
// data. (The earlier version delayed DE/HS/VS in the top level instead, which
// shifted the picture by one pixel against the serialiser.)
module audio_overlay #(
    parameter H_ACTIVE     = 1024,
    parameter ENABLE       = 1,
    parameter X_ORIGIN     = 896,      // H_ACTIVE - 8 - 120
    parameter Y_ORIGIN     = 48,
    parameter MAX_SHOWN    = 9999,
    // The subtitle UI is a light theme (white paper / pale canvas), so the readout
    // must be dark ink; white-on-white was invisible on the real screen.
    parameter [23:0] TEXT_RGB = 24'h172A38
) (
    input  wire        pixel_clk,
    input  wire        reset_n,
    input  wire        video_hs,
    input  wire        video_vs,
    input  wire        video_de,
    input  wire [23:0] video_rgb_in,
    input  wire [19:0] latency_us,
    input  wire        live_us,           // audio loopback timestamp valid
    input  wire [15:0] latency_ms,
    input  wire        live_ms,           // cloud translation latency valid
    input  wire        freeze,            // hold the last values (CTRL bit 2)
    output reg  [23:0] video_rgb_out,
    output reg         video_hs_out,
    output reg         video_vs_out,
    output reg         video_de_out,
    output reg         overlay_pixel,     // 1 while a text pixel is drawn
    output reg  [15:0] text_pixels        // drawn pixels in the current frame
);
    localparam integer CELL_W = 12;
    localparam integer BOX_W  = 120;      // 10 cells
    localparam integer BOX_H  = 32;       // two 16 px lines

    // character codes: 0..9 are digits, 10..14 are letters
    localparam [3:0] CH_L = 4'd10, CH_U = 4'd11, CH_T = 4'd12, CH_M = 4'd13, CH_S = 4'd14;

    reg [10:0] x;                 // active pixel index in the line
    reg [9:0]  y;                 // active line index in the frame
    reg        de_d, vs_d;
    reg [19:0] shown_us;
    reg [15:0] shown_ms;
    reg        shown_live_us, shown_live_ms;

    // Character cell position inside the box: counters instead of a division so
    // no arithmetic sits in the pixel path.
    reg [3:0] cell_ix;        // 0..9 character index
    reg [3:0] cell_x;         // 0..11 column inside the character

    wire in_h = video_de && (x >= X_ORIGIN[10:0]) && (x < (X_ORIGIN + BOX_W));
    wire in_v = (y >= Y_ORIGIN[9:0]) && (y < (Y_ORIGIN + BOX_H));
    wire [5:0] row_off = y[5:0] - Y_ORIGIN[5:0];
    wire       line2   = row_off[4];
    wire [2:0] grow    = row_off[3:1];     // 0..7 row inside the 16 px line
    wire [2:0] gcol    = cell_x[3:1];      // 0..5 glyph column

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
                CH_U: case (r)
                    3'd0: glyph_row = 5'b10001; 3'd1: glyph_row = 5'b10001;
                    3'd2: glyph_row = 5'b10001; 3'd3: glyph_row = 5'b10001;
                    3'd4: glyph_row = 5'b10001; 3'd5: glyph_row = 5'b10001;
                    default: glyph_row = 5'b01110;
                endcase
                CH_T: case (r)
                    3'd0: glyph_row = 5'b11111; 3'd1: glyph_row = 5'b00100;
                    3'd2: glyph_row = 5'b00100; 3'd3: glyph_row = 5'b00100;
                    3'd4: glyph_row = 5'b00100; 3'd5: glyph_row = 5'b00100;
                    default: glyph_row = 5'b00100;
                endcase
                CH_M: case (r)
                    3'd0: glyph_row = 5'b00000; 3'd1: glyph_row = 5'b11011;
                    3'd2: glyph_row = 5'b10101; 3'd3: glyph_row = 5'b10101;
                    3'd4: glyph_row = 5'b10101; 3'd5: glyph_row = 5'b10101;
                    default: glyph_row = 5'b00000;
                endcase
                CH_S: case (r)
                    3'd0: glyph_row = 5'b01111; 3'd1: glyph_row = 5'b10000;
                    3'd2: glyph_row = 5'b10000; 3'd3: glyph_row = 5'b01110;
                    3'd4: glyph_row = 5'b00001; 3'd5: glyph_row = 5'b00001;
                    default: glyph_row = 5'b11110;
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

    // Digit extraction for both latched values: one digit per pixel clock via a
    // free-running four-step pass (a divider missed 50 MHz timing).
    reg  [1:0]  dstep_us, dstep_ms;
    reg  [13:0] drem_us,  drem_ms;
    reg  [3:0]  ud3, ud2, ud1, ud0;   // microseconds
    reg  [3:0]  md3, md2, md1, md0;   // milliseconds

    wire [13:0] us_shown = (shown_us > MAX_SHOWN) ? MAX_SHOWN[13:0] : shown_us[13:0];
    wire [13:0] ms_shown = (shown_ms > MAX_SHOWN) ? MAX_SHOWN[13:0] : shown_ms[13:0];
    wire [13:0] dh  = (dstep_us == 2'd0) ? 14'd1000 : (dstep_us == 2'd1) ? 14'd100 :
                      (dstep_us == 2'd2) ? 14'd10 : 14'd1;
    wire [13:0] m2  = dh << 1;
    wire [13:0] m4  = dh << 2;
    wire [13:0] m8  = dh << 3;
    wire [3:0]  udig = (drem_us >= m8 + dh) ? 4'd9 : (drem_us >= m8) ? 4'd8 : (drem_us >= m8 - dh) ? 4'd7 :
                       (drem_us >= m4 + m2) ? 4'd6 : (drem_us >= m4 + dh) ? 4'd5 : (drem_us >= m4) ? 4'd4 :
                       (drem_us >= m2 + dh) ? 4'd3 : (drem_us >= m2) ? 4'd2 : (drem_us >= dh) ? 4'd1 : 4'd0;
    wire [13:0] dh2 = (dstep_ms == 2'd0) ? 14'd1000 : (dstep_ms == 2'd1) ? 14'd100 :
                      (dstep_ms == 2'd2) ? 14'd10 : 14'd1;
    wire [13:0] n2  = dh2 << 1;
    wire [13:0] n4  = dh2 << 2;
    wire [13:0] n8  = dh2 << 3;
    wire [3:0]  mdig = (drem_ms >= n8 + dh2) ? 4'd9 : (drem_ms >= n8) ? 4'd8 : (drem_ms >= n8 - dh2) ? 4'd7 :
                       (drem_ms >= n4 + n2) ? 4'd6 : (drem_ms >= n4 + dh2) ? 4'd5 : (drem_ms >= n4) ? 4'd4 :
                       (drem_ms >= n2 + dh2) ? 4'd3 : (drem_ms >= n2) ? 4'd2 : (drem_ms >= dh2) ? 4'd1 : 4'd0;

    reg [3:0] ch;
    always @* begin
        case (cell_ix)
            4'd0: ch = line2 ? CH_T : CH_L;
            4'd1: ch = 4'd1;                 // the "1" digit glyph
            4'd8: ch = line2 ? CH_M : CH_U;
            4'd9: ch = CH_S;
            default: ch = 4'd0;
        endcase
    end

    wire [3:0] digit = line2 ? (cell_ix == 4'd3 ? md3 : cell_ix == 4'd4 ? md2 :
                                cell_ix == 4'd5 ? md1 : md0)
                             : (cell_ix == 4'd3 ? ud3 : cell_ix == 4'd4 ? ud2 :
                                cell_ix == 4'd5 ? ud1 : ud0);
    wire in_digits = (cell_ix >= 4'd3) && (cell_ix <= 4'd6);
    wire blank = (cell_ix == 4'd2) || (cell_ix == 4'd7);
    wire valid_line = line2 ? shown_live_ms : shown_live_us;
    wire [3:0] glyph_code = in_digits ? digit : ch;
    wire [4:0] dash_row = (grow == 3'd3) ? 5'b11111 : 5'd0;
    wire [4:0] glyph = blank ? 5'd0 :
                       (in_digits && !valid_line) ? dash_row : glyph_row(glyph_code, grow);
    wire       glyph_bit = !blank && (gcol < 3'd5) && glyph[3'd4 - gcol];
    wire       draw = ENABLE && in_h && in_v && glyph_bit;

    always @(posedge pixel_clk) begin
        if (!reset_n) begin
            x <= 0; y <= 0; de_d <= 0; vs_d <= 0;
            shown_us <= 0; shown_ms <= 0; shown_live_us <= 0; shown_live_ms <= 0;
            cell_ix <= 4'd0; cell_x <= 4'd0;
            dstep_us <= 2'd0; drem_us <= 14'd0; dstep_ms <= 2'd0; drem_ms <= 14'd0;
            ud3 <= 4'd0; ud2 <= 4'd0; ud1 <= 4'd0; ud0 <= 4'd0;
            md3 <= 4'd0; md2 <= 4'd0; md1 <= 4'd0; md0 <= 4'd0;
            video_rgb_out <= 0; video_hs_out <= 0; video_vs_out <= 0; video_de_out <= 0;
            overlay_pixel <= 0; text_pixels <= 0;
        end else begin
            de_d <= video_de;
            vs_d <= video_vs;
            video_hs_out <= video_hs;
            video_vs_out <= video_vs;
            video_de_out <= video_de;

            // One digit per clock for each value; the pass restarts after units.
            if (dstep_us == 2'd3) drem_us <= us_shown;
            else                  drem_us <= drem_us - (udig * dh);
            case (dstep_us)
                2'd0: ud3 <= udig; 2'd1: ud2 <= udig;
                2'd2: ud1 <= udig; default: ud0 <= udig;
            endcase
            dstep_us <= dstep_us + 1'b1;
            if (dstep_ms == 2'd3) drem_ms <= ms_shown;
            else                  drem_ms <= drem_ms - (mdig * dh2);
            case (dstep_ms)
                2'd0: md3 <= mdig; 2'd1: md2 <= mdig;
                2'd2: md1 <= mdig; default: md0 <= mdig;
            endcase
            dstep_ms <= dstep_ms + 1'b1;

            if (!video_de) x <= 0;
            else           x <= x + 1'b1;

            // Character cells advance with the pixels of the box.
            if (!video_de || x == X_ORIGIN[10:0]) begin
                cell_ix <= 4'd0; cell_x <= 4'd0;
            end else if (in_h) begin
                if (cell_x == 4'd11) begin cell_x <= 4'd0; cell_ix <= cell_ix + 1'b1; end
                else                      cell_x <= cell_x + 1'b1;
            end

            if (video_vs && !vs_d) begin
                y <= 0;
                if (!freeze) begin
                    shown_us      <= latency_us;
                    shown_live_us <= live_us;
                    shown_ms      <= latency_ms;
                    shown_live_ms <= live_ms;
                end
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
