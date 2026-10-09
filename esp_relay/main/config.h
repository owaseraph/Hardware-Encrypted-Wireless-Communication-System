#pragma once
#include <stdint.h>

// ── Queue handles — defined in main.cpp, used across all headers ──
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
struct RawFrame;
extern QueueHandle_t g_log_q;
extern QueueHandle_t g_rx_data_q;
extern QueueHandle_t g_rx_ack_q;


// ============================================================
//  PEER MAC — set this to the OTHER ESP's MAC address
//  You can find it in the boot log: "wifi:mode : sta (xx:xx:xx:xx:xx:xx)"
// ============================================================
#define PEER_MAC { 0xE0, 0x72, 0xA1, 0xD4, 0xE5, 0x94 }

// ============================================================
//  WiFi AP (for ControlPanel)
// ============================================================
#define AP_SSID     "ESP_RELAY_E564"
#define AP_PASS     "12345678"
#define AP_CHANNEL  1          // must match ESP-NOW channel

// ============================================================
//  Control Hub UDP
// ============================================================
#define ENABLE_CONTROL_HUB true
#define CONTROL_PORT        8000
#define CONTROL_PANEL_PORT  8001   // ControlPanel listens here
#define MAX_LOG_MSG         256

// ============================================================
//  Protocol
// ============================================================
#define PAYLOAD_SIZE        240
#define WINDOW_SIZE         16
#define TIMEOUT_MS          80
#define SEQ_MOD             32768   // Short.MAX_VALUE + 1

// Packet wire size: 1+2+1+2+PAYLOAD_SIZE+2
#define PACKET_SIZE         (1 + 2 + 1 + 2 + PAYLOAD_SIZE + 2)

// ============================================================
//  Ring buffer
// ============================================================
#define RING_BUFFER_SIZE    (1024 * 1024)   // 1 MB
#define WARN_AT_FILL_PCT    0.5f

// ============================================================
//  UART (FPGA data)
//  UART2 = pins 16 (RX) / 17 (TX) on most ESP32-S3 DevKits
// ============================================================
#define UART_PORT           UART_NUM_2
#define UART_RX_PIN         16
#define UART_TX_PIN         17
#define UART_BAUD           115200
#define UART_READ_CHUNK     64      // bytes read per UART task tick

// ============================================================
//  FreeRTOS task / core assignment
//
//  Core 0 → WiFi stack, ESP-NOW callbacks, ControlHub UDP
//  Core 1 → UART reader/writer, Sender, Receiver
//
//  ALL task stacks are allocated from PSRAM via create_task_psram()
//  in main.cpp — internal RAM is too small to hold them.
//  Sizes are in BYTES.
// ============================================================
#define CORE_COMMS          0
#define CORE_DATA           1

#define STACK_UART_READER   4096
#define STACK_SENDER        16384
#define STACK_RECEIVER      16384
#define STACK_CONTROL_HUB   8192

#define PRIO_UART_READER    5
#define PRIO_SENDER         4
#define PRIO_RECEIVER       4
#define PRIO_CONTROL_HUB    2

// ============================================================
//  Debug
// ============================================================
#define DEBUG               true
