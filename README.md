# ESP8266 Door Access Control System

This project implements a distributed door access control system using
multiple ESP8266 nodes communicating over MQTT.

## System Overview

The system consists of three ESP8266 nodes:

- **ESP1 – RFID & Motion**
  Handles RFID authentication, LCD feedback, and motion sensing.

- **ESP2 – Keypad**
  Handles PIN entry and reports keypad interaction.

- **ESP3 – Door Actuator**
  Controls the door lock servo, LEDs, and buzzer.

A shared `WiFiMqttClient` library provides a unified abstraction for
WiFi and MQTT communication.


The pin maps of ESP2866MOD: 
https://lastminuteengineers.com/esp8266-pinout-reference/

## Doxygen
The homepage of doxygen can be accessed:
**[Locally](docs/html/index.html)**
or
**[Remotely](https://mathiaskes.github.io/34338-Project/html/)**