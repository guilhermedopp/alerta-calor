# Firmware

## `bancada_devkit_etapa11/` (versão atual de bancada)

Firmware testado em 29/10/2026 numa ESP32 DevKit (WROOM-32) com DHT11 e sensor de presença PIR HC-SR501.

- Três níveis de risco com histerese, alerta local (LED e buzzer) e avisos pelo Telegram.
- Sensor de presença: alerta diferente com e sem alguém no cômodo.
- Registro de leituras e eventos em CSV na memória flash (LittleFS) e página web com gráfico.
- **Configuração pelo navegador, como um roteador:** Wi-Fi, token do bot, chat ID, nome do local, limites e tempos ficam gravados na memória da placa (Preferences/NVS). Sem Wi-Fi configurado, ou segurando BOOT nos 3 s depois de ligar, a placa cria a rede `CaixaAlerta-XXXX` (senha padrão `caixa1234`) e a página de configuração abre sozinha no celular.

| Ligação | Pino |
|---|---|
| DHT11 (DATA) | D26 |
| PIR (OUT) | D27 |
| Buzzer (+), com resistor de 220 Ω | D25 |
| LED da placa | 2 |
| Botão BOOT (silenciar / modo configuração) | 0 |

Bibliotecas (Arduino IDE, placa "ESP32 Dev Module"): `DHT sensor library` (Adafruit), `UniversalTelegramBot` e `ArduinoJson`. As demais vêm com o pacote ESP32.

Passo a passo dos testes: [`ROTEIRO_testes.txt`](bancada_devkit_etapa11/ROTEIRO_testes.txt).

### IA na caixa

`ia_embarcada.h` e `modelo_risco.h` (gerado por [`../ia/treinar.py`](../ia/treinar.py)) já estão na pasta do sketch. A integração no laço principal é a primeira entrega do plano:

```cpp
#include "ia_embarcada.h"
ia_registrar(temperatura, umidade);              // a cada leitura; guarda 1 média a cada 10 min (4 h)
IaRisco r = ia_estimar(PERFIL_ACAMADO);           // perfil configurado na página
int nivel = max(nivelPorCorpo(r), nivelPorIndiceCalor(ic));   // vale o mais grave
```

## `testes_de_bancada/`

Testes isolados usados na montagem: DHT11 no D26, PIR no D27, diagnóstico de pinos do PIR e o teste definitivo (DHT11 + PIR com aprovado/reprovado automático).

## Placa final

A caixa usará ESP32-C3 SuperMini com sensor AHT20, LED RGB, buzzer ativo, botão e PIR AM312 ([`../caixa_3d/`](../caixa_3d/)). A adaptação do firmware para essa placa vem depois da chegada das peças.
