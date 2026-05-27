module uart_tx #(
    parameter CLKS_PER_BIT = 33  // 100 MHz / 3 Mbaud ≈ 33.33  //115200 baud
)(
    input        i_clk,
    input        i_rst,
    input        i_tx_dv,        // start signal
    input  [7:0] i_tx_byte,
    output reg   o_tx_active,
    output reg   o_tx_serial,
    output reg   o_tx_done
);

    localparam IDLE      = 3'b000;
    localparam START_BIT = 3'b001;
    localparam DATA_BITS = 3'b010;
    localparam STOP_BIT  = 3'b011;
    localparam CLEANUP   = 3'b100;

    reg [2:0]  r_state = IDLE;
    reg [15:0] r_clk_count = 0;
    reg [2:0]  r_bit_index = 0;
    reg [7:0]  r_tx_data = 0;

    always @(posedge i_clk or posedge i_rst) begin
        if (i_rst) begin
            r_state      <= IDLE;
            o_tx_serial  <= 1'b1;
            o_tx_done    <= 1'b0;
            o_tx_active  <= 1'b0;
        end else begin
            case (r_state)

                IDLE: begin
                    o_tx_serial <= 1'b1;
                    o_tx_done   <= 1'b0;
                    r_clk_count <= 0;
                    r_bit_index <= 0;
                
                    if (i_tx_dv) begin
                        o_tx_active <= 1'b1;
                        r_tx_data   <= i_tx_byte;
                
                        // 🔴 DEBUG: byte accepted
                        $display("UART_TX START: sending byte = %02h at time %t", i_tx_byte, $time);
                
                        r_state     <= START_BIT;
                    end
                end 
    
                START_BIT: begin
                    o_tx_serial <= 1'b0;
                    if (r_clk_count < CLKS_PER_BIT-1)
                        r_clk_count <= r_clk_count + 1;
                    else begin
                        r_clk_count <= 0;
                        r_state <= DATA_BITS;
                    end
                end

                DATA_BITS: begin
                    o_tx_serial <= r_tx_data[r_bit_index];

                    if (r_clk_count < CLKS_PER_BIT-1)
                        r_clk_count <= r_clk_count + 1;
                    else begin
                        r_clk_count <= 0;

                        if (r_bit_index < 7)
                            r_bit_index <= r_bit_index + 1;
                        else begin
                            r_bit_index <= 0;
                            r_state <= STOP_BIT;
                        end
                    end
                end

                STOP_BIT: begin
                    o_tx_serial <= 1'b1;
                    if (r_clk_count < CLKS_PER_BIT-1)
                        r_clk_count <= r_clk_count + 1;
                    else begin
                        o_tx_done  <= 1'b1;
                
                        // 🔴 DEBUG: byte finished
                        $display("UART_TX DONE: byte sent = %02h at time %t", r_tx_data, $time);
                
                        r_clk_count <= 0;
                        r_state <= CLEANUP;
                        o_tx_active <= 1'b0;
                    end
                end

                CLEANUP: begin
                    o_tx_done <= 1'b1;
                    r_state   <= IDLE;
                end

            endcase
        end
    end
endmodule