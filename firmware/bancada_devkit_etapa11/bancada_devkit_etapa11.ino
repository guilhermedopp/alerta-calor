// Firmware da caixa de alerta de calor - versao de BANCADA para o ESP32 DevKit (WROOM-32), 29/10/2026 - Etapa 11 (configuracao)
// Baseado na Etapa 10 (PIR), adaptado ao material emprestado pelo prof. Edison:
//   - sensor DHT11 no D26 (na caixa final: AHT20 por I2C)
//   - LED azul da propria placa (GPIO2) no lugar do LED RGB:
//       apagado = normal | piscando devagar = atencao | aceso direto = perigo | piscando rapido = sensor com falha
//   - botao BOOT da propria placa (GPIO0) como botao de silenciar
//   - buzzer no D25 com resistor de 220 ohm (se ainda nao estiver ligado, o resto funciona igual)
//   - PIR HC-SR501 no D27 (VCC no trilho +/VIN, GND no trilho -, jumper em H, potenciometros no minimo)
// Novidades da Etapa 11 (pedido do prof. Edison):
//   - pagina de configuracao estilo roteador (http://IP/config, usuario admin): Wi-Fi, Telegram,
//     nome do local, limites, tempos em segundos (inclusive a janela de presenca) e senha do painel
//   - tudo gravado na memoria do ESP32 (Preferences/NVS, a "EEPROM" do ESP32): nao precisa mais
//     editar o codigo para trocar de rede, e o codigo nao precisa mais ter token nem senha
//   - sem Wi-Fi configurado, Wi-Fi errado ou BOOT apertado nos 3 s depois de ligar: a placa cria a
//     rede "CaixaAlerta-XXXX" (senha padrao caixa1234) e a pagina abre sozinha no celular
//   - tenta reconectar ao Wi-Fi a cada 60 s; 2 min sem Wi-Fi liga a rede de configuracao
//   - o alerta local (LED e buzzer) continua funcionando em todos esses casos
// Novidades da Etapa 10 (aprovada no teste definitivo de 28/10):
//   - presenca ligada: as mensagens dizem se houve movimento no comodo (so INFORMA, nunca cancela alerta)
//   - PIR ignorado nos primeiros 60 s (aquecimento do sensor)
//   - eventos "presenca detectada" / "sem presenca" no eventos.csv
//   - coluna "presenca" no dados.csv e cartao de presenca na pagina
//   - o dados.csv antigo (sem a coluna) vira dados_etapa9.csv e continua para baixar
//   - se o NTP falhar no inicio, tenta de novo a cada 30 s (antes ficava "+123s" ate reiniciar)
// Novidades da Etapa 9 em relacao a Etapa 8:
//   - leituras fora da faixa fisica (ex.: umidade > 100 % por sopro/condensacao) sao descartadas
//   - para DESCER de nivel, o indice precisa ficar abaixo do limite por TEMPO_DESCIDA
//     (evita a sequencia perigo/atencao/perigo de mensagens vista no teste de 26/10)
//
// Bibliotecas (Ferramentas > Gerenciar Bibliotecas):
//   - "DHT sensor library" (Adafruit) + "Adafruit Unified Sensor"
//   - "UniversalTelegramBot" (Brian Lough)
//   - "ArduinoJson" (Benoit Blanchon)
//
// Versao sem token: o Wi-Fi, o token e o chat ID sao configurados pela pagina (http://IP/config).

#include <DHT.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <esp_system.h>   // esp_reset_reason(): por que a placa (re)iniciou
#include <WebServer.h>     // Etapa 9: pagina web na propria placa
#include <ESPmDNS.h>       // Etapa 9: endereco http://alerta.local
#include <LittleFS.h>      // Etapa 9: arquivos de log na flash
#include <time.h>          // Etapa 9: data e hora pela internet (NTP)
#include <Preferences.h>   // Etapa 11: configuracao gravada na memoria (NVS = "EEPROM" do ESP32)
#include <DNSServer.h>     // Etapa 11: portal cativo (a pagina abre sozinha no celular)

// ---- Configuração (Etapa 11) ----
// Os valores escritos aqui e nas seções abaixo são só os PADRÕES DE FÁBRICA. Os valores usados
// de verdade ficam gravados na memória do ESP32 e são trocados pela página de configuração.
// Wi-Fi vazio = na primeira vez a placa abre a rede de configuração "CaixaAlerta-XXXX".
String WIFI_NOME    = "";
String WIFI_SENHA   = "";
String BOT_TOKEN    = "";                    // criado no @BotFather
String CHAT_ID      = "";                    // descoberto no @userinfobot
String NOME_LOCAL   = "Bancada (DevKit)";   // aparece na mensagem (ex.: "ILPI - Quarto 3")
String SENHA_PAINEL = "caixa1234";          // usuário admin; também é a senha da rede de configuração

// ---- Pinos ----
const int PINO_LED      = 2;    // LED azul da placa
const int PINO_BOTAO    = 0;    // botao BOOT da placa (aperta = LOW)
const int PINO_BUZZER   = 25;   // D25 -> 220 ohm -> buzzer (+); buzzer (-) no GND
const int PINO_SENSOR   = 26;   // D26 -> DATA do DHT11 (ESP plugado na protoboard)
bool USA_PIR            = true;    // PIR HC-SR501 (configurável)
const int PINO_PIR      = 27;      // D27 -> OUT do PIR (saida de 3,3 V: liga direto)
unsigned long AQUECIMENTO_PIR = 60UL * 1000UL;   // o PIR da sinais falsos no 1o minuto (configurável)

// ---- Limites de risco (índice de calor, °C) — provisórios (NOAA), configuráveis ----
float LIMITE_ATENCAO = 27.0;
float LIMITE_PERIGO  = 32.0;
float HISTERESE      = 1.0;
// Para baixar de nivel, o indice tem que ficar abaixo do limite por este tempo.
// Subir de nivel continua imediato (seguranca primeiro).
unsigned long TEMPO_DESCIDA = 600UL * 1000UL;   // padrão 10 min (teste: 60 s) - configurável

// ---- Ajustes ----
const int FREQ_BIPE    = 2000;
const int DURACAO_BIPE = 150;
const unsigned long DEBOUNCE_MS       = 50;
unsigned long INTERVALO_LEITURA = 2000;          // configurável (s na página)
const unsigned long INTERVALO_BIPE    = 5000;

// Lembrete: padrão 30 min (para testar: 60 s) - configurável
unsigned long INTERVALO_LEMBRETE = 1800UL * 1000UL;
// Falha do sensor: quantas leituras erradas seguidas antes de avisar (10 x 2 s = 20 s)
const int MAX_FALHAS = 10;

// MODO DE TESTE: true = finge que o sensor falhou (sem precisar tirar o fio).
// No Wokwi, tirar o fio do DHT22 trava a biblioteca e reinicia a placa (watchdog).
// Deixe false no uso normal.
const bool TESTE_FALHA_SENSOR = false;

// ---- Aquecimento do próprio aparelho (Etapa 7) ----
// Calibração: quanto a caixa lê A MAIS que um termo-higrômetro de referência (°C).
// Medir no teste de acurácia e preencher aqui. 0 = sem correção.
float OFFSET_TEMP = 0.0;                         // configurável
// Sensor interno do chip do ESP32: acima disso, o próprio aparelho está esquentando.
const float LIMITE_TEMP_CHIP = 70.0;    // °C
const float HIST_TEMP_CHIP   = 5.0;     // precisa cair 5 °C abaixo do limite para normalizar
// TESTE no Wokwi: valor > 0 finge essa temperatura no chip (ex.: 75). 0 = usa o sensor real.
const float TESTE_TEMP_CHIP  = 0.0;
// Subida rápida: um cômodo esquenta devagar; um salto grande em pouco tempo indica
// sol direto na caixa ou aquecimento do aparelho.
const unsigned long JANELA_SUBIDA = 1UL * 60UL * 1000UL;   // TESTE: 1 min. Uso real: 10 min
const float LIMITE_SUBIDA = 3.0;                           // °C dentro da janela
const unsigned long PASSO_HIST = 10000;                     // guarda 1 leitura a cada 10 s
const int HIST_MAX = 60;                                    // até 10 min de histórico

// ---- Objetos ----
DHT dht(PINO_SENSOR, DHT11);
WiFiClientSecure clienteSeguro;                    // conexão HTTPS
UniversalTelegramBot* bot = nullptr;               // criado no setup, depois de ler o token da memória

// ---- Cores e níveis ----
enum Cor   { VERDE, AMARELO, VERMELHO, APAGADO };
enum Nivel { NORMAL, ATENCAO, PERIGO };

// Placa de bancada: um LED so. AMARELO = piscando devagar (tratado em atualizarLed).
Cor corAtual = APAGADO;
void mostrarCor(Cor cor) { corAtual = cor; }
const char* nomeDoNivel(Nivel n) {
  switch (n) {
    case NORMAL:  return "NORMAL";
    case ATENCAO: return "ATENCAO";
    case PERIGO:  return "PERIGO";
  }
  return "?";
}

