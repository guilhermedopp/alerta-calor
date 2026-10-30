// TESTE DEFINITIVO - DHT11 (D26) + PIR HC-SR501 (D27) no ESP32 DevKit
// Roda sozinho em 4 etapas, diz o que fazer em cada uma e no fim mostra APROVADO/REPROVADO.
// Depois do resumo, fica monitorando tudo a cada 5 s (modo bancada).
//
// LIGAÇÃO (ESP plugado na protoboard)
//   DHT11: VCC -> trilho +  | DATA -> D26 (j7) | GND -> trilho -   (sem resistor)
//   PIR:   VCC -> trilho +  | OUT  -> D27 (j6) | GND -> trilho -   (jumper em H, 2 potenciômetros no mínimo)
//   Trilho + <- VIN (j1)    | Trilho - <- GND (j2)
//
// Biblioteca: "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor"
// Placa: ESP32 Dev Module | Monitor Serial: 115200 | Para recomeçar: botão EN

#include <DHT.h>

#define PINO_DHT  26
#define PINO_PIR  27
#define LED_PLACA 2

DHT dht(PINO_DHT, DHT11);

// ---------- resultados ----------
bool okDhtLeitura = false, okDhtSopro = false, okPirRepouso = false, okPirMovimento = false;
float tBase = NAN, hBase = NAN, hMax = NAN;
int   movimentos = 0;
float somaTempoLigado = 0;

// ---------- utilidades ----------
void linha() { Serial.println("------------------------------------------------------------"); }

void ledPir() { digitalWrite(LED_PLACA, digitalRead(PINO_PIR)); }   // LED azul = estado do PIR

// espera ms milissegundos mantendo o LED do PIR atualizado
void espera(unsigned long ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) { ledPir(); delay(20); }
}

bool leDht(float &t, float &h) {
  t = dht.readTemperature();
  h = dht.readHumidity();
  return !(isnan(t) || isnan(h)) && t > -10 && t < 60 && h > 5 && h <= 100;
}

// ---------- ETAPA 1: leituras do DHT11 ----------
void etapa1() {
  linha();
  Serial.println("ETAPA 1/4 - DHT11: 5 leituras (10 s). Nao precisa fazer nada.");
  int boas = 0; float st = 0, sh = 0;
  for (int i = 1; i <= 5; i++) {
    espera(2000);
    float t, h;
    if (leDht(t, h)) { boas++; st += t; sh += h; Serial.printf("  leitura %d: %.1f C | %.0f %%\n", i, t, h); }
    else               Serial.printf("  leitura %d: FALHOU\n", i);
  }
  okDhtLeitura = (boas >= 4);
  if (boas) { tBase = st / boas; hBase = sh / boas; }
  Serial.printf("Resultado: %d de 5 leituras boas -> %s\n", boas, okDhtLeitura ? "OK" : "FALHOU");
  if (!okDhtLeitura) Serial.println("  Dica: DATA no D26 (fileira 7), VCC no trilho +, GND no trilho -.");
}

// ---------- ETAPA 2: reação do DHT11 ao sopro ----------
void etapa2() {
  linha();
  if (!okDhtLeitura) { Serial.println("ETAPA 2/4 - pulada (o DHT11 nao leu na etapa 1)."); return; }
  Serial.println("ETAPA 2/4 - SOPRE no DHT11 (sensor azul) por uns 5 s, agora. Tenho 30 s.");
  hMax = hBase;
  unsigned long t0 = millis();
  while (millis() - t0 < 30000) {
    espera(2000);
    float t, h;
    if (leDht(t, h)) {
      if (h > hMax) hMax = h;
      Serial.printf("  %.1f C | %.0f %%  (subiu %+.0f pontos)\n", t, h, h - hBase);
      if (h - hBase >= 5) { okDhtSopro = true; break; }
    }
  }
  Serial.printf("Resultado: umidade foi de %.0f %% ate %.0f %% -> %s\n", hBase, hMax, okDhtSopro ? "OK" : "FALHOU");
  if (!okDhtSopro) Serial.println("  Dica: sopre mais perto e mais tempo; se nada mudar, o sensor pode estar com defeito.");
}

