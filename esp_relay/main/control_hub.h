#pragma once
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "config.h"

// ── Shared runtime config ─────────────────────────────────────
struct TransmissionConfig {
    volatile int payload_size;
    volatile int window_size;
    volatile int timeout_ms;
};

static TransmissionConfig g_config = {
    .payload_size = PAYLOAD_SIZE,
    .window_size  = WINDOW_SIZE,
    .timeout_ms   = TIMEOUT_MS,
};

// ── Log queue — created in app_main before any task starts ────
#define LOG_QUEUE_DEPTH  128
QueueHandle_t g_log_q = nullptr;

// Called from any task context — non-blocking, drops if full
static inline void hub_log(const char* tag, const char* msg) {
    char line[MAX_LOG_MSG];
    snprintf(line, sizeof(line), "[%s] %s\n", tag, msg);
#if DEBUG
    printf("%s", line);
#endif
    if (g_log_q) xQueueSend(g_log_q, line, 0);
}

#define HUB_LOG(tag, fmt, ...) \
    do { \
        char _buf[MAX_LOG_MSG]; \
        snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
        hub_log(tag, _buf); \
    } while(0)

// ── ControlHub task (Core 0) ──────────────────────────────────
// Spawned by create_task_psram() in main.cpp — not static so
// main.cpp can take its address directly.
void control_hub_task(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        printf("[HUB] Failed to create socket\n");
        vTaskDelete(nullptr);
        return;
    }

    int bcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast));

    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_port        = htons(CONTROL_PORT);
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind(sock, (struct sockaddr*)&bind_addr, sizeof(bind_addr));

    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);

    struct sockaddr_in dest = {};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(CONTROL_PANEL_PORT);
    inet_pton(AF_INET, "192.168.4.255", &dest.sin_addr);

    char line[MAX_LOG_MSG];
    char rx_buf[256];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);

    HUB_LOG("HUB", "EVENT=START msg=ControlHub task started");

    while (true) {
        // Drain log queue → UDP broadcast
        while (xQueueReceive(g_log_q, line, 0) == pdTRUE) {
            sendto(sock, line, strlen(line), 0,
                   (struct sockaddr*)&dest, sizeof(dest));
        }

        // Handle incoming SET commands
        int n = recvfrom(sock, rx_buf, sizeof(rx_buf) - 1, 0,
                         (struct sockaddr*)&from, &from_len);
        if (n > 0) {
            rx_buf[n] = '\0';
            if (strncmp(rx_buf, "client=", 7) == 0) {
                const char* ok = "LOGIN OK\n";
                sendto(sock, ok, strlen(ok), 0,
                       (struct sockaddr*)&from, sizeof(from));
            } else if (strncmp(rx_buf, "SET ", 4) == 0) {
                char key[64] = {}, val[64] = {};
                sscanf(rx_buf + 4, "%63s %63s", key, val);
                int v = atoi(val);
                if      (strcmp(key, "PAYLOAD_SIZE") == 0) g_config.payload_size = v;
                else if (strcmp(key, "WINDOW_SIZE")  == 0) g_config.window_size  = v;
                else if (strcmp(key, "TIMEOUT_MS")   == 0) g_config.timeout_ms   = v;
                HUB_LOG("SYS", "EVENT=CONFIG_UPDATE key=%s val=%d", key, v);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
