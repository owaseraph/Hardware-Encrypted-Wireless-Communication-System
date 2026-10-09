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
//  Sender task (Core 1)
//
//  - Reads from g_tx_ring (filled by uart_reader_task).
//  - Maintains a sliding window of unACK'd packets.
//  - Retransmits on timeout.
//  - ACKs arrive as compact 6-byte frames via espnow_recv_ack().
// ============================================================

extern ByteRingBuffer g_tx_ring;

struct WindowSlot {
    Packet   pkt;
    uint8_t  wire[PACKET_SIZE];   // pre-encoded for fast retransmit
    uint32_t sent_at_ms;
    int      attempts;
    bool     used;
};

// WINDOW_SLOTS must be > WINDOW_SIZE so base and nextSeq never
// alias the same slot while both are live.
#define WINDOW_SLOTS  (WINDOW_SIZE * 2)

static WindowSlot s_window[WINDOW_SLOTS];

static inline int seq_distance(int from, int to, int mod) {
    return (to - from + mod) % mod;
}

static inline int window_idx(int seq) {
    return seq % WINDOW_SLOTS;
}

void sender_task(void* arg) {
    uint8_t chunk[PAYLOAD_SIZE];
    int     base    = 0;
    int     nextSeq = 0;
    bool    eof     = false;

    // Local copies of runtime config — refreshed each loop iteration
    // by reading g_config directly (avoids the shared config_has_updated()
    // flag being consumed by the receiver before we see it).
    int payload_size = PAYLOAD_SIZE;
    int window_size  = WINDOW_SIZE;
    int timeout_ms   = TIMEOUT_MS;

    memset(s_window, 0, sizeof(s_window));

    HUB_LOG("TX", "EVENT=START msg=Sender task started");

    while (true) {
        // ── Refresh runtime config ────────────────────────────
        // Read g_config directly instead of config_has_updated() to
        // avoid a race where the receiver clears the flag first.
        // g_config fields are volatile, so this is safe.
        payload_size = g_config.payload_size;
        window_size  = g_config.window_size;
        timeout_ms   = g_config.timeout_ms;

        // ── Fill window ───────────────────────────────────────
        while (seq_distance(base, nextSeq, SEQ_MOD) < window_size) {
            int nread = ring_take(&g_tx_ring, chunk, 0,
                                  payload_size, pdMS_TO_TICKS(1));
            if (nread <= 0) {
                if (!eof) HUB_LOG("TX", "EVENT=EOF msg=End of UART data");
                eof = true;
                break;
            }
            eof = false;

            int slot    = window_idx(nextSeq);
            Packet* p   = &s_window[slot].pkt;
            p->seq      = (int16_t)nextSeq;
            p->flags    = FLAG_DATA;
            p->length   = (int16_t)nread;
            memset(p->payload, 0, PAYLOAD_SIZE);
            memcpy(p->payload, chunk, nread);

            packet_encode(p, s_window[slot].wire);
            s_window[slot].sent_at_ms = (uint32_t)xTaskGetTickCount()
                                        * portTICK_PERIOD_MS;
            s_window[slot].attempts   = 1;
            s_window[slot].used       = true;

            espnow_send(s_window[slot].wire, PACKET_SIZE, (uint16_t)nextSeq);
            HUB_LOG("TX", "EVENT=SEND seq=%d len=%d attempt=1", nextSeq, nread);

            nextSeq = (nextSeq + 1) % SEQ_MOD;
        }

        // ── Receive ACKs ──────────────────────────────────────
        // Poll with a short timeout so retransmit logic stays timely.
        // ACKs are compact 6-byte frames — use compact_ack_decode().
        {
            RawFrame frame;
            if (espnow_recv_ack(&frame, pdMS_TO_TICKS(5))) {
                int16_t ack_seq;
                if (compact_ack_decode(frame.data, frame.len, &ack_seq)) {
                    int ack_seq_i = (int)(uint16_t)ack_seq;
                    int slot      = window_idx(ack_seq_i);

                    if (s_window[slot].used &&
                        (int)(uint16_t)s_window[slot].pkt.seq == ack_seq_i) {

                        s_window[slot].used = false;
                        HUB_LOG("TX", "EVENT=ACK_ACCEPTED seq=%d", ack_seq_i);

                        // Slide base forward over all consecutively ACK'd slots.
                        while (base != nextSeq &&
                               !s_window[window_idx(base)].used) {
                            base = (base + 1) % SEQ_MOD;
                        }
                        HUB_LOG("TX", "EVENT=WINDOW_SLIDE new_base=%d", base);
                    }
                }
                // Silently drop malformed ACK frames — sender will retransmit
                // if the packet times out.
            }
        }

        // ── Retransmit timed-out packets ─────────────────────
        {
            uint32_t now_ms = (uint32_t)xTaskGetTickCount()
                              * portTICK_PERIOD_MS;
            int seq = base;
            while (seq != nextSeq) {
                int slot = window_idx(seq);
                if (s_window[slot].used) {
                    uint32_t age = now_ms - s_window[slot].sent_at_ms;
                    if (age > (uint32_t)timeout_ms) {
                        s_window[slot].attempts++;
                        s_window[slot].sent_at_ms = now_ms;
                        espnow_send(s_window[slot].wire, PACKET_SIZE,
                                    (uint16_t)seq);
                        HUB_LOG("TX", "EVENT=RETRANSMIT seq=%d attempt=%d age=%lu",
                                seq, s_window[slot].attempts,
                                (unsigned long)age);
                    }
                }
                seq = (seq + 1) % SEQ_MOD;
            }
        }

        // ── Yield / sleep ─────────────────────────────────────
        // MUST use vTaskDelay (not taskYIELD) so the IDLE task on
        // Core 1 gets CPU time to reset the watchdog timer.
        // taskYIELD() only yields to tasks of equal or higher priority,
        // which never includes IDLE (priority 0), causing WDT crashes.
        if (eof && base == nextSeq)
            vTaskDelay(pdMS_TO_TICKS(20));   // idle: long sleep
        else
            vTaskDelay(pdMS_TO_TICKS(1));    // active: brief yield, feeds WDT
    }
}
