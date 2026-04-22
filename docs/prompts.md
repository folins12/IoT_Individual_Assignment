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

## Opportunities and Limitations
* **Opportunities:** The LLM was exceptional at explaining the inner workings of the ESP32 hardware, specifically pointing out that the Wi-Fi drivers live on Core 0 (causing watchdog resets if blocked) and that `pow()` invokes heavy Taylor series math instead of simple FPU multiplication. 
* **Limitations:** The LLM frequently hallucinated syntax for third-party libraries (especially `arduinoFFT` v2 and `RadioLib`), providing code designed for older versions that wouldn't compile. Critical thinking and cross-referencing with the official library documentation were absolutely necessary to implement the LoRaWAN join process correctly.