/*
Main controller for the App
-handles the connection between backend and main, backend can't directly modify ui elements
-implements functions and handles connection states and edge cases
-implements block-based transmission logic with logging to logsArea

*/

package com.transmitter;

import java.net.URL;
import java.util.ResourceBundle;

import javafx.application.Platform;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.ComboBox;
import javafx.scene.control.TextArea;
import javafx.scene.control.TextField;

// implements initializable to override initialize for startup 
public class MainController implements Initializable{ 
    // instantiate serialBackend obj
    private SerialBackend backend = new SerialBackend(); 

    private String lastPort = ""; // last opened port name
    
    // Block transmission constants
    private static final int BLOCK_DELAY_MS = 200; // delay between sending to FPGA

    @FXML private TextArea serialInputArea; // input serial area
    @FXML private TextArea serialOutputArea; // output (received) area
    @FXML private TextArea logsArea; // logs area
    @FXML private TextField messageInput; // message input 
    @FXML private ComboBox portComboBox; // port combo box

    // initialize ComboBox with ports
    @Override
    public void initialize(URL url, ResourceBundle rb){
        portComboBox.getItems().addAll(backend.getAvailablePorts());
        if (!backend.getLastError().isBlank()) {
            logsArea.appendText("[WARN] " + backend.getLastError() + "\n");
        }
    }

    
    // refresh ports
    @FXML
    private void onRefresh(){
        portComboBox.getItems().clear();
        portComboBox.getItems().addAll(backend.getAvailablePorts());
        if (!backend.getLastError().isBlank()) {
            logsArea.appendText("[WARN] " + backend.getLastError() + "\n");
        }
    }
    
    // try connecting
    @FXML
    private void onConnect(){
        onDisconnect(); // disconnect from last port

        String portName = (String)portComboBox.getValue();
        if (portName == null){ // check for ports selected
            logsArea.appendText("No port selected\n");
            return;
        }
        int baudRate = backend.getBaudRate();
        
        // set the block log callback (bridge between backend and frontend)
        backend.setBlockLogCallback(message -> {
            Platform.runLater(() -> {
                logsArea.appendText(message + "\n");
            });
        });
        
        // try to connect
        if(backend.connect(portName, baudRate) && backend.isConnected()){
            logsArea.appendText("Connected to " + portName +"\n");
            lastPort = portName;
            
            // start listening to the port
            // pass a lambda function to the backend to be executed called when there's bytes available to read
            // message will be passed to the lambda function
            // used so backend doesn't communicate directly with ui elements
            // received window 
            backend.startListening(message ->{ 
                Platform.runLater(() -> {
                    serialOutputArea.appendText(message + "\n");
                });
            });
        }
        else {
            String reason = backend.getLastError().isBlank() ? "" : " (" + backend.getLastError() + ")";
            logsArea.appendText("Failed to connected to " + portName + reason + "\n");
        }
    }
    
    // disconnect
    @FXML
    private void onDisconnect(){
        if(backend.isConnected()){
            backend.stopListening();
            backend.disconnect();
            logsArea.appendText("Disconnected from " + lastPort + "\n");
        }
    }

    // send message to fpga as 16-byte blocks
    @FXML
    private void onSend() {
        String input = messageInput.getText().trim();
        
        // Check connection
        if(!backend.isConnected()){
            logsArea.appendText("[ERROR] Not connected to any port\n");
            return;
        }
        
        if(input.isEmpty()) {
            logsArea.appendText("[WARN] Empty message, please type something.\n");
            return;
        }
        
        // Create new thread to handle block transmission
        new Thread(() -> {
            try {
                // Wait until ready for next message
                while (!backend.isReadyForInput()) {
                    Thread.sleep(10);
                }
                
                // Convert to bytes and calculate blocks
                byte[] full = input.getBytes();
                int totalBlocks = (full.length + 15) / 16;
                
                // Log transmission start
                Platform.runLater(() -> {
                    logsArea.appendText("[INFO] Sending " + full.length + " byte(s) as " + totalBlocks + " block(s)...\n");
                });
                
                // Setup block transmission state
                backend.setupBlockTransmission(totalBlocks);
                
                // Send each 16-byte block
                int bytesSent = 0;
                int blockNum = 1;
                
                while (bytesSent < full.length) {
                    int chunkSize = Math.min(16, full.length - bytesSent);
                    byte[] block = new byte[16];
                    System.arraycopy(full, bytesSent, block, 0, chunkSize);
                    
                    // Log the TX block
                    String txLog = formatBlockLog("[TX] Block " + blockNum + "/" + totalBlocks + ": ", block);
                    Platform.runLater(() -> {
                        logsArea.appendText(txLog + "\n");
                    });
                    
                    // Send the block
                    backend.sendBytes(block);
                    bytesSent += chunkSize;
                    blockNum++;
                    
                    // Wait between blocks
                    if (bytesSent < full.length) {
                        Thread.sleep(BLOCK_DELAY_MS);
                    }
                }
                
                // Add the input to the serial input area
                Platform.runLater(() -> {
                    serialInputArea.appendText(input + "\n");
                });
                
                // Wait for all blocks to be received
                backend.waitForAllBlocks();
                
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }).start();
        
        messageInput.clear();
    }
    
    
     // Format a block for logging (printable characters as-is, non-printable as hex)
    
    private String formatBlockLog(String prefix, byte[] block) {
        StringBuilder sb = new StringBuilder(prefix);
        for (byte b : block) {
            if (b >= 32 && b < 127) {
                sb.append((char) b);
            } else {
                sb.append(String.format("\\x%02X", b));
            }
        }
        return sb.toString();
    }

    // cleanup for soft close
    public void cleanup(){
        onDisconnect();
    }
} 