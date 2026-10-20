// AXI4-Lite UART mailbox, 50 MHz. Register contract: docs/design/WIFI_LINK.md.
module wifi_uart_axi #(parameter FIFO_ADDR_BITS=12, parameter TX_TRACE=0) (
    input wire aclk, aresetn,
    input wire [31:0] s_axi_awaddr,
    input wire s_axi_awvalid,
    output wire s_axi_awready,
    input wire [31:0] s_axi_wdata,
    input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid,
    output wire s_axi_wready,
    output reg [1:0] s_axi_bresp,
    output reg s_axi_bvalid,
    input wire s_axi_bready,
    input wire [31:0] s_axi_araddr,
    input wire s_axi_arvalid,
    output wire s_axi_arready,
    output reg [31:0] s_axi_rdata,
    output reg [1:0] s_axi_rresp,
    output reg s_axi_rvalid,
    input wire s_axi_rready,
    input wire esp_rx,
    output wire esp_tx
);
    reg aw_hold, w_hold, read_pending, read_had_data;
    reg [31:0] aw_address, w_data;
    reg [3:0] w_strobe, control_pulse;
    reg [15:0] baud_divider;
    reg [31:0] rx_overflows, framing_errors, sent_bytes, received_bytes;
    wire write_commit = aw_hold && w_hold && !s_axi_bvalid;
    assign s_axi_awready = !aw_hold && !s_axi_bvalid;
    assign s_axi_wready = !w_hold && !s_axi_bvalid;
    assign s_axi_arready = !s_axi_rvalid && !read_pending;
    wire read_accept = s_axi_arready && s_axi_arvalid;
    wire rx_empty, rx_full, tx_empty, tx_full;
    wire [FIFO_ADDR_BITS:0] rx_count, tx_count;
    wire [7:0] rx_fifo_data, tx_fifo_data, received_data;
    wire rx_valid, frame_error, rx_busy, rx_level, tx_ready;
    reg tx_pending, tx_offer;
    reg [7:0] tx_holding;
    wire tx_allowed = aresetn && !control_pulse[1] && !control_pulse[2];
    wire tx_valid_phy = tx_offer && tx_allowed;
    wire tx_pop = tx_allowed && tx_ready && !tx_pending && !tx_offer && !tx_empty;
    wire tx_space = !tx_full || tx_pop;
    wire tx_push = write_commit && aw_address == 32'h10 && w_strobe[0] && tx_space;
    wire rx_pop = read_accept && s_axi_araddr == 32'h14 && !rx_empty;
    wire reset_rx = !aresetn || control_pulse[0];
    wire reset_tx = !aresetn || control_pulse[1];
    wire reset_phy = !aresetn || control_pulse[2];
    // One atomic busy bit covers FIFO, prefetch and PHY; there is no false
    // idle interval after the final FIFO pop and before its start bit.
    wire tx_busy = !tx_ready || !tx_empty || tx_pending || tx_offer;
    wire [31:0] status_word = {22'd0,esp_tx,rx_level,4'd0,rx_busy,tx_busy,tx_space,!rx_empty};
    function [31:0] merge_bytes;
        input [31:0] old_value, new_value;
        input [3:0] strobes;
        integer lane;
        begin
            merge_bytes = old_value;
            for (lane=0; lane<4; lane=lane+1)
                if (strobes[lane]) merge_bytes[lane*8+:8] = new_value[lane*8+:8];
        end
    endfunction
    wire [31:0] new_divider = merge_bytes({16'd0,baud_divider},w_data,w_strobe);
    wire trace_control = TX_TRACE && write_commit && aw_address==32'h34 && w_strobe==4'hf;
    wire [31:0] trace_count,trace_partial,trace_first,trace_session,trace_errors,trace_hash;
    wire trace_active;
    wire [6:0] trace_index=(s_axi_araddr-32'h100)>>2;
    generate if(TX_TRACE) begin : g_tx_trace
        wifi_uart_tx_trace monitor(.clk(aclk),.reset(!aresetn),.tx(esp_tx),.baud_divider(baud_divider),
            .start(trace_control && w_data[0]),.stop(trace_control && !w_data[0]),.session(w_data[31:1]),
            .read_index(trace_index),.read_hash(trace_hash),.count(trace_count),.partial_hash(trace_partial),
            .first_hash(trace_first),.trace_session(trace_session),.errors(trace_errors),.active(trace_active));
    end else begin : g_no_tx_trace
        assign trace_count=0;assign trace_partial=0;assign trace_first=0;assign trace_session=0;
        assign trace_errors=0;assign trace_hash=0;assign trace_active=0;
    end endgenerate
    wifi_byte_fifo #(.ADDR_BITS(FIFO_ADDR_BITS)) u_rx_fifo(
        .clk(aclk),.reset(reset_rx),.push(rx_valid),.pop(rx_pop),
        .data_in(received_data),.data_out(rx_fifo_data),.empty(rx_empty),.full(rx_full),.count(rx_count));
    wifi_byte_fifo #(.ADDR_BITS(FIFO_ADDR_BITS)) u_tx_fifo(
        .clk(aclk),.reset(reset_tx),.push(tx_push),.pop(tx_pop),
        .data_in(w_data[7:0]),.data_out(tx_fifo_data),.empty(tx_empty),.full(tx_full),.count(tx_count));
    wifi_uart_phy u_phy(
        .clk(aclk),.reset(reset_phy),.baud_divider(baud_divider),.rx(esp_rx),.tx(esp_tx),
        .tx_data(tx_holding),.tx_valid(tx_valid_phy),.tx_ready(tx_ready),
        .rx_data(received_data),.rx_valid(rx_valid),.framing_error(frame_error),
        .rx_busy(rx_busy),.rx_level(rx_level));
    always @(posedge aclk) begin
        if (reset_tx || reset_phy) begin
            tx_pending <= 0; tx_offer <= 0; tx_holding <= 0;
        end else begin
            if (tx_pop) tx_pending <= 1;
            if (tx_pending) begin tx_holding <= tx_fifo_data; tx_offer <= 1; tx_pending <= 0; end
            if (tx_offer && tx_ready) tx_offer <= 0;
        end
    end
    always @(posedge aclk) begin
        if (!aresetn || control_pulse[3]) begin
            rx_overflows <= 0; framing_errors <= 0; sent_bytes <= 0; received_bytes <= 0;
        end else begin
            if (rx_valid) received_bytes <= received_bytes+1'b1;
            if (tx_valid_phy && tx_ready) sent_bytes <= sent_bytes+1'b1;
            if (rx_valid && rx_full && !rx_pop) rx_overflows <= rx_overflows+1'b1;
            if (frame_error) framing_errors <= framing_errors+1'b1;
        end
    end
    always @(posedge aclk) begin
        control_pulse <= 0;
        if (!aresetn) begin
            aw_hold <= 0; w_hold <= 0; aw_address <= 0; w_data <= 0; w_strobe <= 0;
            s_axi_bvalid <= 0; s_axi_bresp <= 0; baud_divider <= 434;
        end else begin
            if (s_axi_awready && s_axi_awvalid) begin aw_address <= s_axi_awaddr; aw_hold <= 1; end
            if (s_axi_wready && s_axi_wvalid) begin w_data <= s_axi_wdata; w_strobe <= s_axi_wstrb; w_hold <= 1; end
            if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 0;
            if (write_commit) begin
                aw_hold <= 0; w_hold <= 0; s_axi_bvalid <= 1; s_axi_bresp <= 0;
                case (aw_address)
                    32'h08: if (new_divider < 16 || new_divider > 65535 || !tx_ready || !tx_empty || tx_pending || tx_offer || rx_busy || !rx_level)
                                s_axi_bresp <= 2;
                            else baud_divider <= new_divider[15:0];
                    32'h0c: if (w_strobe[0]) control_pulse <= w_data[3:0];
                    32'h10: if (w_strobe[0] && !tx_space) s_axi_bresp <= 2;
                    32'h34: if(!TX_TRACE || w_strobe!=4'hf) s_axi_bresp<=2;
                    default: s_axi_bresp <= 2;
                endcase
            end
        end
    end
    always @(posedge aclk) begin
        if (!aresetn) begin
            read_pending <= 0; read_had_data <= 0;
            s_axi_rvalid <= 0; s_axi_rdata <= 0; s_axi_rresp <= 0;
        end else begin
            if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 0;
            if (read_pending) begin
                read_pending <= 0; s_axi_rvalid <= 1;
                s_axi_rdata <= read_had_data ? {1'b1,23'd0,rx_fifo_data} : 0;
            end
            if (read_accept) begin
                s_axi_rresp <= 0; s_axi_rvalid <= 1;
                case (s_axi_araddr)
                    32'h00: s_axi_rdata <= 32'h57494631;
                    32'h04: s_axi_rdata <= status_word;
                    32'h08: s_axi_rdata <= {16'd0,baud_divider};
                    32'h0c: s_axi_rdata <= 0;
                    32'h14: begin read_pending <= 1; read_had_data <= !rx_empty; s_axi_rvalid <= 0; end
                    32'h18: s_axi_rdata <= rx_count;
                    32'h1c: s_axi_rdata <= tx_count;
                    32'h20: s_axi_rdata <= rx_overflows;
                    32'h24: s_axi_rdata <= framing_errors;
                    32'h28: s_axi_rdata <= sent_bytes;
                    32'h2c: s_axi_rdata <= received_bytes;
                    32'h30: begin s_axi_rdata<=TX_TRACE?32'h31544857:0;if(!TX_TRACE) s_axi_rresp<=2;end
                    32'h34: begin s_axi_rdata<={31'd0,trace_active};if(!TX_TRACE) s_axi_rresp<=2;end
                    32'h38: begin s_axi_rdata<=trace_count;if(!TX_TRACE) s_axi_rresp<=2;end
                    32'h3c: begin s_axi_rdata<=trace_partial;if(!TX_TRACE) s_axi_rresp<=2;end
                    32'h40: begin s_axi_rdata<=trace_first;if(!TX_TRACE) s_axi_rresp<=2;end
                    32'h44: begin s_axi_rdata<=trace_session;if(!TX_TRACE) s_axi_rresp<=2;end
                    32'h48: begin s_axi_rdata<=trace_errors;if(!TX_TRACE) s_axi_rresp<=2;end
                    default: begin
                        if(TX_TRACE && s_axi_araddr>=32'h100 && s_axi_araddr<32'h300 && s_axi_araddr[1:0]==0)
                            s_axi_rdata<=trace_hash;
                        else begin s_axi_rresp<=2;s_axi_rdata<=0;end
                    end
                endcase
            end
        end
    end
endmodule