// ---- Estado do programa ----
Nivel nivelAtual = NORMAL;
bool silenciado = false;
bool estadoEstavel = HIGH;
bool ultimaLeitura = HIGH;
unsigned long ultimaMudancaMs = 0;
unsigned long ultimaLeituraSensorMs = 0;
unsigned long ultimoBipeMs = 0;
unsigned long inicioPerigoMs = 0;     // quando o perigo atual começou
unsigned long ultimoLembreteMs = 0;   // quando foi o último aviso (entrada ou lembrete)
int falhasSeguidas = 0;               // leituras com erro em sequência
bool sensorComFalha = false;          // já avisamos que o sensor parou?
float ultimaTemp = 0, ultimaUmid = 0, ultimoIndice = 0;   // última leitura válida

// Estado da Etapa 7
bool aparelhoQuente = false;       // chip acima do limite?
bool chipSemSensor = false;        // o sensor interno não respondeu (ex.: simulador)
float ultimaTempChip = NAN;
bool subidaSuspeita = false;       // houve subida rápida recente?

// ---- Presença (Etapa 8) ----
// O PIR detecta MOVIMENTO, não presença: uma pessoa acamada ou dormindo pode não ser
// detectada. Por isso a presença só INFORMA o cuidador; nunca cancela nem silencia alertas.
// Janela: considera "há gente" se houve movimento nos últimos X minutos.
unsigned long JANELA_PRESENCA = 900UL * 1000UL;   // padrão 15 min (teste: 60 s) - configurável
unsigned long ultimoMovimentoMs = 0;   // 0 = nenhum movimento desde que ligou
bool movimentoAgora = false;           // estado anterior do pino (para detectar a borda)
bool presencaAnterior = false;         // para registrar so as mudancas no eventos.csv
unsigned long movimentosTotal = 0;     // quantos movimentos desde que ligou
void registrarEvento(const String& texto);   // definida mais abaixo (Etapa 9)
String textoTempo(unsigned long ms);         // definida mais abaixo (Etapa 11)

bool haPresenca() {
  if (!USA_PIR) return false;
  return ultimoMovimentoMs != 0 && millis() - ultimoMovimentoMs < JANELA_PRESENCA;
}

// Texto para as mensagens: "sim (movimento há 2 min)" ou "não detectado nos últimos 15 min"
String textoPresenca() {
  if (!USA_PIR) return "sem sensor de presença nesta montagem";
  if (millis() < AQUECIMENTO_PIR) return "sensor de presença aquecendo (1 min)";
  if (ultimoMovimentoMs == 0) return "sem movimento desde que a caixa ligou";
  unsigned long passou = millis() - ultimoMovimentoMs;
  if (haPresenca()) return passou < 10000UL ? String("sim (movimento agora)") : "sim (movimento há " + textoTempo(passou) + ")";
  return "não detectado nos últimos " + textoTempo(JANELA_PRESENCA);
}

// Lê o PIR a cada volta do loop (o HC-SR501 desta bancada mantém a saída em HIGH ~1 s por movimento;
// a janela JANELA_PRESENCA é que transforma esses pulsos curtos em "há gente").
void verificarPresenca() {
  if (!USA_PIR) return;
  if (millis() < AQUECIMENTO_PIR) return;          // aquecimento: ignora o sensor
  bool agora = digitalRead(PINO_PIR) == HIGH;
  if (agora) ultimoMovimentoMs = millis();
  if (agora && !movimentoAgora) {
    movimentosTotal++;
    Serial.print(">>> Movimento detectado ("); Serial.print(movimentosTotal); Serial.println(")");
  }
  movimentoAgora = agora;

  bool presenca = haPresenca();
  if (presenca != presencaAnterior) {
    presencaAnterior = presenca;
    registrarEvento(presenca ? String("presenca detectada")
                             : "sem presenca (nenhum movimento em " + textoTempo(JANELA_PRESENCA) + ")");
  }
}
unsigned long abaixoDesdeMs = 0;   // desde quando o nivel calculado esta abaixo do atual
float histTemp[HIST_MAX];          // histórico circular de temperaturas
int histPos = 0, histCount = 0;
unsigned long ultimoHistMs = 0;

Nivel calcularNivel(float indice, Nivel atual) {
  switch (atual) {
    case NORMAL:
      if (indice >= LIMITE_PERIGO)  return PERIGO;
      if (indice >= LIMITE_ATENCAO) return ATENCAO;
      return NORMAL;
    case ATENCAO:
      if (indice >= LIMITE_PERIGO)              return PERIGO;
      if (indice <  LIMITE_ATENCAO - HISTERESE) return NORMAL;
      return ATENCAO;
    case PERIGO:
      if (indice >= LIMITE_PERIGO - HISTERESE)  return PERIGO;
      if (indice >= LIMITE_ATENCAO)             return ATENCAO;
      return NORMAL;
  }
  return atual;
}

void mostrarNivel(Nivel n) {
  switch (n) {
    case NORMAL:  mostrarCor(VERDE);    break;
    case ATENCAO: mostrarCor(AMARELO);  break;
    case PERIGO:  mostrarCor(VERMELHO); break;
  }
}

// ---------------------------------------------------------------- Wi-Fi
void conectarWiFi() {
  if (WIFI_NOME.length() == 0) { Serial.println("Nenhuma rede Wi-Fi configurada."); return; }
  Serial.print("Conectando ao Wi-Fi "); Serial.print(WIFI_NOME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NOME.c_str(), WIFI_SENHA.c_str());

  // Espera até 15 s. Se não conectar, a caixa segue funcionando só com luz e som.
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 15000) {
    delay(250);   // aqui pode usar delay: é só na inicialização
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(" conectado! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" falhou. Alerta local continua; abrindo a rede de configuracao.");
  }
}

// ---------------------------------------------------------------- Telegram
// Monta e envia a mensagem. Retorna true se o Telegram aceitou.
bool enviarTelegram(Nivel nivel, float temperatura, float umidade, float indice) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("(sem Wi-Fi: mensagem não enviada)");
    return false;
  }
  if (!bot || BOT_TOKEN.length() == 0 || CHAT_ID.length() == 0) {
    Serial.println("(Telegram nao configurado: mensagem não enviada)");
    return false;
  }

  String texto;
  switch (nivel) {
    case PERIGO:  texto = "🔴 PERIGO DE CALOR\n"; break;
    case ATENCAO: texto = "🟡 Atenção ao calor\n"; break;
    case NORMAL:  texto = "🟢 Calor voltou ao normal\n"; break;
  }
  texto += "Local: " + String(NOME_LOCAL) + "\n";
  texto += "Temperatura: " + String(temperatura, 1) + " °C\n";
  texto += "Umidade: " + String(umidade, 0) + " %\n";
  texto += "Índice de calor: " + String(indice, 1) + " °C";
  texto += "\nPessoas no cômodo: " + textoPresenca();
  if (nivel == PERIGO) {
    texto += "\n\nOfereça água, leve para local fresco e observe sinais de mal-estar.";
  }

  bool ok = bot->sendMessage(CHAT_ID, texto, "");
  Serial.println(ok ? "Telegram: mensagem enviada" : "Telegram: falha no envio");
  return ok;
}

// Envia um texto livre (usado no lembrete e no aviso de falha).
bool enviarTexto(const String& texto) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("(sem Wi-Fi: mensagem não enviada)");
    return false;
  }
  if (!bot || BOT_TOKEN.length() == 0 || CHAT_ID.length() == 0) {
    Serial.println("(Telegram nao configurado: mensagem não enviada)");
    return false;
  }
  bool ok = bot->sendMessage(CHAT_ID, texto, "");
  Serial.println(ok ? "Telegram: mensagem enviada" : "Telegram: falha no envio");
  return ok;
}

// ---------------------------------------------------------------- lembrete
// Chamada a cada volta do loop. Se o perigo já dura INTERVALO_LEMBRETE desde o
// último aviso, avisa de novo e reativa o bipe.
void verificarLembrete() {
  if (nivelAtual != PERIGO) return;
  if (abaixoDesdeMs != 0) return;          // indice ja abaixo do limite, aguardando para descer: nao lembra
  if (sensorComFalha) return;              // sem leitura valida, o aviso de falha ja foi enviado
  if (millis() - ultimoLembreteMs < INTERVALO_LEMBRETE) return;

  ultimoLembreteMs = millis();
  unsigned long minutos = (millis() - inicioPerigoMs) / 60000UL;

  Serial.print(">>> Lembrete: perigo continua ha ");
  Serial.print(minutos);
  Serial.println(" min");

  silenciado = false;   // o bipe volta, mesmo que alguém tenha silenciado
  ultimoBipeMs = 0;

  String texto = "⏰ PERIGO CONTINUA há " + String(minutos) + " min\n";
  texto += "Local: " + String(NOME_LOCAL) + "\n";
  texto += "Índice de calor: " + String(ultimoIndice, 1) + " °C\n\n";
  texto += "Pessoas no cômodo: " + textoPresenca() + "\n\n";
  texto += "Verifique as pessoas no ambiente.";
  enviarTexto(texto);
}

