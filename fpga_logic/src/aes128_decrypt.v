module aes128_decrypt (
    input  clk,
    input  rst,
    input  start,
    input  [127:0] ciphertext,
    output reg [127:0] plaintext,
    output reg done
);

    // FSM states
    parameter IDLE       = 2'd0,
              LOAD       = 2'd1,
              ROUND      = 2'd2,
              DONE_STATE = 2'd3;

    reg [1:0] state, next_state;
    reg [3:0] round_counter;       // 0..10
    reg [127:0] state_reg;

    // Round keys wires
    wire [127:0] rk [0:10]; // round0..round10

    // Generate all round keys
    inv_key_expand ike0(.round_nr(4'd0),  .round_key(rk[0]));
    genvar i;
    generate
        for (i=1; i<=10; i=i+1) begin : rk_gen
            inv_key_expand ike(.round_nr(i[3:0]), .round_key(rk[i]));
        end
    endgenerate

    // Temporary wires for combinational round operations
    wire [127:0] s1, s2, s3;

    // Rounds 1..9: InvShiftRows -> InvSubBytes -> AddRoundKey -> InvMixColumns
    inv_shift_rows irs(.state_in(state_reg), .state_out(s1));
    inv_sub_bytes   isb(.state_in(s1),      .state_out(s2));
    inv_mix_columns imc(.state_in(s2 ^ rk[10-round_counter]), .state_out(s3));

    // Final round (round 10): InvShiftRows -> InvSubBytes -> AddRoundKey (no InvMixColumns)
    wire [127:0] s1f, s2f;
    inv_shift_rows irsf(.state_in(state_reg), .state_out(s1f));
    inv_sub_bytes   isbf(.state_in(s1f),      .state_out(s2f));

    // =============================
    // Sequential FSM
    // =============================
    always @(posedge clk or posedge rst) begin
        if (rst) begin
            state          <= IDLE;
            state_reg      <= 0;
            round_counter  <= 0;
            plaintext      <= 0;
            done           <= 0;
        end else begin
            state <= next_state;

            case(state)
                IDLE: done <= 0;

                LOAD: begin
                    // Initial AddRoundKey: ciphertext ^ round10 key
                    state_reg     <= ciphertext ^ rk[10];
                    round_counter <= 1; // start from round 1
                end

                ROUND: begin
                    if (round_counter < 10) begin
                        // Rounds 1..9
                        state_reg <= s3;
                        round_counter <= round_counter + 1;
                    end else if (round_counter == 10) begin
                        // Final round
                        state_reg <= s2f ^ rk[0];
                        round_counter <= round_counter + 1;
                    end
                end

                DONE_STATE: begin
                    plaintext <= state_reg;
                    done      <= 1; // 1-cycle done pulse
                end
            endcase
        end
    end

    // FSM next-state logic
    always @(*) begin
        case(state)
            IDLE:       next_state = start ? LOAD : IDLE;
            LOAD:       next_state = ROUND;
            ROUND:      next_state = (round_counter > 10) ? DONE_STATE : ROUND;
            DONE_STATE: next_state = IDLE;
            default:    next_state = IDLE;
        endcase
    end

endmodule