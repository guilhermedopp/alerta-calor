// ia_embarcada.h - liga o modelo treinado (modelo_risco.h) ao firmware da caixa.
// Guarda as últimas 4 h de leituras (1 a cada 10 min), monta as 12 entradas do modelo
// e devolve o aumento estimado da temperatura central e a perda de água por suor em 2 h.
// Uso no firmware:
//   #include "ia_embarcada.h"
//   ia_registrar(temperatura, umidade);          // chamar a cada leitura; ele guarda 1 a cada 10 min
//   IaRisco r = ia_estimar(PERFIL_ACAMADO);       // perfil configurado na página
//   if (r.pronto && r.dcore >= LIM_DCORE) ...      // nível de risco
#pragma once
#include "modelo_risco.h"

enum PerfilOcupante { PERFIL_ACAMADO = 0, PERFIL_REPOUSO = 1, PERFIL_ATIVO = 2 };

struct IaRisco {
  bool  pronto;    // false nos primeiros 10 min (sem histórico)
  float dcore;     // aumento estimado da temperatura central (°C) em relação ao normal da pessoa
  float suor2h;    // perda estimada de água por suor e respiração nas últimas 2 h (g)
};

const int IA_JAN = 24;                       // 24 x 10 min = 4 h
const unsigned long IA_PASSO_MS = 600000UL;  // 10 min
static float ia_T[IA_JAN], ia_U[IA_JAN];
static int ia_n = 0, ia_pos = 0;             // quantos guardados / próxima posição
static unsigned long ia_ultimo = 0;
static float ia_somaT = 0, ia_somaU = 0; static int ia_cont = 0;   // média dentro dos 10 min

// valor de k passos atrás (0 = mais recente)
static float ia_val(const float* v, int k) { return v[(ia_pos - 1 - k + 2 * IA_JAN) % IA_JAN]; }

static float ia_media(const float* v, int n) {
  if (n > ia_n) n = ia_n;
  float s = 0; for (int k = 0; k < n; k++) s += ia_val(v, k);
  return s / n;
}

void ia_registrar(float t, float u) {
  ia_somaT += t; ia_somaU += u; ia_cont++;
  if (ia_ultimo != 0 && millis() - ia_ultimo < IA_PASSO_MS) return;
  ia_ultimo = millis();
  ia_T[ia_pos] = ia_somaT / ia_cont; ia_U[ia_pos] = ia_somaU / ia_cont;
  ia_somaT = ia_somaU = 0; ia_cont = 0;
  ia_pos = (ia_pos + 1) % IA_JAN; if (ia_n < IA_JAN) ia_n++;
}

IaRisco ia_estimar(PerfilOcupante perfil) {
  IaRisco r = { false, 0, 0 };
  if (ia_n == 0) return r;
  float tmax = ia_val(ia_T, 0);
  for (int k = 1; k < 12 && k < ia_n; k++) if (ia_val(ia_T, k) > tmax) tmax = ia_val(ia_T, k);
  int k1h = ia_n > 6 ? 6 : ia_n - 1;
  float x[12] = {
    ia_val(ia_T, 0), ia_val(ia_U, 0),
    ia_media(ia_T, 3), ia_media(ia_T, 6), ia_media(ia_T, 12), ia_media(ia_T, 24),
    ia_media(ia_U, 12), tmax, ia_val(ia_T, 0) - ia_val(ia_T, k1h),
    perfil == PERFIL_ACAMADO ? 1.0f : 0.0f, perfil == PERFIL_REPOUSO ? 1.0f : 0.0f, perfil == PERFIL_ATIVO ? 1.0f : 0.0f
  };
  float y[2]; mr_prever(x, y);
  r.pronto = true; r.dcore = y[0]; r.suor2h = y[1] < 0 ? 0 : y[1];
  return r;
}