// ---------------------------------------------------------------- Etapa 7
// Lê o sensor interno do chip. Retorna NAN se não estiver disponível.
float lerTempChip() {
  if (TESTE_TEMP_CHIP > 0) return TESTE_TEMP_CHIP;
  float t = temperatureRead();          // sensor interno do chip (no ESP32 classico e bem impreciso)
  if (isnan(t) || t <= 0 || t > 150) return NAN;
  return t;
}

// Avisa (uma vez) se o próprio aparelho está esquentando, e quando normaliza.
void verificarAparelho() {
  float t = lerTempChip();
  ultimaTempChip = t;
  if (isnan(t)) {
    if (!chipSemSensor) { chipSemSensor = true; Serial.println("(sensor interno do chip indisponivel)"); }
    return;
  }
  if (!aparelhoQuente && t >= LIMITE_TEMP_CHIP) {
    aparelhoQuente = true;
    Serial.println(">>> Aparelho aquecendo!");
    enviarTexto("🔥 Aparelho aquecendo\nLocal: " + String(NOME_LOCAL) +
                "\nChip a " + String(t, 0) + " °C. As leituras do ambiente podem estar mais altas que o real." +
                "\nVerifique se a caixa está no sol ou com a ventilação tampada.");
  } else if (aparelhoQuente && t < LIMITE_TEMP_CHIP - HIST_TEMP_CHIP) {
    aparelhoQuente = false;
    enviarTexto("✅ Temperatura do aparelho normalizada\nLocal: " + String(NOME_LOCAL));
  }
}

// Guarda a temperatura no histórico e verifica se subiu rápido demais.
void verificarSubida(float temperatura) {
  if (ultimoHistMs != 0 && millis() - ultimoHistMs < PASSO_HIST) return;
  ultimoHistMs = millis();
  histTemp[histPos] = temperatura;
  histPos = (histPos + 1) % HIST_MAX;
  if (histCount < HIST_MAX) histCount++;

  int n = (int)(JANELA_SUBIDA / PASSO_HIST);   // quantas leituras cabem na janela
  if (n > HIST_MAX - 1) n = HIST_MAX - 1;
  if (histCount <= n) return;                  // ainda não há histórico suficiente
  float antiga = histTemp[(histPos - 1 - n + 2 * HIST_MAX) % HIST_MAX];
  float subida = temperatura - antiga;

  if (!subidaSuspeita && subida >= LIMITE_SUBIDA) {
    subidaSuspeita = true;
    Serial.print(">>> Subida rapida: +"); Serial.print(subida, 1); Serial.println(" C");
    enviarTexto("⚠️ Subida rápida da temperatura\nLocal: " + String(NOME_LOCAL) +
                "\n+" + String(subida, 1) + " °C em " + String(JANELA_SUBIDA / 60000UL) + " min." +
                "\nPode ser sol direto na caixa ou aquecimento do aparelho. Confira o local." +
                "\nOs alertas continuam funcionando normalmente.");
  } else if (subidaSuspeita && subida < LIMITE_SUBIDA / 2) {
    subidaSuspeita = false;
  }
}

// ================================================================ Etapa 9: servidor de logs
// Ideia do prof. Edison: a propria placa guarda o historico e mostra numa pagina web.
// - Pagina:  http://alerta.local  (ou http://IP_DA_PLACA, que vai na mensagem "ligada")
//   O celular/computador precisa estar na MESMA rede Wi-Fi da placa.
// - /dados.csv  : todas as leituras (data/hora, temp, umid, indice, nivel)
// - /eventos.csv: mudancas de nivel, silenciar, falha do sensor, reinicios
// - Memoria: LittleFS na flash. Log circular: quando o arquivo passa de LIMITE_ARQUIVO,
//   ele vira o "antigo" (o antigo anterior e apagado). O download junta os dois.
// - A pagina e so de leitura e nunca mostra o token.
unsigned long INTERVALO_LOG = 300UL * 1000UL;   // padrão 5 min (teste: 10 s) - configurável
const size_t LIMITE_ARQUIVO = 300UL * 1024UL;        // ~300 KB por arquivo (cabe com folga na flash)
const int PONTOS_GRAFICO = 180;                      // ultimas leituras guardadas na RAM para o grafico

WebServer servidor(80);
bool temArquivos = false;
bool horaOk = false;
unsigned long ultimoLogMs = 0;
float gTemp[PONTOS_GRAFICO], gIc[PONTOS_GRAFICO];
long gSeg[PONTOS_GRAFICO];                           // segundos desde que ligou
int gPos = 0, gCount = 0;

String agoraTexto() {                                // "2026-10-26 19:30:05" ou "+123s" sem relogio
  struct tm t;
  if (horaOk && getLocalTime(&t, 50)) {
    char b[24]; strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &t); return String(b);
  }
  return "+" + String(millis() / 1000UL) + "s";
}

void girarSeCheio(const char* caminho, const char* antigo) {
  File f = LittleFS.open(caminho, "r");
  if (!f) return;
  size_t tam = f.size(); f.close();
  if (tam < LIMITE_ARQUIVO) return;
  LittleFS.remove(antigo);
  LittleFS.rename(caminho, antigo);
}

void anexar(const char* caminho, const char* antigo, const char* cabecalho, const String& linha) {
  if (!temArquivos) return;
  girarSeCheio(caminho, antigo);
  bool novo = !LittleFS.exists(caminho);
  File f = LittleFS.open(caminho, "a");
  if (!f) return;
  if (novo) f.println(cabecalho);
  f.println(linha);
  f.close();
}

void registrarEvento(const String& texto) {
  Serial.print("[evento] "); Serial.println(texto);
  anexar("/eventos.csv", "/eventos_antigo.csv", "data_hora;evento", agoraTexto() + ";" + texto);
}

void registrarLeitura(float t, float u, float ic) {
  gTemp[gPos] = t; gIc[gPos] = ic; gSeg[gPos] = millis() / 1000UL;
  gPos = (gPos + 1) % PONTOS_GRAFICO; if (gCount < PONTOS_GRAFICO) gCount++;
  if (ultimoLogMs != 0 && millis() - ultimoLogMs < INTERVALO_LOG) return;
  ultimoLogMs = millis();
  anexar("/dados.csv", "/dados_antigo.csv", "data_hora;temperatura_C;umidade_pct;indice_calor_C;nivel;presenca",
         agoraTexto() + ";" + String(t, 1) + ";" + String(u, 0) + ";" + String(ic, 1) + ";" + nomeDoNivel(nivelAtual) +
         ";" + (USA_PIR ? (haPresenca() ? "sim" : "nao") : "-"));
}

// envia o antigo + o atual como um arquivo so (o cabecalho do segundo e pulado)
void enviarCsv(const char* atual, const char* antigo, const char* nomeArquivo) {
  servidor.sendHeader("Content-Disposition", String("attachment; filename=") + nomeArquivo);
  servidor.setContentLength(CONTENT_LENGTH_UNKNOWN);
  servidor.send(200, "text/csv; charset=utf-8", "");
  bool primeiro = true;
  const char* lista[2] = { antigo, atual };
  for (int i = 0; i < 2; i++) {
    File f = LittleFS.open(lista[i], "r");
    if (!f) continue;
    if (!primeiro) f.readStringUntil('\n');           // pula o cabecalho repetido
    primeiro = false;
    uint8_t buf[512];
    while (f.available()) { size_t n = f.read(buf, sizeof buf); servidor.sendContent((const char*)buf, n); }
    f.close();
  }
  servidor.sendContent("");
}

