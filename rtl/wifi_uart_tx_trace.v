// Optional diagnostic: decode the actual internal TX wire, retain only hashes.
module wifi_uart_tx_trace(
    input wire clk,reset,tx,start,stop,
    input wire [15:0] baud_divider,
    input wire [30:0] session,
    input wire [6:0] read_index,
    output wire [31:0] read_hash,
    output reg [31:0] count,partial_hash,first_hash,trace_session,errors,
    output reg active
);
    wire [7:0] decoded;
    wire valid,frame_error;
    wifi_uart_phy receiver(
        .clk(clk),.reset(reset || start),.baud_divider(baud_divider),.rx(tx),.tx(),
        .tx_data(8'd0),.tx_valid(1'b0),.tx_ready(),
        .rx_data(decoded),.rx_valid(valid),.framing_error(frame_error),.rx_busy(),.rx_level());
    (* ram_style="distributed" *) reg [31:0] hashes[0:127];
    wire [31:0] next_hash=(partial_hash ^ {24'd0,decoded}) * 32'h01000193;
    assign read_hash=hashes[read_index];
    always @(posedge clk) begin
        if(reset || start) begin
            active<=start;count<=0;partial_hash<=32'h811c9dc5;first_hash<=0;errors<=0;
            trace_session<=start?{1'b0,session}:0;
        end else begin
            if(stop) active<=0;
            if(active && !stop && valid) begin
                count<=count+1'b1;
                if(count[7:0]==8'hff) begin
                    hashes[count[14:8]]<=next_hash;partial_hash<=32'h811c9dc5;
                    if(count==255) first_hash<=next_hash;
                end else partial_hash<=next_hash;
            end
            if(active && !stop && frame_error) errors<=errors+1'b1;
        end
    end
endmodule
