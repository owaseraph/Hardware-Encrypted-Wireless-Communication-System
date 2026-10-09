// client side for TCP connection

#include "WiFi.h"
#include "HardwareSerial.h"

const char* ssid = "ESP32_HOST";
const char* password = "regelepetric";
const int port = 80;
const char* serverIP = "192.168.4.1"; //default esp32 ap ip

HardwareSerial FPGASerial(1); // FPGA object on UART1 (UART0 is occupied by Serial)

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("Serial connection ready");

  // initialize serial connection to FPGA
  FPGASerial.begin(9600,SERIAL_8N1, 16,17);

  // connect to the access point
  WiFi.begin(ssid, password);
  Serial.println("Connecting to acces point...");
  while(WiFi.status() != WL_CONNECTED)
  delay(10);
  Serial.println("Connected to acces point");


}

void loop() {
  WiFiClient client;
  // connect to TCP server
  Serial.println("Connecting to server");
  if(client.connect(serverIP, port)){
    Serial.println("Connected to server");
    Serial.println("\n\tChat started\n");

    while(client.connected()){

      // received something from FPGA -> send to host esp
      if(FPGASerial.available()){
        String clientMsg = FPGASerial.readStringUntil('\n');
        clientMsg.trim();
        client.println(clientMsg);
        Serial.print("FPGA Client -> WiFi: ");
        Serial.println(clientMsg);
      }

      // send message to host from Arduino Serial
      if(Serial.available()){
        String clientMsg = Serial.readStringUntil('\n');
        clientMsg.trim();
        client.println(clientMsg);
        Serial.print("Serial Client -> WIiFi: ");
        Serial.println(clientMsg);
      }

      // received something from host on wifi -> forward to client FPGA
      if(client.available()){
        Serial.print("WiFi -> client FPGA: ");
        String hostMsg = client.readStringUntil('\n');
        hostMsg.trim();
        Serial.println(hostMsg);
        FPGASerial.println(hostMsg);
      }

    }
    Serial.println("Disconnected from server");
  }
  else{
    Serial.println("Connection failed, retrying ...");
    delay(2000);
  }
}
