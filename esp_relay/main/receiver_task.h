#pragma once
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "config.h"
#include "packet.h"
#include "ring_buffer.h"
#include "espnow_transport.h"
#include "control_hub.h"

// ============================================================
//  Receiver task (Core 1)
//
//  - Pops raw frames from g_rx_data_q (filled by ESP-NOW RX cb).
//  - Re-orders out-of-order packets with a fixed OOO buffer.
//  - Delivers in-order data to g_rx_ring (→ uart_writer_task).
//  - Sends compact ACKs (6 bytes, 1 ESP-NOW frame) back to sender.
//  - Duplicate detection via a sliding seen-set.
// ============================================================

extern ByteRingBuffer g_rx_ring;

// ── Out-of-order buffer ───────────────────────────────────────
// One slot per window position, indexed by (seq % OOO_SLOTS).
#define OOO_SLOTS  (WINDOW_SIZE * 4)

struct OooSlot {
    uint8_t  data[PAYLOAD_SIZE];
    int16_t  length;
    int      seq;    // -1 = empty
};

static OooSlot s_ooo[OOO_SLOTS];

// ── Duplicate detection ───────────────────────────────────────
// Linear scan over a small set — window size keeps this cheap.
#define DUP_SEEN_MAX  (WINDOW_SIZE * 4)
static int s_seen[DUP_SEEN_MAX];
static int s_seen_count = 0;

static inline bool seen_contains(int seq) {
    for (int i = 0; i < s_seen_count; i++)
        if (s_seen[i] == seq) return true;
    return false;
}

static inline void seen_add(int seq) {
    if (s_seen_count < DUP_SEEN_MAX)
        s_seen[s_seen_count++] = seq;
}

static inline void seen_prune() {
    s_seen_count = 0;
}

// ── Deliver in-order data to output ring ─────────────────────
static inline void deliver(int seq, const uint8_t* data, int len) {
    HUB_LOG("RX", "EVENT=DELIVER seq=%d bytes=%d", seq, len);
    ring_put(&g_rx_ring, data, len);
}

// ── Send compact ACK back over ESP-NOW ───────────────────────
// Uses the 6-byte compact format so it fits in exactly ONE ESP-NOW
// frame. This avoids the fragmentation + reassembly path entirely,
// which was the root cause of ACKs being silently lost when
// fragments arrived out-of-order.
static inline void send_ack(int seq) {
    uint8_t wire[COMPACT_ACK_SIZE];
    compact_ack_encode((int16_t)seq, wire);
    // Pass frag_id = seq. espnow_send will send exactly 1 frame.
    espnow_send(wire, COMPACT_ACK_SIZE, (uint16_t)seq);
    HUB_LOG("RX", "EVENT=ACK_SENT seq=%d", seq);
}

void receiver_task(void* arg) {
    int expected_seq = 0;

    int window_size = WINDOW_SIZE;
    int timeout_ms  = TIMEOUT_MS;  // kept for symmetry with sender

    // Init OOO buffer
    for (int i = 0; i < OOO_SLOTS; i++) s_ooo[i].seq = -1;

    HUB_LOG("RX", "EVENT=START msg=Receiver task started");

    while (true) {
        // ── Runtime config update ─────────────────────────────
        // Read g_config directly every loop instead of using
        // config_has_updated(), which clears a shared flag and
        // causes the sender to miss updates (or vice versa).
        window_size = g_config.window_size;
        timeout_ms  = g_config.timeout_ms;

        // ── Prune duplicate history if full ──────────────────
        if (s_seen_count >= DUP_SEEN_MAX - 1) {
            HUB_LOG("RX", "EVENT=HISTORY_CLEAN size=%d", s_seen_count);
            seen_prune();
        }

        // ── Wait for next data frame ──────────────────────────
        RawFrame frame;
        if (!espnow_recv_data(&frame, pdMS_TO_TICKS(100)))
            continue;

        Packet p;
        if (!packet_decode(frame.data, frame.len, &p)) {
            HUB_LOG("RX", "EVENT=CORRUPTED_PACKET");
            continue;
        }

        int seq = (int)(uint16_t)p.seq;
        HUB_LOG("RX", "EVENT=PKT_RX seq=%d len=%d expected=%d", seq, p.length, expected_seq);

        // ── Duplicate check ───────────────────────────────────
        if (seen_contains(seq)) {
            HUB_LOG("RX", "EVENT=PKT_DUP seq=%d", seq);
            send_ack(seq);   // ACK anyway so sender can slide its window
            continue;
        }
        seen_add(seq);

        const uint8_t* valid_data = p.payload;
        int            valid_len  = p.length;

        if (seq == expected_seq) {
            // ── In-order ──────────────────────────────────────
            HUB_LOG("RX", "EVENT=PKT_IN_ORDER seq=%d", seq);
            deliver(seq, valid_data, valid_len);
            expected_seq = (expected_seq + 1) % SEQ_MOD;

            // Flush any now-in-order buffered OOO packets.
            while (true) {
                int slot = expected_seq % OOO_SLOTS;
                if (s_ooo[slot].seq != expected_seq) break;
                HUB_LOG("RX", "EVENT=BUFFER_RELEASE seq=%d", expected_seq);
                deliver(expected_seq, s_ooo[slot].data, s_ooo[slot].length);
                s_ooo[slot].seq = -1;
                expected_seq = (expected_seq + 1) % SEQ_MOD;
            }

            HUB_LOG("RX", "EVENT=EXPECTED_ADVANCE new_expected=%d", expected_seq);

        } else {
            // ── Out-of-order ──────────────────────────────────
            int dist = (seq - expected_seq + SEQ_MOD) % SEQ_MOD;
            HUB_LOG("RX", "EVENT=PKT_OOO seq=%d expected=%d dist=%d", seq, expected_seq, dist);

            if (dist < window_size) {
                int slot = seq % OOO_SLOTS;
                if (s_ooo[slot].seq < 0) {
                    memcpy(s_ooo[slot].data, valid_data, valid_len);
                    s_ooo[slot].length = (int16_t)valid_len;
                    s_ooo[slot].seq    = seq;
                    HUB_LOG("RX", "EVENT=BUFFER_ADD seq=%d", seq);
                }
            } else {
                HUB_LOG("RX", "EVENT=PKT_TOO_FAR seq=%d dist=%d", seq, dist);
            }
        }

        // ACK every packet (in-order and OOO).
        send_ack(seq);
    }
}
