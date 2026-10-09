module uart_top (
    input        i_clk,
    input        i_rst,

    // UART pins
    input        i_rx,
    output       o_tx,

    // TX interface
    input        i_tx_dv,
    input  [7:0] i_tx_byte,
    output       o_tx_done,

    // RX interface
    output       o_rx_dv,
    output [7:0] o_rx_byte
);

    uart_tx tx_inst (
        .i_clk(i_clk),
        .i_rst(i_rst),
        .i_tx_dv(i_tx_dv),
        .i_tx_byte(i_tx_byte),
        .o_tx_active(),
        .o_tx_serial(o_tx),
        .o_tx_done(o_tx_done)
    );

    uart_rx rx_inst (
        .i_clk(i_clk),
        .i_rst(i_rst),
        .i_rx_serial(i_rx),
        .o_rx_dv(o_rx_dv),
        .o_rx_byte(o_rx_byte)
    );

endmodule