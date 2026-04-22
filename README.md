# IoT Individual Assignment - Andrea Folino

This IoT system basically collects sensor data, computes Fast Fourier Transform, and dynamically adapts its sampling frequency to minimize energy consumption. Aggregated data is transmitted to the edge (MQTT) and the cloud (LoRaWAN/TTN).

## Hardware Setup
This project requires two **Heltec WiFi LoRa 32 V3** boards:
1. **Target Node:** Runs the main firmware (sampling, FFT, MQTT, LoRaWAN).
2. **Monitor Node:** Through an **INA219 Sensor** measures the current drawn by the Target Node.
   * `VCC` -> Heltec `3V3`
   * `GND` -> Heltec `GND`
   * `SDA` -> Heltec `Pin 41`
   * `SCL` -> Heltec `Pin 42`
   * The INA219 is placed in series with the **3.7V LiPo battery** powering the Target Node.

## Project Structure
* `src/main.cpp`: Main FreeRTOS firmware for the Target Node.
* `src/bonus.cpp`: Anomaly injection and DSP filtering benchmarks (Z-Score & Hampel).
* `src/monitor.cpp`: Power monitoring firmware.
* `src/credentials.h`: Network and TTN API keys (hidden in github).
* `docs/`: output logs, and empirical benchmark data (screen included) separated in the respective case folders.

## Configuration & Deployment
This project is built with **PlatformIO**. 

1. Update `src/credentials.h` with your Wi-Fi SSID/Password and TTN keys.
2. In `src/main.cpp`, you can configure the behavior:
   * `#define SIGNAL_MODE` (Switches between Base (1), Slow (2), and Fast (3) input signals).
   * `#define RUN_BONUS_8_2` (Enables (1) anomaly filtering benchmark task).
3. Open the PlatformIO Core CLI or UI:
   * Upload to the main board: `pio run -e target -t upload`
   * Upload to the power monitor: `pio run -e monitor -t upload`

---

# System Performance & Evaluation 

This section provides an empirical analysis of the IoT adaptive system. All data points are derived from real execution logs stored in the `docs/` directory.

## 1. Physical Hardware Benchmarking
Before initializing the FreeRTOS scheduler, the system established its physical ceiling through a blocking ADC loop in the `setup()` phase.
* **Maximum Raw Sampling Rate:** ~16,164 Hz.
* **Assignment Baseline:** 100 Hz (Initial over-sampled state).

## 2. Dynamic Frequency Adaptation (FFT)
The system analyzes the signal's spectral density every 128 samples to adjust the sensing duty cycle.

| Signal Mode | Physical Freq | FFT Peak Detected | Adapted Sampling | Power State |
| :--- | :--- | :--- | :--- | :--- |
| **Mode 1 (Slow)** | 1.0 Hz | 1.01 Hz | **10 Hz** | Deep Idle |
| **Mode 2 (Base)** | 4.0 Hz | 4.03 Hz | **11 Hz** | Balanced |
| **Mode 3 (Fast)** | 35.0 Hz | 35.23 Hz | **90 Hz** | Active |

**Logic Applied:** $f_s = (f_{peak} \times 2.5) + 1$. This ensures the system stays above the Nyquist limit.

## 3. Communication & Network Efficiency
The aggregation process (5-second window) transforms high-frequency raw data into a single efficient payload.

* **Payload Compression:** A raw 100 Hz stream generates 500 floats (2,000 bytes) every 5 seconds. By adapting the frequency to 11 Hz, the raw stream drops to 220 bytes. Our edge aggregation further reduces this to **4 bytes** (a single float), a **99.8% reduction** in network overhead.
* **End-to-End Latency:** * **MQTT (WiFi):** Stable at **~4950 ms** (this matches the exact 5-second buffer aggregation time).
    * **LoRaWAN (TTN):** Latency spikes up to **6,000-8,600 ms** during Join procedures or uplink windows due to the radio hardware's blocking nature on the SPI bus.

## 4. Power Consumption Diagnostics
Logs from the INA219 sensor reveal a direct correlation between system tasks and energy absorption. Calculations are based on a **3.7V** LiPo battery source:

| Operation Mode | Current (mA) | Power (mW) | Diagnostic Explanation |
| :--- | :--- | :--- | :--- |
| **Boot / Oversampled**| **~60.90 mA** | ~225 mW | CPU highly active at 100Hz sensing, initial boot. |
| **Idle / Adaptive** | **~47.50 mA** | ~175 mW | CPU active at low freq (11Hz), radios in sleep. |
| **WiFi / MQTT TX** | **~105.80 mA** | ~391 mW | Periodic spike every 5s during WiFi radio wake-up. |
| **LoRaWAN Uplink** | **~107.40 mA** | ~397 mW | SX1262 power amplifier active for long-range TX. |


### 4.1 Interpreting Power Consumption for Bonus Filters
In the Bonus implementation (`RUN_BONUS_8_2 = 1`), the Unfiltered, Z-Score, and Hampel evaluations run sequentially inside a dedicated background task. Because the INA219 prints every 5 seconds, it captures a "blended" current reading of these three states running together (fluctuating between **39mA and 48.9mA**). 

**How to evaluate their energy impact:**
Since the filters are heavily optimized for the ESP32's hardware FPU, they do not cause massive instantaneous current spikes. Instead, their energy cost is measured in **CPU execution time** (microseconds). 
* Z-Score takes **~320µs** per batch.
* Hampel takes **~650µs** per batch. 
The true energy cost is the amount of time the processor is forced to stay awake computing these arrays instead of yielding to the FreeRTOS `Idle` task.

