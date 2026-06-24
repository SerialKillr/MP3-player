# ESP32 iPod

A compact DIY portable music player built around the ESP32. This project combines an ESP32, OLED display, MP3-TF-16P audio decoder, rechargeable 18650 battery system, and physical navigation controls to create a fully self-contained "iPod-like" device capable of playing music directly from a microSD card.

## Features

* MP3 playback from microSD card
* 0.96" SSD1306 OLED user interface
* 5-button navigation and media controls
* 3.5mm headphone output
* Rechargeable 18650 battery with USB-C charging
* Hardware MP3 decoding for smooth playback
* Portable handheld design
* Low-cost and beginner-friendly electronics

## Hardware

* ESP32 DevKit
* SSD1306 I2C OLED Display
* MP3-TF-16P MP3 Decoder Module
* 5-Button Analog Keypad Module
* 3.5mm Audio Jack Breakout
* 18650 Li-ion Battery
* TP4056 USB-C Charging Module
* MT3608 Step-Up Converter
* Power Switch
* Status LED

## Power Architecture

18650 Battery → TP4056 Charger → MT3608 Boost Converter → Power Switch → ESP32

The TP4056 provides safe USB-C charging while the MT3608 boosts the battery voltage to a stable 5V supply for the system.

## Software Features

* Song browsing
* Play/Pause control
* Volume adjustment
* Track skipping
* Seek forward/backward
* OLED status display
* Battery-powered portable operation

## Goal

The objective of this project is to create a modern DIY interpretation of the classic iPod using inexpensive, widely available components while maintaining a compact form factor, physical controls, and long battery life.
