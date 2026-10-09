#pragma once
#include <string.h>
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "config.h"
#include "packet.h"
#include "control_hub.h"

// ============================================================
//  ESP-NOW transport
//
//  ESP-NOW callbacks run on Core 0 (WiFi task). They push raw
//  frames into two FreeRTOS queues:
//
//    g_rx_data_q   — full Packet wire-frames arriving from peer
//    g_rx_ack_q    — compact ACK frames (COMPACT_ACK_SIZE bytes)
//
//  Sender/Receiver tasks on Core 1 call:
//    espnow_send()       — sends wire_buf via fragmented ESP-NOW
//    espnow_recv_data()  — blocking pop from data queue
//    espnow_recv_ack()   — blocking pop from ack queue
// ============================================================

// Maximum ESP-NOW payload is 250 bytes; we fragment larger packets.
#define ESPNOW_MAX_FRAME  250

// Per-chunk header: [FRAG_ID 2B][FRAG_TOTAL 1B][FRAG_IDX 1B][DATA ...]
#define FRAG_HDR_SIZE  4

// ── RawFrame ─────────────────────────────────────────────────
// Holds one fully reassembled wire buffer.
// Sized for the larger of a full data packet and a compact ACK.
struct RawFrame {
    uint8_t data[PACKET_SIZE];   // PACKET_SIZE >= COMPACT_ACK_SIZE
    int     len;
};

// ── Queue handles (defined here, extern'd in config.h) ───────
#define QUEUE_DEPTH  16
QueueHandle_t g_rx_data_q = nullptr;
QueueHandle_t g_rx_ack_q  = nullptr;

// ── Reassembly state for DATA packets only ───────────────────
// ACKs are compact (6 bytes, 1 ESP-NOW frame) and need no reassembly.
// Keeping a single reassembly buffer is safe because ESP-NOW is
// serialised — only one frame arrives at a time in the callback.
static uint8_t  s_reassembly_buf[PACKET_SIZE];
static int      s_reassembly_len     = 0;
static uint16_t s_reassembly_frag_id = 0xFFFF;
static uint8_t  s_frag_total         = 0;
static uint8_t  s_frags_received     = 0;

// Peer MAC (from config.h PEER_MAC)
static uint8_t g_peer_mac[6] = PEER_MAC;

// ── ISR-safe log helper ───────────────────────────────────────
// HUB_LOG calls xQueueSend which is NOT safe from an ISR/WiFi-task
// context. Use this instead for callbacks.
static inline void espnow_isr_log(const char* msg) {
#if DEBUG
    // printf is safe from the WiFi task (not a hard ISR, but runs at
    // elevated priority). xQueueSendFromISR is safe either way.
    printf("%s\n", msg);
#endif
    if (g_log_q) {
        char line[MAX_LOG_MSG];
        int n = snprintf(line, sizeof(line), "[ESPNOW] %s\n", msg);
        (void)n;
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(g_log_q, line, &woken);
        // No portYIELD_FROM_ISR — we are in the WiFi task, not a real ISR.
    }
}

// ── TX done callback (WiFi task, Core 0) ─────────────────────
static void espnow_tx_cb(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS)
        espnow_isr_log("EVENT=TX_FAIL");
}

