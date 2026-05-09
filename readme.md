# ATR Monitor Sala Servidores

## Overview
Monitoring system for server room environmental conditions using Arduino.

## Technical Specifications

### Hardware
- **Microcontroller**: Arduino (specify model: UNO/Mega/etc.)
- **Sensors**: Temperature, humidity, and air quality monitoring
- **Communication**: Serial/Ethernet/Wi-Fi connectivity
- **Power**: USB or external power supply

### Software
- **Language**: Arduino C/C++
- **Libraries**: Sensor-specific drivers, data logging
- **Monitoring**: Real-time environmental data collection

### Features
- Continuous temperature and humidity monitoring
- Alert thresholds for critical conditions
- Data logging and reporting
- Remote monitoring capabilities

## Installation

1. Clone the repository
2. Open Arduino IDE
3. Load the sketch from `/src` directory
4. Configure sensor pins in `config.h`
5. Upload to your Arduino board

## Configuration

Edit `config.h` to set:
- Sensor pin mappings
- Alert thresholds
- Logging intervals

## Usage

Connect sensors, power on, and monitor via serial console or web interface.

## License
[Specify your license]

## Contributing
Pull requests welcome. Please follow Arduino coding standards.