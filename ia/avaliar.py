"""Avalia a detecção de risco no conjunto de teste e gera os gráficos.

Risco "de referência" (o que o JOS-3 diz): aumento da temperatura central >= LIM_DCORE
ou perda de água por suor >= LIM_SUOR em 2 h. Os limites são provisórios, só para comparar
os métodos entre si.
Compara: limiar fixo do firmware atual (índice de calor >= 27 °C, nível atenção),
a "tabela" (T e UR de agora) e a MLP com histórico (a que vai no ESP32).
"""
import json, sys, numpy as np
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
from treinar import indice_calor, PERFIS

LIM_DCORE = float(sys.argv[1]) if len(sys.argv) > 1 else 0.2
LIM_SUOR = float(sys.argv[2]) if len(sys.argv) > 2 else 200.0

d = np.load("teste_tudo.npz", allow_pickle=True)
Y, P, Xh = d["Y"], d["P"], d["Xh"]
ref = (Y[:, 0] >= LIM_DCORE) | (Y[:, 1] >= LIM_SUOR)
ic = np.array([indice_calor(t, u) for t, u in zip(Xh[:, 0], Xh[:, 1])])

def metr(pred):
    vp = int((pred & ref).sum()); fp = int((pred & ~ref).sum()); fn = int((~pred & ref).sum()); vn = int((~pred & ~ref).sum())
    sens = vp / max(1, vp + fn); prec = vp / max(1, vp + fp); esp = vn / max(1, vn + fp)
    return dict(sensibilidade=round(sens, 3), precisao=round(prec, 3), especificidade=round(esp, 3), alertas_perdidos=fn, alarmes_falsos=fp)

saida = {"limites": {"dcore_C": LIM_DCORE, "suor_2h_g": LIM_SUOR}, "casos_de_risco_no_teste": int(ref.sum()), "amostras_teste": int(len(ref)), "metodos": {}}
saida["metodos"]["limiar_fixo_IC_27C (firmware atual)"] = metr(ic >= 27)
for nome in ("tabela_T_UR_agora", "mlp_historico_esp32"):
    pr = d[nome]
    saida["metodos"][nome] = metr((pr[:, 0] >= LIM_DCORE) | (pr[:, 1] >= LIM_SUOR))
# por perfil: a mesma sala, riscos diferentes
saida["casos_de_risco_por_perfil_pct"] = {p: round(100 * float(ref[P == p].mean()), 1) for p in PERFIS}
json.dump(saida, open("avaliacao.json", "w"), indent=2, ensure_ascii=False)
print(json.dumps(saida, indent=2, ensure_ascii=False))

# Gráfico 1: erro de cada método
r = json.load(open("resultados.json"))["metricas"]
nomes = ["tabela_T_UR_agora", "gbm_historico", "mlp_historico_esp32"]
rot = ["Tabela\n(T e UR de agora)", "Modelo grande\n(com histórico)", "MLP no ESP32\n(com histórico)"]
fig, ax = plt.subplots(1, 2, figsize=(10, 3.6))
ax[0].bar(rot, [r[n]["MAE_dcore_C"] for n in nomes], color=["#b0a89a", "#7a9cc6", "#c0392b"])
ax[0].set_title("Erro na temperatura central (°C)"); ax[0].grid(axis="y", alpha=.3)
ax[1].bar(rot, [r[n]["MAE_suor_2h_g"] for n in nomes], color=["#b0a89a", "#7a9cc6", "#c0392b"])
ax[1].set_title("Erro na perda de água em 2 h (g)"); ax[1].grid(axis="y", alpha=.3)
for a in ax: a.tick_params(axis="x", labelsize=8)
plt.tight_layout(); plt.savefig("fig_erros.png", dpi=130)

# Gráfico 2: alarmes falsos x alertas perdidos, variando o limite de cada método
def curva(score, lims):
    return np.array([(((score >= L) & ~ref).sum(), ((score < L) & ref).sum()) for L in lims])
fig, ax = plt.subplots(figsize=(7.5, 4.6))
c = curva(ic, np.arange(24, 40, 0.25)); ax.plot(c[:, 0], c[:, 1], color="#888", lw=2, label="Limiar fixo de índice de calor (firmware atual)")
c = curva(d["tabela_T_UR_agora"][:, 0], np.arange(0, 0.4, 0.005)); ax.plot(c[:, 0], c[:, 1], color="#c9a96e", lw=2, ls="--", label="Tabela (só temperatura e umidade de agora)")
c = curva(d["mlp_historico_esp32"][:, 0], np.arange(0, 0.4, 0.005)); ax.plot(c[:, 0], c[:, 1], color="#c0392b", lw=2.5, label="IA embarcada (últimas 4 h + perfil)")
for L in (27, 32, 35):
    p = ic >= L; x, y = (p & ~ref).sum(), (~p & ref).sum()
    ax.scatter([x], [y], color="#555", zorder=5, s=20); ax.annotate(f"IC {L} °C", (x, y), textcoords="offset points", xytext=(5, 5), fontsize=8, color="#555")
ax.set_xlabel("Alarmes falsos (alertas sem risco)"); ax.set_ylabel("Alertas perdidos (risco sem alerta)")
ax.set_xlim(0, 6500); ax.set_ylim(0, 600); ax.grid(alpha=.3); ax.legend(fontsize=8)
ax.set_title(f"{len(ref)} momentos de teste, {int(ref.sum())} com risco (aumento da temp. central >= {LIM_DCORE} °C no JOS-3)\nquanto mais perto do canto inferior esquerdo, melhor", fontsize=9)
plt.tight_layout(); plt.savefig("fig_alarmes.png", dpi=130)
