# Performance Evaluation & Technical Details

This document addresses the assignment's technical requirements, utilizing empirical data collected from the execution logs (available in the `docs/` folders).

### 1. Input Signal & Max Sampling Frequency
* **Signal Construction:** The baseline signal is synthesized via mathematical formulas mapped to FreeRTOS ticks, e.g., `s(t) = 3*sin(2*pi*4*t) + 1.5*sin(2*pi*8*t)`.
* **Max Hardware Rate:** A blocking ADC benchmark in `setup()` reveals the Heltec V3 ESP32-S3 can sample at a maximum rate of **~16,150 Hz** (`docs/base/log.txt`). 
* **Initial State:** The adaptive task initializes at a conservative over-sampled rate of **100 Hz**.

### 2. Optimal Frequency via FFT
Instead of running a long fixed oversampling phase, the system gathers its first 128 samples (1.28 seconds at 100 Hz), computes the Hamming-windowed FFT, and immediately adapts. 
* For the baseline 4 Hz dominant signal, the system instantly drops the sampling rate to **10-11 Hz** (Nyquist limit + safety margin). 

### 3. Aggregation & Transmission
* **Window:** A 5,000 ms sliding window aggregates the samples by calculating the arithmetic mean. Due to the symmetrical nature of sine waves over exactly 5 seconds, the mean converges precisely to `0.00`, proving zero RTOS packet loss.
* **MQTT:** The 4-byte float is sent locally to `broker.hivemq.com`.
* **LoRaWAN:** The exact same payload is transmitted to The Things Network (TTN). A duty-cycle logic (1 uplink every 6 windows) ensures legal airtime compliance.

### 4. Performance Metrics
* **Energy Savings:** Monitored via the INA219 (`docs/power_consumption/log.txt`). The system draws **~106 mA** during active CPU/Radio spikes and drops to **~45 mA** during idle ticks. By reducing the sampling rate from 100 Hz to 11 Hz, FreeRTOS spends ~90% more time in the lower-power 45 mA state.
* **Per-Window Execution:** The 128-point FFT computation on the ESP32 takes exactly **~2,950 µs** (2.95 ms).
* **Data Volume:** Transmitting 100 Hz raw data over 5 seconds requires sending 500 floats (**2,000 bytes**). Edge aggregation reduces this to a single float (**4 bytes**).
* **E2E Latency:** MQTT latency from window-close to transmission is routinely between **1 ms and 5 ms**. When the LoRaWAN join/transmit blocks the RTOS temporarily, latency dynamically resolves without dropping queue data.

---

## Bonus Point 1: Multi-Signal Performance
By swapping the `SIGNAL_MODE`, the system dynamically adapts to three scenarios (`docs/bonus_1`):
1. **Slow Signal (1 Hz):** Adapts to **10 Hz**. Extreme CPU idling and energy savings.
2. **Complex Signal (4 Hz & 8 Hz):** Adapts to **10-11 Hz**.
3. **Fast Signal (35 Hz):** Adapts to **79-95 Hz**. CPU savings are minimal compared to the 100 Hz baseline, but *network savings remain identical* (4 bytes).

---

## Bonus Point 2: Anomaly Detection (Z-Score vs. Hampel)
A sparse, uniform anomaly `+/- U(5, 15)` was injected alongside Gaussian noise (`sigma=0.2`). 

**FFT Spectral Impact:** Because a sparse spike approximates a Dirac impulse, its energy is distributed evenly across all frequencies (broadband noise). As empirically logged, the *dominant* peak often remains anchored to the base sine wave (~4 Hz), but the noise floor rises drastically.

**Filter Performance (Window = 5, Prob = 1%):**
* **Z-Score (Standard Dev):** Fails consistently (TPR: 0.00%, MER: 0.0%). The massive spikes artificially inflate the variance, causing the anomaly to mask itself within the threshold.
* **Hampel (Median):** Succeeds reliably (TPR: 100%, MER: ~50-60%). The median ignores the magnitude of the outlier.

**Computational Cost on ESP32:**
Counter-intuitively, sorting arrays (Hampel) was much faster than floating-point math (Z-score). 
* Initial Z-score using `pow()` took **~2,400 µs**. 
* Hampel (`std::sort`) took **~640 µs**. 
* We subsequently optimized Z-Score by replacing `pow()` with direct FPU multiplication (`diff * diff`), bringing its execution down to **~995 µs**, which is still 35% slower than Hampel. 

**Conclusion:** For sparse hardware transients on embedded microcontrollers, the **Hampel filter** at a small window size ($W=5$) is vastly superior in both detection accuracy and CPU execution time.