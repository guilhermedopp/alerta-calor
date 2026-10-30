// TESTE 1 - DHT11 no ESP32 DevKit (WROOM-32)
// Ligação (ESP plugado na protoboard): DHT11 VCC -> trilho + (VIN, 5 V) | DATA -> D26 (j7) | GND -> trilho -
// DHT11 de 4 pinos: o pino 3 fica sem nada. NÃO coloque resistor de pull-up para 5 V.
// Biblioteca: "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor"
#include <DHT.h>
#define PINO_DHT 26   // D26 (ESP plugado na protoboard)
DHT dht(PINO_DHT, DHT11);

void setup() {
  Serial.begin(115200);
  dht.begin();
  Serial.println("Teste DHT11 - leitura a cada 2 s");
}

void loop() {
  delay(2000);                       // o DHT11 nao aceita leituras mais rapidas
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (isnan(t) || isnan(h)) {
    Serial.println("Falha na leitura: confira se o DATA esta no D26 (fileira 7) e o VCC/GND nos trilhos");
    return;
  }
  float ic = dht.computeHeatIndex(t, h, false);
  Serial.printf("Temp: %.1f C | Umid: %.0f %% | Indice de calor: %.1f C\n", t, h, ic);
}
