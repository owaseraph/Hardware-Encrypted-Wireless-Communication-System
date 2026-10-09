#include <WiFi.h>
#include <esp_now.h>

#define RXD2 16
#define TXD2 17
#define BLOCK_SIZE 16
#define BLOCK_TIMEOUT_MS 100

uint8_t buffer[BLOCK_SIZE];
uint8_t peer[] = {0x3C, 0x0F, 0x02, 0xCF, 0x53, 0x74};

void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "[TX] Block delivered to client" : "[TX] Delivery failed");
}

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    Serial.print("[RX] From: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", info->src_addr[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.println();

    if (len != BLOCK_SIZE) {
        Serial.printf("[RX] Unexpected block size: %d, ignoring\n", len);
        return;
    }

    Serial.print("[RX] Encrypted block from client: ");
    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (data[i] < 16) Serial.print("0");
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();

    Serial2.write(data, BLOCK_SIZE);
    Serial.println("[RX] Forwarded to FPGA via UART");
}

void setup() {
    Serial.begin(115200);
    Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    Serial.print("[INFO] Server MAC: ");
    Serial.println(WiFi.macAddress());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ERROR] ESP-NOW init failed, halting");
        while (true);
    }

    esp_now_register_send_cb(onSent);
    esp_now_register_recv_cb(onReceive);

    esp_now_peer_info_t p = {};
    memcpy(p.peer_addr, peer, 6);
    p.channel = 0;
    p.encrypt = false;

    if (esp_now_add_peer(&p) != ESP_OK) {
        Serial.println("[ERROR] Failed to add peer, halting");
        while (true);
    }

    Serial.print("[INFO] Peer added: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", peer[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.println();
    Serial.println("[INFO] Ready, waiting for UART blocks...");
}

void loop() {
    static int block_index = 0;
    static unsigned long lastByteTime = 0;

    while (Serial2.available() && block_index < BLOCK_SIZE) {
        buffer[block_index++] = Serial2.read();
        lastByteTime = millis();
    }

    if (block_index > 0 && block_index < BLOCK_SIZE && millis() - lastByteTime > BLOCK_TIMEOUT_MS) {
        Serial.printf("[WARN] Block timeout with %d/%d bytes, resetting buffer\n", block_index, BLOCK_SIZE);
        block_index = 0;
    }

    if (block_index == BLOCK_SIZE) {
        Serial.print("[TX] Sending block to client: ");
        for (int i = 0; i < BLOCK_SIZE; i++) {
            if (buffer[i] < 16) Serial.print("0");
            Serial.print(buffer[i], HEX);
            Serial.print(" ");
        }
        Serial.println();

        esp_err_t result = esp_now_send(peer, buffer, BLOCK_SIZE);
        if (result != ESP_OK)
            Serial.printf("[TX] esp_now_send error: %d\n", result);

        block_index = 0;
    }
}