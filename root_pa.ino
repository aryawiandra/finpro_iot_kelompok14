#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID   "TMPL6gJnYJIQa"
#define BLYNK_TEMPLATE_NAME "Library Mesh Monitor"
#define BLYNK_AUTH_TOKEN    "AKio8PcuKqb-DRQAauhZOPCTL0w29qs5"

#include <Arduino.h>
#include <painlessMesh.h>
#include <BlynkSimpleEsp32.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- CONFIG WIFI & MQTT ---
const char* ssid        = "SHAS";
const char* pass        = "sh123123";
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic  = "kampus/library/kelompok14/data";

// --- CONFIG MESH ---
#define   MESH_PREFIX     "MESH_BARU_FIX" 
#define   MESH_PASSWORD   "12345678"
#define   MESH_PORT       7777

painlessMesh  mesh;
WiFiClient    espClient;
PubSubClient  mqttClient(espClient);

// --- RTOS HANDLES ---
TaskHandle_t TaskMeshHandle;
TaskHandle_t TaskInternetHandle;
QueueHandle_t msgQueue;

struct NodeMessage { char payload[512]; };

volatile bool isSystemActive = true; 
unsigned long lastBroadcast = 0;

void receivedCallback(uint32_t from, String &msg) {
  NodeMessage data;
  if (msg.length() < 512) {
    msg.toCharArray(data.payload, 512);
    // Masukkan ke antrian (Timeout 10 tick agar tidak blocking)
    xQueueSend(msgQueue, &data, 10); 
    Serial.printf("RX Node %u\n", from);
  }
}

void broadcastStatus() {
  DynamicJsonDocument doc(256);
  doc["cmd"] = isSystemActive ? "WAKE" : "SLEEP"; 
  String msg;
  serializeJson(doc, msg);
  mesh.sendBroadcast(msg);
}

// Sync Blynk Button
BLYNK_WRITE(V3) {
  isSystemActive = (param.asInt() == 1);
  broadcastStatus();
}

// ==========================================
// TASK 1: MESH
// ==========================================
void TaskMesh(void *pvParameters) {
  for (;;) {
    mesh.update();
    
    // Broadcast status rutin tiap 3 detik
    if (millis() - lastBroadcast > 3000) {
      lastBroadcast = millis();
      broadcastStatus();
    }
    vTaskDelay(1 / portTICK_PERIOD_MS); 
  }
}

// ==========================================
// TASK 2: INTERNET
// ==========================================
void TaskInternet(void *pvParameters) {
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setBufferSize(1024); 
  mqttClient.setKeepAlive(60);    
  espClient.setTimeout(5);        
  
  Blynk.config(BLYNK_AUTH_TOKEN);

  for (;;) {
    // 1. Cek Koneksi WiFi
    if (WiFi.status() == WL_CONNECTED) {
      
      // --- BLYNK LOGIC ---
      if (Blynk.connected()) {
        Blynk.run();
      } else {
        static unsigned long lastBlynk = 0;
        if (millis() - lastBlynk > 10000) { 
           lastBlynk = millis(); 
           Serial.println("[BLYNK] Connecting...");
           Blynk.connect(2000); 
        }
      }

      // --- MQTT LOGIC ---
      if (mqttClient.connected()) {
        mqttClient.loop();
      } else {
        static unsigned long lastMqtt = 0;
        if (millis() - lastMqtt > 5000) { 
           lastMqtt = millis(); 
           String clientId = "Root_" + String(WiFi.macAddress());
           clientId.replace(":","");
           
           Serial.print("[MQTT] Connecting... ");
           if (mqttClient.connect(clientId.c_str())) Serial.println("OK");
           else { Serial.print("FAIL rc="); Serial.println(mqttClient.state()); }
        }
      }

    } else {
       vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    // 2. PROSES DATA DARI QUEUE (BAGIAN INI SAYA TAMBAHKAN PRINT)
    NodeMessage incomingData;
    // Cek apakah ada antrian?
    if (xQueueReceive(msgQueue, &incomingData, 0) == pdTRUE) {
      
      String msg = String(incomingData.payload);
      
      // >>> DEBUG 1: TAMPILKAN RAW JSON <<<
      Serial.print(">> PROCESS: "); 
      Serial.println(msg); 

      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, msg);

      if (!error && doc.containsKey("noise")) {
        uint32_t nodeId = doc["node"]; 
        int noise       = doc["noise"];
        float vib       = doc.containsKey("vib") ? doc["vib"] : 0.0;
        
        Serial.printf("   Parsed: ID=%u, Noise=%d, Vib=%.2f\n", nodeId, noise, vib);

        if (nodeId != 0) {
           // Kirim ke Blynk
           if (Blynk.connected()) {
             if (nodeId == 135941613) { 
               Blynk.virtualWrite(V0, noise);
               Blynk.virtualWrite(V1, vib);
               Blynk.virtualWrite(V2, (double)nodeId);
             }
             else if (nodeId == 4267557053U) { 
               Blynk.virtualWrite(V4, noise);
               Blynk.virtualWrite(V5, (double)nodeId);
             }
             else {
               Blynk.virtualWrite(V5, (double)nodeId);
             }
             Serial.print("   [BLYNK] Sent. ");
           } else {
             Serial.print("   [BLYNK] Disconnected. ");
           }

           // Kirim ke MQTT
           if (mqttClient.connected()) {
             mqttClient.publish(mqtt_topic, msg.c_str());
             Serial.println("[MQTT] Sent.");
           } else {
             Serial.println("[MQTT] Disconnected.");
           }
        }
      } else {
        // Jika JSON Error
        Serial.print("   JSON ERROR: ");
        Serial.println(error.c_str());
      }
    }
    
    // Serial Control Manual
    if (Serial.available()) {
       String input = Serial.readStringUntil('\n');
       if (input.indexOf("OFF") >= 0) isSystemActive = false;
       if (input.indexOf("ON") >= 0)  isSystemActive = true;
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// SETUP
// ==========================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  Serial.begin(115200);
  delay(1000); 

  msgQueue = xQueueCreate(20, sizeof(NodeMessage));

  mesh.setDebugMsgTypes(ERROR | STARTUP); 
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  
  mesh.stationManual(ssid, pass); 
  mesh.setRoot(true);
  mesh.setContainsRoot(true); 
  mesh.onReceive(&receivedCallback);

  Serial.println(">>> ROOT SYSTEM START");

  xTaskCreatePinnedToCore(TaskMesh, "MeshTask", 10000, NULL, 2, &TaskMeshHandle, 0);
  xTaskCreatePinnedToCore(TaskInternet, "NetTask", 10000, NULL, 1, &TaskInternetHandle, 1);
}

void loop() {
  vTaskDelete(NULL);
}