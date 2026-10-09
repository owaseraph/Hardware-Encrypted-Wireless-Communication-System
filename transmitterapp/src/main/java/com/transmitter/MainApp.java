package com.transmitter;

import javafx.application.Application;
import javafx.fxml.FXMLLoader;
import javafx.scene.Scene;
import javafx.stage.Stage;

public class MainApp extends Application {

    @Override
    public void start(Stage primaryStage) throws Exception {
        FXMLLoader loader = new FXMLLoader(
            getClass().getResource("/com/transmitter/main.fxml")
        );

        Scene scene = new Scene(loader.load(), 800, 600);
        primaryStage.setTitle("FPGA Secure Transmitter");
        scene.getStylesheets().add(
            getClass().getResource("/com/transmitter/style.css").toExternalForm()
        );
        primaryStage.setScene(scene);
        primaryStage.show();

        MainController controller = loader.getController();
        primaryStage.setOnCloseRequest(event -> {
            controller.cleanup();
        });
    }

    public static void main(String[] args) {
        SerialBackend.clearNativeCache(); // clear cache for jSerialComm
        launch(args);
    }
}