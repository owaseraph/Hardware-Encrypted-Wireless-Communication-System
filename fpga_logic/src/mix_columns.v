module mix_columns(input [127:0] in, output [127:0] out);

    function [7:0] xtime;
        input [7:0] x;
        xtime = (x << 1) ^ (8'h1b & {8{x[7]}});
    endfunction

    function [31:0] mix;
        input [31:0] c;
        reg [7:0] s0,s1,s2,s3;
        begin
            s0=c[31:24]; s1=c[23:16];
            s2=c[15:8];  s3=c[7:0];

            mix[31:24]=xtime(s0) ^ (xtime(s1)^s1) ^ s2 ^ s3;
            mix[23:16]=s0 ^ xtime(s1) ^ (xtime(s2)^s2) ^ s3;
            mix[15:8] =s0 ^ s1 ^ xtime(s2) ^ (xtime(s3)^s3);
            mix[7:0]  =(xtime(s0)^s0) ^ s1 ^ s2 ^ xtime(s3);
        end
    endfunction

    assign out[127:96]=mix(in[127:96]);
    assign out[95:64] =mix(in[95:64]);
    assign out[63:32] =mix(in[63:32]);
    assign out[31:0]  =mix(in[31:0]);

endmodule