# Hardware Encrypted Wireless Communication System

Hardware-assisted wireless communication between FPGA boards and ESP32 relay
nodes. The project combines AES-128 RTL, UART transport, ESP-NOW networking,
and a JavaFX desktop application for serial control and monitoring.

> **Project status:** this repository contains working components and
> experimental integration code. Hardware-specific settings, pin assignments,
> and the end-to-end deployment procedure still need to be adapted to the
> target boards.

## System overview

The intended data path is:

```text
JavaFX application / host serial port
              │
              ▼
        FPGA UART + AES-128
              │
              ▼
        ESP32 UART relay
              │
        ESP-NOW wireless link
              │
        ESP32 UART relay
              │
              ▼
        FPGA UART + AES-128
```

The ESP32 firmware presents a transparent, full-duplex UART bridge. It wraps
UART data in sequenced packets, adds CRC-16 validation, retransmits packets
that are not acknowledged, and reorders packets received out of sequence.
ESP-NOW frames are fragmented and reassembled to fit the wireless payload
limit. See [`esp_relay/README.md`](./esp_relay/README.md) for the protocol and
task-level details.

## Repository layout

| Directory or file | Purpose |
| --- | --- |
| [`esp_relay/`](./esp_relay/) | ESP-IDF firmware for the ESP32 full-duplex UART/ESP-NOW relay |
| [`fpga_logic/`](./fpga_logic/) | Verilog UART, buffering, AES-128, and top-level FPGA modules |
| [`fpga_logic/constr/`](./fpga_logic/constr/) | Vivado constraints, including the Basys 3 XDC file |
| [`fpga_logic/packet_helpers/`](./fpga_logic/packet_helpers/) | Java serial-port utilities used while testing packet transport |
| [`transmitterapp/`](./transmitterapp/) | Java 21 / JavaFX desktop serial application |
| [`PINOUT.md`](./PINOUT.md) | Working document for the physical wiring and pin assignments |

Generated build output is intentionally not part of the source workflow:
Vivado output, ESP-IDF build directories, and Maven `target/` directories
should be regenerated locally.

## Prerequisites

Install the tools appropriate for the component you are working on:

- **FPGA:** Xilinx Vivado with a Verilog simulator/programming workflow and a
  Basys 3 (or a compatible board). The current RTL assumes a 100 MHz clock and
  115200 baud UART.
- **ESP32:** ESP-IDF with the `IDF_PATH` environment variable configured.
  The firmware uses FreeRTOS, Wi-Fi/ESP-NOW, NVS, the UART driver, and PSRAM.
- **Desktop application:** JDK 21 and Maven. The Maven project uses JavaFX
  21.0.2 and `jSerialComm`.
- **Hardware:** Two ESP32 relay nodes, FPGA-to-ESP32 UART connections, and
  suitable power, ground, and signal-level connections.

## Building and flashing the ESP32 relay

From the repository root:

```bash
cd esp_relay
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace the serial port and target with the values for the board being used.
The project configuration is stored in [`esp_relay/sdkconfig`](./esp_relay/sdkconfig).
Do not commit credentials or board-specific secrets.

Before flashing each relay:

1. Set the peer MAC address in
   [`esp_relay/main/config.h`](./esp_relay/main/config.h) to the MAC address of
   the other ESP32.
2. Confirm the UART pins and baud rate match the FPGA design and the physical
   wiring.
3. Configure both units with compatible runtime parameters.
4. Use the monitor output to confirm Wi-Fi/ESP-NOW initialization and record
   each device's MAC address.

The relay also exposes a UDP control and logging interface. Runtime control
commands are received on port `8000`; diagnostic broadcasts use port `8001`.
The exact command set is implemented by `control_hub.h`.

## Building the JavaFX application

```bash
cd transmitterapp
mvn clean package
```

The shaded application JAR is produced under `transmitterapp/target/`.
To run through the JavaFX Maven plugin:

```bash
mvn javafx:run
```

The application requires access to the host serial port. On Linux, the user
may need to be a member of the group that owns the serial device (commonly
`dialout`).

## FPGA workflow

Open the Verilog sources in [`fpga_logic/src/`](./fpga_logic/src/) in Vivado,
add the appropriate top-level module, and apply the constraints in
[`fpga_logic/constr/basys3.xdc`](./fpga_logic/constr/basys3.xdc). The design
contains:

- UART RX/TX modules and packet buffering;
- AES-128 encryption and decryption datapaths;
- top-level UART/AES integration modules;
- buffering and handshaking logic for continuous serial traffic.

The notes in [`fpga_logic/Logic.md`](./fpga_logic/Logic.md) describe the
current UART timing, FIFO behavior, and module handshakes. They are design
notes rather than a complete Vivado project file, so clock, pins, top-level
module selection, and synthesis settings must be verified in Vivado.

## Configuration and protocol notes

- UART timing currently targets **115200 baud** with a **100 MHz FPGA clock**.
- The relay uses large PSRAM-backed TX/RX ring buffers and separates network
  handling from UART/data tasks across the ESP32 cores.
- Relay packets include sequence information and CRC-16 validation. ACKs,
  retransmission, duplicate handling, and out-of-order buffering are handled
  by the sender and receiver tasks.
- ESP-NOW has a limited frame payload, so larger relay packets are fragmented
  before transmission and reassembled by the receiver.
- Physical pin assignments are not finalized; update
  [`PINOUT.md`](./PINOUT.md) together with the FPGA constraints and ESP32
  configuration.

## Security considerations

The FPGA contains AES-128 RTL, but the complete security properties of an
end-to-end deployment depend on how the AES modules, key handling, and relay
are integrated. The current ESP-NOW configuration in the relay documentation
does **not** enable ESP-NOW link-layer encryption. Do not treat the default
configuration as suitable for protecting sensitive traffic until key
management, authenticated encryption, replay protection, and ESP-NOW
encryption have been designed and verified.

## Development guidelines

- Keep generated files out of commits; build artifacts should remain ignored.
- Change the FPGA constraints and [`PINOUT.md`](./PINOUT.md) together when
  wiring changes.
- Test the UART path locally before introducing the ESP-NOW hop.
- Use the relay monitor and UDP logging interface when diagnosing packet loss,
  retransmission, or buffer pressure.
- Keep board-specific MAC addresses and credentials out of shared source code.

## License

See [`LICENSE`](./LICENSE).