String jsonEsc(const String& s);   // Etapa 11
void paginaInicial() {
  // celular conectado na rede de configuração: vai direto para a configuração
  extern bool apAtivo;
  if (apAtivo && servidor.client().localIP() == WiFi.softAPIP()) {
    servidor.sendHeader("Location", "http://192.168.4.1/config"); servidor.send(302, "text/plain", ""); return;
  }
  String h;
  h.reserve(4000);
  h += F("<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Caixa de alerta</title><style>"
         "body{font-family:system-ui,Arial;margin:0;background:#f6f3ee;color:#222}main{max-width:720px;margin:auto;padding:16px}"
         ".c{background:#fff;border-radius:12px;padding:14px;margin:10px 0;box-shadow:0 1px 3px #0002}"
         ".g{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.v{font-size:1.6em;font-weight:700}"
         "#niv{font-size:1.4em;font-weight:700;padding:8px 12px;border-radius:8px;display:inline-block}"
         "a{display:inline-block;margin:6px 8px 0 0;padding:8px 12px;background:#8a2c0d;color:#fff;border-radius:8px;text-decoration:none}"
         "small{color:#666}canvas{width:100%;height:220px}</style></head><body><main>"
         "<h2>Caixa de alerta de calor</h2><div class='c'><div id='loc'></div><div id='niv'>...</div> <span id='ext'></span>"
         "<div class='g' style='margin-top:10px'><div><small>Temperatura</small><div class='v' id='t'>-</div></div>"
         "<div><small>Umidade</small><div class='v' id='u'>-</div></div><div><small>Índice de calor</small><div class='v' id='i'>-</div></div></div>"
         "<div style='margin-top:10px'><small>Presença no cômodo</small><div id='pres' style='font-weight:700'>-</div></div>"
         "<small id='hora'></small></div>"
         "<div class='c'><b>Últimas leituras</b> <small>(vermelho: índice de calor · cinza: temperatura)</small><canvas id='cv' width='680' height='220'></canvas></div>"
         "<div class='c'><b>Baixar registros</b><br><a href='/dados.csv'>Leituras (CSV)</a><a href='/eventos.csv'>Eventos (CSV)</a><a href='/dados_etapa9.csv' id='old' style='display:none'>Leituras antigas (Etapa 9)</a><a href='/config'>Configurações</a>"
         "<br><small id='mem'></small></div></main><script>"
         "let L=[27,32];const cores={NORMAL:['#2e7d32','#e8f5e9'],ATENCAO:['#8a6d00','#fff6cc'],PERIGO:['#b71c1c','#fde0de']};"
         "async function at(){try{const r=await fetch('/api/agora');const d=await r.json();"
         "loc.textContent=d.local;niv.textContent=d.falha?'SENSOR COM FALHA':d.nivel+(d.silenciado?' (silenciado)':'');"
         "const c=cores[d.nivel]||['#555','#eee'];niv.style.color=d.falha?'#555':c[0];niv.style.background=d.falha?'#eee':c[1];"
         "t.textContent=d.temp.toFixed(1)+' °C';u.textContent=d.umid.toFixed(0)+' %';i.textContent=d.ic.toFixed(1)+' °C';"
         "hora.textContent='Atualizado: '+d.hora+' · ligada há '+Math.round(d.seg/60)+' min';"
         "mem.textContent='Memória usada: '+d.usado+' de '+d.total+' KB';pres.textContent=d.presenca;L=[d.la,d.lp];old.style.display=d.antigo?'inline-block':'none';"
         "const g=await (await fetch('/api/grafico')).json();desenha(g)}catch(e){hora.textContent='Sem conexão com a caixa'}}"
         "function desenha(g){const x=cv.getContext('2d'),W=cv.width,H=cv.height;x.clearRect(0,0,W,H);if(g.ic.length<2)return;"
         "const all=g.ic.concat(g.t),mn=Math.floor(Math.min(...all)-1),mx=Math.ceil(Math.max(...all)+1),"
         "Y=v=>H-20-(v-mn)/(mx-mn)*(H-30),X=k=>40+k/(g.ic.length-1)*(W-50);x.font='11px sans-serif';x.fillStyle='#666';"
         "[[L[0],'#e0b000'],[L[1],'#d32f2f']].forEach(([v,c])=>{if(v>mn&&v<mx){x.strokeStyle=c;x.setLineDash([4,4]);x.beginPath();x.moveTo(40,Y(v));x.lineTo(W-10,Y(v));x.stroke();x.setLineDash([]);x.fillText(v+'°',8,Y(v)+4)}});"
         "x.fillText(mx+'°',8,14);x.fillText(mn+'°',8,H-20);"
         "[[g.t,'#999'],[g.ic,'#c62828']].forEach(([s,c])=>{x.strokeStyle=c;x.lineWidth=2;x.beginPath();s.forEach((v,k)=>k?x.lineTo(X(k),Y(v)):x.moveTo(X(k),Y(v)));x.stroke()})}"
         "at();setInterval(at,5000);</script></body></html>");
  servidor.send(200, "text/html; charset=utf-8", h);
}

void apiAgora() {
  String j = "{";
  j += "\"local\":\"" + jsonEsc(NOME_LOCAL) + "\",";
  j += "\"la\":" + String(LIMITE_ATENCAO, 1) + ",\"lp\":" + String(LIMITE_PERIGO, 1) + ",";
  j += "\"nivel\":\"" + String(nomeDoNivel(nivelAtual)) + "\",";
  j += "\"silenciado\":" + String(silenciado ? "true" : "false") + ",";
  j += "\"falha\":" + String(sensorComFalha ? "true" : "false") + ",";
  j += "\"temp\":" + String(ultimaTemp, 1) + ",\"umid\":" + String(ultimaUmid, 0) + ",\"ic\":" + String(ultimoIndice, 1) + ",";
  j += "\"hora\":\"" + agoraTexto() + "\",\"seg\":" + String(millis() / 1000UL) + ",";
  j += "\"usado\":" + String((unsigned long)(temArquivos ? LittleFS.usedBytes() / 1024 : 0)) + ",";
  j += "\"total\":" + String((unsigned long)(temArquivos ? LittleFS.totalBytes() / 1024 : 0)) + ",";
  j += "\"presenca\":\"" + textoPresenca() + (USA_PIR ? String(" · ") + String(movimentosTotal) + " movimentos desde que ligou" : String("")) + "\",";
  j += "\"antigo\":" + String(temArquivos && LittleFS.exists("/dados_etapa9.csv") ? "true" : "false") + "}";
  servidor.send(200, "application/json", j);
}

void apiGrafico() {
  String a = "{\"t\":[", b = "],\"ic\":[";
  int ini = (gPos - gCount + PONTOS_GRAFICO) % PONTOS_GRAFICO;
  for (int k = 0; k < gCount; k++) {
    int i = (ini + k) % PONTOS_GRAFICO;
    if (k) { a += ","; b += ","; }
    a += String(gTemp[i], 1); b += String(gIc[i], 1);
  }
  servidor.send(200, "application/json", a + b + "]}");
}

// ================================================================ Etapa 11: configuração pela página (estilo roteador)
// Pedido do prof. Edison: gravar na memória do ESP32 a rede Wi-Fi, os tempos, os limites etc.
// pela própria página, sem precisar mudar o código e gravar de novo.
// - Memória: Preferences (NVS). É a "EEPROM" do ESP32: no ESP32 a biblioteca EEPROM é emulada
//   em cima da NVS, que é mais segura (cada valor tem nome, e uma queda de energia no meio
//   da gravação não corrompe os outros). Sobrevive a desligar a placa e a gravar outro código.
// - Página:  http://IP_DA_PLACA/config  (usuário admin, senha do painel)
// - Sem Wi-Fi configurado, com Wi-Fi errado ou apertando BOOT nos 3 s depois de ligar,
//   a placa cria a própria rede "CaixaAlerta-XXXX" (senha = senha do painel) e a página
//   abre sozinha no celular (portal cativo), como na configuração de um roteador.
// - A página nunca mostra a senha do Wi-Fi, o token nem a senha do painel: campo vazio = manter.
// - Enquanto isso, o alerta local (LED e buzzer) continua funcionando normalmente.

const char* VERSAO = "Etapa 11.3 (29/10/2026)";
const char* SENHA_AP_PADRAO = "caixa1234";      // se a senha do painel tiver menos de 8 caracteres

Preferences prefs;
DNSServer dns;
bool apAtivo = false;             // rede de configuração ligada?
bool modoConfigForcado = false;   // entrou pelo botão BOOT: a rede fica ligada até salvar
bool jaConectou = false;          // aoConectar() já rodou?
bool avisarLigada = false;        // mandar "ligada" no Telegram quando conectar
unsigned long ultimaTentativaWiFi = 0;
unsigned long conectadoDesdeMs = 0, desconectadoDesdeMs = 0;
unsigned long reiniciarEmMs = 0;  // 0 = não reiniciar
String nomeAP;

void carregarConfig() {
  prefs.begin("caixa", true);                      // só leitura; na 1a vez não existe e fica o padrão
  WIFI_NOME    = prefs.getString("ssid",   WIFI_NOME);
  WIFI_SENHA   = prefs.getString("wpass",  WIFI_SENHA);
  BOT_TOKEN    = prefs.getString("token",  BOT_TOKEN);
  CHAT_ID      = prefs.getString("chat",   CHAT_ID);
  NOME_LOCAL   = prefs.getString("local",  NOME_LOCAL);
  SENHA_PAINEL = prefs.getString("painel", SENHA_PAINEL);
  LIMITE_ATENCAO = prefs.getFloat("lat", LIMITE_ATENCAO);
  LIMITE_PERIGO  = prefs.getFloat("lpe", LIMITE_PERIGO);
  HISTERESE      = prefs.getFloat("his", HISTERESE);
  OFFSET_TEMP    = prefs.getFloat("off", OFFSET_TEMP);
  USA_PIR        = prefs.getBool("pir", USA_PIR);
  // tempos guardados em SEGUNDOS (como aparecem na página); o programa usa milissegundos
  JANELA_PRESENCA    = prefs.getULong("jpres", JANELA_PRESENCA / 1000UL) * 1000UL;
  AQUECIMENTO_PIR    = prefs.getULong("aqpir", AQUECIMENTO_PIR / 1000UL) * 1000UL;
  TEMPO_DESCIDA      = prefs.getULong("desc",  TEMPO_DESCIDA / 1000UL) * 1000UL;
  INTERVALO_LEMBRETE = prefs.getULong("lemb",  INTERVALO_LEMBRETE / 1000UL) * 1000UL;
  INTERVALO_LOG      = prefs.getULong("log",   INTERVALO_LOG / 1000UL) * 1000UL;
  INTERVALO_LEITURA  = prefs.getULong("leit",  INTERVALO_LEITURA / 1000UL) * 1000UL;
  prefs.end();
}

