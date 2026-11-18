// TESTE DA PROTOBOARD DEFINITIVA, SEM O AHT20 (ESP32-C3 SuperMini)
// Testa o LED RGB, o buzzer (pelo BC337), o botão e o PIR, nessa ordem.
// O AHT20 fica para depois (ainda sem solda); este teste não usa o I2C.
//
// LIGAÇÃO (igual ao mapa "Protoboard da Caixa Definitiva")
//   GPIO3  -> R1 220 Ω -> LED vermelho
//   GPIO4  -> R2 47 Ω  -> LED verde        (catodo K do LED no GND)
//   GPIO10 -> R4 1 kΩ  -> base do BC337    (buzzer entre 5V e o coletor; diodo ainda fora)
//   GPIO1  -> botão -> GND                 (pull-up interno)
//   GPIO0  -> OUT do PIR AM312             (VCC no 3V3, GND no GND)
//
// NA ARDUINO IDE
//   Ferramentas > Placa > esp32 > "ESP32C3 Dev Module" (ou "Nologo ESP32C3 Super Mini")
//   Ferramentas > USB CDC On Boot > "Enabled"   (sem isso o Monitor Serial fica vazio)
//   Monitor Serial em 115200.
//   Se a gravação não começar: segure o botão BOOT da placa, aperte e solte RESET, solte o BOOT
//   e grave de novo. Depois de gravar, aperte RESET.

#define PINO_LED_R   3
#define PINO_LED_G   4
#define PINO_BUZZER 10
#define PINO_BOTAO   1
#define PINO_PIR     0

const unsigned long AQUECIMENTO_PIR_MS = 60000;  // o PIR precisa de ~1 min parado depois de ligar
const unsigned long PRAZO_BOTAO_MS     = 20000;  // tempo para apertar o botão
const unsigned long PRAZO_PIR_MS       = 30000;  // tempo para o PIR ver movimento depois do aquecimento

bool okLed = false, okBuzzer = false, okBotao = false, okPir = false;
unsigned long inicio;

void led(bool r, bool g) {
  digitalWrite(PINO_LED_R, r);
  digitalWrite(PINO_LED_G, g);
}

void bip(int ms) {
  digitalWrite(PINO_BUZZER, HIGH);
  delay(ms);
  digitalWrite(PINO_BUZZER, LOW);
}

// espera o usuário digitar s/n no Monitor Serial (Enter); sem resposta em 15 s conta como "n"
bool perguntar(const char* pergunta) {
  Serial.printf("%s (digite s ou n e Enter): ", pergunta);
  while (Serial.available()) Serial.read();
  unsigned long t = millis();
  while (millis() - t < 15000) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 's' || c == 'S') { Serial.println("s"); return true; }
      if (c == 'n' || c == 'N') { Serial.println("n"); return false; }
    }
    delay(10);
  }
  Serial.println("(sem resposta)");
  return false;
}

void testeLed() {
  Serial.println("\n1) LED RGB");
  for (int i = 0; i < 3; i++) {
    Serial.println("   vermelho..."); led(1, 0); delay(1000);
    Serial.println("   verde...");    led(0, 1); delay(1000);
    Serial.println("   amarelo (os dois)..."); led(1, 1); delay(1000);
    led(0, 0); delay(400);
  }
  okLed = perguntar("   O LED acendeu vermelho, verde e amarelo?");
  if (!okLed) Serial.println("   Confira: perna K (a mais longa) no GND, R1 em i11-i14, R2 em j12-j16, fios h6->h11 e h5->h12.");
}

void testeBuzzer() {
  Serial.println("\n2) Buzzer");
  for (int i = 0; i < 3; i++) { bip(150); delay(250); }
  delay(300);
  bip(800);
  okBuzzer = perguntar("   Ouviu 3 bips curtos e 1 longo?");
  if (!okBuzzer) Serial.println("   Confira: BC337 em d20 d21 d22 (E B C, lado reto para a coluna a), a20 no GND, buzzer + em a27 e d27 no 5V, fio c22->d24.");
}

