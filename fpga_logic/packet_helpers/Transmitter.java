import java.io.ByteArrayOutputStream;
import java.util.Scanner;
import java.util.concurrent.atomic.AtomicInteger;
import com.fazecast.jSerialComm.*;
//Tcaciuc Rares-Stefan, 27.03.2026
public class Transmitter {

    //*
    // blockBuffer accumulates incoming echo bytes until a full 16-byte block is assembled
    // readyForInput is the flag that pauses the main thread while waiting for all echoes 
    // blocksExpected holds how many blocks were sent, blocksReceived counts how many echoes came back. 
    // AtomicInteger because they're written by the main thread and read/written by the listener thread simultaneously*/
    static ByteArrayOutputStream blockBuffer = new ByteArrayOutputStream(16);
    static volatile boolean readyForInput = true;
    static AtomicInteger blocksExpected = new AtomicInteger(0);
    static AtomicInteger blocksReceived = new AtomicInteger(0);

    public static void main(String[] args) {

        //Port setup
        SerialPort comPort = SerialPort.getCommPort("ttyUSB1");
        comPort.setBaudRate(115200);
        
        if (!comPort.openPort()) {
            System.out.println("Could not open port!");
            return;
        }
        System.out.println("Connected to: " + comPort.getSystemPortName());
        System.out.println("Type a message of any length. It will be split into 16-byte blocks.\n");

        //This runs on a separate thread managed by jSerialComm; every time bytes arrive on the serial port 
        //this fires automatically
        comPort.addDataListener(new SerialPortDataListener() {
            @Override
            public int getListeningEvents() {
                return SerialPort.LISTENING_EVENT_DATA_AVAILABLE;
            }

            @Override
            public void serialEvent(SerialPortEvent event) {
                if (event.getEventType() != SerialPort.LISTENING_EVENT_DATA_AVAILABLE)
                    return;

                byte[] newData = new byte[comPort.bytesAvailable()];
                comPort.readBytes(newData, newData.length);
                

                //Adds each byte one at a time into blockBuffer.
                //The synchronized prevents the main thread and listener thread from touching the buffer 
                //at the same time 
                //Once 16 bytes are accumulated, the block is complete and gets printed, then the buffer is reset for the next block
                synchronized (blockBuffer) {
                    for (byte b : newData) {
                        blockBuffer.write(b);
                        if (blockBuffer.size() == 16) {
                            byte[] block = blockBuffer.toByteArray();
                            int blockNum = blocksReceived.get() + 1;
                            System.out.printf("\n[Block %d/%d] Received echo: ", blockNum, blocksExpected.get());
                            for (byte c : block) {
                                if (c >= 32 && c < 127)
                                    System.out.print((char) c);
                                else
                                    System.out.printf("\\x%02X", c);
                            }
                            System.out.println();
                            blockBuffer.reset();
                            

                            //After each complete block is received, the counter increments 
                            //Only when all expected blocks are back does it set readyForInput = true, 
                            //which unblocks the main thread to prompt for the next message
                            if (blocksReceived.incrementAndGet() >= blocksExpected.get()) {
                                System.out.println("[INFO] All blocks received.");
                                readyForInput = true;
                                blocksExpected.set(0);
                                blocksReceived.set(0);
                            }
                        }
                    }
                }
            }
        });

        Scanner scanner = new Scanner(System.in);
        while (true) {
            //busy-waits in 10ms increments untill the listener signal all echoes are back.
            //this keeps CPU usage low
            while (!readyForInput) {
                try { Thread.sleep(10); } catch (InterruptedException e) {}
            }

            System.out.print("Message: ");
            String input = scanner.nextLine();

            if (input.equalsIgnoreCase("exit"))
                break;

            if (input.isEmpty()) {
                System.out.println("[WARN] Empty input, please type a message.");
                continue;
            }

            //converts input strign to raw bytes, then calc.
            //how many 16 byte blocks are needed
            byte[] full = input.getBytes();
            int totalBlocks = (full.length + 15) / 16;

            System.out.printf("[INFO] Sending %d byte(s) as %d block(s)...\n", full.length, totalBlocks);

            blocksExpected.set(totalBlocks);
            blocksReceived.set(0);
            readyForInput = false;

            int bytesSent = 0;
            int blockNum = 1;

            //creates a fresh zero-filled 16 byte array for each block
            //array copy copies the relevant chunk of the message to it, the remaining bytes staying 0
            while (bytesSent < full.length) {
                int chunkSize = Math.min(16, full.length - bytesSent);
                byte[] block = new byte[16];
                System.arraycopy(full, bytesSent, block, 0, chunkSize);

                System.out.printf("[TX] Block %d/%d: ", blockNum++, totalBlocks);
                for (byte b : block) {
                    if (b >= 32 && b < 127)
                        System.out.print((char) b);
                    else
                        System.out.printf("\\x%02X", b);
                }
                System.out.println();

                comPort.writeBytes(block, 16);
                bytesSent += chunkSize;

                //Wait between blocks so the ESP has time to process
                //and send each block before the next one arrives
                if (bytesSent < full.length) {
                    try { Thread.sleep(200); } catch (InterruptedException e) {}
                }
            }
        }

        comPort.removeDataListener();
        comPort.closePort();
        System.out.println("Closed Port.");
    }
}