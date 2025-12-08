// ==========================================
// NODE - SILENT POLLING MODE (FINAL)
// ==========================================
#include <Arduino.h>
#include <painlessMesh.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// CONFIG MESH (WAJIB SAMA)
#define   MESH_PREFIX     "MESH_BARU_FIX" 
#define   MESH_PASSWORD   "12345678"
#define   MESH_PORT       7777

// HARDWARE PINS
#define PIN_MIC 34  
#define PIN_LED_R 25
#define PIN_LED_G 26
#define PIN_LED_B 27
#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 

// VARIABEL RTC (Tersimpan saat tidur)
// Ini kuncinya! Kita simpan status terakhir di memori anti-lupa
RTC_DATA_ATTR bool isWorkMode = true; 

// OBJEK
painlessMesh mesh;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_MPU6050 mpu;

// STATE LOCAL
bool oledConnected = false;
bool gyroConnected = false;
unsigned long lastSend = 0;
unsigned long wakeUpTime = 0;

void setRGB(int r, int g, int b) {
  analogWrite(PIN_LED_R, 255 - r);
  analogWrite(PIN_LED_G, 255 - g);
  analogWrite(PIN_LED_B, 255 - b);
}

// --- FUNGSI TIDUR ---
void goToSleep(int duration) {
  // Pastikan semuanya mati sebelum tidur
  setRGB(0,0,0);
  if (oledConnected) {
     display.clearDisplay();
     display.display();
     display.ssd1306_command(SSD1306_DISPLAYOFF);
  }
  
  // Masuk Deep Sleep
  esp_sleep_enable_timer_wakeup(duration * 1000000ULL);
  esp_deep_sleep_start();
}

// --- CALLBACK PESAN ---
void receivedCallback(uint32_t from, String &msg) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, msg);
  
  if (doc.containsKey("cmd")) {
    String cmd = doc["cmd"];

    // KASUS 1: SEDANG KERJA -> DISURUH TIDUR
    if (cmd == "SLEEP" && isWorkMode == true) {
       Serial.println("CMD: SLEEP DITERIMA. MEMATIKAN SISTEM...");
       isWorkMode = false; // Tandai mode tidur
       goToSleep(10);      // Langsung tidur
    }

    // KASUS 2: SEDANG TIDUR (SILENT) -> DISURUH KERJA
    else if (cmd == "WAKE" && isWorkMode == false) {
       Serial.println("CMD: WAKE DITERIMA. RESTARTING SYSTEM...");
       isWorkMode = true;  // Tandai mode kerja
       ESP.restart();      // Restart agar Hardware (OLED/Gyro) di-init ulang dgn bersih
    }
    
    // KASUS 3: SEDANG TIDUR -> DISURUH TIDUR LAGI (Keep Silent)
    else if (cmd == "SLEEP" && isWorkMode == false) {
       // Oke bos, lanjut tidur.
       goToSleep(10);
    }
  }
}

void setup() {
  Serial.begin(115200);
  wakeUpTime = millis();

  // 1. CEK PENYEBAB BANGUN
  // Jika baru dicolok listrik (bukan timer), paksa Mode Kerja
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
      isWorkMode = true; 
  }

  // 2. INIT MESH (Selalu nyala biar bisa denger perintah)
  mesh.setDebugMsgTypes( ERROR | CONNECTION ); 
  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT );
  mesh.onReceive(&receivedCallback);

  // 3. LOGIKA HARDWARE (CONDITIONAL)
  // Hanya nyalakan OLED/Sensor JIKA Mode Kerja AKTIF
  if (isWorkMode) {
      Serial.println("MODE: ACTIVE WORK");
      
      pinMode(PIN_LED_R, OUTPUT); pinMode(PIN_LED_G, OUTPUT); pinMode(PIN_LED_B, OUTPUT);
      setRGB(0,0,255); // Biru

      Wire.begin();
      if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        oledConnected = true;
        display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
        display.setCursor(0,0); display.println("SYSTEM ONLINE"); display.display();
      }
      if(mpu.begin()) gyroConnected = true;
  } 
  else {
      Serial.println("MODE: SILENT CHECK (Hardware OFF)");
      // Matikan LED biar gelap total
      pinMode(PIN_LED_R, OUTPUT); pinMode(PIN_LED_G, OUTPUT); pinMode(PIN_LED_B, OUTPUT);
      setRGB(0,0,0); 
  }
}

void loop() {
  mesh.update();

  // --- JIKA MODE KERJA (NORMAL) ---
  if (isWorkMode) {
    if (millis() - lastSend > 1000) {
      lastSend = millis();
      
      // Baca Sensor & Tampil OLED (Normal)
      long sum=0; for(int i=0;i<30;i++) sum+=analogRead(PIN_MIC);
      int noise = sum/30;
      float vib = 0;
      if(gyroConnected) { /* baca gyro code */ }
      
      if(oledConnected) {
         display.clearDisplay(); display.setCursor(0,0); 
         display.println("STATUS: ON"); display.printf("Noise: %d", noise); display.display();
      }
      
      // Logic LED
      if(noise > 2000) setRGB(255,0,0); else setRGB(0,255,0);

      // Kirim Data
      DynamicJsonDocument doc(1024);
      doc["node"] = mesh.getNodeId(); doc["noise"] = noise; doc["vib"] = vib;
      String msg; serializeJson(doc, msg); mesh.sendBroadcast(msg);
    }
  }

  // --- JIKA MODE SILENT (TIDUR) ---
  else {
    // Kita cuma nunggu pesan dari Root.
    // TAPI, kalau Root mati atau sinyal jelek, jangan nunggu selamanya (boros batre).
    // Beri waktu 5 detik untuk denger "WAKE", kalau gak ada kabar -> TIDUR LAGI.
    
    if (millis() - wakeUpTime > 120000) {
       Serial.println("TIMEOUT: Gak ada perintah WAKE. Tidur lagi ah.");
       goToSleep(10);
    }
  }
}
