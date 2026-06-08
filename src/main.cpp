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

// Nuevos pines de control de Potencia (Módulo de Relés HL-58s)
#define RELAY1_ATX_PIN   21   // Control de encendido/apagado de Fuente ATX
#define RELAY2_LINE1_PIN 13   // Canal genérico para Línea Genérica 1 (Futuro 12V)
#define RELAY3_LINE2_PIN 14   // Canal genérico para Línea Genérica 2 (Futuro 12V)

// --- Configuraciones de Periféricos ---
#define DHTTYPE          DHT22
DHT dht(DHTPIN, DHTTYPE);

// Configuración PWM para el Extractor (Intel Spec: 25kHz)
#define PWM_FREQ         25000 
#define PWM_CHANNEL      0     
#define PWM_RES          8     // 8 bits de resolución (valores de 0 a 255)

// --- Constantes de Regla de Negocio ---
const int PERSISTENCIA_MAX = 5;  // Muestras consecutivas requeridas para confirmar incendio

// --- Variables Globales Compartidas (Protegidas lógicamente) ---
float temperaturaActual = 0.0;
int valorMq2Filtrado = 0;
bool alarmaIncendioActiva = false;

// Variables de Calibración Dinámica del MQ-2
int umbralAlerta = 1000;         // Valor seguro por defecto hasta que se calibre
int offsetMq2 = 300;             // Margen por encima del valor base del aire limpio
bool mq2Calibrado = false;
const unsigned long TIEMPO_CALIBRACION = 300000; // 5 minutos (300,000 ms)

// Variables globales para la lógica de Snooze
unsigned long snoozeStartTime = 0;
const unsigned long SNOOZE_DURATION = 60000; // 60 segundos de silencio
bool enSnooze = false;

// Variables de estado físicas sincronizadas con la UI
bool estadoLedPlaca = false;
bool estadoRelay1Atx = false;
bool estadoRelay2Line1 = false;
bool estadoRelay3Line2 = false;

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
    StaticJsonDocument<512> doc;
    doc["type"] = "system_update";
    doc["temp"] = isnan(temperaturaActual) ? 0.0 : temperaturaActual;
    doc["mq2"] = valorMq2Filtrado;
    doc["alarm"] = alarmaIncendioActiva;
    doc["autoMode"] = isAutoMode;
    
    // Estados de relés y luces compartidos hacia la interfaz web
    doc["ledStatus"] = estadoLedPlaca;
    doc["relay1"] = estadoRelay1Atx;
    doc["relay2"] = estadoRelay2Line1;
    doc["relay3"] = estadoRelay3Line2;
    
    // Variables dinámicas de calibración
    doc["umbral"] = umbralAlerta;
    doc["calibrado"] = mq2Calibrado;
    // Envía los segundos restantes si aún no está calibrado
    doc["tiempoCal"] = mq2Calibrado ? 0 : (TIEMPO_CALIBRACION - millis()) / 1000;
    
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
            
            StaticJsonDocument<256> doc;
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
                else if (action == "setUmbral") {
                    umbralAlerta = doc["value"];
                    mq2Calibrado = true; // Si el usuario interviene, forzamos el fin de la calibración auto
                    Serial.printf("[MQ-2] Umbral actualizado manualmente desde UI a: %d\n", umbralAlerta);
                }
                else if (action == "toggleLed") {
                    estadoLedPlaca = !estadoLedPlaca;
                    if (!alarmaIncendioActiva) {
                        digitalWrite(LED_PIN, estadoLedPlaca ? HIGH : LOW);
                    }
                }
                else if (action == "toggleRelay1") {
                    estadoRelay1Atx = !estadoRelay1Atx;
                    digitalWrite(RELAY1_ATX_PIN, estadoRelay1Atx ? LOW : HIGH);
                    Serial.printf("[ATX] Estado de la fuente conmutado a: %s\n", estadoRelay1Atx ? "ON" : "OFF");
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
                    Serial.println("[INFO] Alarma silenciada manualmente (Snooze 60s activado).");
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
        
        broadcastSystemStatus();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

// Tarea 2: Procesamiento de Señal MQ-2 (Filtro, Calibración Dinámica y Persistencia)
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
        // 1. Filtro de Promedio Móvil
        sumaMuestras -= muestras[indiceSample];
        muestras[indiceSample] = analogRead(MQ2_PIN);
        sumaMuestras += muestras[indiceSample];
        indiceSample = (indiceSample + 1) % N;

        valorMq2Filtrado = sumaMuestras / N;

        // 2. Lógica de Auto-Calibración No Bloqueante
        if (!mq2Calibrado) {
            if (millis() > TIEMPO_CALIBRACION) {
                mq2Calibrado = true;
                // Calculamos el nuevo umbral en base al aire limpio detectado + offset
                umbralAlerta = valorMq2Filtrado + offsetMq2;
                Serial.printf("[MQ-2] Calibración automática finalizada. Base: %d | Nuevo Umbral: %d\n", valorMq2Filtrado, umbralAlerta);
            }
        }

        // 3. Algoritmo Determinístico de Persistencia contra Umbral Dinámico
        if (valorMq2Filtrado > umbralAlerta) {
            if (contadorPersistencia < PERSISTENCIA_MAX) {
                contadorPersistencia++;
            }
        } else {
            if (contadorPersistencia > 0) {
                contadorPersistencia--;
            }
        }

        // 4. Evaluación de Estado Crítico
        if (contadorPersistencia >= PERSISTENCIA_MAX) {
            if (!alarmaIncendioActiva && !enSnooze) {
                alarmaIncendioActiva = true;
                Serial.println("[ALERTA CRÍTICA] Incendio confirmado.");
            }
        } else if (contadorPersistencia == 0) {
            if (alarmaIncendioActiva) {
                alarmaIncendioActiva = false;
                Serial.println("[INFO] Estado de peligro normalizado.");
            }
        }

        if (enSnooze && (millis() - snoozeStartTime > SNOOZE_DURATION)) {
            enSnooze = false;
            Serial.println("[INFO] Fin del modo Snooze. Alarma reactivada.");
        }

        // 5. Control de Actuadores
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

// Tarea 3: Control Lógico del Extractor de Aire
void Task_ControlExtractor(void *pvParameters) {
    for (;;) {
        if (alarmaIncendioActiva) {
            ledcWrite(PWM_CHANNEL, 255);
        } 
        else if (isAutoMode) {
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
            ledcWrite(PWM_CHANNEL, manualSpeedDuty);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// ============================================================================
// --- CONFIGURACIÓN INICIAL (SETUP) ---
// ============================================================================
void setup() {
    Serial.begin(115200);

    // 1. Primero fijamos el estado HIGH (Relés apagados)
    digitalWrite(RELAY1_ATX_PIN, HIGH);
    digitalWrite(RELAY2_LINE1_PIN, HIGH);
    digitalWrite(RELAY3_LINE2_PIN, HIGH);
    
    // Los LEDs sí se apagan con LOW, los dejamos así
    digitalWrite(LED_PIN, LOW);
    digitalWrite(ALARMA_LED_PIN, LOW);

    // 2. Ahora sí los declaramos como salidas (ya nacen apagados)
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
    Serial.println("Servidor web asincrónico corriendo de manera autónoma.");

    xTaskCreatePinnedToCore(Task_Ambiental, "Task_Ambiental", 3072, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(Task_SeguridadIncendio, "Task_Incendio", 3072, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(Task_ControlExtractor, "Task_Extractor", 2048, NULL, 2, NULL, 1);
}

void loop() {
    vTaskDelete(NULL);
}