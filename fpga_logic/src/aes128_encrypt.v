module aes128_encrypt (
    input clk,
    input rst,
    input start,
    input [127:0] plaintext,
    output reg [127:0] ciphertext,
    output reg done
);

    parameter [127:0] KEY = 128'h000102030405060708090a0b0c0d0e0f;

    reg [127:0] state;
    reg [3:0] round;

    // start edge detect
    reg start_d;
    always @(posedge clk) begin
        start_d <= start;
    end
    wire start_pulse = start & ~start_d;

    wire [127:0] round_key;

    key_expand ke (
        .round(round),
        .key_in(KEY),
        .round_key(round_key)
    );

    wire [127:0] sb, sr, mc;

    sub_bytes   u1 (.in(state), .out(sb));
    shift_rows  u2 (.in(sb), .out(sr));
    mix_columns u3 (.in(sr), .out(mc));

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            round <= 0;
            done  <= 0;
            state <= 0;
            ciphertext <= 0;
        end else begin
            done <= 0; // default: 1-cycle pulse

            // start only if idle
            if (start_pulse && round == 0) begin
                state <= plaintext ^ KEY;
                round <= 1;
            end
            else if (round >= 1 && round <= 9) begin
                state <= mc ^ round_key;
                round <= round + 1;
            end
            else if (round == 10) begin
                ciphertext <= sr ^ round_key;
                state <= sr ^ round_key;
                done <= 1;
                round <= 0;
            end
        end
    end

endmodule