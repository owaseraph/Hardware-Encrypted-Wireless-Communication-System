module uart_rx #(
    parameter CLKS_PER_BIT = 33  // 100 MHz / 3 Mbaud ≈ 33.33 //115200 baud
)(
    input        i_clk,
    input        i_rst,
    input        i_rx_serial,
    output reg   o_rx_dv,
    output reg [7:0] o_rx_byte
);

    localparam IDLE      = 3'b000;
    localparam START_BIT = 3'b001;
    localparam DATA_BITS = 3'b010;
    localparam STOP_BIT  = 3'b011;
    localparam CLEANUP   = 3'b100;

    reg [2:0]  r_state = IDLE;
    reg [15:0] r_clk_count = 0;
    reg [2:0]  r_bit_index = 0;
    reg [7:0]  r_rx_byte = 0;

    // double register (good practice)
    reg r_rx1 = 1'b1;
    reg r_rx2 = 1'b1;

    always @(posedge i_clk) begin
        r_rx1 <= i_rx_serial;
        r_rx2 <= r_rx1;
    end

    always @(posedge i_clk or posedge i_rst) begin
        if (i_rst) begin
            r_state <= IDLE;
            o_rx_dv <= 0;
        end else begin
            case (r_state)

                IDLE: begin
                    o_rx_dv <= 0;
                    r_clk_count <= 0;
                    r_bit_index <= 0;

                    if (r_rx2 == 0)
                        r_state <= START_BIT;
                end

                START_BIT: begin
                    if (r_clk_count == (CLKS_PER_BIT-1)/2) begin
                        if (r_rx2 == 0) begin
                            r_clk_count <= 0;
                            r_state <= DATA_BITS;
                        end else
                            r_state <= IDLE;
                    end else
                        r_clk_count <= r_clk_count + 1;
                end

                DATA_BITS: begin
                    if (r_clk_count < CLKS_PER_BIT-1)
                        r_clk_count <= r_clk_count + 1;
                    else begin
                        r_clk_count <= 0;
                        r_rx_byte[r_bit_index] <= r_rx2;

                        if (r_bit_index < 7)
                            r_bit_index <= r_bit_index + 1;
                        else begin
                            r_bit_index <= 0;
                            r_state <= STOP_BIT;
                        end
                    end
                end

                STOP_BIT: begin
                    if (r_clk_count < CLKS_PER_BIT-1)
                        r_clk_count <= r_clk_count + 1;
                    else begin
                        o_rx_dv   <= 1'b1;
                        o_rx_byte <= r_rx_byte;
                        r_clk_count <= 0;
                        r_state <= CLEANUP;
                    end
                end

                CLEANUP: begin
                    r_state <= IDLE;
                    o_rx_dv <= 0;
                end

            endcase
        end
    end
endmodule