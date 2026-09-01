# ESP32 iPod
A compact DIY portable music player built around the ESP32. This project combines an ESP32, OLED display, SPI SD card module, rechargeable 18650 battery system, and physical navigation controls to create a fully self-contained "iPod-like" device capable of playing music directly from a microSD card.

## Features
* WAV playback from microSD card via ESP32 built-in DAC
* 0.96" SSD1306 OLED user interface
* 5-button navigation and media controls
* 2x side tactile buttons for volume
* 3.5mm headphone output
* Rechargeable 18650 battery with USB-C charging
* Portable handheld design
* Low-cost and beginner-friendly electronics

## Implementing Soon
* 🔜 Screen auto-dim after X seconds of no input — saves battery
* 🔜 Screen off completely on longer idle — any button press wakes it

## Planned Features
* Shuffle mode — random song order
* Resume from last position when powered back on
* Bluetooth output — auto toggle between wired and BT depending on what is connected
* Long press centre button to enter settings menu
* Settings menu — navigated entirely with existing physical buttons
* Bluetooth device scan and connect from settings
* Auto return to playback screen after 5 seconds idle in menu

## Hardware
* ESP32 WROOM DevKit
* SSD1306 I2C OLED Display (0.96")
* SPI MicroSD Card Module
* AD Keyboard 5-Button Analog Module
* 2x Metal Tactile Side Buttons (volume)
* 3.5mm Audio Jack
* 18650 Li-ion Battery + Removable Holder
* TP4056 USB-C Charging Module (with protection)
* MT3608 Step-Up Boost Converter
* Latching Power Switch
* Status LED + 220Ω resistor

## Controls
| Button | Action |
|---|---|
| ▲ Up | Previous song |
| ▼ Down | Next song |
| ◀ Left | Seek −10 sec |
| ▶ Right | Seek +10 sec |
| ● Centre (short) | Play / Pause |
| ● Centre (long) | Enter / exit settings menu |
| Side + | Volume up |
| Side − | Volume down |
| Latching switch | Power on / off |

## Power Architecture
```
18650 Battery → TP4056 Charger → MT3608 Boost (set to 5V) → Latching Switch → ESP32 VIN
```
The TP4056 provides safe USB-C charging and stops automatically at 4.2V. The MT3608 boosts battery voltage to a stable 5V. The latching switch cuts all power when off, drawing zero current from the battery.

## Software Features
* Song playback from WAV files on SD card
* Play / Pause control
* Volume adjustment (0–30)
* Track skipping (previous / next)
* Seek forward / backward 10 seconds (real hardware seeking, not display-only)
* Scrolling OLED marquee for long song names
* Song name lookup table — map track numbers to display names in code
* OLED status display — track number, song title, elapsed time, volume bar
* Calibration mode for AD button module ADC thresholds

## Libraries Required
Install via Arduino Library Manager:
* `Adafruit SSD1306` by Adafruit
* `Adafruit GFX Library` by Adafruit
* `SD` by Arduino (built-in)

## Goal
The objective of this project is to create a modern DIY interpretation of the classic iPod using inexpensive, widely available components while maintaining a compact form factor, physical controls, and long battery life.
