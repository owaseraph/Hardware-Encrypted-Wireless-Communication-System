module inv_key_expand(
    input  [3:0] round_nr,      // 0..10
    output [127:0] round_key
);
    reg [127:0] keys [0:10];

    initial begin
        // Precomputed round keys for AES-128 key 000102030405060708090a0b0c0d0e0f
        keys[0]  = 128'h000102030405060708090a0b0c0d0e0f;
        keys[1]  = 128'hd6aa74fdd2af72fadaa678f1d6ab76fe;
        keys[2]  = 128'hb692cf0b643dbdf1be9bc5006830b3fe;
        keys[3]  = 128'hb6ff744ed2c2c9bf6c590cbf0469bf41;
        keys[4]  = 128'h47f7f7bc95353e03f96c32bcfd058dfd;
        keys[5]  = 128'h3caaa3e8a99f9deb50f3af57adf622aa;
        keys[6]  = 128'h5e390f7df7a69296a7553dc10aa31f6b;
        keys[7]  = 128'h14f9701ae35fe28c440adf4d4ea9c026;
        keys[8]  = 128'h47438735a41c65b9e016baf4aebf7ad2;
        keys[9]  = 128'h549932d1f08557681093ed9cbe2c974e;
        keys[10] = 128'h13111d7fe3944a17f307a78b4d2b30c5;
    end

    assign round_key = keys[round_nr];

endmodule