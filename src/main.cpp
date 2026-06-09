/**
 * @file main.cpp
 * @brief ATR Monitor - Sistema de Control para Sala de Servidores
 * @author [Tu Nombre/Usuario]
 * @version 2.0.0
 * * Este proyecto utiliza FreeRTOS para procesamiento concurrente en Tiempo Real,
 * controlando la temperatura, calidad del aire (humo) y potencia mediante un ESP32.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "DHT.h"

// ============================================================================
// --- DEFINICIÓN DE PINES (HARDWARE MAP) ---
// ============================================================================
#define LED_PIN          33   // Led testigo integrado o de placa
#define ALARMA_LED_PIN   27   // Control del transistor para la baliza de 9 LEDs
#define EXTRACTOR_PIN    25   // Salida PWM hacia el Hub de ventiladores
#define DHTPIN           32   // Pin de datos del sensor DHT22
#define MQ2_PIN          34   // Pin analógico ADC para el sensor de humo MQ-2

// Pines de control de Potencia (Módulo de Relés HL-58s)
#define RELAY1_ATX_PIN   21   // Control de encendido/apagado de Fuente ATX
#define RELAY2_LINE1_PIN 13   // Canal genérico para Línea Genérica 1 (Futuro 12V)
#define RELAY3_LINE2_PIN 14   // Canal genérico para Línea Genérica 2 (Futuro 12V)

// ============================================================================
// --- CONFIGURACIÓN DE PERIFÉRICOS ---
// ============================================================================
#define DHTTYPE          DHT22
DHT dht(DHTPIN, DHTTYPE);

// Configuración PWM para el Extractor (Intel Spec: 25kHz)
#define PWM_FREQ         25000 
#define PWM_CHANNEL      0     
#define PWM_RES          8     // 8 bits de resolución (0 a 255)

// ============================================================================
// --- CONSTANTES Y VARIABLES GLOBALES DE NEGOCIO ---
// ============================================================================
const int PERSISTENCIA_MAX = 5;  // Muestras consecutivas para confirmar incendio

// Sensores Ambientales
float temperaturaActual = 0.0;
int valorMq2Filtrado = 0;
bool alarmaIncendioActiva = false;

// Variables de Calibración Dinámica del MQ-2
int umbralAlerta = 1000;         
int offsetMq2 = 300;             
bool mq2Calibrado = false;
const unsigned long TIEMPO_CALIBRACION = 300000; // 5 min (300,000 ms)

// Variables globales para la lógica de Snooze
unsigned long snoozeStartTime = 0;
const unsigned long SNOOZE_DURATION = 60000; // 60 segundos de silencio
bool enSnooze = false;

// Variables de estado sincronizadas con la UI
bool estadoLedPlaca = false;
bool estadoRelay1Atx = false;
bool estadoRelay2Line1 = false;
bool estadoRelay3Line2 = false;

// Variables de control del Extractor
bool isAutoMode = true;
int manualSpeedDuty = 0;

// Servidor Web y WebSockets
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ============================================================================
// --- COMUNICACIÓN WEBSOCKETS ---
// ============================================================================

/**
 * @brief Envía toda la telemetría del sistema a los clientes conectados.
 */
void broadcastSystemStatus() {
    // Aumentado a 1024 bytes para soportar más módulos
    StaticJsonDocument<1024> doc;
    doc["type"] = "system_update";
    
    // 1. Módulo Ventilación
    doc["temp"] = isnan(temperaturaActual) ? 0.0 : temperaturaActual;
    doc["autoMode"] = isAutoMode;
    int currentDuty = ledcRead(PWM_CHANNEL);
    doc["fanSpeed"] = map(currentDuty, 0, 255, 0, 100);

    // 2. Módulo Alarma (MQ-2)
    doc["mq2"] = valorMq2Filtrado;
    doc["alarm"] = alarmaIncendioActiva;
    doc["umbral"] = umbralAlerta;
    doc["calibrado"] = mq2Calibrado;
    doc["tiempoCal"] = mq2Calibrado ? 0 : (TIEMPO_CALIBRACION - millis()) / 1000;
    
    // 3. Módulo Control de Energía
    doc["relay1"] = estadoRelay1Atx;
    doc["relay2"] = estadoRelay2Line1;
    doc["relay3"] = estadoRelay3Line2;
    // Valores simulados de V/I. (A futuro, reemplazar con lectura real de sensores ej: ACS712)
    doc["voltA"] = 12.2;
    doc["currA"] = (estadoRelay2Line1) ? 1.5 : 0.0; 
    doc["voltB"] = 12.1;
    doc["currB"] = (estadoRelay3Line2) ? 3.2 : 0.0;

    // 4. Módulo Iluminación
    doc["ledStatus"] = estadoLedPlaca;

    String response;
    serializeJson(doc, response);
    ws.textAll(response);
}

