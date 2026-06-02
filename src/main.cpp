#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "DHT.h"

// Configuración de Pines
#define DHTPIN 32
#define DHTTYPE DHT22
#define FAN_PWM_PIN 25 // Pin de señal PWM al Hub/Ventilador

// Parámetros PWM para el ESP32 (Periférico LEDC)
const int pwmChannel = 0;
const int pwmFreq = 25000; // 25 kHz (frecuencia estándar para fan de PC)
const int pwmResolution = 8; // 8 bits de resolución (0 a 255)

// Umbrales para el Modo Automático
const float TEMP_MIN = 25.0; // Ventilador al mínimo/apagado
const float TEMP_MAX = 35.0; // Ventilador al 100%

// Variables globales de control (Sincronizadas)
float temperatura_actual = 0.0;
bool isAutoMode = true;        // Por defecto inicia en Automático
int fanSpeedPercent = 0;       // Velocidad del fan en % (0-100)

DHT dht(DHTPIN, DHTTYPE);
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Función para aplicar la velocidad al periférico PWM
void setFanSpeed(int percent) {
    fanSpeedPercent = constrain(percent, 0, 100);
    // Mapeamos de 0-100% a 0-255 (resolución de 8 bits)
    int dutyCycle = map(fanSpeedPercent, 0, 100, 0, 255);
    ledcWrite(pwmChannel, dutyCycle);
}

// Envío de actualizaciones de estado a la UI
void broadcastSystemStatus() {
    StaticJsonDocument<200> doc;
    doc["type"] = "status_update";
    doc["temperature"] = isnan(temperatura_actual) ? 0.0 : temperatura_actual;
    doc["autoMode"] = isAutoMode;
    doc["fanSpeed"] = fanSpeedPercent;

    String response;
    serializeJson(doc, response);
    ws.textAll(response);
}

// Manejador de mensajes entrantes desde la Web (WebSockets)
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
        data[len] = 0;
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, data);
        
        if (!error) {
            if (doc.containsKey("action")) {
                String action = doc["action"].as<String>();
                
                if (action == "setMode") {
                    isAutoMode = doc["value"].as<bool>();
                    Serial.printf("Modo cambiado a: %s\n", isAutoMode ? "AUTOMATICO" : "MANUAL");
                } 
                else if (action == "setSpeed" && !isAutoMode) {
                    // Solo permite cambiar velocidad manualmente si NO está en modo automático
                    int speed = doc["value"].as<int>();
                    setFanSpeed(speed);
                    Serial.printf("Velocidad manual seteada a: %d%%\n", speed);
                }
                // Forzamos un broadcast para mantener todas las pestañas web sincronizadas
                broadcastSystemStatus();
            }
        }
    }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("Cliente WebSocket conectado desde %s\n", client->remoteIP().toString().c_str());
            broadcastSystemStatus(); // Envía el estado actual al conectar
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("Cliente WebSocket desconectado\n");
            break;
        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;
        default:
            break;
    }
}

// Tarea FreeRTOS para lectura de sensores y control automático
void Task_Control(void *pvParameters) {
    for (;;) {
        float t = dht.readTemperature();
        if (!isnan(t)) {
            temperatura_actual = t;
            
            // Lógica de control en Tiempo Real para el Modo Automático
            if (isAutoMode) {
                if (temperatura_actual < TEMP_MIN) {
                    setFanSpeed(0);
                } else if (temperatura_actual > TEMP_MAX) {
                    setFanSpeed(100);
                } else {
                    // Mapeo lineal proporcional entre los umbrales de temperatura
                    int calculatedSpeed = map(temperatura_actual, TEMP_MIN, TEMP_MAX, 0, 100);
                    setFanSpeed(calculatedSpeed);
                }
            }
        }
        
        // Transmitir datos de manera periódica a la UI
        broadcastSystemStatus();
        
        // Tasa de muestreo (Polling rate) estable
        vTaskDelay(pdMS_TO_TICKS(2000)); 
    }
}

void Task_WebSockets(void *pvParameters) {
    for (;;) {
        ws.cleanupClients();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup() {
    Serial.begin(115200);
    
    // Inicializar LittleFS y DHT
    if(!LittleFS.begin(true)){ Serial.println("Error al montar LittleFS"); return; }
    dht.begin();

    // Configuración del periférico LEDC para el Fan PWM
    ledcSetup(pwmChannel, pwmFreq, pwmResolution);
    ledcAttachPin(FAN_PWM_PIN, pwmChannel);
    setFanSpeed(0); // Inicia apagado

    // Configuración de Red Access Point
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    Serial.print("IP del servidor: "); Serial.println(WiFi.softAPIP());

    // Configurar rutas del Servidor Web y WebSockets
    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();

    // Crear tareas concurrentes en FreeRTOS
    xTaskCreatePinnedToCore(Task_Control, "ControlYRead", 4096, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(Task_WebSockets, "WS_Clean", 2048, NULL, 1, NULL, 0);
}

void loop() {
    vTaskDelete(NULL); // El bucle principal se elimina para ceder el control total a FreeRTOS
}