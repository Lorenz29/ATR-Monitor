#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"

// Definición de pines
#define LED_PIN 33
#define EXTRACTOR_PIN 26

// Credenciales del Punto de Acceso
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Variables globales para simular datos
float temp_simulada = 24.0;

// Declaración de tareas FreeRTOS
void Task_Sensores(void *pvParameters);
void Task_WebSockets(void *pvParameters);

// Función para procesar mensajes WebSocket (Extractor)
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
      data[len] = 0;
      String message = (char*)data;
      Serial.printf("Mensaje recibido: %s\n", message.c_str());

      // Parseamos el JSON
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, message);

      if (!error) {
        if (doc.containsKey("action")) {
          String action = doc["action"].as<String>();
          
          if (action == "toggle_extractor") {
             int estadoActual = digitalRead(EXTRACTOR_PIN);
             digitalWrite(EXTRACTOR_PIN, !estadoActual);
             Serial.println("¡Comando ejecutado: Estado del extractor cambiado!");
          }
        }
      } else {
        Serial.println("Error al leer el JSON de WebSocket");
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(2000); // Esperamos 2 segundos antes de hacer nada
  Serial.println("Iniciando sistema...");
  
  // Configurar Pines
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Arranca apagado
  
  pinMode(EXTRACTOR_PIN, OUTPUT);
  digitalWrite(EXTRACTOR_PIN, LOW); // Arranca apagado

  // Inicializar LittleFS
  if(!LittleFS.begin(true)){
    Serial.println("Ocurrió un error al montar LittleFS");
    return;
  }

  // Configurar WiFi como Access Point
  Serial.println("Configurando Access Point...");
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Servidor montado. IP: http://");
  Serial.println(IP);

  // Configurar WebSockets
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  
  // Ruta GET para el LED
  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
    int estadoActual = digitalRead(LED_PIN);
    digitalWrite(LED_PIN, !estadoActual); 
    String respuesta = (!estadoActual) ? "ENCENDIDO" : "APAGADO";
    Serial.println("Acción ejecutada: LED " + respuesta);
    request->send(200, "text/plain", respuesta);
  });

  // Servir archivos estáticos
  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();

  // Iniciar Tareas FreeRTOS
  xTaskCreatePinnedToCore(Task_Sensores, "Sensores", 2048, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(Task_WebSockets, "WebSockets", 4096, NULL, 1, NULL, 0);
}

void loop() {
  vTaskDelete(NULL); // Eliminamos loop, usamos FreeRTOS
}

// --- TAREAS FREERTOS ---
void Task_Sensores(void *pvParameters) {
  for (;;) {
    temp_simulada += 0.5; 
    if(temp_simulada > 35.0) temp_simulada = 24.0; 

    StaticJsonDocument<100> doc;
    doc["temp"] = temp_simulada;
    String jsonString;
    serializeJson(doc, jsonString);
    ws.textAll(jsonString);

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

void Task_WebSockets(void *pvParameters) {
  for (;;) {
    ws.cleanupClients();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}