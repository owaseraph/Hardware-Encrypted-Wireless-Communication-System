import com.fazecast.jSerialComm.SerialPort;

import java.io.OutputStream;
import java.util.Scanner;

public class test {

    public static void main(String[] args) throws Exception {
        String portName = "/dev/ttyUSB2";
        int baudRate = 115200;

        SerialPort port = SerialPort.getCommPort(portName);
        port.setBaudRate(baudRate);
        port.setNumDataBits(8);
        port.setNumStopBits(SerialPort.ONE_STOP_BIT);
        port.setParity(SerialPort.NO_PARITY);

        // 🔴 VERY IMPORTANT: blocking mode
        port.setComPortTimeouts(SerialPort.TIMEOUT_READ_BLOCKING, 0, 0);

        if (!port.openPort()) {
            System.out.println("Failed to open port!");
            return;
        }

        System.out.println("Connected to " + portName);

        // ---------- Wait for FPGA ----------
        System.out.println("Waiting for FPGA to stabilize...");
        Thread.sleep(1000);

        OutputStream out = port.getOutputStream();
        Scanner scanner = new Scanner(System.in);

        // ---------- Start RX thread ----------
        Thread rxThread = new Thread(() -> {
            byte[] oneByte = new byte[1];
            byte[] block = new byte[16];
            int index = 0;

            while (true) {
                int n = port.readBytes(oneByte, 1);
                if (n > 0) {
                    int val = oneByte[0] & 0xFF;

                    // Print immediately (raw stream)
                    System.out.printf("[%d us] RX byte: %02X\n",
                            System.nanoTime() / 1000, val);

                    // Build 16-byte block
                    block[index++] = (byte) val;
                    if (index == 16) {
                        System.out.print("RX BLOCK: ");
                        for (int i = 0; i < 16; i++) {
                            System.out.printf("%02X ", block[i]);
                        }
                        System.out.println("\n");
                        index = 0;
                    }
                }
            }
        });

        rxThread.setDaemon(true); // so it won't block program exit
        rxThread.start();

        // ---------- Interactive TX loop ----------
        while (true) {
            System.out.print("Enter hex to send (or 'quit'): ");
            String line = scanner.nextLine().trim();
            if (line.equalsIgnoreCase("quit")) break;
            if (line.isEmpty()) continue;

            try {
                byte[] data = hexStringToByteArray(line);
                System.out.print("TX: ");
                for (byte b : data) System.out.printf("%02X ", b);
                System.out.println();

                out.write(data);
                out.flush();
            } catch (Exception e) {
                System.out.println("Invalid hex string. Try again.");
            }
        }

        port.closePort();
        System.out.println("Port closed. Exiting.");
    }

    // ---------- HEX helper ----------
    public static byte[] hexStringToByteArray(String s) {
        s = s.replaceAll("\\s+", "");
        if (s.length() % 2 != 0)
            throw new IllegalArgumentException("Even-length hex required");

        byte[] data = new byte[s.length() / 2];
        for (int i = 0; i < s.length(); i += 2) {
            data[i / 2] = (byte) Integer.parseInt(s.substring(i, i + 2), 16);
        }
        return data;
    }
}