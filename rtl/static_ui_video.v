`timescale 1ns / 1ps
`include "static_ui_config.vh"

// Same verified raster as ../hdmi. Two synchronous ROM reads require matching
// two-cycle delay on HS/VS/DE and the coordinates exposed for verification.
module static_ui_video (
    input wire pixel_clk,
    input wire reset,
    output reg video_hs,
    output reg video_vs,
    output reg video_de,
    output wire [23:0] video_rgb,
    output reg [11:0] h_count,
    output reg [11:0] v_count,
    output reg [31:0] frame_count
);
    localparam H_SYNC = 20, H_BACK = 140, H_ACTIVE = 1024, H_TOTAL = 1312;
    localparam V_SYNC = 3, V_BACK = 20, V_ACTIVE = 600, V_TOTAL = 635;
    reg [11:0] raw_h, raw_v;
    reg [31:0] raw_frame;
    wire raw_hs = raw_h >= H_SYNC;
    wire raw_vs = raw_v >= V_SYNC;
    wire raw_de = (raw_h >= H_SYNC + H_BACK) &&
                  (raw_h < H_SYNC + H_BACK + H_ACTIVE) &&
                  (raw_v >= V_SYNC + V_BACK) &&
                  (raw_v < V_SYNC + V_BACK + V_ACTIVE);
    wire [11:0] x = raw_h - (H_SYNC + H_BACK);
    wire [11:0] y = raw_v - (V_SYNC + V_BACK);
    // 128 tiles per row and 75 rows; blanking always reads safe address zero.
    wire [13:0] tile_address = raw_de ? {y[9:3], x[9:3]} : 14'd0;
    wire [5:0] pixel_offset = raw_de ? {y[2:0], x[2:0]} : 6'd0;

    (* rom_style = "block" *) reg [11:0] tile_map [0:9599];
    (* rom_style = "block" *) reg [3:0] tile_pixels [0:`STATIC_UI_TILE_PIXELS-1];
    (* rom_style = "distributed" *) reg [23:0] palette [0:15];
    reg [11:0] tile_id;
    reg [5:0] offset_d;
    reg [3:0] color_index;
    reg hs_d, vs_d, de_d;
    reg [11:0] h_d, v_d;
    reg [31:0] frame_d;

    initial begin
        $readmemh("static_ui_tilemap.mem", tile_map);
        $readmemh("static_ui_tiles.mem", tile_pixels);
        $readmemh("static_ui_palette.mem", palette);
    end
    // Keep ROM ports free of asynchronous reset so synthesis can infer BRAM.
    always @(posedge pixel_clk) begin
        tile_id <= tile_map[tile_address];
        offset_d <= pixel_offset;
        color_index <= tile_pixels[{tile_id, offset_d}];
    end
    assign video_rgb = video_de ? palette[color_index] : 24'h000000;

    always @(posedge pixel_clk or posedge reset) begin
        if (reset) begin
            raw_h <= 0; raw_v <= 0; raw_frame <= 0;
        end else if (raw_h == H_TOTAL - 1) begin
            raw_h <= 0;
            if (raw_v == V_TOTAL - 1) begin
                raw_v <= 0; raw_frame <= raw_frame + 1;
            end else begin
                raw_v <= raw_v + 1;
            end
        end else begin
            raw_h <= raw_h + 1;
        end
    end
    always @(posedge pixel_clk or posedge reset) begin
        if (reset) begin
            hs_d <= 0; vs_d <= 0; de_d <= 0;
            video_hs <= 0; video_vs <= 0; video_de <= 0;
            h_d <= 0; v_d <= 0; frame_d <= 0;
            h_count <= 0; v_count <= 0; frame_count <= 0;
        end else begin
            hs_d <= raw_hs; vs_d <= raw_vs; de_d <= raw_de;
            video_hs <= hs_d; video_vs <= vs_d; video_de <= de_d;
            h_d <= raw_h; v_d <= raw_v; frame_d <= raw_frame;
            h_count <= h_d; v_count <= v_d; frame_count <= frame_d;
        end
    end
endmodule
