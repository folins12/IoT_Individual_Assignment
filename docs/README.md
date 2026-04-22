# IoT System Performance & Detailed Evaluation Report

This report provides an in-depth empirical analysis of the IoT adaptive system. All data points are derived from real execution logs stored in the `docs/` directory.

## 1. Physical Hardware Benchmarking
Before initializing the FreeRTOS scheduler, the system established its physical ceiling through a blocking ADC loop in the `setup()` phase.
* **Maximum Raw Sampling Rate:** ~16,150 Hz.
* **Assignment Baseline:** 100 Hz (Initial over-sampled state).
* **Observation:** The hardware limit is ~160x higher than our initial sampling rate, demonstrating the ESP32-S3's high-speed sensing capabilities.

## 2. Dynamic Frequency Adaptation (FFT)
The system analyzes the signal's spectral density every 128 samples to adjust the sensing duty cycle.

| Signal Mode | Physical Freq | FFT Peak Detected | Adapted Sampling | Power State |
| :--- | :--- | :--- | :--- | :--- |
| **Mode 1 (Slow)** | 1.0 Hz | 1.01 Hz | **10 Hz** | Deep Idle |
| **Mode 2 (Base)** | 4.0 Hz | 4.03 Hz | **11 Hz** | Balanced |
| **Mode 3 (Fast)** | 35.0 Hz | 35.23 Hz | **90 Hz** | Active |

**Logic Applied:** $f_s = (f_{peak} \times 2.5) + 1$. This ensures the system stays above the Nyquist limit while accounting for RTOS tick granularity.

## 3. Communication & Network Efficiency
The aggregation process (5-second window) transforms high-frequency raw data into a single efficient payload.

* **Payload Compression:** A raw 100 Hz stream generates 500 floats (2,000 bytes) every 5 seconds. Our edge aggregation reduces this to **4 bytes**, a **99.8% reduction** in network overhead.
* **End-to-End Latency:** * **MQTT (WiFi):** Stable at **1ms - 5ms** under normal conditions.
    * **LoRaWAN (TTN):** Latency spikes up to **5-15 seconds** during Join procedures or uplink windows due to the radio hardware's blocking nature on the SPI bus.

## 4. Power Consumption Diagnostics
Logs from the INA219 sensor reveal a direct correlation between system tasks and energy absorption:

| Operation Mode | Current (mA) | Power (mW) | Diagnostic Explanation |
| :--- | :--- | :--- | :--- |
| **Idle / Sensing** | **~45.00 mA** | ~174 mW | CPU active at low freq (11Hz), radios in sleep. |
| **WiFi / MQTT TX** | **~106.50 mA** | ~396 mW | Periodic spike every 5s during WiFi radio wake-up. |
| **LoRaWAN Uplink** | **~136.50 mA** | ~502 mW | SX1262 power amplifier active for long-range TX. |
| **System Peak** | **~221.30 mA** | ~806 mW | Occurs during boot or simultaneous radio init. |

**Observation:** By adapting the frequency from 100Hz to 11Hz, we maximize the time spent in the **45mA state**, significantly extending battery life compared to static oversampling.

---

## 5. Anomaly Filtering Benchmark (Bonus Part 8.2)
The system was tested against 9 unique configurations to characterize the trade-off between statistical accuracy and computational effort.

### 5.1 Results Table (Average Metrics)

| Probability ($P$) | Window ($W$) | Z-Score MER | Hampel MER | Hampel TPR | Hampel Exec Time |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **0.01 (1%)** | **5** | 0.0% | **57.4%** | 1.00 | 646 µs |
| 0.01 (1%) | 15 | 0.0% | 13.1% | 1.00 | 2,386 µs |
| 0.01 (1%) | 31 | 19.8% | 38.5% | 1.00 | 3,662 µs |
| **0.05 (5%)** | **5** | 0.0% | **60.6%** | 0.83 | 637 µs |
| 0.05 (5%) | 15 | 13.9% | 39.1% | 0.67 | 4,200 µs |
| 0.05 (5%) | 31 | 14.1% | 12.8% | 0.25 | 3,642 µs |
| **0.10 (10%)** | **5** | 0.0% | **61.5%** | 0.64 | 625 µs |
| 0.10 (10%) | 15 | 22.4% | 27.1% | 0.30 | 2,099 µs |
| 0.10 (10%) | 31 | 10.6% | 11.8% | 0.33 | 3,610 µs |

### 5.2 Comparative Analysis
* **Z-Score Masking Effect:** At small windows ($W=5$), the Z-score consistently achieves **0.0% MER**. Large outliers ($+/- 15$) inflate the Standard Deviation so much that the detection threshold moves beyond the anomalies themselves.
* **Hampel Resilience:** The Median-based filter ignores outlier magnitude, maintaining a **True Positive Rate (TPR) of 1.00** at low contamination ($P=0.01$).
* **Window Size Trade-off:** * **Latency:** Computational time increases linearly with $W$ (from **~630µs** at $W=5$ to **~3.6ms** at $W=31$).
    * **Accuracy:** Larger windows ($W=31$) perform worse as they are more likely to contain multiple anomalies, which eventually biases the median estimate and reduces MER.
* **FFT Spectral Impact:** Unfiltered anomalies act as Dirac impulses, raising the broadband noise floor. While the dominant 4Hz peak remains visible, the signal-to-noise ratio is degraded. Hampel filtering restores the clean spectral peak, preventing the adaptive logic from being "tricked" by noise.

---