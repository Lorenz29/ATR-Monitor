#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config.h"
#include "DHT.h"

// --- Definición de Pines ---
#define LED_PIN          33   // Led testigo integrado o de placa
#define ALARMA_LED_PIN   27   // Control del transistor para la baliza de 9 LEDs
#define EXTRACTOR_PIN    25   // Salida PWM hacia el Hub de ventiladores
#define DHTPIN           32   // Pin de datos del sensor DHT22
#define MQ2_PIN          34   // Pin analógico ADC para el sensor de humo MQ-2

// --- Configuraciones de Periféricos ---
#define DHTTYPE          DHT22
DHT dht(DHTPIN, DHTTYPE);

// Configuración PWM para el Extractor (Intel Spec: 25kHz)
#define PWM_FREQ         25000 
#define PWM_CHANNEL      0     
#define PWM_RES          8     // 8 bits de resolución (valores de 0 a 255)

// --- Constantes de Regla de Negocio (Sincronizadas con script.js) ---
const int UMBRAL_ALERTA = 550;   // Umbral de disparo del MQ-2
const int PERSISTENCIA_MAX = 5;  // Muestras consecutivas requeridas para confirmar incendio

// --- Variables Globales Compartidas (Protegidas lógicamente) ---
float temperaturaActual = 0.0;
int valorMq2Filtrado = 0;
bool alarmaIncendioActiva = false;

// Variables de control del Extractor
bool isAutoMode = true;
int manualSpeedDuty = 0; // Guardará el valor enviado desde la interfaz web (0-255)

// --- Servidor y WebSockets ---
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// --- Función para transmitir telemetría por WebSockets ---
void broadcastSystemStatus() {
    StaticJsonDocument<200> doc;
    doc["type"] = "system_update";
    doc["temp"] = isnan(temperaturaActual) ? 0.0 : temperaturaActual;
    doc["mq2"] = valorMq2Filtrado;
    doc["alarm"] = alarmaIncendioActiva;
    doc["autoMode"] = isAutoMode;
    
    // Convertimos el duty actual a porcentaje (0-100) para el slider de la interfaz
    int currentDuty = ledcRead(PWM_CHANNEL);
    doc["fanSpeed"] = map(currentDuty, 0, 255, 0, 100);

    String response;
    serializeJson(doc, response);
    ws.textAll(response);
}

// --- Manejador de Eventos WebSocket (Entrada desde la UI Web) ---
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            data[len] = 0;
            String message = (char*)data;
            
            StaticJsonDocument<150> doc;
            DeserializationError error = deserializeJson(doc, message);
            if (error) return;

            // Procesamiento de comandos enviados desde el Frontend
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
                // Sincronizamos a todos los clientes conectados inmediatamente
                broadcastSystemStatus();
            }
        }
    }
}

// ============================================================================
// --- TAREAS CONCURRENTES DE FreeRTOS ---
// ============================================================================

