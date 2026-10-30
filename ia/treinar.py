"""Treina o modelo embarcado de risco térmico a partir das simulações JOS-3.

Compara três abordagens, separando treino e teste por simulação (nenhum pedaço de uma
mesma simulação aparece nos dois lados):
  1) "Tabela": só a temperatura e a umidade de agora + perfil (equivale à tabela pré-calculada);
  2) MLP pequena com o histórico das últimas 4 h + perfil (o modelo que vai para o ESP32);
  3) Gradient boosting com o mesmo histórico (referência de quanto um modelo grande ganharia).
Saídas: métricas, gráficos e modelo_risco.h com os pesos da MLP em C.
"""
import json, sys, numpy as np
from sklearn.neural_network import MLPRegressor
from sklearn.ensemble import HistGradientBoostingRegressor
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import mean_absolute_error

PERFIS = ["acamado", "repouso", "ativo"]
PASSO = 10            # min
JAN = 24              # 4 h de histórico (24 x 10 min)

def indice_calor(T, RH):
    # NOAA (Rothfusz) com o ajuste simples abaixo de 26,7 °C, em °C
    Tf = T * 9 / 5 + 32
    hi = 0.5 * (Tf + 61 + (Tf - 68) * 1.2 + RH * 0.094)
    if (hi + Tf) / 2 >= 80:
        hi = (-42.379 + 2.04901523 * Tf + 10.14333127 * RH - 0.22475541 * Tf * RH - 6.83783e-3 * Tf ** 2
              - 5.481717e-2 * RH ** 2 + 1.22874e-3 * Tf ** 2 * RH + 8.5282e-4 * Tf * RH ** 2 - 1.99e-6 * Tf ** 2 * RH ** 2)
    return (hi - 32) * 5 / 9

def janela(v, k, n):           # média dos últimos n passos até k (inclusive)
    return float(np.mean(v[max(0, k - n + 1):k + 1]))

def features(T, RH, k, perfil):
    oh = [1.0 if perfil == p else 0.0 for p in PERFIS]
    f_hist = [T[k], RH[k], janela(T, k, 3), janela(T, k, 6), janela(T, k, 12), janela(T, k, 24),
              janela(RH, k, 12), float(np.max(T[max(0, k - 11):k + 1])), T[k] - T[max(0, k - 6)]]
    f_tab = [T[k], RH[k]]
    return f_tab + oh, f_hist + oh

NOMES_HIST = ["T_agora", "UR_agora", "T_media_30min", "T_media_1h", "T_media_2h", "T_media_4h",
              "UR_media_2h", "T_max_2h", "T_variacao_1h", "perfil_acamado", "perfil_repouso", "perfil_ativo"]

def carrega(caminho):
    Xt, Xh, Y, G, P = [], [], [], [], []
    for linha in open(caminho):
        d = json.loads(linha)
        if not (np.all(np.isfinite(d["tcb"])) and np.all(np.isfinite(d["perda"])) and np.isfinite(d["base"])):
            continue   # o JOS-3 diverge em alguns homens acima de ~83 anos: simulação descartada
        T, RH = np.array(d["T"]), np.array(d["RH"])
        dcb = np.array(d["tcb"]) - d["base"]
        perda = np.array(d["perda"])
        for k in range(len(T)):
            ft, fh = features(T, RH, k, d["perfil"])
            suor_2h = float(perda[max(0, k - 11):k + 1].sum()) * (12 / min(12, k + 1))   # g em 2 h
            Xt.append(ft); Xh.append(fh); Y.append([dcb[k], suor_2h]); G.append(d["idx"] + 1000 * PERFIS.index(d["perfil"])); P.append(d["perfil"])
    return np.array(Xt), np.array(Xh), np.array(Y), np.array(G), np.array(P)

