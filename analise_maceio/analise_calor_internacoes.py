"""
Calor x internações de idosos em Maceió (2015–2024)

Junta a temperatura horária da estação automática do INMET (A303, Maceió) com a série diária de
internações de idosos (SIH/SUS) e estima o efeito da temperatura com regressão de Poisson.

Desenho: estratificação por ano-mês (cada dia é comparado com outros dias do mesmo mês do mesmo
ano), mais dia da semana. Isso controla sazonalidade, tendência de longo prazo e a pandemia.

Uso: coloque nesta pasta
  - os arquivos INMET_NE_AL_A303_MACEIO_*.CSV
  - internacoes_calor_maceio_diario.csv
e rode:  python analise_calor_internacoes.py

Dependências: pip install pandas numpy statsmodels
"""
import glob
import os

import numpy as np
import pandas as pd
import statsmodels.api as sm
import statsmodels.formula.api as smf

BASE = os.path.dirname(os.path.abspath(__file__))
MIN_HORAS = 18          # horas válidas mínimas para aceitar o dia
DESFECHOS = {"circulatoria": "Circulatórias", "respiratoria": "Respiratórias",
             "desidratacao_renal": "Insuficiência renal aguda", "total": "Todas as causas"}


# ------------------------------------------------------------------ INMET
def ler_inmet(caminho):
    df = pd.read_csv(caminho, sep=";", skiprows=8, encoding="latin-1", decimal=",", dtype=str)
    df.columns = [c.strip() for c in df.columns]
    col_data = df.columns[0]
    col_hora = df.columns[1]
    achar = lambda trecho: next(c for c in df.columns if trecho in c.upper())
    col_t = achar("BULBO SECO")
    col_tmax = achar("MÁXIMA NA HORA") if any("MÁXIMA NA HORA" in c.upper() for c in df.columns) else achar("MAXIMA NA HORA")
    col_ur = next(c for c in df.columns if "UMIDADE RELATIVA DO AR, HORARIA" in c.upper())

    data = pd.to_datetime(df[col_data].str.replace("/", "-"), format="%Y-%m-%d", errors="coerce")
    hora = df[col_hora].str.extract(r"(\d{2})")[0].astype(float)
    utc = data + pd.to_timedelta(hora, unit="h")
    out = pd.DataFrame({"hora_local": utc - pd.Timedelta(hours=3)})
    for nome, col in [("t", col_t), ("tmax_h", col_tmax), ("ur", col_ur)]:
        v = pd.to_numeric(df[col].str.replace(",", "."), errors="coerce")
        out[nome] = v.where(v > -9000)
    return out.dropna(subset=["hora_local"])


def indice_calor(t, ur):
    """Índice de calor (NOAA, Rothfusz) em °C, a partir de temperatura (°C) e umidade (%)."""
    f = t * 9 / 5 + 32
    hi = (-42.379 + 2.04901523 * f + 10.14333127 * ur - 0.22475541 * f * ur - 6.83783e-3 * f ** 2
          - 5.481717e-2 * ur ** 2 + 1.22874e-3 * f ** 2 * ur + 8.5282e-4 * f * ur ** 2 - 1.99e-6 * f ** 2 * ur ** 2)
    simples = 0.5 * (f + 61 + (f - 68) * 1.2 + ur * 0.094)
    hi = np.where(simples < 80, simples, hi)
    return (hi - 32) * 5 / 9


arquivos = sorted(glob.glob(os.path.join(BASE, "*INMET*A303*MACEIO*.CSV")))
if not arquivos:
    raise SystemExit("Nenhum arquivo do INMET encontrado na pasta.")
h = pd.concat([ler_inmet(a) for a in arquivos], ignore_index=True)
h["ic"] = indice_calor(h["t"], h["ur"])
h["dia"] = h["hora_local"].dt.normalize()

g = h.groupby("dia")
clima = pd.DataFrame({
    "horas_validas": g["t"].count(),
    "tmax": g["tmax_h"].max(),
    "tmed": g["t"].mean(),
    "tmin": g["t"].min(),
    "urmed": g["ur"].mean(),
    "icmax": g["ic"].max(),
})
for c in ["tmax", "tmed", "tmin", "urmed", "icmax"]:
    clima.loc[clima["horas_validas"] < MIN_HORAS, c] = np.nan

# ------------------------------------------------------------------ internações
y = pd.read_csv(os.path.join(BASE, "internacoes_calor_maceio_diario.csv"), parse_dates=["data"]).set_index("data")
d = y.join(clima, how="left")
d = d[(d.index >= "2015-01-01") & (d.index <= "2024-12-31")].copy()

# médias móveis de defasagem (efeito do calor costuma aparecer em 0 a 3 dias)
for c in ["tmax", "tmed", "icmax"]:
    d[f"{c}_l02"] = d[c].rolling(3, min_periods=3).mean()
d["anomes"] = d.index.strftime("%Y-%m")
d["dow"] = d.index.dayofweek
p95 = d["tmax"].quantile(0.95)
d["dia_quente"] = (d["tmax"] >= p95).astype(float).where(d["tmax"].notna())
d.to_csv(os.path.join(BASE, "serie_diaria_clima_internacoes.csv"))

# ------------------------------------------------------------------ cobertura
print("\nDias com clima válido por ano:")
print(d.groupby(d.index.year)["tmax"].count().to_string())
print(f"\nTemperatura máxima diária: média {d['tmax'].mean():.1f} °C, P95 = {p95:.1f} °C, máximo {d['tmax'].max():.1f} °C")
print("\nTmax média por mês:")
print(d.groupby(d.index.month)["tmax"].mean().round(1).to_string())


# ------------------------------------------------------------------ modelos
def ajustar(desfecho, exposicao):
    dd = d.dropna(subset=[exposicao, desfecho])
    m = smf.glm(f"{desfecho} ~ {exposicao} + C(anomes) + C(dow)", data=dd,
                family=sm.families.Poisson()).fit(scale="X2")
    b, ep = m.params[exposicao], m.bse[exposicao]
    return dict(desfecho=DESFECHOS[desfecho], exposicao=exposicao, n_dias=len(dd),
                RR=np.exp(b), IC95_inf=np.exp(b - 1.96 * ep), IC95_sup=np.exp(b + 1.96 * ep), p=m.pvalues[exposicao])


linhas = []
for desf in DESFECHOS:
    for exp in ["tmax", "tmax_l02", "icmax_l02", "dia_quente"]:
        linhas.append(ajustar(desf, exp))
res = pd.DataFrame(linhas)
res.to_csv(os.path.join(BASE, "resultados_poisson.csv"), index=False)
pd.set_option("display.width", 140)
print("\nRazão de taxas (RR) por 1 °C (ou dia quente vs não quente), estratificado por ano-mês e dia da semana:")
print(res.round(4).to_string(index=False))
