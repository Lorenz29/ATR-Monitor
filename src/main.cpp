#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "DHT.h"

#define LED_PIN 33
#define EXTRACTOR_PIN 26
#define DHTPIN 32
#define DHTTYPE DHT22

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --- Función para enviar datos a la web ---
void broadcastSensorData() {
  StaticJsonDocument<100> doc;
  float t = dht.readTemperature();

  if (!isnan(t)) {
    doc["type"] = "sensor_update";
    doc["value"] = t;
    
    String response;
    serializeJson(doc, response);
    ws.textAll(response); 
    
    // Imprimimos en serie para saber que funciona
    Serial.printf("Temperatura enviada: %.1f °C\n", t);
  }
}

// --- Manejador de mensajes WebSocket (Entrada) ---
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    data[len] = 0;
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, (char*)data);

    if (!error) {
      String action = doc["action"].as<String>();
      
      if (action == "toggle_extractor") {
        digitalWrite(EXTRACTOR_PIN, !digitalRead(EXTRACTOR_PIN));
        Serial.println("Cambio estado Extractor");
      }
      // AGREGAMOS EL LED AQUÍ PARA QUE FUNCIONE POR WEBSOCKET
      else if (action == "toggle_led") {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        Serial.println("Cambio estado LED");
      }
    }
  }
}

// --- TAREAS FREERTOS ---
void Task_DHT(void *pvParameters) {
  dht.begin();
  for (;;) {
    broadcastSensorData();
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }
}

void Task_WS_Cleanup(void *pvParameters) {
  for (;;) {
    ws.cleanupClients();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  pinMode(EXTRACTOR_PIN, OUTPUT);

  if(!LittleFS.begin(true)) return;

  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.softAP(ssid, password);

  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();

  // Iniciamos las dos tareas necesarias
  xTaskCreatePinnedToCore(Task_DHT, "DHT", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(Task_WS_Cleanup, "WS_Clean", 2048, NULL, 1, NULL, 0);
}

void loop() {
  vTaskDelete(NULL);
}