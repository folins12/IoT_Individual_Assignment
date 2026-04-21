#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_INA219.h>

Adafruit_INA219 ina219;
bool ina219_connected = false;

unsigned long last_print_time = 0;
const unsigned long print_interval = 5000;

void setup() {
  // Seriale verso il tuo PC (USB)
  Serial.begin(115200);
  
  // Seriale verso il Target (Cavo dal TX del Target al Pin 4 del Monitor)
  // RX = Pin 4, TX = Pin 5 (non usato)
  Serial1.begin(115200, SERIAL_8N1, 4, 5); 

  delay(2000);
  Serial.println("\n--- INA219 Monitor Station Booting ---");

  Wire.begin(41, 42); 
  if (ina219.begin()) {
    ina219_connected = true;
    Serial.println("[INIT] INA219 Power Sensor Connected.");
  } else {
    Serial.println("[ERROR] INA219 non trovato. Controlla i collegamenti!");
  }
}

void loop() {
  // 1. ASCOLTO TARGET: Se il Target sta parlando, inoltra tutto al PC
  while (Serial1.available()) {
    Serial.write(Serial1.read());
  }

  // 2. LETTURA INA219
  if (ina219_connected) {
    unsigned long current_millis = millis();

    if (current_millis - last_print_time >= print_interval) {
      float current_mA = ina219.getCurrent_mA();
      float power_mW = ina219.getPower_mW();

      // Stampiamo in formato compatibile con Teleplot
      Serial.printf("\n[INA219] --- RILEVAMENTO CONSUMI ---\n");
      Serial.printf(">Current_mA:%.2f\n", current_mA);
      Serial.printf(">Power_mW:%.2f\n", power_mW);
      Serial.printf("------------------------------------\n");
      
      last_print_time = current_millis;
    }
  }
  
  // Nessun delay lungo qui, altrimenti perdiamo i dati seriali in arrivo!
  delay(1); 
}