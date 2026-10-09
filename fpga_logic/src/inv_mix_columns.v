module inv_mix_columns(
    input [127:0] state_in,
    output [127:0] state_out
);
    function [7:0] gmul;
        input [7:0] a;
        input [7:0] b;
        integer i;
        reg [7:0] p, hi_bit_set;
        begin
            p = 0;
            for(i=0;i<8;i=i+1) begin
                if(b[0]) p = p ^ a;
                hi_bit_set = a & 8'h80;
                a = a << 1;
                if(hi_bit_set) a = a ^ 8'h1b;
                b = b >> 1;
            end
            gmul = p;
        end
    endfunction

    genvar c;
    generate
        for(c=0;c<4;c=c+1) begin : col
            wire [7:0] s0 = state_in[127-8*(c*4+0) -:8];
            wire [7:0] s1 = state_in[127-8*(c*4+1) -:8];
            wire [7:0] s2 = state_in[127-8*(c*4+2) -:8];
            wire [7:0] s3 = state_in[127-8*(c*4+3) -:8];
            assign state_out[127-8*(c*4+0) -:8] = gmul(s0,14) ^ gmul(s1,11) ^ gmul(s2,13) ^ gmul(s3,9);
            assign state_out[127-8*(c*4+1) -:8] = gmul(s0,9)  ^ gmul(s1,14) ^ gmul(s2,11) ^ gmul(s3,13);
            assign state_out[127-8*(c*4+2) -:8] = gmul(s0,13) ^ gmul(s1,9)  ^ gmul(s2,14) ^ gmul(s3,11);
            assign state_out[127-8*(c*4+3) -:8] = gmul(s0,11) ^ gmul(s1,13) ^ gmul(s2,9)  ^ gmul(s3,14);
        end
    endgenerate
endmodule