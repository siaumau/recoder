#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <I2S.h>

const char* ssid = "eric";
const char* password = "07140623";
const char* serverHost = "400.com.tw";
const int serverPort = 80;
const String serverPath = "/api/voice.html";

const int ledPin = 2;  // Assuming LED is connected to GPIO2
const int touchPin = 4; // Assuming TTP223 is connected to GPIO4

const int i2sBufSize = 1024;
const int sampleRate = 44100;

bool isRecording = false;
unsigned long touchStartTime = 0;
unsigned long blinkStartTime = 0;
bool fastBlinking = false;

WiFiClient client;

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  pinMode(touchPin, INPUT);

  // Initialize I2S
  if (!I2S.begin(I2S_PHILIPS_MODE, sampleRate, 16)) {
    Serial.println("Failed to initialize I2S!");
    while (1); // Stay here twiddling thumbs if we can't initialize I2S
  }

  connectToWifi();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectToWifi();
  }

  int touchState = digitalRead(touchPin);
  
  if (touchState == HIGH) {
    if (!isRecording) {
      startRecording();
    } else {
      if (millis() - touchStartTime > 3000) {
        prepareToStopRecording();
      }
    }
  } else {
    if (isRecording && fastBlinking) {
      stopRecording();
    }
    touchStartTime = millis();
  }

  updateLED();

  if (isRecording) {
    streamAudio();
  }
}

void connectToWifi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    digitalWrite(ledPin, HIGH);
    delay(1000);
    for (int i = 0; i < 5; i++) {
      digitalWrite(ledPin, LOW);
      delay(100);
      digitalWrite(ledPin, HIGH);
      delay(100);
    }
  }
  digitalWrite(ledPin, HIGH);
}

void startRecording() {
  isRecording = true;
  blinkStartTime = millis();
}

void prepareToStopRecording() {
  fastBlinking = true;
  blinkStartTime = millis();
}

void stopRecording() {
  isRecording = false;
  fastBlinking = false;
}

void updateLED() {
  if (!isRecording) {
    digitalWrite(ledPin, HIGH);
  } else if (fastBlinking) {
    int elapsedTime = millis() - blinkStartTime;
    if (elapsedTime < 5000) {
      digitalWrite(ledPin, (millis() / 50) % 2 == 0);
    } else {
      fastBlinking = false;
    }
  } else {
    digitalWrite(ledPin, (millis() / 500) % 2 == 0);
  }
}

void streamAudio() {
  int16_t samples[i2sBufSize];
  size_t bytesRead = I2S.read(samples, sizeof(samples));

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      if (!client.connect(serverHost, serverPort)) {
        Serial.println("Connection to server failed");
        return;
      }
    }

    client.print(String("POST ") + serverPath + " HTTP/1.1\r\n" +
                 "Host: " + serverHost + "\r\n" +
                 "Content-Type: application/octet-stream\r\n" +
                 "Content-Length: " + bytesRead + "\r\n" +
                 "Connection: keep-alive\r\n\r\n");

    client.write((uint8_t*)samples, bytesRead);

    // Wait for the server's response
    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        Serial.println(">>> Client Timeout !");
        client.stop();
        return;
      }
    }

    // Read all the lines of the reply from server and print them to Serial
    while (client.available()) {
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }
  }
}
