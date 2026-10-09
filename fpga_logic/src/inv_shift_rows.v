module inv_shift_rows(
    input [127:0] state_in,
    output [127:0] state_out
);
    wire [7:0] s[0:15];
    genvar i;
    generate
        for(i=0;i<16;i=i+1)
            assign s[i] = state_in[127-8*i -:8];
    endgenerate

    assign state_out[127-8*0 -:8]   = s[0];
    assign state_out[127-8*1 -:8]   = s[13];
    assign state_out[127-8*2 -:8]   = s[10];
    assign state_out[127-8*3 -:8]   = s[7];

    assign state_out[127-8*4 -:8]   = s[4];
    assign state_out[127-8*5 -:8]   = s[1];
    assign state_out[127-8*6 -:8]   = s[14];
    assign state_out[127-8*7 -:8]   = s[11];

    assign state_out[127-8*8 -:8]   = s[8];
    assign state_out[127-8*9 -:8]   = s[5];
    assign state_out[127-8*10 -:8]  = s[2];
    assign state_out[127-8*11 -:8]  = s[15];

    assign state_out[127-8*12 -:8]  = s[12];
    assign state_out[127-8*13 -:8]  = s[9];
    assign state_out[127-8*14 -:8]  = s[6];
    assign state_out[127-8*15 -:8]  = s[3];
endmodule