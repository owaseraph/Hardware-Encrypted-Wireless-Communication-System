#include <WiFi.h>
#include <esp_now.h>

#define RXD2 16
#define TXD2 17
#define BLOCK_SIZE 16

// Replace with your SERVER ESP32 MAC address:
const uint8_t SERVER_MAC[6] = {0x1C,0xDB,0xD4,0x74,0xAF,0xB8};

size_t good_blocks = 0, short_blocks = 0, unknown_src = 0;

void onData(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Serial.print("Received block [len=");
  Serial.print(len);
  Serial.print("] from ");
  for(int i=0; i<6; ++i) {
    Serial.printf("%02X", info->src_addr[i]);
    if(i<5) Serial.print(":");
  }
  Serial.print(" : ");

  // Check source MAC
  if(memcmp(info->src_addr, SERVER_MAC, 6) != 0) {
    Serial.println("WARNING: Packet from unknown source!");
    unknown_src++;
    return;
  }
  
  // Print all bytes in one line, with position if you prefer
  for (int i = 0; i < len; ++i) {
    if (data[i] < 16) Serial.print("0");
    Serial.print(data[i], HEX);
    if(i < len-1) Serial.print(" ");
  }
  Serial.print(" | END\n");

  if (len == BLOCK_SIZE) {
    Serial2.write(data, len);
    Serial.println("Sent to FPGA.");
    good_blocks++;
  } else {
    Serial.println("WARNING: Received block not 16 bytes!");
    short_blocks++;
  }

  // Show diagnostic counters every 10 packets
  static int counter = 0;
  if(++counter >= 10) {
    Serial.printf("Blocks OK: %u, Short: %u, Unknown src: %u\n", good_blocks, short_blocks, unknown_src);
    counter = 0;
  }
}

void setup() {
  Serial.begin(115200);                          // Debug print to USB serial
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2); // UART to FPGA

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    while(1);
  }
  esp_now_register_recv_cb(onData);

  Serial.print("Client MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Waiting for 16-byte ESP-NOW blocks from server.");
}

void loop() {
  delay(100);
}