module uart_packet_buffer (
    input  wire       clk,
    input  wire       rst,

    input  wire       i_byte_dv,
    input  wire [7:0] i_byte,

    output reg [127:0] o_block,
    output reg         o_block_valid
);

    reg [127:0] buffer;
    reg [3:0]   count;

    always @(posedge clk) begin
        if (rst) begin
            count <= 0;
            o_block_valid <= 0;
        end else begin
            o_block_valid <= 0; // default: pulse

            if (i_byte_dv) begin
                buffer <= {buffer[119:0], i_byte};
                count <= count + 1;

                if (count == 15) begin
                    o_block <= {buffer[119:0], i_byte};
                    o_block_valid <= 1; // 1-cycle pulse
                    count <= 0;
                end
            end
        end
    end

endmodule