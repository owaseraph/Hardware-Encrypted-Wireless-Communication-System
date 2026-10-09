# ESP32 Full-Duplex UART Relay — C++ Port

## System Overview
This project implements a highly reliable, full duplex, transparent UART bridge between two ESP32 devices over the ESP-NOW infrastructure.

## File Overview

| File | Responsability | Core |
|---|---|---|
| `config.h` | Global constants, MAC definition, queue externs | — |
| `packet.h` | Wire layout definition and CRC-16 encoding/decoding | — |
| `ring_buffer.h` | Thread-safe PSRAM ring buffer implementation | — |
| `control_hub.h` | UDP server for dynamic config and log broadcasting | 0 |
| `espnow_transport.h` | ESP-NOW init, fragmentation and packet reassembly | 0 (callbacks) |
| `uart_tasks.h` | Hardware UART driver initialization and I/O tasks | 1 |
| `sender_task.h` | Packetizez TX ring buffer, manages sliding window and re-sends | 1 |
| `receiver_task.h` | Reorders packets, drops duplicates, generates ACKs, fills RX ring | 1 |
| `main.cpp` | *(orchestration)* | — |

---

## How it Works: The Data Lifecycle
To understand what happens in this firmware, here is the lifecycle of data travelling from one ESP32 to the other:

1. **UART Ingestion (Core 1)**
Data arrives from the FPGA via the hardware UART RX pin. the `uart_reader_task` reads this data in chunks and pushes it directly into a massive 1MB thread-safe ring buffer (*g_tx_ring*) allocated in PSRAM.

2. **Packetization & Sliding Window (Core 1)**
The `sender_task` constantly monitors the TX ring buffer.
 - It pulls available data and wraps it into a `Packet` structure (Sequence Number, Flags, Length, Payload and a CRC-16 checksum).
 - It manages a **Sliding Window** protocol. It sends packets out and keeps a copy in a local window buffer (`s_window`) along with a timestamp.
 - If an ACK is not received within a set timeout (`TIMEOUT_MS`), the packet is retransmitted.

3. **ESP-NOW Transport & Fragmentation (Core 0)**
The packet is passed to the ESP-NOW transport layer. Because ESP-NOW has a maximum payload of 250 bytes per frame, and  out total packet size might exceed this, the `espnow_send()` function automatically splits the packet into smaller fragments. It adds a 4-byte header to each fragment `[FRAG_ID, FRAG_TOTAL, FRAG_IDX]` and transmits them sequentially.

4. **Reception & Reassembly (Core 0)**
On the receiving ESP32, the WiFi driver triggers `espnow_rx_cb`.
 - It checks if the incoming frame is a data fragment or a fast-path Compact ACK.
 - Data fragments are written into an assembly buffer. Once all fragments for a `FRAG_ID` arrive, the fully reassembled packet is pushed into a FreeRTOS queue (`g_rx_data_q`).

5. **Validation, OOO Handling, ACKs (Core 1)**
The `receiver_task` pulls the reassembled wire buffer from the queue:
  - **Validation**: It decodes the packet and checks the CRC-16. Corrupted packets are dropped silently (relying on the sender to timeout and retry).
  - **Duplicates**: It checks a sliding history of sequence numbers. If it's a duplicate, it's ingored, but an ACK is still sent.
  - **Out-of-Order (OOO)**: If a packet arrives out of sequence, it is held in a temporary OOO buffer (`s_ooo`) until the missing packets arrive.
  - **ACK Generation**: For every valid packet received, a 6-byte **Compact ACK** is generated and sent back. Because it's only 6 bytes, it fits entirely in a single ESP-NOW frame, bypassing the fragmentation logic and ensuring high capability.
  - **Deliver**: In-order data is pushed into the 1MB RX ring buffer (`g_rx_ring`).

6. **UART Output (Core 1)**
The `uart_writer_task` blocks on the RX ring buffer. As soon as validated, in-order data is available, it pulls the bytes and writes them out via the UART TX pin on the receiving FPGA.

---

## Memory & Task Architecture
Because standard ESP32 internal RAM is too small for heavy buffering, this firmware heavily leverages **PSRAM** (SPI RAM) and strictly divides labor across the two CPU cores.

  - **Core 0 (Comms Core)**: Handles the WiFi driver, ESP-NOW interrupts and the Control Hub UDP socket. This core manages the network stack and avoids heavy blocking operations.

  - **Core 1 (Data Core)**: Handles all UART I/O, ring buffer operations, packet encoding/decoding and sliding window logic.

  - **PSRAM Usage**: Both the 1MB TX and RX ring buffers (`RING_BUFFER_SIZE`), as well as the FreeRTOS task stacks(`STACK_SENDER`,`STACK_RECEIVER`, etc.), are allocated in PSRAM using `MALLOC_CAP_SPIRAM` via the `create_tast_psram()` helper. The TCBs (Task Control Blocks) remain in the internal RAM for fast context switching.
---


## Architecture

```
         ESP A                              ESP B
  ┌──────────────────┐             ┌──────────────────┐
  │ FPGA ──UART──►   │             │   ◄──UART── FPGA │
  │ uart_reader_task │             │ uart_writer_task  │
  │        │         │             │        ▲          │
  │        ▼         │             │        │          │
  │    g_tx_ring     │             │    g_rx_ring      │
  │        │         │             │        │          │
  │        ▼         │  ESP-NOW    │        │          │
  │   sender_task  ──┼────────────►├─ receiver_task    │
  │                  │◄────────────┼── (ACKs back)     │
  │  receiver_task ◄─┼────────────┤   sender_task ─── │
  │        │         │  (reverse)  │        │          │
  │    g_rx_ring     │             │    g_tx_ring      │
  │        │         │             │        │          │
  │        ▼         │             │        ▼          │
  │ uart_writer_task │             │ uart_reader_task  │
  │   ◄──UART── FPGA │             │ FPGA ──UART──►   │
  └──────────────────┘             └──────────────────┘
         Core 0: WiFi + ESP-NOW stack + ControlHub UDP
         Core 1: All data tasks (reader/writer/sender/receiver)
```

---

## Live Control & Monitoring (UDP)
Alongside the serial relay, a `control_hub_task` runs a UDP server on port `8000`.
 - **Runtime Configuration**: You can dynamically adjust the sliding window size, payload size and timeout delays over WiFi without recompiling the firmware by sending commands like `SET WINDOW_SIZE 16`.
 - **Remote Logging**: The ESP32 captures internal events via `HUB_LOG()` and broadcasts them to `192.168.4.255:8001`. A connected Control Panel application can listen to this port to monitor dropped packets, sliding window behavior and buffer fill warnings in real-time.

---

## Known Limitations / Future Work

- **Fragmentation is ordered** — fragments are sent sequentially per packet.
  If a fragment is lost, the whole packet times out and is retransmitted.
  ESP-NOW itself has a link-layer ACK, so fragment loss is rare.
- **Single reassembly slot** — only one packet's fragments are being assembled
  at a time per direction. Fine for sliding-window operation where the sender
  doesn't overlap packet transmissions at the fragment level.
- **`PEER_MAC` is compile-time** — for a production build, store it in NVS and
  read at boot so both units can use the same firmware binary.
- **No encryption** — `esp_now_peer_info_t.encrypt = false`. Add PMK/LMK if
  the FPGA data is sensitive.
