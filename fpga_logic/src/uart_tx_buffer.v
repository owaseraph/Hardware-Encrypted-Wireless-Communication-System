module uart_tx_buffer(
    input clk,
    input rst,
    input start,
    input [127:0] block_in,
    input tx_done,          // ✅ ADD THIS
    output reg tx_dv,
    output reg [7:0] tx_byte,
    output reg done
);

    reg [127:0] buffer;
    reg [3:0] index;
    reg busy;
    reg waiting; // waiting for tx_done

always @(posedge clk or posedge rst) begin
    if (rst) begin
        index <= 0;
        tx_dv <= 0;
        tx_byte <= 0;
        done <= 0;
        busy <= 0;
        waiting <= 0;
    end else begin
        done <= 0;
        tx_dv <= 0; // default

        if (start && !busy) begin
            buffer <= block_in;
            index <= 0;
            busy <= 1;
            waiting <= 0;
        end

        else if (busy) begin
            // send new byte only if not waiting
            if (!waiting) begin
                tx_byte <= buffer[127 - index*8 -: 8];
                tx_dv <= 1;
                waiting <= 1;

                //$display("TX Buffer sending byte %0d: %02h", index,
                         //buffer[127 - index*8 -: 8]);
            end

            // wait until UART finishes
            else if (tx_done) begin
                waiting <= 0;

                if (index < 15)
                    index <= index + 1;
                else begin
                    busy <= 0;
                    done <= 1;
                end
            end
        end
    end
end
endmodule