// "45 s", "15 min" ou "1 min 30 s" (para as mensagens)
String textoTempo(unsigned long ms) {
  unsigned long s = ms / 1000UL;
  if (s < 60) return String(s) + " s";
  if (s % 60 == 0) return String(s / 60) + " min";
  return String(s / 60) + " min " + String(s % 60) + " s";
}

String jsonEsc(const String& s) {
  String o; o.reserve(s.length() + 8);
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += c;
  }
  return o;
}

bool autorizado() {
  if (servidor.authenticate("admin", SENHA_PAINEL.c_str())) return true;
  servidor.requestAuthentication(BASIC_AUTH, "Caixa de alerta", "Senha incorreta");
  return false;
}

// ---- rede própria de configuração (ponto de acesso) ----
void iniciarAP() {
  if (apAtivo) return;
  WiFi.mode(WIFI_AP_STA);                          // AP + cliente: dá para procurar redes e continuar tentando
  uint8_t mac[6]; WiFi.macAddress(mac);
  char nome[24]; snprintf(nome, sizeof nome, "CaixaAlerta-%02X%02X", mac[4], mac[5]);
  nomeAP = nome;
  String senhaAP = SENHA_PAINEL.length() >= 8 ? SENHA_PAINEL : String(SENHA_AP_PADRAO);
  WiFi.softAP(nomeAP.c_str(), senhaAP.c_str());
  WiFi.setSleep(false);
  dns.start(53, "*", WiFi.softAPIP());             // qualquer endereço digitado cai na placa (portal cativo)
  apAtivo = true;
  Serial.println("======================================================");
  Serial.print  ("Rede de configuracao: "); Serial.println(nomeAP);
  Serial.println("Senha da rede: a senha do painel (padrao: caixa1234)");
  Serial.print  ("Pagina: http://"); Serial.print(WiFi.softAPIP()); Serial.println("/config  (usuario admin)");
  Serial.println("======================================================");
  registrarEvento("rede de configuracao ligada (" + nomeAP + ")");
}

void pararAP() {
  if (!apAtivo) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  apAtivo = false;
  registrarEvento("rede de configuracao desligada (Wi-Fi conectado)");
}

// Chamada uma vez, na primeira vez que o Wi-Fi conecta (no início ou mais tarde).
void aoConectar() {
  jaConectou = true;
  Serial.print("Wi-Fi conectado: "); Serial.println(WiFi.localIP());
  configTime(-3 * 3600, 0, "pool.ntp.org", "a.st1.ntp.br");   // horario de Brasilia
  struct tm t; horaOk = getLocalTime(&t, 5000);
  Serial.println(horaOk ? "Relogio sincronizado (NTP)" : "Sem horario da internet: tentando de novo a cada 30 s");
  if (MDNS.begin("alerta")) Serial.println("Pagina: http://alerta.local");
  registrarEvento("wifi conectado em " + WIFI_NOME + " (" + WiFi.localIP().toString() + ")");
  if (avisarLigada) {
    avisarLigada = false;
    enviarTexto("✅ Caixa de alerta ligada: " + NOME_LOCAL +
                "\nPainel: http://" + WiFi.localIP().toString() + "  (ou http://alerta.local)" +
                "\nConfiguração: http://" + WiFi.localIP().toString() + "/config");
  }
}

// Chamada a cada volta do loop: reconecta, liga e desliga a rede de configuração.
void verificarWiFi() {
  if (apAtivo) dns.processNextRequest();
  if (reiniciarEmMs && millis() > reiniciarEmMs) ESP.restart();   // depois de salvar a configuração

  bool conectado = WiFi.status() == WL_CONNECTED;
  // registra quedas e voltas do Wi-Fi (só depois da primeira conexão)
  static bool estavaConectado = false;
  if (jaConectou && conectado != estavaConectado) {
    if (conectado) {
      unsigned long fora = desconectadoDesdeMs ? (millis() - desconectadoDesdeMs) / 1000UL : 0;
      Serial.println("Wi-Fi reconectado");
      registrarEvento("wifi reconectado (ficou fora " + String(fora) + " s)");
    } else {
      Serial.println("Wi-Fi caiu");
      registrarEvento("wifi caiu");
    }
  }
  estavaConectado = conectado;
  if (conectado) {
    if (!jaConectou) aoConectar();
    if (conectadoDesdeMs == 0) conectadoDesdeMs = millis();
    desconectadoDesdeMs = 0;
    // a rede de configuração foi ligada só porque o Wi-Fi falhou: desliga 30 s depois de conectar
    if (apAtivo && !modoConfigForcado && millis() - conectadoDesdeMs > 30000) pararAP();
    return;
  }
  conectadoDesdeMs = 0;
  if (desconectadoDesdeMs == 0) desconectadoDesdeMs = millis();
  if (WIFI_NOME.length() == 0) return;              // nenhuma rede configurada: só a rede de configuração
  if (modoConfigForcado) return;                    // entrou pelo BOOT: só configura, não tenta o Wi-Fi
  // tenta de novo a cada 60 s (depois de uma queda de energia o roteador pode demorar a voltar)
  if (millis() - ultimaTentativaWiFi > 60000) {
    ultimaTentativaWiFi = millis();
    // Enquanto o ESP32 tenta o Wi-Fi, a rede de configuração troca de canal e o celular cai dela.
    // Por isso não tenta enquanto tiver alguém conectado na CaixaAlerta configurando.
    if (apAtivo && WiFi.softAPgetStationNum() > 0) {
      Serial.println("(alguem conectado na rede de configuracao: reconexao adiada)");
    } else {
      Serial.println("Tentando reconectar ao Wi-Fi...");
      WiFi.disconnect();                            // encerra a tentativa anterior (evita "sta is connecting")
      WiFi.begin(WIFI_NOME.c_str(), WIFI_SENHA.c_str());
    }
  }
  // 2 min sem Wi-Fi (senha do roteador mudou?): liga a rede de configuração, sem parar o alerta local
  if (!apAtivo && millis() - desconectadoDesdeMs > 120000) iniciarAP();
}

