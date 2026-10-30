"""Gera simulações JOS-3 de idosos expostos a temperaturas internas variáveis.

Cada simulação: 1 h de aquecimento a uma temperatura inicial + 6 h de trajetória
aleatória de temperatura e umidade (passos de 10 min). Registra, a cada 10 min,
a temperatura do sangue central (t_cb) e a perda de água por suor e respiração.
Três perfis: acamado, em repouso (sentado) e ativo (em pé, andando devagar).
"""
import sys, json, numpy as np
from multiprocessing import Pool
from pythermalcomfort.models import JOS3

PERFIS = {
    "acamado": dict(posture="lying",    par=1.0, clo=0.5),
    "repouso": dict(posture="sitting",  par=1.2, clo=0.5),
    "ativo":   dict(posture="standing", par=1.8, clo=0.4),
}
PASSO = 10          # min
HORAS = 6

def trajetoria(rng):
    n = HORAS * 60 // PASSO
    t0 = rng.uniform(23, 31)
    alvo = rng.uniform(24, 39)                    # para onde a sala caminha
    tau = rng.uniform(1, 5) * 60 / PASSO          # quão rápido
    T = [t0]
    for k in range(1, n):
        T.append(T[-1] + (alvo - T[-1]) / tau + rng.normal(0, 0.25))
        if rng.random() < 0.03:                   # mudança brusca (abrir janela, ligar ventilador, sol)
            alvo = rng.uniform(24, 39)
    T = np.clip(np.array(T), 20, 41)
    rh0 = rng.uniform(45, 90)
    RH = np.clip(rh0 - 1.5 * (T - T[0]) + rng.normal(0, 2, n), 30, 95)
    return t0, T, RH

def simula(args):
    idx, perfil, seed = args
    rng = np.random.default_rng(seed)
    p = PERFIS[perfil]
    sexo = rng.choice(["male", "female"])
    altura = rng.uniform(1.50, 1.78); peso = rng.uniform(50, 88)
    idade = int(rng.uniform(65, 90))
    t0, T, RH = trajetoria(rng)
    m = JOS3(height=altura, weight=peso, age=idade, sex=sexo)
    m.posture = p["posture"]; m.par = p["par"]; m.clo = p["clo"]; m.v = 0.1
    m.tdb = t0; m.tr = t0; m.rh = RH[0]
    AQ = 120
    m.tdb = max(t0, 25); m.tr = m.tdb
    m.simulate(times=AQ, dtime=60)                # aquecimento (2 h) para sair do estado inicial do modelo
    n0 = len(m.dict_results()["t_cb"])
    for k in range(len(T)):
        m.tdb = T[k]; m.tr = T[k]; m.rh = RH[k]
        m.simulate(times=PASSO, dtime=60)
    r = m.dict_results()
    cb = np.array([float(np.ravel(x)[0]) for x in r["t_cb"]])[n0:]
    wl = np.array([float(np.ravel(x)[0]) for x in r["weight_loss_by_evap_and_res"]])[n0:]
    base = float(np.ravel(r["t_cb"][n0 - 1])[0])
    tcb = cb.reshape(len(T), PASSO)[:, -1].tolist()
    perda = (wl.reshape(len(T), PASSO).sum(1) * 60).tolist()   # g por passo
    return dict(idx=idx, perfil=perfil, sexo=str(sexo), altura=altura, peso=peso, idade=idade,
                base=base, T=T.tolist(), RH=RH.tolist(), tcb=tcb, perda=perda)

if __name__ == "__main__":
    n_por_perfil = int(sys.argv[1]); saida = sys.argv[2]
    jobs = [(i, perfil, 1000 * j + i) for j, perfil in enumerate(PERFIS) for i in range(n_por_perfil)]
    with Pool(2) as pool, open(saida, "w") as f:
        for k, res in enumerate(pool.imap_unordered(simula, jobs, chunksize=4)):
            f.write(json.dumps(res) + "\n")
            if k % 50 == 0: print(k, flush=True)
    print("ok")