// ---------- ETAPA 3: PIR aquece e fica em repouso ----------
void etapa3() {
  linha();
  Serial.println("ETAPA 3/4 - PIR: aquecimento de 60 s.");
  Serial.println("  Vire a cupula do PIR para a PAREDE e fique parado (ou saia da frente).");
  while (millis() < 60000UL) {                       // conta desde que a placa ligou
    static unsigned long ult = 0;
    if (millis() - ult >= 10000) { ult = millis(); Serial.printf("  aquecendo... faltam %lu s\n", (60000UL - millis()) / 1000); }
    espera(200);
  }
  Serial.println("  Aquecido. Esperando o PIR ficar em repouso (saida em 0) - ate 2 min...");
  unsigned long t0 = millis();
  while (millis() - t0 < 120000UL) {
    ledPir();
    if (digitalRead(PINO_PIR) == LOW) {
      espera(3000);                                  // confirma 3 s seguidos em repouso
      if (digitalRead(PINO_PIR) == LOW) { okPirRepouso = true; break; }
    }
    delay(50);
  }
  Serial.printf("Resultado: repouso %s\n", okPirRepouso ? "OK (saida em 0 com tudo parado)" : "FALHOU (saida presa em 1)");
  if (!okPirRepouso) Serial.println("  Dica: potenciometro de TEMPO no minimo (anti-horario), longe de ventilador/sol, voce fora da frente.");
}

// ---------- ETAPA 4: detectar 3 movimentos ----------
void etapa4() {
  linha();
  if (!okPirRepouso) { Serial.println("ETAPA 4/4 - pulada (o PIR nao ficou em repouso)."); return; }
  Serial.println("ETAPA 4/4 - PASSE A MAO na frente do PIR (30-50 cm). Preciso de 3 movimentos, tenho 90 s.");
  Serial.println("  Depois de cada um, pare e espere o 'voltou ao repouso' antes do proximo.");
  unsigned long t0 = millis(), inicio = 0;
  int ant = LOW;
  while (millis() - t0 < 90000UL && movimentos < 3) {
    int e = digitalRead(PINO_PIR);
    digitalWrite(LED_PLACA, e);
    if (e == HIGH && ant == LOW) { inicio = millis(); Serial.printf("  movimento %d detectado...\n", movimentos + 1); }
    if (e == LOW && ant == HIGH) {
      movimentos++;
      float dur = (millis() - inicio) / 1000.0;
      somaTempoLigado += dur;
      Serial.printf("  ...voltou ao repouso (ficou ligado %.1f s)\n", dur);
    }
    ant = e;
    delay(20);
  }
  okPirMovimento = (movimentos >= 3);
  Serial.printf("Resultado: %d de 3 movimentos -> %s\n", movimentos, okPirMovimento ? "OK" : "FALHOU");
  if (!okPirMovimento) Serial.println("  Dica: mao mais perto e mais rapida; sensibilidade um pouco acima do minimo.");
}

// ---------- resumo ----------
void resumo() {
  linha();
  Serial.println("RESUMO DO TESTE");
  Serial.printf("  DHT11 leitura ............ %s  (%.1f C, %.0f %%)\n", okDhtLeitura ? "APROVADO " : "REPROVADO", tBase, hBase);
  Serial.printf("  DHT11 reage ao sopro ..... %s\n", okDhtSopro     ? "APROVADO " : "REPROVADO");
  Serial.printf("  PIR em repouso ........... %s\n", okPirRepouso   ? "APROVADO " : "REPROVADO");
  Serial.printf("  PIR detecta movimento .... %s", okPirMovimento ? "APROVADO " : "REPROVADO");
  if (movimentos) Serial.printf("  (fica ligado ~%.1f s por movimento)", somaTempoLigado / movimentos);
  Serial.println();
  bool tudo = okDhtLeitura && okDhtSopro && okPirRepouso && okPirMovimento;
  Serial.println(tudo ? ">>> BANCADA APROVADA: pode seguir para o firmware completo."
                      : ">>> Algo falhou: veja as dicas acima e aperte EN para repetir.");
  linha();
  Serial.println("Modo bancada: estado a cada 5 s e aviso a cada movimento.");
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_PIR, INPUT);
  pinMode(LED_PLACA, OUTPUT);
  dht.begin();
  delay(1000);
  Serial.println();
  Serial.println("TESTE DEFINITIVO - DHT11 (D26) + PIR (D27)");
  etapa1();
  etapa2();
  etapa3();
  etapa4();
  resumo();
}

void loop() {
  static int ant = LOW;
  static unsigned long ultimo = 0, n = 0;
  int e = digitalRead(PINO_PIR);
  digitalWrite(LED_PLACA, e);
  if (e == HIGH && ant == LOW) { n++; Serial.printf("[%lu s] MOVIMENTO (%lu)\n", millis() / 1000, n); }
  ant = e;
  if (millis() - ultimo >= 5000) {
    ultimo = millis();
    float t, h;
    if (leDht(t, h)) Serial.printf("[%lu s] %.1f C | %.0f %% | indice de calor %.1f C | PIR=%d\n",
                                   millis() / 1000, t, h, dht.computeHeatIndex(t, h, false), e);
    else             Serial.printf("[%lu s] DHT11 falhou | PIR=%d\n", millis() / 1000, e);
  }
  delay(20);
}
