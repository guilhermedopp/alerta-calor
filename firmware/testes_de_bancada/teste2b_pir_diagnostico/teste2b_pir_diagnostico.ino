// DIAGNÓSTICO DO PIR - descobre em qual pino o fio roxo (OUT) está entrando
// Lê ao mesmo tempo D13, D12, D14, D27, D25, D33 e D32 e mostra quando algum muda.
// Monitor Serial em 115200. Não precisa esperar: já mostra tudo desde o início
// (mas o PIR só funciona direito depois de ~60 s ligado).

const int PINOS[] = {13, 12, 14, 27, 25, 33, 32};
const int N = sizeof(PINOS) / sizeof(PINOS[0]);
int ultimo[N];

void mostra() {
  for (int i = 0; i < N; i++) Serial.printf("D%d=%d  ", PINOS[i], digitalRead(PINOS[i]));
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);
  for (int i = 0; i < N; i++) { pinMode(PINOS[i], INPUT_PULLDOWN); ultimo[i] = digitalRead(PINOS[i]); }
  delay(500);
  Serial.println("\nDiagnostico do PIR - passe a mao e veja qual pino vira 1");
  mostra();
}

void loop() {
  bool algumAlto = false;
  for (int i = 0; i < N; i++) {
    int v = digitalRead(PINOS[i]);
    if (v) algumAlto = true;
    if (v != ultimo[i]) {
      ultimo[i] = v;
      Serial.printf("[%lu s] D%d mudou para %d  <-- o fio roxo esta neste pino\n", millis() / 1000, PINOS[i], v);
    }
  }
  digitalWrite(2, algumAlto);            // LED azul acende se qualquer um estiver em 1
  static unsigned long t = 0;
  if (millis() - t >= 5000) { t = millis(); mostra(); }   // a cada 5 s, o estado de todos
  delay(50);
}
