import com.fazecast.jSerialComm.SerialPort;
import java.util.Scanner;

public class PortCheck{
    public static void main (String[] args){
        System.out.println("Checking for COM ports");

        //uses C++ structure to get to the Linux ports
        //uses type SerialPort to define COM connection
        for(SerialPort port: SerialPort.getCommPorts()){
            System.out.println("Found: "+port.getSystemPortName());
        }
    }
}