// TESTE DO SENSOR DE PRESENÇA (PIR) no ESP32 DevKit
// Funciona com HC-SR501 (o grande, de cúpula branca e 2 parafusos) e com AM312 (o pequeno).
//
// LIGAÇÃO
//   HC-SR501: VCC -> VIN (5 V)   OUT -> D27   GND -> GND
//   AM312:    VCC -> 3V3         OUT -> D27   GND -> GND
//   (a saída dos dois é 3,3 V: liga direto, sem resistor)
//
// USO: Ferramentas > Placa > ESP32 Dev Module, carregue e abra o Monitor Serial em 115200.
//      Espere 60 s sem se mexer na frente do sensor (aquecimento) e depois passe a mão.
//      O LED azul da placa acende enquanto o PIR detecta movimento.

#define PINO_PIR  27
#define LED_PLACA 2
const unsigned long AQUECIMENTO_MS = 60000;   // o PIR precisa estabilizar depois de ligar

int estadoAnterior = LOW;
unsigned long inicioMovimento = 0;
unsigned long contagem = 0;

void hora(unsigned long ms) {                 // tempo desde que a placa ligou, mm:ss
  unsigned long s = ms / 1000;
  Serial.printf("[%02lu:%02lu] ", s / 60, s % 60);
}

void setup() {
  Serial.begin(115200);
  pinMode(PINO_PIR, INPUT);
  pinMode(LED_PLACA, OUTPUT);
  delay(500);
  Serial.println();
  Serial.println("Teste do PIR - aguarde 60 s de aquecimento sem se mexer na frente do sensor...");
}

void loop() {
  unsigned long agora = millis();
  int estado = digitalRead(PINO_PIR);
  digitalWrite(LED_PLACA, estado);

  if (agora < AQUECIMENTO_MS) {               // durante o aquecimento só mostra a contagem regressiva
    static unsigned long ultimo = 0;
    if (agora - ultimo >= 10000) { ultimo = agora; Serial.printf("aquecendo... faltam %lu s\n", (AQUECIMENTO_MS - agora) / 1000); }
    estadoAnterior = estado;
    delay(50);
    return;
  }
  static bool avisou = false;
  if (!avisou) { avisou = true; Serial.println("Pronto! Passe a mao na frente do sensor."); }

  if (estado == HIGH && estadoAnterior == LOW) {        // começou a detectar
    contagem++;
    inicioMovimento = agora;
    hora(agora); Serial.printf("MOVIMENTO detectado (%lu)\n", contagem);
  }
  if (estado == LOW && estadoAnterior == HIGH) {        // parou de detectar
    hora(agora); Serial.printf("parado - sinal ficou ligado por %.1f s\n", (agora - inicioMovimento) / 1000.0);
  }
  estadoAnterior = estado;
  delay(50);
}