// ── RX callback (WiFi task, Core 0) ──────────────────────────
static void espnow_rx_cb(const esp_now_recv_info_t* info,
                         const uint8_t* data, int data_len) {
    if (data_len < FRAG_HDR_SIZE) return;

    uint16_t frag_id    = ((uint16_t)data[0] << 8) | data[1];
    uint8_t  frag_total = data[2];
    uint8_t  frag_idx   = data[3];
    const uint8_t* payload  = data + FRAG_HDR_SIZE;
    int            payload_len = data_len - FRAG_HDR_SIZE;

    // ── Compact ACK fast path (1 fragment, COMPACT_ACK_SIZE bytes) ──
    // Compact ACKs always arrive as a single ESP-NOW frame
    // (frag_total == 1, payload == COMPACT_ACK_SIZE bytes).
    // We detect them BEFORE touching the data reassembly state so the
    // two directions never interfere with each other.
    if (frag_total == 1 && payload_len == COMPACT_ACK_SIZE
            && payload[0] == PKT_START && payload[3] == FLAG_ACK) {
        int16_t ack_seq;
        if (compact_ack_decode(payload, payload_len, &ack_seq)) {
            RawFrame frame;
            memcpy(frame.data, payload, COMPACT_ACK_SIZE);
            frame.len = COMPACT_ACK_SIZE;
            BaseType_t woken = pdFALSE;
            xQueueSendFromISR(g_rx_ack_q, &frame, &woken);
            return;
        }
        // If decode fails (CRC bad) silently drop — sender will retransmit.
        return;
    }

    // ── Data packet reassembly ────────────────────────────────
    // New logical packet arriving — reset reassembly state.
    if (frag_id != s_reassembly_frag_id) {
        s_reassembly_frag_id = frag_id;
        s_frag_total         = frag_total;
        s_frags_received     = 0;
        s_reassembly_len     = 0;
        memset(s_reassembly_buf, 0, sizeof(s_reassembly_buf));
    }

    // Write fragment at its correct position in the reassembly buffer.
    int write_offset = frag_idx * (ESPNOW_MAX_FRAME - FRAG_HDR_SIZE);
    if (write_offset + payload_len > PACKET_SIZE) return;  // sanity

    memcpy(s_reassembly_buf + write_offset, payload, payload_len);
    s_reassembly_len += payload_len;
    s_frags_received++;

    if (s_frags_received < s_frag_total) return;  // still waiting for more

    // All fragments received — push to data queue.
    RawFrame frame;
    memcpy(frame.data, s_reassembly_buf, s_reassembly_len);
    frame.len = s_reassembly_len;
    s_frags_received = 0;  // ready for next packet

    // Sanity-check the flags byte at wire offset 3 (should be FLAG_DATA here,
    // since ACKs are handled by the fast path above, but guard anyway).
    uint8_t flags_byte = (s_reassembly_len > 3) ? s_reassembly_buf[3] : 0xFF;
    if (flags_byte == FLAG_ACK) {
        // Should not reach here with the fast path above, but handle safely.
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(g_rx_ack_q, &frame, &woken);
    } else {
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(g_rx_data_q, &frame, &woken);
    }
}

// ── Init (call once from app_main, after WiFi is started) ────
static inline esp_err_t espnow_transport_init() {
    esp_now_init();
    esp_now_register_send_cb(espnow_tx_cb);
    esp_now_register_recv_cb(espnow_rx_cb);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, g_peer_mac, 6);
    peer.channel = AP_CHANNEL;
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    HUB_LOG("ESPNOW", "EVENT=INIT peer_mac=%02X:%02X:%02X:%02X:%02X:%02X",
            g_peer_mac[0], g_peer_mac[1], g_peer_mac[2],
            g_peer_mac[3], g_peer_mac[4], g_peer_mac[5]);
    return ESP_OK;
}

// ── Send a wire buffer via ESP-NOW (fragmented if needed) ────
// frag_id should be unique per logical packet (use the packet seq).
// For compact ACKs (COMPACT_ACK_SIZE bytes) this sends exactly 1 frame.
static inline void espnow_send(const uint8_t* wire_buf, int wire_len, uint16_t frag_id) {
    int data_per_frag = ESPNOW_MAX_FRAME - FRAG_HDR_SIZE;  // 246 bytes
    int total_frags   = (wire_len + data_per_frag - 1) / data_per_frag;

    for (int i = 0; i < total_frags; i++) {
        uint8_t frame[ESPNOW_MAX_FRAME];
        frame[0] = (uint8_t)(frag_id >> 8);
        frame[1] = (uint8_t)(frag_id & 0xFF);
        frame[2] = (uint8_t)total_frags;
        frame[3] = (uint8_t)i;

        int offset     = i * data_per_frag;
        int chunk_size = wire_len - offset;
        if (chunk_size > data_per_frag) chunk_size = data_per_frag;

        memcpy(frame + FRAG_HDR_SIZE, wire_buf + offset, chunk_size);
        esp_now_send(g_peer_mac, frame, FRAG_HDR_SIZE + chunk_size);

        // Yield between fragments so the WiFi task can breathe.
        // Skip the delay after the last fragment — no benefit there.
        if (i < total_frags - 1)
            vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ── Blocking receive helpers ──────────────────────────────────
static inline bool espnow_recv_data(RawFrame* out, TickType_t timeout) {
    return xQueueReceive(g_rx_data_q, out, timeout) == pdTRUE;
}

static inline bool espnow_recv_ack(RawFrame* out, TickType_t timeout) {
    return xQueueReceive(g_rx_ack_q, out, timeout) == pdTRUE;
}
