# IA 2: assistente do cuidador (em construção)

Ideia de Matheus Henrique Ferreira Pimentel. A IA da caixa sabe **quando** o quarto ficou perigoso; o assistente ajuda o cuidador a decidir **o que fazer**.

## O que ele vai fazer

- Conversar no Telegram (protótipo) ou WhatsApp, por texto ou áudio.
- Conhecer o que a caixa está vendo: temperatura, umidade, nível de risco, aumento estimado da temperatura central, presença e histórico das últimas horas.
- Usar a previsão do tempo das próximas horas (Open-Meteo) para avisar antes do pico.
- Adaptar as orientações ao contexto que o cuidador informar (por exemplo, remédios para pressão que aumentam o risco no calor).
- Sugerir locais climatizados de acesso livre por perto, a partir de uma lista verificada.

## Como vai funcionar

```
contexto = {
  caixa:    últimas leituras e risco (Supabase),
  perfil:   "acamado" | "repouso" | "ativo"  (configurado na caixa),
  pessoa:   o que o cuidador informou (com consentimento),
  previsao: temperatura e radiação das próximas 6 h (Open-Meteo),
  locais:   até 3 locais da lista verificada mais próximos (só se o perfil permitir sair)
}
resposta = modelo_de_linguagem(regras_de_seguranca + contexto + mensagem_do_cuidador)
```

A caixa envia as leituras ao Supabase a cada 5 minutos ([`supabase_schema.sql`](supabase_schema.sql)). Sem internet, elas ficam numa fila na memória e vão quando a conexão volta. **O alerta principal continua na caixa e não depende do assistente.**

- Regras de segurança: [`regras_de_seguranca.md`](regras_de_seguranca.md)
- Como vamos testar: [`perguntas_de_teste.md`](perguntas_de_teste.md)
