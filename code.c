#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <I2S.h>
#include <WiFiManager.h>
#include <FS.h>
#include <ArduinoJson.h>

// 可配置的服務器設置
char serverHost[40] = "400.com.tw";
char serverPath[60] = "/api/voice.html";
char serverPort[6] = "80";

const int ledPin = 2;    // LED 連接到 GPIO2 (內建LED)
const int touchPin = 4;  // 觸摸傳感器連接到 GPIO4
const int i2sBufSize = 1024;
const int sampleRate = 44100;

// LED 狀態
enum LedState {
  LED_OFF,
  LED_ON,
  LED_NO_CONNECTION,    // 沒有網路時的特殊閃爍
  LED_RECORDING,        // 錄音時的閃爍
  LED_FAST_BLINK        // 長按 TTP223 時的快速閃爍
};

LedState currentLedState = LED_ON;
unsigned long lastLedToggle = 0;
bool isRecording = false;
bool isConnected = false;
unsigned long touchStartTime = 0;
unsigned long fastBlinkStart = 0;

// 定義全域變數 WiFiClient
WiFiClient client;
WiFiManager wifiManager;

void setLedState(LedState newState) {
  currentLedState = newState;
  lastLedToggle = 0;
}

void updateLED() {
  unsigned long currentTime = millis();
  
  switch (currentLedState) {
    case LED_ON:
      digitalWrite(ledPin, HIGH);  // 長亮
      break;
    case LED_NO_CONNECTION:
      if (currentTime - lastLedToggle < 1000) {
        digitalWrite(ledPin, HIGH);  // 亮一秒
      } else if (currentTime - lastLedToggle < 1500) {
        digitalWrite(ledPin, LOW);   // 閃爍一下
      } else if (currentTime - lastLedToggle < 2000) {
        digitalWrite(ledPin, HIGH);  // 再次亮
      } else if (currentTime - lastLedToggle < 2500) {
        digitalWrite(ledPin, LOW);   // 再次閃爍
      } else if (currentTime - lastLedToggle > 5000) {
        lastLedToggle = currentTime;  // 重複五秒週期
      }
      break;
    case LED_RECORDING:
      if (currentTime - lastLedToggle >= 500) {  // 錄音時每秒閃兩次
        digitalWrite(ledPin, !digitalRead(ledPin));
        lastLedToggle = currentTime;
      }
      break;
    case LED_FAST_BLINK:
      if (currentTime - lastLedToggle >= 333) {  // 快速閃爍，每秒3次
        digitalWrite(ledPin, !digitalRead(ledPin));
        lastLedToggle = currentTime;
      }
      if (currentTime - fastBlinkStart >= 5000) {  // 快速閃爍 5 秒後回到長亮
        setLedState(LED_ON);
      }
      break;
    case LED_OFF:
      digitalWrite(ledPin, LOW);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);  // 設定內建 LED 為輸出模式
  pinMode(touchPin, INPUT);

  setLedState(LED_ON); // 初始化時 LED 長亮 (還沒連線)

  // 初始化 WiFiManager
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);

  // 嘗試自動連接 WiFi，若連接失敗則進入配置模式
  if (!wifiManager.autoConnect("ESP8266-AudioStreamer")) {
    setLedState(LED_NO_CONNECTION);  // 連接失敗時進行特殊閃爍
    Serial.println("WiFi 連接失敗，進入配置模式");
  } else {
    setLedState(LED_ON);  // 成功連接 WiFi，LED 長亮
    isConnected = true;
    Serial.println("WiFi 連接成功，LED 長亮");
  }
}

void loop() {
  unsigned long currentTime = millis();

  int touchState = digitalRead(touchPin);
  
  // 檢查 TTP223 的長按，若長按超過 3 秒則快速閃爍
  if (touchState == HIGH) {
    if (currentTime - touchStartTime > 3000) {  // 長按超過3秒
      setLedState(LED_FAST_BLINK);  // 快速閃爍5秒
      fastBlinkStart = millis();
    }
  } else {
    touchStartTime = currentTime;
  }

  updateLED();

  if (isRecording && isConnected) {
    // 錄音邏輯
    streamAudio();
  }
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("進入配置模式，等待設定");
  setLedState(LED_NO_CONNECTION);  // 沒有網路時的特殊閃爍
}

void streamAudio() {
  int16_t samples[i2sBufSize];
  size_t bytesRead = I2S.read(samples, sizeof(samples));

  if (!client.connected()) {
    if (!client.connect(serverHost, atoi(serverPort))) {
      Serial.println("連接到伺服器失敗");
      return;
    }
  }

  // 發送數據到伺服器
  client.print(String("POST ") + serverPath + " HTTP/1.1\r\n" +
               "Host: " + serverHost + "\r\n" +
               "Content-Type: application/octet-stream\r\n" +
               "Content-Length: " + bytesRead + "\r\n" +
               "Connection: keep-alive\r\n\r\n");

  client.write((uint8_t*)samples, bytesRead);
  Serial.println("音頻數據已傳送: " + String(bytesRead) + " bytes");

  // 檢查伺服器回應
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> 伺服器回應超時！");
      client.stop();
      return;
    }
  }

  // 讀取並打印伺服器回應
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
}


void startRecording() {
  isRecording = true;
  setLedState(LED_RECORDING);  // 錄音時每秒閃兩次
  Serial.println("開始錄音");
}

void stopRecording() {
  isRecording = false;
  setLedState(LED_ON);  // 停止錄音，回到長亮
  Serial.println("停止錄音");
}
