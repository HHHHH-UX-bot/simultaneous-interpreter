module wifi_uart_phy (
    input wire clk, reset,
    input wire [15:0] baud_divider,
    input wire rx,
    output wire tx,
    input wire [7:0] tx_data,
    input wire tx_valid,
    output wire tx_ready,
    output reg [7:0] rx_data,
    output reg rx_valid, framing_error,
    output wire rx_busy, rx_level
);
    (* ASYNC_REG="TRUE" *) reg [1:0] rx_sync = 2'b11;
    reg [1:0] rx_state;
    reg [15:0] rx_timer, tx_timer;
    reg [2:0] rx_bit;
    reg [7:0] rx_shift;
    reg [9:0] tx_shift;
    reg [3:0] tx_bits;
    assign rx_level = rx_sync[1];
    assign rx_busy = rx_state != 0;
    assign tx_ready = tx_bits == 0;
    assign tx = tx_ready ? 1'b1 : tx_shift[0];
    always @(posedge clk) begin
        if (reset) rx_sync <= 2'b11;
        else rx_sync <= {rx_sync[0],rx};
    end
    always @(posedge clk) begin
        if (reset) begin
            tx_shift <= 10'h3ff; tx_bits <= 0; tx_timer <= 0;
        end else if (tx_ready) begin
            if (tx_valid) begin
                tx_shift <= {1'b1,tx_data,1'b0};
                tx_bits <= 10; tx_timer <= baud_divider-1'b1;
            end
        end else if (tx_timer == 0) begin
            tx_shift <= {1'b1,tx_shift[9:1]};
            tx_bits <= tx_bits-1'b1; tx_timer <= baud_divider-1'b1;
        end else tx_timer <= tx_timer-1'b1;
    end
    always @(posedge clk) begin
        rx_valid <= 0; framing_error <= 0;
        if (reset) begin
            rx_state <= 0; rx_timer <= 0; rx_bit <= 0;
            rx_shift <= 0; rx_data <= 0;
        end else case (rx_state)
            0: if (!rx_sync[1]) begin
                rx_state <= 1; rx_timer <= (baud_divider >> 1)-1'b1;
            end
            1: if (rx_timer != 0) rx_timer <= rx_timer-1'b1;
               else if (rx_sync[1]) rx_state <= 0; // Reject short start glitches.
               else begin rx_state <= 2; rx_bit <= 0; rx_timer <= baud_divider-1'b1; end
            2: if (rx_timer != 0) rx_timer <= rx_timer-1'b1;
               else begin
                   rx_shift[rx_bit] <= rx_sync[1];
                   rx_timer <= baud_divider-1'b1;
                   if (rx_bit == 7) rx_state <= 3; else rx_bit <= rx_bit+1'b1;
               end
            3: if (rx_timer != 0) rx_timer <= rx_timer-1'b1;
               else begin
                   rx_state <= 0;
                   if (rx_sync[1]) begin rx_data <= rx_shift; rx_valid <= 1; end
                   else framing_error <= 1;
               end
        endcase
    end
endmodule