---

## 5. Anomaly Filtering Benchmark (Bonus Part 8.2)
The system was tested against 9 unique configurations to characterize the trade-off between statistical accuracy and computational effort.

### 5.1 Results Table (Average Metrics from Logs)

| Probability ($P$) | Window ($W$) | Z-Score MER | Hampel MER | Hampel TPR | Hampel Exec Time |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **0.01 (1%)** | **5** | 0.0% | **17.1%** | 0.40 | ~660 µs |
| 0.01 (1%) | 15 | 21.6% | 29.7% | 0.80 | ~2,420 µs |
| 0.01 (1%) | 31 | 16.5% | 22.0% | 0.58 | ~3,630 µs |
| **0.05 (5%)** | **5** | 0.0% | **62.9%** | 0.82 | ~655 µs |
| 0.05 (5%) | 15 | 32.7% | 54.9% | 0.75 | ~2,360 µs |
| 0.05 (5%) | 31 | 28.3% | 40.7% | 0.56 | ~3,590 µs |
| **0.10 (10%)** | **5** | 0.0% | **63.1%** | 0.70 | ~665 µs |
| 0.10 (10%) | 15 | 13.0% | 48.3% | 0.55 | ~2,300 µs |
| 0.10 (10%) | 31 | 19.1% | 36.4% | 0.38 | ~3,570 µs |

### 5.2 Comparative Analysis
* **Z-Score Masking Effect:** At small windows ($W=5$), the Z-score consistently achieves **0.0% MER**. Large outliers ($+/- 15$) inflate the Standard Deviation so much that the detection threshold moves beyond the anomalies themselves.
* **Hampel Resilience:** The Median-based filter ignores outlier magnitude, maintaining a high **True Positive Rate (TPR)** and strong Mean Error Reduction (MER) at higher contamination levels.
* **Window Size Trade-off:** * **Latency:** Computational time increases linearly with $W$ (from **~660µs** at $W=5$ to **~3.6ms** at $W=31$).
    * **Accuracy:** Larger windows ($W=31$) perform worse as they are more likely to contain multiple anomalies, which eventually biases the median estimate and reduces MER.
* **FFT Spectral Impact:** Unfiltered anomalies act as Dirac impulses, raising the broadband noise floor. While the dominant 4Hz peak remains visible, the signal-to-noise ratio is degraded. Hampel filtering restores the clean spectral peak, preventing the adaptive logic from being "tricked" by noise.

---

# LLM Usage Report & Prompts

During the development of this project, Google Gemini was utilized as an advanced documentation retrieval tool and algorithmic brainstorming partner. 

## Prompts Issued

1. **FreeRTOS Queue Structuring:**
   > *"I have two FreeRTOS tasks on an ESP32. Task 1 samples a simulated sine wave at 100Hz. Task 2 runs an FFT on 128 samples. What is the safest way to pass float data between these tasks without blocking the high-frequency sampling loop? Show me the QueueHandle_t setup."*

2. **LoRaWAN RadioLib Migration:**
   > *"I'm using RadioLib v6.6.0 on a Heltec V3 ESP32-S3. TTN is giving me an error saying 'devnonce has already been used'. How do I correctly structure the `node.beginOTAA` and `activateOTAA` loops to handle this, and how should my keys be formatted (MSB or LSB)?"*

3. **FFT Library Implementation:**
   > *"Using the `arduinoFFT` library (v2), I have an array of 128 floats. I need to apply a Hamming window and compute the forward FFT to find the dominant frequency peak. Provide the exact syntax for `complexToMagnitude` and `majorPeak`."*

4. **C++ Algorithm Optimization (Hampel):**
   > *"I need to write a Hampel filter in C++ for an ESP32. It calculates the median of a sliding window of 5 items, and then the Median Absolute Deviation (MAD). What is the most memory-efficient way to sort a 5-element float array without using the heap?"*

5. **ESP32 FPU Math Bottleneck:**
   > *"My Z-Score filter is running slower than my Hampel filter on the ESP32, which doesn't make sense since sorting should be O(n log n). I am using the `pow(val, 2)` function for the variance. Is `pow()` heavily unoptimized on the ESP32? What's the alternative?"*

6. **FreeRTOS Deadlock Debugging:**
   > *"My ESP32 keeps restarting and throwing this error: `Task watchdog got triggered. - IDLE0 (CPU 0)`. My MQTT transmit task is pinned to Core 0. Is the MQTT connection blocking the internal Wi-Fi driver?"*

7. **Signal Processing Theory:**
   > *"If I have a clean 4 Hz sine wave and I inject a single large-magnitude spike (e.g. value of 15) into an array of 128 samples, will the dominant frequency in the FFT change? Or does a single spike act like a Dirac delta and just raise the noise floor?"*

8. **Power Calculation Context:**
   > *"Calculate the true power in mW for an INA219 reading of 47mA if the power source is actually a 3.7V battery instead of a 5V USB rail."*

## Opportunities and Limitations
* **Opportunities:** The LLM was exceptional at explaining the inner workings of the ESP32 hardware, specifically pointing out that the Wi-Fi drivers live on Core 0 (causing watchdog resets if blocked) and that `pow()` invokes heavy Taylor series math instead of simple FPU multiplication. 
* **Limitations:** The LLM frequently hallucinated syntax for third-party libraries (especially `arduinoFFT` v2 and `RadioLib`), providing code designed for older versions that wouldn't compile. Critical thinking and cross-referencing with the official library documentation were absolutely necessary to implement the LoRaWAN join process correctly.