/**
 * @brief Recepción y procesamiento de comandos desde el frontend.
 */
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            String message = (char*)data;
            
            StaticJsonDocument<256> doc;
            DeserializationError error = deserializeJson(doc, message);
            if (error) return;

            if (doc.containsKey("action")) {
                String action = doc["action"];
                
                if (action == "setMode") {
                    isAutoMode = doc["value"];
                } 
                else if (action == "setSpeed" && !isAutoMode) {
                    int percent = doc["value"];
                    manualSpeedDuty = map(percent, 0, 100, 0, 255);
                    ledcWrite(PWM_CHANNEL, manualSpeedDuty);
                }
                else if (action == "setUmbral") {
                    umbralAlerta = doc["value"];
                    mq2Calibrado = true;
                    Serial.printf("[MQ-2] Umbral manual UI: %d\n", umbralAlerta);
                }
                else if (action == "toggleLed") {
                    estadoLedPlaca = !estadoLedPlaca;
                    if (!alarmaIncendioActiva) digitalWrite(LED_PIN, estadoLedPlaca ? HIGH : LOW);
                }
                else if (action == "toggleRelay1") {
                    estadoRelay1Atx = !estadoRelay1Atx;
                    digitalWrite(RELAY1_ATX_PIN, estadoRelay1Atx ? LOW : HIGH);
                }
                else if (action == "toggleRelay2") {
                    estadoRelay2Line1 = !estadoRelay2Line1;
                    digitalWrite(RELAY2_LINE1_PIN, estadoRelay2Line1 ? LOW : HIGH);
                }
                else if (action == "toggleRelay3") {
                    estadoRelay3Line2 = !estadoRelay3Line2;
                    digitalWrite(RELAY3_LINE2_PIN, estadoRelay3Line2 ? LOW : HIGH);
                }
                else if (action == "stopAlarm") {
                    enSnooze = true;
                    snoozeStartTime = millis();
                    alarmaIncendioActiva = false;
                    digitalWrite(ALARMA_LED_PIN, LOW);
                    digitalWrite(LED_PIN, estadoLedPlaca ? HIGH : LOW);
                }
                
                broadcastSystemStatus();
            }
        }
    }
}

// ============================================================================
// --- TAREAS CONCURRENTES DE FreeRTOS ---
// ============================================================================

void Task_Ambiental(void *pvParameters) {
    for (;;) {
        float t = dht.readTemperature();
        if (!isnan(t)) temperaturaActual = t;
        broadcastSystemStatus();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void Task_SeguridadIncendio(void *pvParameters) {
    const int N = 8; 
    int muestras[N] = {0};
    int indiceSample = 0;
    long sumaMuestras = 0;
    int contadorPersistencia = 0;

    for(int i=0; i<N; i++) {
        muestras[i] = analogRead(MQ2_PIN);
        sumaMuestras += muestras[i];
    }

    for (;;) {
        // Promedio Móvil
        sumaMuestras -= muestras[indiceSample];
        muestras[indiceSample] = analogRead(MQ2_PIN);
        sumaMuestras += muestras[indiceSample];
        indiceSample = (indiceSample + 1) % N;
        valorMq2Filtrado = sumaMuestras / N;

        // Auto-Calibración
        if (!mq2Calibrado && (millis() > TIEMPO_CALIBRACION)) {
            mq2Calibrado = true;
            umbralAlerta = valorMq2Filtrado + offsetMq2;
        }

        // Persistencia
        if (valorMq2Filtrado > umbralAlerta) {
            if (contadorPersistencia < PERSISTENCIA_MAX) contadorPersistencia++;
        } else {
            if (contadorPersistencia > 0) contadorPersistencia--;
        }

        // Estados
        if (contadorPersistencia >= PERSISTENCIA_MAX && !alarmaIncendioActiva && !enSnooze) {
            alarmaIncendioActiva = true;
        } else if (contadorPersistencia == 0 && alarmaIncendioActiva) {
            alarmaIncendioActiva = false;
        }

        if (enSnooze && (millis() - snoozeStartTime > SNOOZE_DURATION)) {
            enSnooze = false;
        }

        // Actuadores Críticos
        if (alarmaIncendioActiva && !enSnooze) {
            digitalWrite(LED_PIN, HIGH);
            digitalWrite(ALARMA_LED_PIN, HIGH);
        } else if (!alarmaIncendioActiva) {
            digitalWrite(LED_PIN, estadoLedPlaca ? HIGH : LOW);
            digitalWrite(ALARMA_LED_PIN, LOW);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void Task_ControlExtractor(void *pvParameters) {
    for (;;) {
        if (alarmaIncendioActiva) {
            ledcWrite(PWM_CHANNEL, 255);
        } 
        else if (isAutoMode) {
            if (temperaturaActual < 25.0) ledcWrite(PWM_CHANNEL, 0);
            else if (temperaturaActual > 35.0) ledcWrite(PWM_CHANNEL, 255);
            else ledcWrite(PWM_CHANNEL, map(temperaturaActual, 25, 35, 0, 255));
        } 
        else {
            ledcWrite(PWM_CHANNEL, manualSpeedDuty);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ============================================================================
// --- CONFIGURACIÓN INICIAL ---
// ============================================================================
void setup() {
    Serial.begin(115200);

    digitalWrite(RELAY1_ATX_PIN, HIGH);
    digitalWrite(RELAY2_LINE1_PIN, HIGH);
    digitalWrite(RELAY3_LINE2_PIN, HIGH);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(ALARMA_LED_PIN, LOW);

    pinMode(RELAY1_ATX_PIN, OUTPUT);
    pinMode(RELAY2_LINE1_PIN, OUTPUT);
    pinMode(RELAY3_LINE2_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    pinMode(ALARMA_LED_PIN, OUTPUT);

    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
    ledcAttachPin(EXTRACTOR_PIN, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0); 

    dht.begin();

    if (!LittleFS.begin(true)) {
        Serial.println("Error al montar LittleFS");
        return;
    }

    WiFi.softAP(ssid, password);
    Serial.print("Access Point levantado. IP: ");
    Serial.println(WiFi.softAPIP());

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.begin();

    xTaskCreatePinnedToCore(Task_Ambiental, "Task_Ambiental", 3072, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(Task_SeguridadIncendio, "Task_Incendio", 3072, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(Task_ControlExtractor, "Task_Extractor", 2048, NULL, 2, NULL, 1);
}

void loop() { vTaskDelete(NULL); }