def exporta_c(mlp, esc_x, esc_y, caminho):
    L = []
    L.append("// modelo_risco.h - gerado por treinar.py (MLP treinada com simulacoes JOS-3)")
    L.append("// Entradas: " + ", ".join(NOMES_HIST))
    L.append("// Saidas: [0] aumento da temperatura central (C) em relacao ao basal; [1] perda de agua por suor nas ultimas 2 h (g)")
    L.append("#pragma once")
    L.append(f"const int MR_N_IN = {esc_x.mean_.size};")
    def lit(v):
        t = f"{float(v):.7g}"
        if "." not in t and "e" not in t and "inf" not in t: t += ".0"
        return t + "f"
    fmt = lambda a: "{" + ", ".join(lit(v) for v in np.ravel(a)) + "}"
    L.append(f"const float MR_X_MEDIA[] = {fmt(esc_x.mean_)};")
    L.append(f"const float MR_X_ESCALA[] = {fmt(esc_x.scale_)};")
    L.append(f"const float MR_Y_MEDIA[] = {fmt(esc_y.mean_)};")
    L.append(f"const float MR_Y_ESCALA[] = {fmt(esc_y.scale_)};")
    tam = [mlp.coefs_[0].shape[0]] + [c.shape[1] for c in mlp.coefs_]
    L.append(f"const int MR_N_CAMADAS = {len(mlp.coefs_)};")
    L.append(f"const int MR_TAM[] = {{{', '.join(map(str, tam))}}};")
    for i, (W, b) in enumerate(zip(mlp.coefs_, mlp.intercepts_)):
        L.append(f"const float MR_W{i}[] = {fmt(W)};   // {W.shape[0]} x {W.shape[1]}, linha = entrada")
        L.append(f"const float MR_B{i}[] = {fmt(b)};")
    L.append("""
// Calcula as 2 saidas a partir das 12 entradas. ReLU nas camadas ocultas, linear na saida.
inline void mr_prever(const float* x_in, float* y_out) {
  static const float* W[] = {""" + ", ".join(f"MR_W{i}" for i in range(len(mlp.coefs_))) + """};
  static const float* B[] = {""" + ", ".join(f"MR_B{i}" for i in range(len(mlp.coefs_))) + """};
  float a[32], z[32];
  for (int j = 0; j < MR_N_IN; j++) a[j] = (x_in[j] - MR_X_MEDIA[j]) / MR_X_ESCALA[j];
  for (int c = 0; c < MR_N_CAMADAS; c++) {
    int nin = MR_TAM[c], nout = MR_TAM[c + 1];
    for (int o = 0; o < nout; o++) {
      float s = B[c][o];
      for (int i = 0; i < nin; i++) s += a[i] * W[c][i * nout + o];
      z[o] = (c < MR_N_CAMADAS - 1 && s < 0) ? 0 : s;
    }
    for (int o = 0; o < nout; o++) a[o] = z[o];
  }
  for (int o = 0; o < MR_TAM[MR_N_CAMADAS]; o++) y_out[o] = a[o] * MR_Y_ESCALA[o] + MR_Y_MEDIA[o];
}""")
    open(caminho, "w").write("\n".join(L) + "\n")

if __name__ == "__main__":
    Xt, Xh, Y, G, P = carrega(sys.argv[1])
    rng = np.random.default_rng(42)
    grupos = np.unique(G); rng.shuffle(grupos)
    teste_g = set(grupos[: len(grupos) // 5])
    te = np.array([g in teste_g for g in G]); tr = ~te
    print(f"amostras: {len(Y)} ({tr.sum()} treino, {te.sum()} teste) de {len(grupos)} simulacoes")

    res = {}
    # 1) tabela: GBM só com T e UR de agora (é o melhor que uma tabela muito fina conseguiria)
    modelos_gbm = {}
    for nome, X in (("tabela_T_UR_agora", Xt), ("gbm_historico", Xh)):
        ms = [HistGradientBoostingRegressor(max_iter=300, random_state=0).fit(X[tr], Y[tr, j]) for j in range(2)]
        modelos_gbm[nome] = ms
        res[nome] = np.column_stack([m.predict(X[te]) for m in ms])
    # 2) MLP pequena para o ESP32
    ex, ey = StandardScaler().fit(Xh[tr]), StandardScaler().fit(Y[tr])
    mlp = MLPRegressor(hidden_layer_sizes=(16, 16), activation="relu", max_iter=600, early_stopping=True,
                       random_state=0, alpha=1e-4, learning_rate_init=3e-3)
    mlp.fit(ex.transform(Xh[tr]), ey.transform(Y[tr]))
    res["mlp_historico_esp32"] = ey.inverse_transform(mlp.predict(ex.transform(Xh[te])))
    n_par = sum(w.size for w in mlp.coefs_) + sum(b.size for b in mlp.intercepts_)

    out = {"n_amostras": int(len(Y)), "n_simulacoes": int(len(grupos)), "mlp_parametros": int(n_par),
           "mlp_kb_float32": round(n_par * 4 / 1024, 2), "metricas": {}}
    for nome, pr in res.items():
        m = {"MAE_dcore_C": mean_absolute_error(Y[te, 0], pr[:, 0]), "MAE_suor_2h_g": mean_absolute_error(Y[te, 1], pr[:, 1])}
        for perf in PERFIS:
            s = te & (P == perf)
            if s.sum() == 0: continue
            m[f"MAE_dcore_{perf}"] = mean_absolute_error(Y[s, 0], pr[(P[te] == perf), 0])
        out["metricas"][nome] = {k: round(float(v), 4) for k, v in m.items()}
    json.dump(out, open("resultados.json", "w"), indent=2, ensure_ascii=False)
    print(json.dumps(out, indent=2, ensure_ascii=False))
    exporta_c(mlp, ex, ey, "modelo_risco.h")
    import pickle
    pickle.dump({"mlp": mlp, "ex": ex, "ey": ey, "tab_dcore": modelos_gbm["tabela_T_UR_agora"][0],
                 "teste_ids_acamado": sorted(int(g) for g in teste_g if g < 1000)}, open("modelos.pkl", "wb"))
    np.savez("teste_mlp.npz", X=Xh[te][:200], Y=res["mlp_historico_esp32"][:200], Yv=Y[te][:200])
    np.savez("teste_tudo.npz", Y=Y[te], P=P[te], Xh=Xh[te], **{k: v for k, v in res.items()})
