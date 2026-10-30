# Calor x internações de idosos em Maceió (2015–2024)

Junta a temperatura horária da estação automática do INMET (A303, Maceió) com a série diária de internações de idosos (60+) do SIH/SUS e estima o efeito da temperatura com regressão de Poisson, estratificada por ano-mês e dia da semana.

**Principais resultados**

- 99.759 internações, nenhuma com código de efeito direto do calor (T67/X30).
- Dia quente (máxima ≥ percentil 95): internações por todas as causas sobem 5,2% (RR 1,052; IC95% 1,009–1,097; p = 0,017).
- Cada 1 °C a mais na máxima: RR 1,007 (1,000–1,014).
- Sem associação significativa para os desfechos circulatório, respiratório e renal isolados.

**Arquivos**

- `analise_calor_internacoes.py`: o script.
- `internacoes_calor_maceio_diario.csv`: contagens diárias agregadas (sem dados individuais).
- `resultados/resultados_poisson.csv`: resultados de todos os modelos.

**Para rodar:** baixe os CSVs horários da estação A303 em https://portal.inmet.gov.br/dadoshistoricos, coloque nesta pasta e rode `pip install -r requirements.txt` e `python analise_calor_internacoes.py`.
