#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID   "TMPL6gJnYJIQa"
#define BLYNK_TEMPLATE_NAME "Library Mesh Monitor"
#define BLYNK_AUTH_TOKEN    "AKio8PcuKqb-DRQAauhZOPCTL0w29qs5"

#include <Arduino.h>
#include <painlessMesh.h>
#include <BlynkSimpleEsp32.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

//WiFi & MQTT
const char* ssid        = "fadhlureza";
const char* pass        = "acebkutek";
const char* mqtt_server = "broker.hivemq.com";
const char* mqtt_topic  = "kampus/library/kelompok14/data";

//Mesh
#define   MESH_PREFIX     "MESH_BARU_FIX" 
#define   MESH_PASSWORD   "12345678"
#define   MESH_PORT       7777

painlessMesh  mesh;
WiFiClient    espClient;
PubSubClient  mqttClient(espClient);

//RTOS
TaskHandle_t TaskMeshHandle;
TaskHandle_t TaskInternetHandle;
QueueHandle_t msgQueue;

struct NodeMessage { char payload[512]; };

volatile bool isSystemActive = true; 
unsigned long lastBroadcast = 0;

//Kirim status ke semua node
void broadcastStatus() {
  DynamicJsonDocument doc(256);
  doc["cmd"] = isSystemActive ? "WAKE" : "SLEEP"; 
  String msg;
  serializeJson(doc, msg);
  mesh.sendBroadcast(msg);
}

void receivedCallback(uint32_t from, String &msg) {
  NodeMessage data;
  msg.toCharArray(data.payload, 512);
  xQueueSend(msgQueue, &data, 0);
  Serial.printf("MESH RX: %s\n", data.payload);
}

//Toggle Blynk
BLYNK_WRITE(V3) {
  int value = param.asInt(); 
  isSystemActive = (value == 1);
  Serial.println(isSystemActive ? ">>> BLYNK CMD: ON" : ">>> BLYNK CMD: OFF");
  broadcastStatus();
}

void TaskMesh(void *pvParameters) {
  for (;;) {
    mesh.update();
    if (millis() - lastBroadcast > 1500) {
      lastBroadcast = millis();
      broadcastStatus();
    }
    vTaskDelay(1 / portTICK_PERIOD_MS); 
  }
}

//koneksi internet dan proses data
void TaskInternet(void *pvParameters) {
  mqttClient.setServer(mqtt_server, 1883);
  Blynk.config(BLYNK_AUTH_TOKEN);

  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      if (Blynk.connected()) Blynk.run();
      else {
        static unsigned long lastBlynk = 0;
        if (millis() - lastBlynk > 5000) { lastBlynk = millis(); Blynk.connect(); }
      }
      if (mqttClient.connected()) mqttClient.loop();
      else {
        static unsigned long lastMqtt = 0;
        if (millis() - lastMqtt > 5000) { 
           lastMqtt = millis(); 
           String clientId = "Root_" + String(random(0xffff), HEX);
           mqttClient.connect(clientId.c_str()); 
        }
      }
    }

//Ambil data dari node
    NodeMessage incomingData;
    if (xQueueReceive(msgQueue, &incomingData, 0) == pdTRUE) {
      String msg = String(incomingData.payload);
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, msg);

      if (!error && doc.containsKey("noise")) {
        uint32_t nodeId = doc["node"]; 
        int noise       = doc["noise"];
        float vib       = doc["vib"];

        if (nodeId != 0) {
           if (Blynk.connected()) {
             if (nodeId == 135941613) { 
               Blynk.virtualWrite(V0, noise);
               Blynk.virtualWrite(V1, vib);
               Blynk.virtualWrite(V2, nodeId);
             }
             else if (nodeId == 4267557053) { 
               Blynk.virtualWrite(V4, noise);
               Blynk.virtualWrite(V5, nodeId);
             }
             else {
               Blynk.virtualWrite(V5, nodeId);
             }
           }
           if (mqttClient.connected()) {
             mqttClient.publish(mqtt_topic, msg.c_str());
           }
        }
      }
    }
    
// Kontrol manual via serial
    if (Serial.available()) {
       String input = Serial.readStringUntil('\n');
       input.trim(); input.toUpperCase();
       if (input == "OFF") { isSystemActive = false; if(Blynk.connected()) Blynk.virtualWrite(V3, 0); }
       if (input == "ON")  { isSystemActive = true;  if(Blynk.connected()) Blynk.virtualWrite(V3, 1); }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// Setup awal
void setup() {
  Serial.begin(115200);
  msgQueue = xQueueCreate(10, sizeof(NodeMessage));

  mesh.setDebugMsgTypes(ERROR | STARTUP); 
  mesh.init(MESH_PREFIX, MESH_PASSWORD, MESH_PORT);
  mesh.stationManual(ssid, pass); 
  mesh.setRoot(true);
  mesh.setContainsRoot(true); 
  mesh.onReceive(&receivedCallback);

  Serial.println(">>> ROOT RTOS STARTED");

  xTaskCreatePinnedToCore(TaskMesh, "MeshTask", 10000, NULL, 2, &TaskMeshHandle, 1);
  xTaskCreatePinnedToCore(TaskInternet, "NetTask", 10000, NULL, 1, &TaskInternetHandle, 1);
}

// Loop tidak dipakai
void loop() {
  vTaskDelete(NULL);
}