void testeBotao() {
  Serial.println("\n3) Botao: aperte o botao agora (20 s)");
  unsigned long t = millis();
  int apertos = 0;
  bool anterior = HIGH;
  while (millis() - t < PRAZO_BOTAO_MS && apertos < 3) {
    bool agora = digitalRead(PINO_BOTAO);
    if (anterior == HIGH && agora == LOW) {
      apertos++;
      Serial.printf("   apertou (%d de 3)\n", apertos);
      led(0, 1); bip(60);
    }
    if (agora == HIGH) led(0, 0);
    anterior = agora;
    delay(30);   // filtra o repique do contato
  }
  led(0, 0);
  okBotao = apertos > 0;
  if (apertos == 0) Serial.println("   Nenhum aperto. Confira: perna A em i8, a perna na diagonal no trilho - (GND).");
  else if (apertos < 3) Serial.println("   Funcionou (aperte 3 vezes da proxima vez para conferir melhor).");
  else Serial.println("   OK.");
}

void testePir() {
  Serial.println("\n4) PIR: fique parado longe do sensor durante o aquecimento");
  while (millis() - inicio < AQUECIMENTO_PIR_MS) {
    unsigned long falta = (AQUECIMENTO_PIR_MS - (millis() - inicio)) / 1000;
    Serial.printf("   aquecendo... %lu s\n", falta);
    delay(5000);
  }
  Serial.println("   Pronto. Passe a mao na frente do PIR (30 s).");
  unsigned long t = millis();
  int deteccoes = 0;
  bool anterior = digitalRead(PINO_PIR);
  while (millis() - t < PRAZO_PIR_MS && deteccoes < 2) {
    bool agora = digitalRead(PINO_PIR);
    led(agora, 0);
    if (!anterior && agora) { deteccoes++; Serial.printf("   movimento detectado (%d)\n", deteccoes); }
    if (anterior && !agora) Serial.println("   parou");
    anterior = agora;
    delay(50);
  }
  led(0, 0);
  okPir = deteccoes > 0;
  if (!okPir) Serial.println("   Nada detectado. Confira a ordem VCC/OUT/GND embaixo do PIR, i4->j22 (3V3), h9->h23 e j24 no GND.");
}

void setup() {
  // buzzer e LED desligados antes de tudo
  pinMode(PINO_BUZZER, OUTPUT); digitalWrite(PINO_BUZZER, LOW);
  pinMode(PINO_LED_R, OUTPUT);
  pinMode(PINO_LED_G, OUTPUT);
  led(0, 0);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_PIR, INPUT);
  inicio = millis();

  Serial.begin(115200);
  delay(2500);   // tempo para o Monitor Serial conectar pela USB
  Serial.println("\n=== Teste da protoboard (ESP32-C3, sem AHT20) ===");

  testeLed();
  testeBuzzer();
  testeBotao();
  testePir();

  Serial.println("\n=== RESULTADO ===");
  Serial.printf("LED RGB : %s\n", okLed    ? "OK" : "FALHOU");
  Serial.printf("Buzzer  : %s\n", okBuzzer ? "OK" : "FALHOU");
  Serial.printf("Botao   : %s\n", okBotao  ? "OK" : "FALHOU");
  Serial.printf("PIR     : %s\n", okPir    ? "OK" : "FALHOU");
  Serial.println("AHT20   : nao testado (sem solda)");
  Serial.println("\nModo livre: PIR acende o vermelho, botao acende o verde e bipa. Aperte RESET para repetir.");
}

void loop() {
  // modo livre, para ficar testando a vontade
  static bool botaoAnterior = HIGH;
  bool botao = digitalRead(PINO_BOTAO);
  bool pir = digitalRead(PINO_PIR);
  led(pir, botao == LOW);
  if (botaoAnterior == HIGH && botao == LOW) bip(80);
  botaoAnterior = botao;
  delay(30);
}
