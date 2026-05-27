module top_aes_uart_decrypt(
    input        clk,
    input        rst,
    input        i_rx,
    output       o_tx
);

    wire [7:0]   rx_byte;
    wire         rx_dv;
    wire         tx_dv;
    wire [7:0]   tx_byte;
    wire         tx_done;

    uart_top uart_inst (
        .i_clk(clk), .i_rst(rst),
        .i_rx(i_rx), .o_tx(o_tx),
        .i_tx_dv(tx_dv), .i_tx_byte(tx_byte),
        .o_tx_done(tx_done),
        .o_rx_dv(rx_dv), .o_rx_byte(rx_byte)
    );

    wire [127:0] ciphertext_block;
    wire         block_valid;

    uart_packet_buffer packet_buf (
        .clk(clk), .rst(rst),
        .i_byte_dv(rx_dv), .i_byte(rx_byte),
        .o_block(ciphertext_block),
        .o_block_valid(block_valid)
    );

    reg          aes_start;
    wire [127:0] plaintext_block;
    wire         aes_done;

    aes128_decrypt aes (
        .clk(clk), .rst(rst),
        .start(aes_start),
        .ciphertext(ciphertext_block),
        .plaintext(plaintext_block),
        .done(aes_done)
    );

    always @(posedge clk or posedge rst) begin
        if (rst) aes_start <= 0;
        else     aes_start <= block_valid;
    end

    // ============================================================
    // 8-deep FIFO between AES and uart_tx_buffer.
    // See top_aes_uart_encrypt.v for full explanation.
    // ============================================================
    localparam FIFO_DEPTH = 8;
    localparam FIFO_AW    = 3;

    reg [127:0]        fifo_mem [0:FIFO_DEPTH-1];
    reg [FIFO_AW-1:0]  wr_ptr;
    reg [FIFO_AW-1:0]  rd_ptr;
    reg [FIFO_AW:0]    count;

    wire fifo_empty = (count == 0);
    wire fifo_full  = (count == FIFO_DEPTH);

    wire fifo_we = aes_done && !fifo_full;
    reg  fifo_re;

    always @(posedge clk) begin
        if (fifo_we) fifo_mem[wr_ptr] <= plaintext_block;
    end
    always @(posedge clk or posedge rst) begin
        if (rst)         wr_ptr <= 0;
        else if (fifo_we) wr_ptr <= wr_ptr + 1;
    end
    always @(posedge clk or posedge rst) begin
        if (rst)         rd_ptr <= 0;
        else if (fifo_re) rd_ptr <= rd_ptr + 1;
    end
    always @(posedge clk or posedge rst) begin
        if (rst) count <= 0;
        else case ({fifo_we, fifo_re})
            2'b10: count <= count + 1;
            2'b01: count <= count - 1;
            default: ;
        endcase
    end

    reg         tx_buf_busy;
    reg         launch;
    reg [127:0] launch_data;
    wire        tx_buf_done_w;

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            tx_buf_busy <= 0;
            launch      <= 0;
            fifo_re     <= 0;
            launch_data <= 0;
        end else begin
            launch  <= 0;
            fifo_re <= 0;

            if (tx_buf_done_w)
                tx_buf_busy <= 0;

            if (!tx_buf_busy && !fifo_empty && !launch) begin
                launch_data <= fifo_mem[rd_ptr];
                launch      <= 1;
                fifo_re     <= 1;
                tx_buf_busy <= 1;
            end
        end
    end

    uart_tx_buffer tx_buf (
        .clk(clk), .rst(rst),
        .start(launch),
        .block_in(launch_data),
        .tx_done(tx_done),
        .tx_dv(tx_dv),
        .tx_byte(tx_byte),
        .done(tx_buf_done_w)
    );

    always @(posedge clk) begin
        if (aes_done) $display("AES Done: %h (fifo count -> %0d)", plaintext_block, count + 1);
        if (aes_done && fifo_full)
            $display("WARNING: AES output dropped, FIFO full!");
    end

endmodule