// host side for TCP connection

#include <WiFi.h>
#include <HardwareSerial.h>

const char* ssid = "ESP32_HOST";
const char* password = "regelepetric";
const int port = 80;

WiFiServer server(port); //listen on port 80
HardwareSerial FPGASerial(1); // FPGA object on UART1 (UART0 is occupied by Serial)

void setup(){
  Serial.begin(115200);
  delay(2000);
  Serial.println("Serial connection ready");

  FPGASerial.begin(9600, SERIAL_8N1, 16,17); // FPGA uart serial
  // start the server
  WiFi.softAP(ssid, password); // create acces point
  server.begin(); // start listening for connections
  Serial.println("Server started");

  // show host ip
  Serial.print("Server ip: ");
  Serial.println(WiFi.softAPIP());
}
void loop(){
  //check if any client is connected
  WiFiClient client = server.available();
  if (client){
    Serial.println("Client connected");
    Serial.println("\n\tChat Started\n");

    while(client.connected()){

       // received something from FPGA -> send to client esp
      if(FPGASerial.available()){
         String hostMsg = FPGASerial.readStringUntil('\n');
         hostMsg.trim();
         client.println(hostMsg);
         Serial.print("Host FPGA -> WiFi: ");
         Serial.println(hostMsg);
      }

       // send message to client from Arduino Serial
      if(Serial.available()){
         String hostMsg = Serial.readStringUntil('\n');
         hostMsg.trim();
         client.println(hostMsg);
         Serial.print("Serial Host -> WiFI: ");
         Serial.println(hostMsg);
      }

      // receive something from client on WiFi -> send to FPGA
      if(client.available()){
          Serial.print("WiFi -> Host FPGA: ");
          String clientMsg = client.readStringUntil('\n');
          clientMsg.trim();
          Serial.println(clientMsg);
          FPGASerial.println(clientMsg);
      }
    }
    Serial.println("Client disconneced");
  }
}