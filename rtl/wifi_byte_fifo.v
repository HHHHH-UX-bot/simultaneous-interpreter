module wifi_byte_fifo #(parameter ADDR_BITS=12) (
    input wire clk, reset, push, pop,
    input wire [7:0] data_in,
    output reg [7:0] data_out,
    output wire empty, full,
    output reg [ADDR_BITS:0] count
);
    localparam DEPTH = 1 << ADDR_BITS;
    (* ram_style="block" *) reg [7:0] memory [0:DEPTH-1];
    reg [ADDR_BITS-1:0] write_pointer, read_pointer;
    wire do_pop = pop && !empty;
    wire do_push = push && (!full || do_pop);
    assign empty = count == 0;
    assign full = count == DEPTH;
    // Synchronous read: data_out is valid one clock after an accepted pop.
    // No reset on memory or its output register, to infer block RAM.
    always @(posedge clk) begin
        if (do_push) memory[write_pointer] <= data_in;
        if (do_pop) data_out <= memory[read_pointer];
    end
    always @(posedge clk) begin
        if (reset) begin
            write_pointer <= 0; read_pointer <= 0; count <= 0;
        end else begin
            if (do_push) write_pointer <= write_pointer + 1'b1;
            if (do_pop) read_pointer <= read_pointer + 1'b1;
            case ({do_push,do_pop})
                2'b10: count <= count + 1'b1;
                2'b01: count <= count - 1'b1;
                default: count <= count;
            endcase
        end
    end
endmodule
