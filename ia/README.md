# IA 1: risco no corpo, calculado na própria caixa

Uma rede neural pequena (MLP 12 → 16 → 16 → 2, **514 parâmetros, cerca de 2 KB**) que roda no ESP32 e estima, a partir das **últimas 4 horas** de temperatura e umidade do cômodo e do **perfil do ocupante** (acamado, em repouso, ativo):

- o aumento da temperatura central da pessoa em relação ao normal dela (°C);
- a perda de água por suor e respiração nas últimas 2 horas (g).

Ela foi treinada com simulações do modelo termofisiológico **JOS-3** (pythermalcomfort 4.6): aprende a reproduzir o JOS-3 de forma leve o bastante para rodar sem internet no aparelho.

## Dados

- 1.200 simulações (400 por perfil), idosos de 65 a 90 anos, homens e mulheres, 1,50–1,78 m, 50–88 kg.
- Cada simulação: 2 h de adaptação + 6 h com a temperatura do quarto variando entre 20 e 41 °C e a umidade entre 30 e 95%, em passos de 10 min.
- 8 simulações descartadas porque o JOS-3 diverge (homens acima de ~83 anos).
- 42.912 momentos; treino e teste separados **por simulação** (80/20).

## Resultados no conjunto de teste (8.568 momentos)

| Método | Erro na temp. central | Erro na perda de água (2 h) |
|---|---:|---:|
| Tabela (só temperatura e umidade de agora) | 0,051 °C | 21,1 g |
| Modelo grande com histórico (gradient boosting) | 0,036 °C | 11,9 g |
| **IA embarcada (MLP com histórico)** | **0,039 °C** | **11,6 g** |

Detecção de risco (referência: aumento ≥ 0,2 °C no JOS-3; limite provisório):

| Método | Alertas perdidos | Alarmes falsos |
|---|---:|---:|
| Limite fixo de índice de calor 32 °C | 67 | 3.445 |
| Tabela, ajustada para o mesmo número de falsos que a IA | 220 | 801 |
| **IA embarcada (limite 0,15 °C)** | **86** | **784** |

Figuras em [`resultados/`](resultados/).

## Como rodar

```bash
pip install -r requirements.txt
gunzip -k dados/sims.jsonl.gz                 # ou: python gerar_simulacoes.py 400 dados/sims.jsonl
python treinar.py dados/sims.jsonl            # gera resultados.json, modelo_risco.h e teste_*.npz
python avaliar.py 0.2 99999                   # detecção de risco, fig_alarmes.png e fig_erros.png
python verifica_c.py                          # compila o C e compara com o Python
```

| Arquivo | O que é |
|---|---|
| `gerar_simulacoes.py` | Gera as simulações JOS-3 |
| `treinar.py` | Treina e compara os métodos; exporta `modelo_risco.h` |
| `avaliar.py` | Detecção de risco e gráficos |
| `verifica_c.py` | Compila o modelo em C e compara com o Python |
| `modelo_risco.h` | Pesos da rede em C (gerado) |
| `ia_embarcada.h` | Liga o modelo ao firmware: guarda 4 h de leituras e chama o modelo |
| `dados/sims.jsonl.gz` | As 1.200 simulações |

## Limitações

- A IA aprende o **JOS-3**, não pessoas reais. O JOS-3 não foi validado para idosos (Novieto, 2013; Forbes et al., 2025), e o aumento da temperatura central que ele prevê é pequeno.
- Os limites de risco são provisórios; a calibração com dados reais é etapa futura.
- Sem calor radiante, vento e sol direto (temperatura radiante = temperatura do ar).
- O código C foi conferido contra o Python, mas ainda não rodou na placa.