// ---- página ----
const char PAGINA_CONFIG[] PROGMEM = R"HTML(<!doctype html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Configuração da caixa</title><style>
body{font-family:system-ui,Arial;margin:0;background:#f6f3ee;color:#222}main{max-width:640px;margin:auto;padding:16px}
fieldset{background:#fff;border:0;border-radius:12px;padding:14px;margin:12px 0;box-shadow:0 1px 3px #0002}
legend{font-weight:700;padding:0 6px}label{display:block;margin:10px 0 2px;font-weight:600}
input[type=text],input[type=password],input[type=number]{width:100%;box-sizing:border-box;padding:9px;border:1px solid #bbb;border-radius:8px;font-size:1em}
small{color:#666;display:block}.l2{display:grid;grid-template-columns:1fr 1fr;gap:10px;align-items:end}
button{padding:10px 14px;border:0;border-radius:8px;background:#8a2c0d;color:#fff;font-size:1em;margin:4px 6px 4px 0}
button.s{background:#e9e2d8;color:#222}.ck{font-weight:400}.ck input{margin-right:6px}
#msg{padding:10px;border-radius:8px;display:none;margin:10px 0}.ok{background:#e8f5e9;color:#1b5e20}.er{background:#fde0de;color:#b71c1c}
#est{font-size:.9em;color:#444}a{color:#8a2c0d}#redes button{background:#fff;color:#222;border:1px solid #ccc;margin:3px}
</style></head><body><main>
<h2>Configuração da caixa de alerta</h2><div id="est">carregando...</div><div id="msg"></div>
<form id="f" onsubmit="return salvar(event)">
<fieldset><legend>Rede Wi-Fi</legend>
<label>Nome da rede (SSID)</label><input type="text" name="ssid" maxlength="32" required>
<button type="button" class="s" onclick="procurar()">Procurar redes</button><div id="redes"></div>
<label>Senha do Wi-Fi</label><input type="password" name="wpass" maxlength="63" id="wpass">
<small id="hwpass">Deixe em branco para manter a senha gravada.</small>
<label class="ck"><input type="checkbox" name="aberta">Rede aberta (sem senha)</label>
</fieldset>
<fieldset><legend>Telegram</legend>
<label>Token do bot</label><input type="password" name="token" maxlength="60" id="token">
<small id="htoken">Criado no @BotFather. Em branco = manter o token gravado.</small>
<label>Chat ID (quem recebe os alertas)</label><input type="text" name="chat" maxlength="20"><small>Descoberto no @userinfobot. Grupo: número negativo.</small>
<label>Nome do local</label><input type="text" name="local" maxlength="40" required><small>Aparece nas mensagens. Ex.: ILPI - Quarto 3</small>
</fieldset>
<fieldset><legend>Limites do índice de calor (°C)</legend><div class="l2">
<div><label>Atenção a partir de</label><input type="number" step="0.1" name="lat"></div>
<div><label>Perigo a partir de</label><input type="number" step="0.1" name="lpe"></div></div>
<label>Histerese</label><input type="number" step="0.1" name="his"><small>Quanto o índice precisa cair abaixo do limite para baixar de nível.</small>
<label>Correção do sensor (OFFSET)</label><input type="number" step="0.1" name="off"><small>Quanto a caixa lê A MAIS que o termômetro de referência. 0 = sem correção.</small>
</fieldset>
<fieldset><legend>Presença (PIR)</legend>
<label class="ck"><input type="checkbox" name="pir">Usar o sensor de presença</label>
<div class="l2"><div><label>Janela de presença (s)</label><input type="number" name="jpres"><small>Há alguém se houve movimento nesse tempo.</small></div>
<div><label>Aquecimento do PIR (s)</label><input type="number" name="aqpir"><small>Ignora o PIR logo depois de ligar.</small></div></div>
</fieldset>
<fieldset><legend>Tempos (segundos)</legend><div class="l2">
<div><label>Leitura do sensor</label><input type="number" name="leit"></div>
<div><label>Gravar no registro</label><input type="number" name="log"></div>
<div><label>Lembrete de perigo</label><input type="number" name="lemb"></div>
<div><label>Espera para baixar de nível</label><input type="number" name="desc"></div></div>
<button type="button" class="s" onclick="pre(1)">Valores de teste</button><button type="button" class="s" onclick="pre(0)">Valores de uso real</button>
</fieldset>
<fieldset><legend>Senha do painel</legend>
<div class="l2"><div><label>Nova senha</label><input type="password" name="painel" maxlength="32"></div>
<div><label>Repita</label><input type="password" name="painel2" maxlength="32"></div></div>
<small>Usuário: admin. Também é a senha da rede de configuração. Em branco = manter. Mínimo 8 caracteres.</small>
</fieldset>
<button type="submit">Salvar e reiniciar</button> <a href="/">Voltar ao painel</a>
</form>
<fieldset><legend>Restaurar</legend><small>Apaga todas as configurações gravadas e volta aos valores de fábrica. Os registros (CSV) não são apagados.</small>
<button type="button" class="s" onclick="apagar()">Apagar configurações</button></fieldset>
<script>
const f=document.getElementById('f'),M=document.getElementById('msg');
function msg(t,ok){M.className=ok?'ok':'er';M.innerHTML=t;M.style.display='block';scrollTo(0,0)}
async function carregar(){const d=await(await fetch('/api/config')).json();
for(const k of['ssid','chat','local','lat','lpe','his','off','jpres','aqpir','leit','log','lemb','desc'])f[k].value=d[k];
f.pir.checked=d.pir;
document.getElementById('hwpass').textContent=d.tem_wpass?'Há uma senha gravada. Em branco = manter.':'Nenhuma senha gravada.';
document.getElementById('htoken').textContent=d.tem_token?'Há um token gravado. Em branco = manter.':'Nenhum token gravado: sem Telegram.';
const e=d.estado;document.getElementById('est').innerHTML='<b>'+e.modo+'</b> · '+e.versao+'<br>'+e.detalhe;}
async function procurar(){const r=document.getElementById('redes');r.textContent='procurando (uns 3 s)...';
const l=await(await fetch('/scan')).json();r.innerHTML='';
if(!l.length)r.textContent='nenhuma rede encontrada';
l.forEach(n=>{const b=document.createElement('button');b.type='button';b.textContent=n.s+' ('+n.r+' dBm'+(n.c?'':', aberta')+')';
b.onclick=()=>{f.ssid.value=n.s;f.aberta.checked=!n.c;f.wpass.focus()};r.appendChild(b)})}
function pre(t){const v=t?{jpres:60,aqpir:60,leit:2,log:10,lemb:60,desc:60}:{jpres:900,aqpir:60,leit:2,log:300,lemb:1800,desc:600};for(const k in v)f[k].value=v[k]}
async function salvar(ev){ev.preventDefault();
if(f.painel.value!==f.painel2.value){msg('As duas senhas do painel não são iguais.');return false}
const b=new URLSearchParams(new FormData(f));b.set('pir',f.pir.checked?'1':'0');b.set('aberta',f.aberta.checked?'1':'0');
const d=await(await fetch('/salvar',{method:'POST',body:b})).json();
if(d.ok)msg('Salvo! A caixa vai reiniciar e conectar em <b>'+f.ssid.value+'</b>. Daqui a uns 20 s, abra o painel pelo endereço que chega no Telegram ou por http://alerta.local',1);
else msg('Não salvei. Corrija:<br>• '+d.erros.join('<br>• '));return false}
async function apagar(){if(!confirm('Apagar todas as configurações?'))return;await fetch('/apagar',{method:'POST'});
msg('Configurações apagadas. A caixa vai reiniciar e abrir a rede de configuração.',1)}
carregar();
</script></main></body></html>)HTML";

void paginaConfig() {
  if (!autorizado()) return;
  servidor.send(200, "text/html; charset=utf-8", PAGINA_CONFIG);
}

void apiConfig() {
  if (!autorizado()) return;
  String modo, detalhe;
  if (WiFi.status() == WL_CONNECTED) {
    modo = "Conectada ao Wi-Fi";
    detalhe = "Rede " + jsonEsc(WIFI_NOME) + " · IP " + WiFi.localIP().toString() + " · sinal " + String(WiFi.RSSI()) + " dBm";
  } else {
    modo = WIFI_NOME.length() ? "Sem conexão com " + jsonEsc(WIFI_NOME) : "Nenhuma rede configurada";
  }
  if (apAtivo) detalhe += String(detalhe.length() ? "<br>" : "") + "Rede de configuração " + nomeAP + " ligada (192.168.4.1)";
  String j = "{";
  j += "\"ssid\":\"" + jsonEsc(WIFI_NOME) + "\",\"tem_wpass\":" + String(WIFI_SENHA.length() ? "true" : "false") + ",";
  j += "\"tem_token\":" + String(BOT_TOKEN.length() ? "true" : "false") + ",\"chat\":\"" + jsonEsc(CHAT_ID) + "\",";
  j += "\"local\":\"" + jsonEsc(NOME_LOCAL) + "\",";
  j += "\"lat\":" + String(LIMITE_ATENCAO, 1) + ",\"lpe\":" + String(LIMITE_PERIGO, 1) + ",\"his\":" + String(HISTERESE, 1) + ",";
  j += "\"off\":" + String(OFFSET_TEMP, 1) + ",\"pir\":" + String(USA_PIR ? "true" : "false") + ",";
  j += "\"jpres\":" + String(JANELA_PRESENCA / 1000UL) + ",\"aqpir\":" + String(AQUECIMENTO_PIR / 1000UL) + ",";
  j += "\"leit\":" + String(INTERVALO_LEITURA / 1000UL) + ",\"log\":" + String(INTERVALO_LOG / 1000UL) + ",";
  j += "\"lemb\":" + String(INTERVALO_LEMBRETE / 1000UL) + ",\"desc\":" + String(TEMPO_DESCIDA / 1000UL) + ",";
  j += "\"estado\":{\"modo\":\"" + modo + "\",\"detalhe\":\"" + detalhe + "\",\"versao\":\"" + VERSAO + "\"}}";
  servidor.send(200, "application/json", j);
}

void apiScan() {
  if (!autorizado()) return;
  int n = WiFi.scanNetworks();
  String j = "[";
  int mostradas = 0;
  for (int i = 0; i < n && mostradas < 15; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;                 // rede oculta
    bool repetida = false;                         // a mesma rede em vários canais aparece uma vez só
    for (int k = 0; k < i; k++) if (WiFi.SSID(k) == s) { repetida = true; break; }
    if (repetida) continue;
    if (mostradas++) j += ",";
    j += "{\"s\":\"" + jsonEsc(s) + "\",\"r\":" + String(WiFi.RSSI(i)) + ",\"c\":" +
         String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "false" : "true") + "}";
  }
  j += "]";
  WiFi.scanDelete();
  servidor.send(200, "application/json", j);
}

bool ehNumero(const String& s) {                   // aceita 12, -3, 27.5 e 27,5
  if (s.length() == 0) return false;
  int pontos = 0;
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '-' && i == 0) continue;
    if (c == '.' || c == ',') { if (++pontos > 1) return false; continue; }
    if (c < '0' || c > '9') return false;
  }
  return true;
}

void apiSalvar() {
  if (!autorizado()) return;
  String erros = "";
  auto erro = [&](const String& e) { if (erros.length()) erros += ","; erros += "\"" + jsonEsc(e) + "\""; };
  auto lerF = [&](const char* k, float mn, float mx, const char* rotulo, float atual) -> float {
    String s = servidor.arg(k); s.trim(); s.replace(",", ".");
    if (!ehNumero(s) || s.toFloat() < mn || s.toFloat() > mx) {
      erro(String(rotulo) + ": use um número entre " + String(mn, 1) + " e " + String(mx, 1)); return atual;
    }
    return s.toFloat();
  };
  auto lerSeg = [&](const char* k, unsigned long mn, unsigned long mx, const char* rotulo, unsigned long atualMs) -> unsigned long {
    String s = servidor.arg(k); s.trim();
    long v = s.toInt();
    if (!ehNumero(s) || s.indexOf('.') >= 0 || s.indexOf(',') >= 0 || v < (long)mn || v > (long)mx) {
      erro(String(rotulo) + ": use um número inteiro de segundos entre " + String(mn) + " e " + String(mx)); return atualMs;
    }
    return (unsigned long)v * 1000UL;
  };

  // --- textos ---
  String ssid = servidor.arg("ssid"); ssid.trim();
  if (ssid.length() == 0 || ssid.length() > 32) erro("Nome da rede: obrigatório, até 32 caracteres");
  String wpass = WIFI_SENHA;
  if (servidor.arg("aberta") == "1") wpass = "";
  else if (servidor.arg("wpass").length()) {
    wpass = servidor.arg("wpass");
    if (wpass.length() < 8 || wpass.length() > 63) erro("Senha do Wi-Fi: de 8 a 63 caracteres");
  } else if (ssid != WIFI_NOME && WIFI_SENHA.length()) {
    // trocou de rede e não digitou senha: a senha antiga provavelmente não serve
    erro("Senha do Wi-Fi: digite a senha da nova rede (ou marque rede aberta)");
  }
  String token = BOT_TOKEN;
  if (servidor.arg("token").length()) {
    token = servidor.arg("token"); token.trim();
    if (token.indexOf(':') < 0 || token.length() < 30 || token.length() > 60) erro("Token: formato inválido (algo como 123456789:ABC...)");
  }
  String chat = servidor.arg("chat"); chat.trim();
  if (chat.length() && (!ehNumero(chat) || chat.indexOf('.') >= 0 || chat.indexOf(',') >= 0 || chat.length() > 20)) erro("Chat ID: só números (grupo começa com -)");
  String local = servidor.arg("local"); local.trim();
  if (local.length() == 0 || local.length() > 40) erro("Nome do local: obrigatório, até 40 caracteres");
  String painel = SENHA_PAINEL;
  if (servidor.arg("painel").length()) {
    painel = servidor.arg("painel");
    if (painel != servidor.arg("painel2")) erro("Senha do painel: as duas não são iguais");
    if (painel.length() < 8 || painel.length() > 32) erro("Senha do painel: de 8 a 32 caracteres");
  }

  // --- números ---
  float lat = lerF("lat", 15, 45, "Limite de atenção", LIMITE_ATENCAO);
  float lpe = lerF("lpe", 16, 55, "Limite de perigo", LIMITE_PERIGO);
  float his = lerF("his", 0, 5, "Histerese", HISTERESE);
  float off = lerF("off", -10, 10, "Correção do sensor", OFFSET_TEMP);
  if (lpe < lat + 1.0) erro("O limite de perigo precisa ser pelo menos 1 °C acima do de atenção");
  unsigned long jpres = lerSeg("jpres", 10, 7200, "Janela de presença", JANELA_PRESENCA);
  unsigned long aqpir = lerSeg("aqpir", 0, 300, "Aquecimento do PIR", AQUECIMENTO_PIR);
  unsigned long leit  = lerSeg("leit", 2, 60, "Leitura do sensor", INTERVALO_LEITURA);
  unsigned long logS  = lerSeg("log", 5, 3600, "Gravar no registro", INTERVALO_LOG);
  unsigned long lemb  = lerSeg("lemb", 30, 86400, "Lembrete de perigo", INTERVALO_LEMBRETE);
  unsigned long desc  = lerSeg("desc", 0, 3600, "Espera para baixar de nível", TEMPO_DESCIDA);
  bool pir = servidor.arg("pir") == "1";

  if (erros.length()) {
    servidor.send(400, "application/json", "{\"ok\":false,\"erros\":[" + erros + "]}");
    return;
  }

  prefs.begin("caixa", false);
  prefs.putString("ssid", ssid);   prefs.putString("wpass", wpass);
  prefs.putString("token", token); prefs.putString("chat", chat);
  prefs.putString("local", local); prefs.putString("painel", painel);
  prefs.putFloat("lat", lat); prefs.putFloat("lpe", lpe); prefs.putFloat("his", his); prefs.putFloat("off", off);
  prefs.putBool("pir", pir);
  prefs.putULong("jpres", jpres / 1000UL); prefs.putULong("aqpir", aqpir / 1000UL);
  prefs.putULong("leit", leit / 1000UL);   prefs.putULong("log", logS / 1000UL);
  prefs.putULong("lemb", lemb / 1000UL);   prefs.putULong("desc", desc / 1000UL);
  prefs.end();

  // o evento não leva senha nem token
  registrarEvento("configuracao salva pela pagina (rede " + ssid + ", atencao " + String(lat, 1) +
                  " C, perigo " + String(lpe, 1) + " C, presenca " + String(jpres / 1000UL) + " s)");
  servidor.send(200, "application/json", "{\"ok\":true}");
  reiniciarEmMs = millis() + 1500;                 // dá tempo da resposta chegar ao navegador
}

void apiApagar() {
  if (!autorizado()) return;
  prefs.begin("caixa", false); prefs.clear(); prefs.end();
  registrarEvento("configuracoes apagadas pela pagina (valores de fabrica)");
  servidor.send(200, "application/json", "{\"ok\":true}");
  reiniciarEmMs = millis() + 1500;
}

// Nos 3 s depois de ligar, o LED pisca rápido: apertar BOOT nesse tempo liga a rede de configuração.
// (Não dá para segurar o BOOT ANTES de ligar: com ele apertado no reset, o ESP32 entra no modo de gravação.)
bool botaoConfigNoInicio() {
  Serial.println("Aperte BOOT nos proximos 3 s para abrir a rede de configuracao...");
  unsigned long t0 = millis();
  while (millis() - t0 < 3000) {
    digitalWrite(PINO_LED, (millis() / 100) % 2);
    if (digitalRead(PINO_BOTAO) == LOW) {
      digitalWrite(PINO_LED, LOW);
      while (digitalRead(PINO_BOTAO) == LOW) delay(10);   // espera soltar, para não silenciar nada depois
      return true;
    }
    delay(10);
  }
  digitalWrite(PINO_LED, LOW);
  return false;
}

void iniciarServidorLogs() {
  temArquivos = LittleFS.begin(true);                // true = formata na 1a vez
  Serial.println(temArquivos ? "LittleFS ok" : "LittleFS falhou (sem registro em arquivo)");
  if (temArquivos && LittleFS.exists("/dados.csv")) {  // Etapa 10: CSV da Etapa 9 nao tem a coluna presenca
    File f = LittleFS.open("/dados.csv", "r");
    String cab = f ? f.readStringUntil('\n') : "";
    if (f) f.close();
    if (cab.indexOf("presenca") < 0) {
      LittleFS.remove("/dados_etapa9.csv");
      LittleFS.rename("/dados.csv", "/dados_etapa9.csv");
      LittleFS.remove("/dados_antigo.csv");          // o antigo da Etapa 9 tambem nao tem a coluna
      Serial.println("dados.csv da Etapa 9 guardado como dados_etapa9.csv");
    }
  }
  // relogio (NTP) e alerta.local: agora em aoConectar(), que roda quando o Wi-Fi conecta (Etapa 11)
  servidor.on("/", paginaInicial);
  servidor.on("/config", paginaConfig);                       // Etapa 11
  servidor.on("/api/config", apiConfig);
  servidor.on("/scan", apiScan);
  servidor.on("/salvar", HTTP_POST, apiSalvar);
  servidor.on("/apagar", HTTP_POST, apiApagar);
  servidor.on("/api/agora", apiAgora);
  servidor.on("/api/grafico", apiGrafico);
  servidor.on("/dados.csv", []() { enviarCsv("/dados.csv", "/dados_antigo.csv", "dados_caixa_alerta.csv"); });
  servidor.on("/dados_etapa9.csv", []() { enviarCsv("/dados_etapa9.csv", "/nao_existe.csv", "dados_etapa9.csv"); });
  servidor.on("/eventos.csv", []() { enviarCsv("/eventos.csv", "/eventos_antigo.csv", "eventos_caixa_alerta.csv"); });
  servidor.onNotFound([]() {
    if (apAtivo) {   // portal cativo: o celular testa endereços quaisquer; manda para a configuração
      servidor.sendHeader("Location", "http://192.168.4.1/config"); servidor.send(302, "text/plain", ""); return;
    }
    servidor.send(404, "text/plain", "Nao encontrado");
  });
  servidor.begin();
}

// ---------------------------------------------------------------- sensor
void lerAmbiente() {
  float temperatura, umidade;

  if (TESTE_FALHA_SENSOR) {
    temperatura = NAN;   // finge a falha sem nem chamar o sensor
    umidade = NAN;
  } else {
    temperatura = dht.readTemperature();
    umidade     = dht.readHumidity();
  }

  if (isnan(temperatura) || isnan(umidade)) {
    falhasSeguidas++;
    Serial.print("Erro ao ler o sensor! (");
    Serial.print(falhasSeguidas);
    Serial.println(" seguidas)");

    if (falhasSeguidas >= MAX_FALHAS && !sensorComFalha) {
      sensorComFalha = true;
      registrarEvento("sensor sem resposta");
      mostrarCor(AMARELO);   // sinal local imediato, antes de esperar a internet
      enviarTexto("⚠️ Sensor sem resposta\nLocal: " + String(NOME_LOCAL) +
                  "\nA caixa não está medindo o ambiente. Verifique o aparelho.");
    }
    return;
  }

  // Faixa fisica: umidade acima de 100 % (sopro, condensacao) ou temperatura absurda = leitura descartada.
  if (umidade > 100 || umidade < 0 || temperatura < -10 || temperatura > 60) {
    Serial.print("Leitura descartada (fora da faixa): T ");
    Serial.print(temperatura, 1); Serial.print(" C, U "); Serial.print(umidade, 0); Serial.println(" %");
    return;
  }

  // Leitura boa: se o sensor estava com falha, avisa que voltou
  if (sensorComFalha) {
    sensorComFalha = false;
    registrarEvento("sensor voltou a funcionar");
    enviarTexto("✅ Sensor voltou a funcionar\nLocal: " + String(NOME_LOCAL));
    mostrarNivel(nivelAtual);
  }
  falhasSeguidas = 0;

  temperatura -= OFFSET_TEMP;            // correção de calibração (Etapa 7)
  verificarAparelho();
  verificarSubida(temperatura);

  float indice = dht.computeHeatIndex(temperatura, umidade, false);
  ultimaTemp = temperatura; ultimaUmid = umidade; ultimoIndice = indice;
  Nivel novoNivel = calcularNivel(indice, nivelAtual);

  // Descer de nivel so depois de TEMPO_DESCIDA abaixo do limite; subir e imediato.
  if (novoNivel < nivelAtual) {
    if (abaixoDesdeMs == 0) abaixoDesdeMs = millis();
    if (millis() - abaixoDesdeMs < TEMPO_DESCIDA) novoNivel = nivelAtual;   // ainda aguardando
  } else {
    abaixoDesdeMs = 0;
  }
  if (novoNivel != nivelAtual) abaixoDesdeMs = 0;

  if (novoNivel != nivelAtual) {
    Serial.print(">>> Nivel mudou: ");
    Serial.print(nomeDoNivel(nivelAtual));
    Serial.print(" -> ");
    Serial.println(nomeDoNivel(novoNivel));

    if (novoNivel == PERIGO) {
      silenciado = false;
      ultimoBipeMs = 0;
      inicioPerigoMs = millis();     // começa a contar o tempo de perigo
      ultimoLembreteMs = millis();   // o próprio aviso de entrada conta como 1º aviso
    }
    nivelAtual = novoNivel;
    mostrarNivel(nivelAtual);   // a luz muda ANTES do envio: o alerta local nunca espera a internet

    registrarEvento(String("nivel ") + nomeDoNivel(nivelAtual) + " (indice " + String(indice, 1) + " C)");
    enviarTelegram(nivelAtual, temperatura, umidade, indice);
  }

  registrarLeitura(temperatura, umidade, indice);   // depois de decidir o nivel: o CSV mostra o nivel ja atualizado

  Serial.print("Temp: ");       Serial.print(temperatura, 1);
  Serial.print(" °C | Umid: "); Serial.print(umidade, 1);
  Serial.print(" % | IC: ");    Serial.print(indice, 1);
  Serial.print(" °C | ");       Serial.print(nomeDoNivel(nivelAtual));
  if (nivelAtual == PERIGO && silenciado) Serial.print(" (silenciado)");
  if (!isnan(ultimaTempChip)) { Serial.print(" | chip: "); Serial.print(ultimaTempChip, 0); Serial.print(" C"); }
  if (abaixoDesdeMs != 0) { Serial.print(" (baixando em "); Serial.print((TEMPO_DESCIDA - (millis() - abaixoDesdeMs)) / 1000); Serial.print(" s)"); }
  if (aparelhoQuente) Serial.print(" [APARELHO QUENTE]");
  if (subidaSuspeita) Serial.print(" [SUBIDA SUSPEITA]");
  if (USA_PIR) Serial.print(millis() < AQUECIMENTO_PIR ? " | PIR aquecendo" : (haPresenca() ? " | presenca: sim" : " | presenca: nao"));
  Serial.println();
}

// ---------------------------------------------------------------- botão e bipe
void verificarBotao() {
  bool leitura = digitalRead(PINO_BOTAO);
  if (leitura != ultimaLeitura) ultimaMudancaMs = millis();
  ultimaLeitura = leitura;

  if (millis() - ultimaMudancaMs > DEBOUNCE_MS && leitura != estadoEstavel) {
    estadoEstavel = leitura;
    if (estadoEstavel == LOW && nivelAtual == PERIGO && !silenciado) {
      silenciado = true;
      registrarEvento("alerta silenciado pelo botao");
      noTone(PINO_BUZZER);
      Serial.println(">>> Alerta silenciado pelo botao");
    }
  }
}

void tocarAlerta() {
  if (nivelAtual != PERIGO || silenciado) return;
  if (ultimoBipeMs == 0 || millis() - ultimoBipeMs >= INTERVALO_BIPE) {
    ultimoBipeMs = millis();
    tone(PINO_BUZZER, FREQ_BIPE, DURACAO_BIPE);
  }
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);          // processador a 80 MHz esquenta menos (mínimo para o Wi-Fi)
  pinMode(PINO_LED, OUTPUT);
  pinMode(PINO_BOTAO, INPUT_PULLUP);
  pinMode(PINO_BUZZER, OUTPUT);
  if (USA_PIR) pinMode(PINO_PIR, INPUT);
  mostrarNivel(nivelAtual);

  dht.begin();
  Serial.println();
  Serial.print("Caixa de alerta de calor - "); Serial.println(VERSAO);

  carregarConfig();                    // Etapa 11: Wi-Fi, token, limites e tempos gravados na memória
  modoConfigForcado = botaoConfigNoInicio();
  // Liga o Wi-Fi (a pilha de rede) ANTES de qualquer coisa de rede. Sem isso, no modo configuração
  // (BOOT apertado) ou sem rede gravada, o servidor web era iniciado antes do Wi-Fi e a placa
  // travava com "assert failed: xQueueSemaphoreTake" (teste de 29/10).
  WiFi.mode(WIFI_STA);

  // Certificado do Telegram, para a conexão HTTPS ser verificada
  clienteSeguro.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  bot = new UniversalTelegramBot(BOT_TOKEN, clienteSeguro);

  if (!modoConfigForcado) conectarWiFi();
  iniciarServidorLogs();               // Etapa 9: arquivos e pagina web

  // Só avisa "ligada" quando foi ligada na energia. Se a placa reiniciou sozinha
  // (travamento/watchdog), mostra só no Serial — senão um defeito que reinicia a
  // placa em loop encheria o Telegram de mensagens. A mensagem sai quando o Wi-Fi conectar.
  esp_reset_reason_t motivo = esp_reset_reason();
  registrarEvento(String("placa iniciou (motivo ") + (int)motivo + ")");
  avisarLigada = (motivo == ESP_RST_POWERON);
  if (!avisarLigada) {
    Serial.print("Reinicio (motivo ");
    Serial.print((int)motivo);
    Serial.println(") - sem mensagem de 'ligada'");
  }

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(true);
    aoConectar();                      // relógio, alerta.local e mensagem de "ligada"
  } else {
    iniciarAP();                       // sem rede, rede errada ou BOOT apertado: rede de configuração
  }
}

