#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "config.h"
#include "ring_buffer.h"
#include "control_hub.h"
#include "espnow_transport.h"
#include "uart_tasks.h"
#include "sender_task.h"
#include "receiver_task.h"

ByteRingBuffer g_tx_ring;
ByteRingBuffer g_rx_ring;

// ── PSRAM task creation helper ────────────────────────────────
// The TCB must stay in fast internal RAM (context-switch path).
// The stack can live in PSRAM — acceptable latency for task data.
// stack_bytes is the stack size in BYTES (converted to words internally).
static void create_task_psram(TaskFunction_t func,
                              const char*    name,
                              uint32_t       stack_bytes,
                              UBaseType_t    priority,
                              BaseType_t     core) {
    StaticTask_t* tcb = (StaticTask_t*)heap_caps_malloc(
        sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    StackType_t* stk = (StackType_t*)heap_caps_malloc(
        stack_bytes, MALLOC_CAP_SPIRAM);

    if (!tcb || !stk) {
        printf("[MAIN] FATAL: failed to allocate stack for task '%s' "
               "(requested %lu bytes from PSRAM)\n",
               name, (unsigned long)stack_bytes);
        printf("[MAIN] Free internal: %lu  Free PSRAM: %lu\n",
               (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
               (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    TaskHandle_t handle = xTaskCreateStaticPinnedToCore(
        func, name,
        stack_bytes / sizeof(StackType_t),  // FreeRTOS wants words, not bytes
        nullptr,
        priority,
        stk, tcb,
        core);

    if (!handle) {
        printf("[MAIN] FATAL: xTaskCreateStaticPinnedToCore failed for '%s'\n", name);
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    printf("[MAIN] Task '%s' created — PSRAM stack %lu B, core %d\n",
           name, (unsigned long)stack_bytes, (int)core);
}

static void wifi_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    wifi_config_t ap_cfg = {};
    strncpy((char*)ap_cfg.ap.ssid,     AP_SSID, sizeof(ap_cfg.ap.ssid));
    strncpy((char*)ap_cfg.ap.password, AP_PASS,  sizeof(ap_cfg.ap.password));
    ap_cfg.ap.ssid_len       = strlen(AP_SSID);
    ap_cfg.ap.channel        = AP_CHANNEL;
    ap_cfg.ap.authmode       = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.max_connection = 4;
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);

    wifi_config_t sta_cfg = {};
    esp_wifi_set_config(WIFI_IF_STA, &sta_cfg);

    esp_wifi_start();
    esp_wifi_set_channel(AP_CHANNEL, WIFI_SECOND_CHAN_NONE);

    printf("[MAIN] WiFi started — AP SSID: %s  CH: %d\n", AP_SSID, AP_CHANNEL);
}

extern "C" void app_main() {

    // ── 1. Queues — must exist before any task that uses them ────
    g_log_q     = xQueueCreate(LOG_QUEUE_DEPTH, MAX_LOG_MSG);
    g_rx_data_q = xQueueCreate(QUEUE_DEPTH, sizeof(RawFrame));
    g_rx_ack_q  = xQueueCreate(QUEUE_DEPTH, sizeof(RawFrame));

    if (!g_log_q || !g_rx_data_q || !g_rx_ack_q) {
        printf("[MAIN] FATAL: queue creation failed\n");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // ── 2. Ring buffers in PSRAM ──────────────────────────────────
    uint8_t* tx_buf = (uint8_t*)heap_caps_malloc(RING_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    uint8_t* rx_buf = (uint8_t*)heap_caps_malloc(RING_BUFFER_SIZE, MALLOC_CAP_SPIRAM);

    if (!tx_buf || !rx_buf) {
        printf("[MAIN] FATAL: failed to allocate ring buffers in PSRAM\n");
        printf("[MAIN] Free PSRAM: %lu bytes\n",
               (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ring_init(&g_tx_ring, tx_buf, RING_BUFFER_SIZE);
    ring_init(&g_rx_ring, rx_buf, RING_BUFFER_SIZE);
    printf("[MAIN] Ring buffers in PSRAM: %d KB each\n", RING_BUFFER_SIZE / 1024);

    // ── 3. WiFi ────────────────────────────────────────────────────
    wifi_init();

    // ── 4. ESP-NOW (must come after WiFi) ─────────────────────────
    espnow_transport_init();

    // ── 5. UART driver ─────────────────────────────────────────────
    uart_init();

    // ── 6. Tasks — all stacks allocated from PSRAM ────────────────
    // control_hub runs on Core 0 alongside the WiFi stack.
    // All data tasks run on Core 1.
    #if ENABLE_CONTROL_HUB
        create_task_psram(control_hub_task, "ctrl_hub",
                        STACK_CONTROL_HUB, PRIO_CONTROL_HUB, CORE_COMMS);
    #endif
    create_task_psram(uart_reader_task, "uart_rd",
                      STACK_UART_READER, PRIO_UART_READER, CORE_DATA);

    create_task_psram(uart_writer_task, "uart_wr",
                      STACK_UART_READER, PRIO_UART_READER, CORE_DATA);

    create_task_psram(sender_task,   "sender",
                      STACK_SENDER,   PRIO_SENDER,   CORE_DATA);

    create_task_psram(receiver_task, "receiver",
                      STACK_RECEIVER, PRIO_RECEIVER, CORE_DATA);

    printf("[MAIN] All tasks launched.\n");
}