// Tarea 1: Monitoreo periódico del Sensor Ambiental DHT22
void Task_Ambiental(void *pvParameters) {
    for (;;) {
        float t = dht.readTemperature();
        if (!isnan(t)) {
            temperaturaActual = t;
        }
        
        // Transmitimos las actualizaciones del sistema
        broadcastSystemStatus();
        
        // El DHT22 no lee más rápido que cada 2 segundos físicamente
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// Tarea 2: Procesamiento de Señal MQ-2 (Filtro de Promedio Móvil + Persistencia)
void Task_SeguridadIncendio(void *pvParameters) {
    const int N = 8; // Tamaño de la ventana del promedio móvil
    int muestras[N] = {0};
    int indiceSample = 0;
    long sumaMuestras = 0;
    int contadorPersistencia = 0;

    // Inicialización del arreglo de filtrado
    for(int i=0; i<N; i++) {
        muestras[i] = analogRead(MQ2_PIN);
        sumaMuestras += muestras[i];
    }

    for (;;) {
        // 1. Filtro de Promedio Móvil para remover ruido eléctrico de la protoboard
        sumaMuestras -= muestras[indiceSample];
        muestras[indiceSample] = analogRead(MQ2_PIN);
        sumaMuestras += muestras[indiceSample];
        indiceSample = (indiceSample + 1) % N;

        valorMq2Filtrado = sumaMuestras / N;

        // 2. Algoritmo Determinístico de Persistencia para Evitar Lecturas Espurias
        if (valorMq2Filtrado > UMBRAL_ALERTA) {
            if (contadorPersistencia < PERSISTENCIA_MAX) {
                contadorPersistencia++;
            }
        } else {
            if (contadorPersistencia > 0) {
                contadorPersistencia--;
            }
        }

        // Evaluación de Estado Crítico
        if (contadorPersistencia >= PERSISTENCIA_MAX) {
            if (!alarmaIncendioActiva) {
                alarmaIncendioActiva = true;
                Serial.println("[ALERTA CRÍTICA] Incendio confirmado por persistencia.");
            }
        } else if (contadorPersistencia == 0) {
            if (alarmaIncendioActiva) {
                alarmaIncendioActiva = false;
                Serial.println("[INFO] Estado de peligro normalizado.");
            }
        }

        // 3. Control de Actuadores Locales de Alarma
        if (alarmaIncendioActiva) {
            digitalWrite(LED_PIN, HIGH);
            digitalWrite(ALARMA_LED_PIN, HIGH); // Activa transistor de la baliza de 9 LEDs
        } else {
            digitalWrite(LED_PIN, LOW);
            digitalWrite(ALARMA_LED_PIN, LOW);
        }

        // Tasa de muestreo del MQ-2: 100ms (10 muestras por segundo garantiza respuesta en Tiempo Real)
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Tarea 3: Control Lógico del Extractor de Aire (Lazo de Control / Lógica Manual)
void Task_ControlExtractor(void *pvParameters) {
    for (;;) {
        if (alarmaIncendioActiva) {
            // Regla de máxima prioridad por seguridad: Extractor a fondo si hay humo
            ledcWrite(PWM_CHANNEL, 255);
        } 
        else if (isAutoMode) {
            // Lazo de control automático basado en temperatura (Mapeo determinístico)
            // Ejemplo de regla: de 25°C (Fan apagado) a 35°C (Fan al 100%)
            if (temperaturaActual < 25.0) {
                ledcWrite(PWM_CHANNEL, 0);
            } else if (temperaturaActual > 35.0) {
                ledcWrite(PWM_CHANNEL, 255);
            } else {
                int dutyCalculado = map(temperaturaActual, 25, 35, 0, 255);
                ledcWrite(PWM_CHANNEL, dutyCalculado);
            }
        } 
        else {
            // Modo Manual: Acata directamente el último valor retenido desde la UI Web
            ledcWrite(PWM_CHANNEL, manualSpeedDuty);
        }

        // Ajuste de lazo de control cada 500ms
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ============================================================================
// --- CONFIGURACIÓN INICIAL (SETUP) ---
// ============================================================================
void setup() {
    Serial.begin(115200);

    // Inicialización de Salidas Digitales
    pinMode(LED_PIN, OUTPUT);
    pinMode(ALARMA_LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);
    digitalWrite(ALARMA_LED_PIN, LOW);

    // Configuración del periférico LEDC (Hardware PWM dedicado para Fan)
    ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
    ledcAttachPin(EXTRACTOR_PIN, PWM_CHANNEL);
    ledcWrite(PWM_CHANNEL, 0); // Arranca apagado

    // Inicialización del Sensor DHT22
    dht.begin();

    // Inicialización del Sistema de Archivos LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("Error al montar LittleFS");
        return;
    }

    // Configuración de Red (Access Point)
    WiFi.softAP(ssid, password);
    Serial.print("Access Point levantado. IP: ");
    Serial.println(WiFi.softAPIP());

    // Inicialización de Servidor Web y Rutas Estáticas
    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server.begin();
    Serial.println("Servidor web asincrónico corriendo de manera autónoma.");

    // Creación de Tareas FreeRTOS en el Planificador Concurrente
    xTaskCreatePinnedToCore(Task_Ambiental, "Task_Ambiental", 3072, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(Task_SeguridadIncendio, "Task_Incendio", 3072, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(Task_ControlExtractor, "Task_Extractor", 2048, NULL, 2, NULL, 1);
}

void loop() {
    // El loop se deja completamente libre eliminando hilos bloqueantes. 
    // FreeRTOS toma control absoluto del procesador de forma determinística.
    vTaskDelete(NULL);
}