// LED unico da placa: apagado = normal | piscando devagar = atencao | aceso = perigo
// piscando rapido = sensor com falha
void atualizarLed() {
  bool aceso = false;
  if (sensorComFalha) aceso = (millis() / 150) % 2;
  else switch (corAtual) {
    case AMARELO:  aceso = (millis() / 700) % 2; break;
    case VERMELHO: aceso = true; break;
    default:       aceso = false; break;
  }
  digitalWrite(PINO_LED, aceso ? HIGH : LOW);
}

// Etapa 10: se o relogio nao sincronizou no inicio (NTP demorou), tenta de novo a cada 30 s.
// O configTime() ja deixou a sincronizacao rodando em segundo plano; aqui so conferimos.
void verificarRelogio() {
  static unsigned long ultimaTentativa = 0;
  if (horaOk || WiFi.status() != WL_CONNECTED) return;
  if (millis() - ultimaTentativa < 30000) return;
  ultimaTentativa = millis();
  struct tm t;
  if (getLocalTime(&t, 10)) {
    horaOk = true;
    Serial.println("Relogio sincronizado (NTP) - registros passam a ter data e hora");
    registrarEvento("relogio sincronizado (ligada ha " + String(millis() / 1000UL) + " s)");
  }
}

void loop() {
  servidor.handleClient();             // Etapa 9: atende a pagina web
  verificarWiFi();                     // Etapa 11: portal, reconexão e reinício depois de salvar
  verificarRelogio();
  atualizarLed();
  verificarBotao();
  verificarPresenca();
  tocarAlerta();
  verificarLembrete();

  if (millis() - ultimaLeituraSensorMs >= INTERVALO_LEITURA) {
    ultimaLeituraSensorMs = millis();
    lerAmbiente();
  }
}