"""Peça de teste 2 (v5b): escadas de folga para o botão, o AHT20 e o trilho da placa.

A primeira peça de teste (teste_encaixes_v5) aprovou o LED, o PIR e o buzzer, mas:
  - o botão não passou no furo de 10,9 mm;
  - o rasgo do AHT20 tinha as duas abas com 6 mm de altura, e na caixa a aba da frente tem só 1,5 mm
    (os chips do módulo ficam a 1,9 mm da borda de baixo). Por isso o módulo não entrava;
  - o "trilho" da placa era um bloco com uma fresta de 24 mm de vão, que cedeu na impressão.
    Na caixa o trilho é diferente: uma aba curta de 1,6 mm, que imprime bem.

Aqui cada encaixe aparece em 3 a 5 tamanhos. O número de pontinhos ao lado diz qual é qual.
Plaquinha de 92 x 56 x 2,4 mm (com os trilhos na borda, 95,6 x 59,6), uns 30 min em ABS, sem suporte, com a face lisa na mesa.
"""
import os, numpy as np, trimesh
from manifold3d import Manifold

def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])

def cyl(cx, cy, r, z0, z1, n=32):
    return Manifold.cylinder(z1 - z0, r, r, n).translate([cx, cy, z0])

B = 2.4                                   # espessura da base
m = box(0, 0, 0, 92, 56, B)

def pontos(x, y, n, z=B):
    """n pontinhos em fila (Ø1,4, 0,6 de altura) a partir de (x, y)."""
    global m
    for i in range(n):
        m += cyl(x + i * 2.2, y, 0.7, z - 0.01, z + 0.6, 16)

# ---- 1) botão: furos quadrados de 10,9 a 12,9 (fileira de baixo)
BOTAO = [10.9, 11.4, 11.9, 12.4, 12.9]
for i, s in enumerate(BOTAO):
    cx, cy = 9 + i * 18.5, 11
    m -= box(cx - s / 2, cy - s / 2, -1, cx + s / 2, cy + s / 2, B + 1)
    pontos(cx - 2.2 * i / 2, cy + s / 2 + 2.2, i + 1)

# ---- 2) AHT20: perfil igual ao da caixa (aba da frente de 1,5 mm, aba de trás de 3 mm), rasgos de 2,0 / 2,3 / 2,6
AHT = [2.0, 2.3, 2.6]
for i, g in enumerate(AHT):
    x0 = 4 + i * 14
    y_f = 26.0                                  # face da frente do rasgo
    m += box(x0, y_f - 2.0, B, x0 + 11, y_f, B + 1.5)          # aba da frente (baixa)
    m += box(x0, y_f + g, B, x0 + 11, y_f + g + 2.0, B + 3.0)  # aba de trás
    m += box(x0 + 11, y_f - 2.0, B, x0 + 13, y_f + g + 2.0, B + 6.4)   # batente
    pontos(x0 + 1, y_f - 4.0, i + 1)

# ---- 3) trilho da placa de circuito: perfil igual ao da tampa (parede, apoio de 6 mm e aba de 1,2 mm por cima)
#      rasgos de 2,0 / 2,4 / 2,8 de altura para a placa de 1,6 mm. Ficam na borda da peça, virados para fora,
#      para dar para encaixar um pedaço de placa de qualquer tamanho, entrando pela ponta.
TRILHO = [2.0, 2.4, 2.8]
def trilho(h, eixo, pos, a0, a1):
    """eixo 'y': parede em y=pos..pos+2, aba virada para +y; eixo 'x': parede em x=pos..pos+2, aba para +x."""
    topo = B + 6 + h + 1.2
    if eixo == 'y':
        par = box(a0, pos, 0, a1, pos + 2, topo)
        apo = box(a0, pos + 2, 0, a1, pos + 3.6, B + 6)
        aba = box(a0, pos + 2, B + 6 + h, a1, pos + 3.6, topo)
    else:
        par = box(pos, a0, 0, pos + 2, a1, topo)
        apo = box(pos + 2, a0, 0, pos + 3.6, a1, B + 6)
        aba = box(pos + 2, a0, B + 6 + h, pos + 3.6, a1, topo)
    return par + apo + aba
m += trilho(TRILHO[0], 'y', 54, 4, 44)
pontos(6, 51.5, 1)
m += trilho(TRILHO[1], 'y', 54, 48, 88)
pontos(50, 51.5, 2)
m += trilho(TRILHO[2], 'x', 90, 26, 54)
pontos(83, 30, 3)

mm = m.to_mesh()
t = trimesh.Trimesh(np.array(mm.vert_properties)[:, :3], np.array(mm.tri_verts))
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "imprimir", "teste_encaixes_v5b.stl")
t.export(out)
print("teste v5b fechado:", t.is_watertight, np.round(t.extents, 1))
