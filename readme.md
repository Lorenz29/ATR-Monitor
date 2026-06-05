# ATR Monitor Sala Servidores

## Overview
Sistema de Control en Tiempo Real (STR) diseñado para el monitoreo ambiental y la gestión automatizada de actuadores en una sala de servidores. Desarrollado con arquitectura no bloqueante (FreeRTOS) para garantizar el determinismo y la concurrencia.

## Technical Specifications

### Hardware
- **Microcontrolador**: ESP32 (Lolin32)
- **Sensores**: 
  - DHT22 (Monitoreo de Temperatura y Humedad)
  - MQ-2 (Detección de Humo/Gases con divisor de tensión)
- **Actuadores**:
  - Módulo de Relés HL-58s (Control de Fuente ATX y líneas auxiliares de 12V)
  - Hub de ventiladores (Control PWM de velocidad)
  - Baliza visual de 9 LEDs y alarma sonora
- **Power**: Fuente ATX (Uso de línea Standby 5VSB para el ESP32 y 12V para actuadores de potencia)

### Software
- **Language**: C++ (Framework Arduino / PlatformIO)
- **Libraries**: FreeRTOS (nativo del ESP32), ESPAsyncWebServer, AsyncTCP, LittleFS, ArduinoJson, DHT sensor library
- **Monitoring**: Interfaz Web Asincrónica almacenada internamente (LittleFS) y actualizada vía WebSockets.

### Features
- Arquitectura basada en tareas de **FreeRTOS** para priorización y respuesta garantizada.
- **Auto-calibración Dinámica (Baseline)** y Filtro de Promedio Móvil para el sensor de humo MQ-2 (prevención de falsos positivos).
- Control de ventiladores por hardware PWM (LEDC) a **25kHz** (Cumplimiento de especificación Intel para reducción de ruido acústico).
- Gestión de estado "Snooze" (silenciado temporal de 60s) 100% no bloqueante usando `millis()`.
- Control seguro de encendido/apagado de Fuente ATX aislando lógicas de potencia.
- Dashboard web reactivo con alertas estroboscópicas de emergencia.

## Installation

1. Clonar el repositorio.
2. Abrir el proyecto en **VS Code** con la extensión **PlatformIO**.
3. Configurar las credenciales Wi-Fi: copiar `src/config_example.h` como `src/config.h` e ingresar SSID y Password.
4. Compilar y subir el sistema de archivos web: Ejecutar `Build Filesystem Image` y luego `Upload Filesystem Image`.
5. Compilar y subir el firmware principal al ESP32 (`Upload`).

## Configuration

Editar `src/config.h` para establecer:
- Credenciales de red (SSID y Password).
- Umbrales base (aunque el sistema soporta auto-calibración inicial).

## Usage

Conectar la fuente ATX al circuito asegurando la alimentación de 5VSB directa al ESP32 (sin conectar el USB en simultáneo). Ingresar a la IP mostrada en el monitor serie mediante cualquier navegador web para observar la telemetría en tiempo real y accionar manualmente relés y ventiladores.

## License
MIT License

## Contributing
Pull requests welcome. Favor de mantener los estándares de Sistemas de Tiempo Real (arquitecturas no bloqueantes, sin uso de `delay()`).