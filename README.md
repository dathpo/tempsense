# TempSense

TempSense is a Bluetooth-enabled temperature sensing device that streams real-time temperature data and provides a quick visual indication of whether the environment is warm (above 20°C) or cool (below 20°C).

## Features

- **Temperature Streaming**: Streams temperature data over Bluetooth in real-time.
- **LED Indicator**: Uses two LEDs (green and orange) to indicate if the temperature is above or below 20°C.
- **Button Control**: Press the button to instantly check if the temperature is warm (above 20°C).
- **Power-off Mechanism**: Hold the button for 18 seconds to power off the device.
- **OTA Updates**: Supports Over-the-Air (OTA) firmware updates for easy improvements.

## Hardware Requirements

- **nRF device**: The firmware uses the nRF die temperature sensor.
- **2 LEDs**: One green, one orange for temperature indication.
- **1 Button**: For manual temperature check trigger.

## Usage

1. **Bluetooth Streaming**: TempSense automatically streams the current temperature over Bluetooth. Pair your device and subscribe to the temperature characteristic.
2. **LED Indicator**: Press the button to trigger a visual indication:
   - **Green LED**: Temperature is 20°C or above (warm).
   - **Orange LED**: Temperature is below 20°C (cool).
3. **Power-off**: Hold the button for 18 seconds to turn off the device.
