#pragma once
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "config.h"
#include "ring_buffer.h"
#include "control_hub.h"

// ============================================================
//  UART I/O tasks (both run on Core 1)
//
//  uart_reader_task  — reads bytes from UART (FPGA TX side)
//                      and pushes them into g_tx_ring
//                      (consumed by sender_task)
//
//  uart_writer_task  — drains g_rx_ring
//                      (filled by receiver_task)
//                      and writes bytes to UART (FPGA RX side)
// ============================================================

extern ByteRingBuffer g_tx_ring;
extern ByteRingBuffer g_rx_ring;

// ── UART driver init (call once from app_main) ───────────────
static inline void uart_init() {
    uart_config_t cfg = {
        .baud_rate           = UART_BAUD,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk          = UART_SCLK_APB,
    };

    uart_driver_install(UART_PORT,
                        UART_READ_CHUNK * 16,   // RX hw ring buffer
                        UART_READ_CHUNK * 16,   // TX hw ring buffer
                        0, nullptr, 0);

    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    HUB_LOG("UART", "EVENT=INIT baud=%d rx=%d tx=%d", UART_BAUD, UART_RX_PIN, UART_TX_PIN);
}

// ── Reader task: UART RX → g_tx_ring ────────────────────────
void uart_reader_task(void* arg) {
    uint8_t chunk[UART_READ_CHUNK];
    HUB_LOG("UART", "EVENT=READER_START");

    while (true) {
        int n = uart_read_bytes(UART_PORT, chunk, sizeof(chunk),
                                pdMS_TO_TICKS(10));   // 20 ms timeout
        if (n > 0) {
            HUB_LOG("UART", "EVENT=READ_CHUNK size=%d", n);

            // Warn if TX ring buffer is getting full
            if ((float)ring_size(&g_tx_ring) /
                (float)RING_BUFFER_SIZE > WARN_AT_FILL_PCT) {
                HUB_LOG("UART", "EVENT=WARN_TX_BUFFER_FULL size=%d", ring_size(&g_tx_ring));
            }

            ring_put(&g_tx_ring, chunk, n);
        }
        // uart_read_bytes already waits, so no extra delay needed
    }
}

// ── Writer task: g_rx_ring → UART TX ────────────────────────
void uart_writer_task(void* arg) {
    uint8_t chunk[UART_READ_CHUNK];
    HUB_LOG("UART", "EVENT=WRITER_START");
    while (true) {
        // Block up to 50 ms waiting for data to deliver
        int n = ring_take(&g_rx_ring, chunk, 0, sizeof(chunk), pdMS_TO_TICKS(50));
        if (n > 0) {
            
            uart_write_bytes(UART_PORT, (const char*)chunk, n);
            //uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(100)); 
            HUB_LOG("UART", "EVENT=WRITE_CHUNK size=%d", n);
        }
    }
}