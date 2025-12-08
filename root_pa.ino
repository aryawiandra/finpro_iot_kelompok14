// ==========================================
// ROOT - BLYNK REMOTE CONTROL
// ==========================================
#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID   "TMPL6gJnYJIQa"
#define BLYNK_TEMPLATE_NAME "Library Mesh Monitor"
#define BLYNK_AUTH_TOKEN    "AKio8PcuKqb-DRQAauhZOPCTL0w29qs5"

#include <Arduino.h>
#include <painlessMesh.h>
#include <BlynkSimpleEsp32.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// CONFIG
const char* ssid     = "fadhlureza";
const char* pass     = "acebkutek";
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic  = "kampus/library/kelompok14/data";

#define   MESH_PREFIX     "MESH_BARU_FIX" 
#define   MESH_PASSWORD   "12345678"
#define   MESH_PORT       7777

painlessMesh  mesh;
WiFiClient    espClient;
PubSubClient  mqttClient(espClient);

// STATUS GLOBAL
bool isSystemActive = true; 
unsigned long lastBroadcast = 0;

// --- FUNGSI BROADCAST STATUS ---
void broadcastStatus() {
  DynamicJsonDocument doc(256);
  doc["cmd"] = isSystemActive ? "WAKE" : "SLEEP"; 
  String msg;
  serializeJson(doc, msg);
  mesh.sendBroadcast(msg);
}

// --- FUNGSI DARI TOMBOL BLYNK (V3) ---
// Dipanggil otomatis saat kamu tekan tombol di HP
BLYNK_WRITE(V3) {
  int value = param.asInt(); // 1 = ON, 0 = OFF
  
  if (value == 1) {
    isSystemActive = true;
    Serial.println(">>> BLYNK CMD: ON (Membangunkan Sistem)");
  } else {
    isSystemActive = false;
    Serial.println(">>> BLYNK CMD: OFF (Menidurkan Sistem)");
  }
  
  // Langsung broadcast tanpa nunggu timer
  broadcastStatus();
}

void receivedCallback(uint32_t from, String &msg) {
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, msg);
  
  if (!error && doc.containsKey("noise")) { 
    int nodeId = doc["node"]; 
    int noise  = doc["noise"];
    float vib  = doc["vib"];
    
    Serial.printf("Node %u: Noise %d\n", nodeId, noise);

    if (Blynk.connected()) {
       Blynk.virtualWrite(V0, noise);
       Blynk.virtualWrite(V1, vib);
       Blynk.virtualWrite(V2, nodeId);
    }
    if (mqttClient.connected()) {
       mqttClient.publish(mqtt_topic, msg.c_str());
    }
  }
}

void setup() {
  Serial.begin(115200);

  mesh.setDebugMsgTypes( ERROR | CONNECTION ); 
  mesh.init( MESH_PREFIX, MESH_PASSWORD, MESH_PORT );
  mesh.stationManual(ssid, pass); 
  mesh.setRoot(true);
  mesh.setContainsRoot(true); 
  mesh.onReceive(&receivedCallback);

  Blynk.config(BLYNK_AUTH_TOKEN);
  mqttClient.setServer(mqtt_server, 1883);
  
  Serial.println("ROOT SIAP. KENDALI: SERIAL atau BLYNK (V3)");
}

void loop() {
  mesh.update();

  // 1. INPUT SERIAL (Manual Backup)
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim(); input.toUpperCase();

    if (input == "OFF") {
      isSystemActive = false;
      Serial.println(">>> SERIAL CMD: OFF");
      // Update juga tombol di Blynk App biar sinkron
      if(Blynk.connected()) Blynk.virtualWrite(V3, 0);
    } 
    else if (input == "ON") {
      isSystemActive = true;
      Serial.println(">>> SERIAL CMD: ON");
      if(Blynk.connected()) Blynk.virtualWrite(V3, 1);
    }
    broadcastStatus();
  }

  // 2. BROADCAST RUTIN (Agar node yg baru bangun tau status)
  if (millis() - lastBroadcast > 2000) {
    lastBroadcast = millis();
    broadcastStatus();
  }

  // 3. INTERNET
  if(WiFi.status() == WL_CONNECTED) {
     if (Blynk.connected()) Blynk.run();
     else if(millis()%5000==0) Blynk.connect();

     if (!mqttClient.connected()) {
        if(millis()%5000==0) mqttClient.connect("Root_Blynk_Ctrl");
     } else {
        mqttClient.loop();
     }
  }
}
