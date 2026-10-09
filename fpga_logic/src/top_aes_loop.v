module top_aes_loop(
    input        clk,                // System clock
    input        rst,                // System reset, active high
    
    // UART interface for encryption
    input        uart_encrypt_rx,    // UART RX to AES encryptor
    output       uart_encrypt_tx,    // UART TX from AES encryptor
    
    // UART interface for decryption
    input        uart_decrypt_rx,    // UART RX to AES decryptor
    output       uart_decrypt_tx     // UART TX from AES decryptor
);

    // Instantiate AES Encryptor UART
    top_aes_uart_encrypt u_encrypt (
        .clk(clk),
        .rst(rst),
        .i_rx(uart_encrypt_rx),
        .o_tx(uart_encrypt_tx)
    );

    // Instantiate AES Decryptor UART
    top_aes_uart_decrypt u_decrypt (
        .clk(clk),
        .rst(rst),
        .i_rx(uart_decrypt_rx),
        .o_tx(uart_decrypt_tx)
    );

endmodule