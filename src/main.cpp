#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "DHT.h"

// --- Configuración de Pines ---
#define LED_PIN 33
#define EXTRACTOR_PIN 26
#define DHTPIN 32
#define DHTTYPE DHT22
#define MQ2_PIN 34  // GPIO seguro para lectura analógica (ADC1_CH6)

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --- Variables Globales Sincronizadas ---
float temperaturaActual = 0.0;
int humoFiltradoGlobal = 0;

// --- Configuración del Filtro Promedio Móvil para MQ-2 ---
const int VENTANA_MUESTRAS = 10;
int muestrasMQ2[VENTANA_MUESTRAS];
int indiceMuestra = 0;
long sumaMuestras = 0;

// --- Función para enviar datos unificados a la web ---
void broadcastSensorData() {
  // Ajustamos el tamaño del JSON a 200 para soportar múltiples variables holgadamente
  StaticJsonDocument<200> doc;
  
  doc["type"] = "sensor_update";
  doc["temperature"] = isnan(temperaturaActual) ? 0.0 : temperaturaActual;
  doc["smoke"] = humoFiltradoGlobal;
  
  String response;
  serializeJson(doc, response);
  ws.textAll(response); 
  
  // Impresión en consola serie para debug
  Serial.printf("[STR] Datos Enviados -> Temp: %.1f °C | Humo Filtrado: %d\n", temperaturaActual, humoFiltradoGlobal);
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
      else if (action == "toggle_led") {
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        Serial.println("Cambio estado LED");
      }
    }
  }
}

// --- TAREAS FREERTOS ---

// Tarea para leer la temperatura (baja prioridad/frecuencia)
void Task_DHT(void *pvParameters) {
  dht.begin();
  for (;;) {
    temperaturaActual = dht.readTemperature();
    // Enviamos la actualización consolidada a la web
    broadcastSensorData();
    vTaskDelay(pdMS_TO_TICKS(2000)); // Muestreo cada 2 segundos
  }
}

// Tarea crítica para el sensor de humo (Alta prioridad y frecuencia)
void Task_MQ2(void *pvParameters) {
  // Inicializamos el array de muestras en 0
  for (int i = 0; i < VENTANA_MUESTRAS; i++) muestrasMQ2[i] = 0;
  
  const int UMBRAL_ALERTA = 550; // Valor ADC límite de peligro (0 a 4095)
  int contadorPersistencia = 0;
  const int PERSISTENCIA_REQUERIDA = 5; // Muestras seguidas que deben superar el umbral

  for (;;) {
    int lecturaCruda = analogRead(MQ2_PIN);

    // 1. Aplicamos Filtro de Promedio Móvil
    sumaMuestras -= muestrasMQ2[indiceMuestra];
    muestrasMQ2[indiceMuestra] = lecturaCruda;
    sumaMuestras += muestrasMQ2[indiceMuestra];
    indiceMuestra = (indiceMuestra + 1) % VENTANA_MUESTRAS;

    humoFiltradoGlobal = sumaMuestras / VENTANA_MUESTRAS;

    // 2. Lógica Concurrente de Seguridad (Persistencia)
    if (humoFiltradoGlobal > UMBRAL_ALERTA) {
      contadorPersistencia++;
      if (contadorPersistencia >= PERSISTENCIA_REQUERIDA) {
        // ACCIÓN DETERMINÍSTICA DE TIEMPO REAL: Peligro de incendio inminente
        if (digitalRead(EXTRACTOR_PIN) == LOW) {
          digitalWrite(EXTRACTOR_PIN, HIGH); // Forzamos extractor para evacuar humo
          Serial.println("[ALERTA CRÍTICA] ¡Humo detectado! Extractor activado por hardware.");
        }
      }
    } else {
      contadorPersistencia = 0; // Se resetea el contador ante fluctuaciones o caídas de humo
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Muestreo rápido cada 100ms
  }
}

// Tarea para limpiar clientes WebSocket caídos
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
  pinMode(MQ2_PIN, INPUT); // Configurado como entrada analógica

  if(!LittleFS.begin(true)) return;

  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.softAP(ssid, password);

  ws.onEvent(onEvent);
  server.addHandler(&ws);

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
  server.begin();

  // --- Creación Balanceada de Tareas ---
  // Core 1: Encargado de los Sensores y Periféricos
  xTaskCreatePinnedToCore(Task_DHT, "DHT", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(Task_MQ2, "MQ2_Humo", 3072, NULL, 3, NULL, 1); // Mayor prioridad por ser crítico

  // Core 0: Encargado del stack de conectividad de red y WebSockets
  xTaskCreatePinnedToCore(Task_WS_Cleanup, "WS_Clean", 2048, NULL, 1, NULL, 0);
}

void loop() {
  vTaskDelete(NULL); // Eliminamos el loop por defecto para ceder el 100% a las tareas FreeRTOS
}