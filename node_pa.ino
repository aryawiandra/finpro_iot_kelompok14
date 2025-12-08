#include <Arduino.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include "esp_log.h" // Matikan log error

// MESH CONFIG
#define MESH_PREFIX   "MESH_BARU_FIX"
#define MESH_PASSWORD "12345678"
#define MESH_PORT     7777

// PIN
#define PIN_MIC 34
#define PIN_LED_R 25
#define PIN_LED_G 26
#define PIN_LED_B 27
#define I2C_SDA 21
#define I2C_SCL 22

// I2C ADDRESS
#define ADDR_OLED 0x3D
#define ADDR_MPU  0x68

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// MPU6050
Adafruit_MPU6050 mpu;

// FLAGS
bool hasOLED = false;
bool hasGyro = false;

// RTC
RTC_DATA_ATTR bool isWorkMode = true;

// OBJECTS
painlessMesh mesh;

unsigned long wakeUpTime = 0;

TaskHandle_t TaskMeshHandle;
TaskHandle_t TaskMainHandle;

void setRGB(int r, int g, int b) {
  analogWrite(PIN_LED_R, 255 - r);
  analogWrite(PIN_LED_G, 255 - g);
  analogWrite(PIN_LED_B, 255 - b);
}

// Cek perangkat I2C
bool checkI2C_Device(uint8_t address) {
  Wire.beginTransmission(address);
  byte error = Wire.endTransmission();
  return (error == 0);
}

void goToSleep(int duration) {
  Serial.println(">>> SLEEPING...");
  setRGB(0, 0, 0);

  if (hasOLED) {
    display.clearDisplay();
    display.display();
  }

  if (TaskMeshHandle != NULL) vTaskDelete(TaskMeshHandle);
  if (TaskMainHandle != NULL) vTaskDelete(TaskMainHandle);

  esp_sleep_enable_timer_wakeup(duration * 1000000ULL);
  esp_deep_sleep_start();
}

// Callback pesan mesh
void receivedCallback(uint32_t from, String &msg) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, msg);

  if (doc.containsKey("cmd")) {
    String cmd = doc["cmd"];
    if (cmd == "SLEEP") {
      isWorkMode = false;
      goToSleep(10);
    }
    else if (cmd == "WAKE" && !isWorkMode) {
      isWorkMode = true;
      ESP.restart();
    }
  }
}

// Task mesh
void TaskMesh(void *pvParameters) {
  for (;;) {
    mesh.update();
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

// Task sensor & logika
void TaskMain(void *pvParameters) {
  for (;;) {

    // Mode aktif
    if (isWorkMode) {
      long sum = 0;
      for (int i = 0; i < 30; i++) sum += analogRead(PIN_MIC);
      int noise = sum / 30;

      float accelMag = 0;

      // Baca gyro jika ada
      if (hasGyro) {
        sensors_event_t a, g, temp;
        if (mpu.getEvent(&a, &g, &temp)) {
          accelMag = sqrt(sq(a.acceleration.x) + sq(a.acceleration.y) + sq(a.acceleration.z));
        } else {
          hasGyro = false;
        }
      }

      // LED indikator noise
      if (noise > 2000) setRGB(255, 0, 0);
      else setRGB(0, 255, 0);

      // Tampilkan di OLED
      if (hasOLED) {
        display.clearDisplay();
        display.setCursor(0, 0);
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.print("ID: "); display.println(mesh.getNodeId());
        display.print("N: "); display.println(noise);
        display.print("V: "); display.println(hasGyro ? String(accelMag) : "N/A");
        display.display();
      }

      // Kirim data mesh
      DynamicJsonDocument doc(1024);
      doc["node"] = mesh.getNodeId();
      doc["noise"] = noise;
      if (hasGyro) doc["vib"] = accelMag;

      String msg;
      serializeJson(doc, msg);
      mesh.sendBroadcast(msg);

      // Log ringkas
      Serial.printf("DATA: N=%d | Gyro=%s | OLED=%s\n", noise, hasGyro ? "OK" : "NO", hasOLED ? "OK" : "NO");

      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
    // Mode diam
    else {
      setRGB(0, 0, 50); vTaskDelay(50); setRGB(0, 0, 0);
      if (hasOLED) { display.clearDisplay(); display.display(); }
      if (millis() - wakeUpTime > 15000) goToSleep(10);
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }
}


void setup() {
  Serial.begin(115200);

  esp_log_level_set("i2c", ESP_LOG_NONE);
  esp_log_level_set("*", ESP_LOG_NONE);

  wakeUpTime = millis();

  // I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);

  // Deteksi OLED
  if (checkI2C_Device(ADDR_OLED)) {
    if (display.begin(SSD1306_SWITCHCAPVCC, ADDR_OLED)) {
      hasOLED = true;
    }
  }

  // Deteksi gyro
  if (checkI2C_Device(ADDR_MPU)) {
    if (mpu.begin()) {
      hasGyro = true;
      mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
      mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    }
  }

  // Inisialisasi pin dan mesh
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) isWorkMode = true;

  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  setRGB(0, 0, 0);

  mesh.setDebugMsgTypes(ERROR);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&receivedCallback);

  Serial.println("\n>>> BOOT COMPLETE");
  Serial.printf(">>> DETECTED: OLED [%s] | GYRO [%s]\n", hasOLED ? "YES" : "NO", hasGyro ? "YES" : "NO");

  xTaskCreatePinnedToCore(TaskMesh, "MeshTask", 5000, NULL, 2, &TaskMeshHandle, 1);
  xTaskCreatePinnedToCore(TaskMain, "MainTask", 5000, NULL, 1, &TaskMainHandle, 1);
}

void loop() {
  vTaskDelete(NULL);
}