Arduino IDE setup
=================

# Table of Contents
<details>
 <summary>Click to open TOC</summary>
<!-- MarkdownTOC autolink="true" levels="1,2,3,4,5,6" bracket="round" style="unordered" indent="    " autoanchor="false" markdown_preview="github" -->

- [About Arduino IDE](#about-arduino-ide)
- [Installing Arduino IDE and plugins](#installing-arduino-ide-and-plugins)
- [Configuring Arduino IDE](#configuring-arduino-ide)

<!-- /MarkdownTOC -->
</details>

# About Arduino IDE

Arduino IDE is an open-source integrated development environment with support for multiple platforms.  

Learn more : https://www.arduino.cc/en/software

# Installing Arduino IDE and plugins

[Download](https://www.arduino.cc/en/software#download) the IDE, and follow the [Getting Started](https://www.arduino.cc/en/Guide)
guide.

Additionally, install (by following the instructions in the following links) the ESP32 filesystem plugin:
* https://github.com/me-no-dev/arduino-esp32fs-plugin

When you start the Arduino IDE, you should now have an additional option in the `Tools` menu:
* ESP32 Sketch Data Upload

# Configuring Arduino IDE

In the `Preferences` pane for the IDE, look for `Additional Boards Manager URLs`, click on the button on the right, and append the following URL:
`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`

In the `Tools` menu, select the `Board` entry, click on the `Boards Manager...` submenu, enter `esp32` in the search box and press Enter.

You should have one entry named `esp32 by Espressif Systems` ; click on `Install` and wait for installation.

Open the project by navigating to the `src` folder and opening the `main.cpp` file as a sketch.

Go back to the `Tools` menu, and in the `Board` entry select the `ESP32 Arduino` section and choose your board (`ESP32 Dev Module` or `ESP32 WROOM-32`)

## Lilygo T-CAN485 Specific Configuration
This project is optimized for Lilygo T-CAN485 module with the following pin configuration:
- CAN_TX: GPIO 27
- CAN_RX: GPIO 26
- WS2812B LED: GPIO 4 (uses FastLED library with asynchronous color coding):
  - **Green**: System initialization (100ms auto-off)
  - **Blue**: CAN communication active (100ms auto-off)
  - **White**: Web page loading (100ms auto-off)
  - **Red**: Error conditions (100ms auto-off)
  - **Off**: Idle state
  - **Asynchronous**: Non-blocking, immediate state changes
- Serial to inverter: RX GPIO 16, TX GPIO 17

Configure the other parameters the following way:

* Upload Speed : 460800
* CPU Frequency : 240MHz
* Flash Frequency : 80MHz
* Flash Mode : DIO
* Flash Size : 4MB (FS:2MB OTA:~1019KB)
* Partition Scheme : Default 4MB with SPIFFS
* PSRAM : Disabled
* Arduino Runs On : Core 1
* Events Run On : Core 1
* Port: (_lookup the port on which your USB/Serial adapter is. You can also choose the board if it's up, connected to your WiFi, for OTA flashing_)

That's it ! Your IDE should now be configured for your day to day operations.
