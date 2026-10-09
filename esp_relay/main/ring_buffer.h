#pragma once
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "config.h"

// ============================================================
//  ByteRingBuffer — thread-safe, blocking put/get
//
//  Uses a mutex for mutual exclusion and two binary-style
//  counting semaphores for flow control. The key fix vs the
//  previous version: semaphore max-count is set to a small
//  sentinel (1) and we use the mutex + rb->size for the actual
//  available/free accounting. This avoids creating counting
//  semaphores with max-count = 1,048,576 which is legal but
//  causes unpredictable behaviour on ESP-IDF under load.
//
//  put()  — blocks if full, writes as many bytes as possible
//            in one lock acquisition, loops until all written
//  take() — blocks up to timeout_ticks for at least 1 byte,
//            returns however many are available up to `length`
// ============================================================

struct ByteRingBuffer {
    uint8_t*          buf;
    volatile int      head;       // write index
    volatile int      tail;       // read index
    volatile int      size;       // bytes currently stored
    int               capacity;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t not_empty;  // given when data is added
    SemaphoreHandle_t not_full;   // given when data is removed
};

static inline void ring_init(ByteRingBuffer* rb, uint8_t* storage, int capacity) {
    rb->buf       = storage;
    rb->head      = 0;
    rb->tail      = 0;
    rb->size      = 0;
    rb->capacity  = capacity;
    rb->mutex     = xSemaphoreCreateMutex();
    // Binary-style: max count 1, initial count reflects state.
    // not_empty starts at 0 (nothing to read yet).
    // not_full  starts at 1 (space available).
    rb->not_empty = xSemaphoreCreateCounting(1, 0);
    rb->not_full  = xSemaphoreCreateCounting(1, 1);
}

// ── put: write `length` bytes from src, blocking if full ─────
static inline void ring_put(ByteRingBuffer* rb, const uint8_t* src, int length) {
    int offset = 0;
    while (offset < length) {
        // Wait until there is at least 1 byte of space.
        xSemaphoreTake(rb->not_full, portMAX_DELAY);

        xSemaphoreTake(rb->mutex, portMAX_DELAY);

        // Write as many bytes as fit right now.
        int space = rb->capacity - rb->size;
        int chunk = (length - offset < space) ? (length - offset) : space;

        for (int i = 0; i < chunk; i++) {
            rb->buf[rb->head] = src[offset + i];
            rb->head = (rb->head + 1) % rb->capacity;
        }
        rb->size += chunk;
        offset   += chunk;

        // If still space left, re-signal not_full so next put
        // (or the remainder of this one) can proceed.
        int still_free = rb->capacity - rb->size;

        xSemaphoreGive(rb->mutex);

        // Signal readers that data is available.
        xSemaphoreGive(rb->not_empty);

        // If buffer is not yet full, keep not_full signalled.
        if (still_free > 0)
            xSemaphoreGive(rb->not_full);
    }
}

// ── take: read up to `length` bytes into dest[offset..] ──────
// Blocks up to timeout_ticks for at least 1 byte.
// Returns number of bytes actually read (>= 1 on success, 0 on timeout).
static inline int ring_take(ByteRingBuffer* rb, uint8_t* dest, int offset,
                            int length, TickType_t timeout_ticks) {
    // Wait until at least 1 byte is available.
    if (xSemaphoreTake(rb->not_empty, timeout_ticks) != pdTRUE)
        return 0;

    xSemaphoreTake(rb->mutex, portMAX_DELAY);

    int available = rb->size;
    int count     = (length < available) ? length : available;

    for (int i = 0; i < count; i++) {
        dest[offset + i] = rb->buf[rb->tail];
        rb->tail = (rb->tail + 1) % rb->capacity;
    }
    rb->size -= count;

    int remaining = rb->size;

    xSemaphoreGive(rb->mutex);

    // Signal writers that space is available.
    xSemaphoreGive(rb->not_full);

    // If more data still in buffer, keep not_empty signalled
    // so the next reader doesn't block unnecessarily.
    if (remaining > 0)
        xSemaphoreGive(rb->not_empty);

    return count;
}

// ── Approximate fill level (no lock) ─────────────────────────
static inline int ring_size(const ByteRingBuffer* rb) {
    return rb->size;
}