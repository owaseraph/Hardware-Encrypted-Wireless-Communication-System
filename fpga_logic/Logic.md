# RX Module


##THIS .MD NEEDS UPDATING



## 1. Timing Logic
The Basys 3 runs at **100 MHz**. To communicate at a **115,200 baud rate**, we calculate the number of clock cycles required per serial bit:
$Cycles = \frac{100,000,000}{115,200} \approx 868$

## 2. Receiver Strategy: Mid-Bit Sampling
To ensure data integrity, the `uart_rx` module does not sample at the edge of a transition. Instead:
* It waits for a falling edge on the `rx_pin` (Start Bit).
* It counts to **434** (half a bit period) to reach the center of the Start Bit.
* Once centered, it samples every **868** cycles to stay in the middle of every subsequent data bit.

## 3. Finite State Machine (FSM)
The module uses four states:
| State | Description |
| :--- | :--- |
| **IDLE** | Waiting for `rx_pin` to go LOW. |
| **START** | Validating the start bit at the 50% mark. |
| **DATA** | Sampling 8 bits of data into a shift register. |
| **STOP** | Confirming the line returns HIGH and flagging `data_ready`. |

# TX Module

## 1. Timing Logic
Just like the Receiver, the Transmitter must match the **115,200 baud rate**. 
* The module uses a `clock_count` register to hold the `tx_pin` state for exactly **868 clock cycles** per bit.
* This ensures that the PC receiving the data sees a stable signal at the expected frequency.

## 2. Transmission Strategy: Parallel-to-Serial
The TX module acts as a "Parallel-to-Serial" converter. It takes an 8-bit byte and sends it out one bit at a time.

* **Data Latching:** When `send_signal` goes high, the module copies `data_in` to `saved_data`. This "snapshot" prevents the data from changing mid-transmission if the input source updates.
* **Bit Selection:** We use `bit_index` (0 to 7) to select which specific bit of our 8-bit "saved_data" is currently being driven onto the `tx_pin`.

## 3. Finite State Machine (FSM)
The transmitter transitions through four distinct states to create a valid UART frame:

| State | Action | Logic |
| :--- | :--- | :--- |
| **IDLE** | Line is HIGH | Waits for `send_signal` to begin. `is_idle` is set to 1. |
| **START** | Line goes LOW | Signals the start of a frame for 868 cycles. |
| **DATA** | Serial Output | Sends bits 0 through 7, waiting 868 cycles for each. |
| **STOP** | Line goes HIGH | Ensures the line returns to the IDLE state for at least one bit. |

## 4. Handshaking Logic
The module provides an `is_idle` output using an asynchronous assignment:
`assign is_idle = (state == IDLE);`

This is vital for the Top-Level module or FIFO. It prevents the system from trying to send a new character while the State Machine is still busy in the `DATA` or `STOP` states.


# FIFO Module

## 1. Timing & Purpose
The FIFO (First-In, First-Out) serves as the "elastic" bridge between the RX and TX modules to prevent data loss.
* **The Race Condition:** The Receiver (RX) and Transmitter (TX) operate at the same baud rate, but they don't start or stop at the same time. 
* **The Solution:** The FIFO acts as a buffer. It allows the RX to store a byte as soon as it arrives, even if the TX is still busy finishing a previous transmission.

## 2. Architecture Strategy: Circular Buffer
The module implements a **Circular Buffer** using an array of 16 memory registers.
* **Memory Depth:** `reg[7:0] memory [0:15]` provides storage for 16 characters.
* **Dual Pointers:** * `write_ptr`: Increments every time a byte is received from the RX.
    * `read_ptr`: Increments every time the TX successfully sends a byte.
* **Pointer Roll-over:** Because the pointers are 4 bits wide, they naturally wrap back to `0` after reaching `15`, allowing the memory to be reused indefinitely.

## 3. Counting Logic
To manage the flow of data, the module maintains a `count` register (0 to 16). This determines the status of the "waiting room":
* **Increment:** Occurs when `write_en` is high (RX has data) and the buffer is not full.
* **Decrement:** Occurs when `read_en` is high (TX is fetching data) and the buffer is not empty.
* **Stability:** If a read and write happen at the same time, the count remains unchanged, ensuring the system never "loses" track of its contents.

## 4. Handshaking & Control
The FIFO uses a simple flag to communicate with the rest of the system:
* **`empty` Flag:** `assign empty = (count == 0);`
* This flag tells the Top-Level module that there is no data to send. The TX will only pull data from the FIFO when `empty` is **LOW**, ensuring the Transmitter never sends "garbage" data from an uninitialized memory slot.

# Top Level Module

## 1. Timing & Integration Logic
The Top Level module acts as the system coordinator, synchronizing three components that operate at the same baud rate but different phase timings.
* **Clock Domain:** All modules share the 100 MHz Basys 3 clock.
* **The Timing Gap:** The RX signals "data ready" after 9.5 bit durations, while the TX requires 10 full bit durations to finish. This module uses the **FIFO** to bridge that 0.5-bit discrepancy, preventing the "skipped character" race condition.

## 2. Architecture Strategy: Data Transformation
The system is designed as a linear pipeline that modifies data in real-time as it passes from input to output:
* **The Modifier:** A simple combinational logic wire (`received_byte + 1`) is placed between the RX and the FIFO. 
* **The Flow:** Serial Data → **RX** → **+1 Logic** → **FIFO** → **TX** → Serial Data.
* This ensures that by the time a byte reaches the FIFO, it has already been transformed (e.g., 'A' becomes 'B') before it is even stored.

## 3. Control Logic: Pulse Sequencing
Since the FIFO and TX need precise triggers to move data, the Top Level uses a small sequential state-logic block to handle the "Handshake":
* **Step 1 (Fetch):** If TX is idle and FIFO is not empty, it pulses `read_from_fifo` to pull data from the memory array.
* **Step 2 (Trigger):** Once the data is available at the FIFO output, it pulses `trigger_tx` to start the transmission.
* **Step 3 (Clear):** It immediately clears these signals to ensure the transmitter only sends exactly **one** character per FIFO "read" event.

## 4. Handshaking & Problem Solving
The module uses the status flags of the sub-components to solve the original race condition encountered during initial testing:
* **tx_is_idle:** Used as a gatekeeper to ensure we never force data onto the TX while it is busy sending the previous stop bit.
* **fifo_empty:** Used to ensure the system remains dormant until the RX has successfully provided new data.
* **Result:** This coordination allows the FPGA to handle high-speed serial streams without dropping bytes, effectively managing the communication overhead.