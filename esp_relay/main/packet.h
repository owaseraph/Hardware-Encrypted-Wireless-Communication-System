#pragma once
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "config.h"

// ============================================================
//  Wire layout (identical to Java version):
//
//  [START 1B][SEQ 2B][FLAGS 1B][LENGTH 2B][PAYLOAD 1024B][CRC 2B]
//  Total = PACKET_SIZE bytes
// ============================================================

#define PKT_START   0x7E
#define FLAG_DATA   0x00
#define FLAG_ACK    0x01

struct Packet {
    int16_t  seq;               // sequence number
    uint8_t  flags;             // FLAG_DATA or FLAG_ACK
    int16_t  length;            // valid bytes in payload
    uint8_t  payload[PAYLOAD_SIZE];
};

// ── CRC-16/CCITT (0x1021 polynomial, init 0xFFFF) ──────────
static inline uint16_t crc16(const uint8_t* data, int offset, int length) {
    uint32_t crc = 0xFFFF;
    for (int i = offset; i < length; i++) {
        crc ^= ((uint32_t)data[i] << 8);
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return (uint16_t)(crc & 0xFFFF);
}

// ── Encode packet → wire bytes (big-endian) ─────────────────
// buf must be at least PACKET_SIZE bytes.
static inline void packet_encode(const Packet* p, uint8_t* buf) {
    int pos = 0;
    buf[pos++] = PKT_START;

    buf[pos++] = (uint8_t)((p->seq >> 8) & 0xFF);
    buf[pos++] = (uint8_t)( p->seq       & 0xFF);

    buf[pos++] = p->flags;

    buf[pos++] = (uint8_t)((p->length >> 8) & 0xFF);
    buf[pos++] = (uint8_t)( p->length       & 0xFF);

    memcpy(buf + pos, p->payload, PAYLOAD_SIZE);
    pos += PAYLOAD_SIZE;

    uint16_t crc = crc16(buf, 0, pos);
    buf[pos++] = (uint8_t)((crc >> 8) & 0xFF);
    buf[pos++] = (uint8_t)( crc       & 0xFF);
    // pos == PACKET_SIZE
}

// ── Decode wire bytes → packet ───────────────────────────────
// Returns true on success, false on bad start byte / CRC / length.
static inline bool packet_decode(const uint8_t* buf, int bufLen, Packet* out) {
    if (bufLen < PACKET_SIZE) return false;
    if (buf[0] != PKT_START)  return false;

    uint16_t computed = crc16(buf, 0, PACKET_SIZE - 2);
    uint16_t received = ((uint16_t)buf[PACKET_SIZE - 2] << 8)
                      |  (uint16_t)buf[PACKET_SIZE - 1];
    if (computed != received) return false;

    int pos = 1;
    out->seq    = (int16_t)(((uint16_t)buf[pos] << 8) | buf[pos+1]); pos += 2;
    out->flags  = buf[pos++];
    out->length = (int16_t)(((uint16_t)buf[pos] << 8) | buf[pos+1]); pos += 2;
    memcpy(out->payload, buf + pos, PAYLOAD_SIZE);

    if (out->length < 0 || out->length > PAYLOAD_SIZE) return false;
    return true;
}

// ── Build an ACK packet ──────────────────────────────────────
static inline void make_ack(int16_t seq, Packet* out) {
    memset(out, 0, sizeof(Packet));
    out->seq   = seq;
    out->flags = FLAG_ACK;
    out->length = 0;
}

// ============================================================
//  Compact ACK wire format — 6 bytes total, fits in ONE ESP-NOW
//  frame (no fragmentation, no reassembly, no corruption risk).
//
//  [START 1B][SEQ_HI 1B][SEQ_LO 1B][FLAG_ACK 1B][CRC_HI 1B][CRC_LO 1B]
// ============================================================
#define COMPACT_ACK_SIZE  6

// Encode a compact ACK into buf (must be >= COMPACT_ACK_SIZE bytes).
static inline void compact_ack_encode(int16_t seq, uint8_t* buf) {
    buf[0] = PKT_START;
    buf[1] = (uint8_t)((seq >> 8) & 0xFF);
    buf[2] = (uint8_t)( seq       & 0xFF);
    buf[3] = FLAG_ACK;
    uint16_t crc = crc16(buf, 0, 4);
    buf[4] = (uint8_t)(crc >> 8);
    buf[5] = (uint8_t)(crc & 0xFF);
}

// Decode a compact ACK. Returns true on success.
// Distinguishable from a full data packet by its length (6 vs PACKET_SIZE).
static inline bool compact_ack_decode(const uint8_t* buf, int len, int16_t* seq_out) {
    if (len != COMPACT_ACK_SIZE)    return false;
    if (buf[0] != PKT_START)        return false;
    if (buf[3] != FLAG_ACK)         return false;
    uint16_t computed = crc16(buf, 0, 4);
    uint16_t received = ((uint16_t)buf[4] << 8) | buf[5];
    if (computed != received)       return false;
    *seq_out = (int16_t)(((uint16_t)buf[1] << 8) | buf[2]);
    return true;
}
