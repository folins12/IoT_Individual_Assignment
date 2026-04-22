# Adaptive IoT Edge Sensing System

An ESP32-based IoT system running FreeRTOS that collects sensor data, computes localized DSP (Fast Fourier Transform), and dynamically adapts its sampling frequency to minimize energy consumption. Aggregated data is transmitted to the edge (MQTT) and the cloud (LoRaWAN/TTN).

## Hardware Setup
This project requires two **Heltec WiFi LoRa 32 V3** boards:
1. **Target Node:** Runs the main firmware (sampling, FFT, MQTT, LoRaWAN).
2. **Monitor Node:** Hooked to an **INA219 Power Sensor** to measure the current drawn by the Target Node.
   * `VCC` -> Heltec `3V3`
   * `GND` -> Heltec `GND`
   * `SDA` -> Heltec `Pin 41`
   * `SCL` -> Heltec `Pin 42`

## Project Structure
* `src/main.cpp`: Main FreeRTOS firmware for the Target Node.
* `src/bonus.cpp`: Anomaly injection and DSP filtering benchmarks (Z-Score & Hampel).
* `src/monitor.cpp`: Power monitoring firmware.
* `include/credentials.h`: Network and TTN API keys.
* `docs/`: Technical reports, LLM prompt logs, and empirical benchmark data.

## Configuration & Deployment
This project is built with **PlatformIO**. 

1. Update `include/credentials.h` with your Wi-Fi SSID/Password and TTN keys.
2. In `src/main.cpp`, you can configure the behavior:
   * `#define SIGNAL_MODE` (Switches between Base (1), Slow (2), and Fast (3) input signals).
   * `#define RUN_BONUS_8_2` (Enables (1) anomaly filtering benchmark task).
3. Open the PlatformIO Core CLI or UI:
   * Upload to the main board: `pio run -e target -t upload`
   * Upload to the power monitor: `pio run -e monitor -t upload`