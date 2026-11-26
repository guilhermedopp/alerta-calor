"""Peça de teste completa: todos os encaixes da caixa v5, cada um em vários tamanhos.

Uma plaquinha de 118 x 98 x 3 mm (com os trilhos da borda, 120 x 98) (3 mm = espessura da parede da caixa), para imprimir em ABS,
sem suporte, com a face lisa na mesa. O número de pontinhos ao lado de cada furo diz qual tamanho é
(1 ponto = o menor). Depois de testar, anote o melhor de cada linha e me passe.

  LED (furo redondo)            Ø5,2  5,6  6,0  6,4  6,8
  furo de parafuso (passante)   Ø3,2  3,4  3,6  3,8         parafuso M3 da tampa
  lente do PIR                  Ø12,4 12,8 13,2 13,6
  rasgo do USB-C                8x13,5  8,6x14,1  9,2x14,7
  botão (furo quadrado)         10,9  11,4  11,9  12,4  12,9
  copo do buzzer (Ø interno)    12,3  12,6  12,9  13,2
  bossa com furo guia           Ø2,5  2,8  3,1               parafuso M3 autoatarraxante
  rasgo do AHT20                2,0  2,3  2,6  2,9           aba da frente baixa (1,5 mm), igual à caixa
  trilho da placa de circuito   2,0  2,4  2,8                na borda; a placa entra deitada pela ponta
"""
import os, numpy as np, trimesh
from manifold3d import Manifold

def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])

def cyl(cx, cy, r, z0, z1, n=48):
    return Manifold.cylinder(z1 - z0, r, r, n).translate([cx, cy, z0])

def rrect(cx, cy, w, h, r, z0, z1):
    dx, dy = w / 2 - r, h / 2 - r
    return Manifold.batch_hull([cyl(cx + a * dx, cy + b * dy, r, z0, z1, 24) for a in (-1, 1) for b in (-1, 1)])

T = 3.0
W, H = 118.0, 96.0
furos, solidos = [], []

def pontos(x, y, n):
    for i in range(n):
        solidos.append(cyl(x + (i - (n - 1) / 2) * 2.2, y, 0.7, T - 0.01, T + 0.6, 16))

# linha 1: LED e furos passantes de parafuso
for i, d in enumerate([5.2, 5.6, 6.0, 6.4, 6.8]):
    x = 8 + i * 10
    furos.append(cyl(x, 9, d / 2, -1, T + 1)); pontos(x, 15.5, i + 1)
for i, d in enumerate([3.2, 3.4, 3.6, 3.8]):
    x = 64 + i * 9
    furos.append(cyl(x, 9, d / 2, -1, T + 1, 32)); pontos(x, 15.5, i + 1)

# linha 2: lente do PIR
for i, d in enumerate([12.4, 12.8, 13.2, 13.6]):
    x = 11 + i * 19
    furos.append(cyl(x, 28, d / 2, -1, T + 1, 64)); pontos(x, 37, i + 1)

# coluna da direita: rasgos do USB-C (atravessam a placa como atravessam a parede)
for i, (w, h) in enumerate([(8.0, 13.5), (8.6, 14.1), (9.2, 14.7)]):
    y = 12 + i * 19
    furos.append(rrect(104, y, w, h, 1.5, -1, T + 1)); pontos(96, y, i + 1)

# linha 3: botão (furos quadrados)
for i, s in enumerate([10.9, 11.4, 11.9, 12.4, 12.9]):
    x = 10 + i * 17
    furos.append(box(x - s / 2, 50 - s / 2, -1, x + s / 2, 50 + s / 2, T + 1)); pontos(x, 59.5, i + 1)

# linha 4: copos do buzzer (8 mm de altura, parede 1,5) e bossas de parafuso
for i, d in enumerate([12.3, 12.6, 12.9, 13.2]):
    x = 11 + i * 19
    r = d / 2
    solidos.append(cyl(x, 72, r + 1.5, T - 0.01, T + 8) - cyl(x, 72, r, T - 0.1, T + 9))
    pontos(x, 63.2, i + 1)
for i, d in enumerate([2.5, 2.8, 3.1]):
    x = 89 + i * 10
    solidos.append(cyl(x, 72, 3.8, T - 0.01, T + 10) - cyl(x, 72, d / 2, T + 1, T + 11, 32))
    pontos(x, 63.2, i + 1)

# linha 5: rasgos do AHT20 (perfil da caixa: aba da frente 1,5 mm, aba de trás 3 mm, batente)
for i, g in enumerate([2.0, 2.3, 2.6, 2.9]):
    x0 = 4 + i * 18
    yf = 86.0
    solidos.append(box(x0, yf - 2, T - 0.01, x0 + 13, yf, T + 1.5))
    solidos.append(box(x0, yf + g, T - 0.01, x0 + 13, yf + g + 2, T + 3.0))
    solidos.append(box(x0 + 13, yf - 2, T - 0.01, x0 + 15, yf + g + 2, T + 6.4))
    pontos(x0 + 6.5, 82.2, i + 1)

# trilhos da placa de circuito: na borda de cima (2) e na da direita (1), virados para fora
def trilho(h, eixo, pos, a0, a1):
    topo = T + 6 + h + 1.2
    if eixo == 'y':
        return (box(a0, pos, 0, a1, pos + 2, topo) + box(a0, pos + 2, 0, a1, pos + 3.6, T + 6)
                + box(a0, pos + 2, T + 6 + h, a1, pos + 3.6, topo))
    return (box(pos, a0, 0, pos + 2, a1, topo) + box(pos + 2, a0, 0, pos + 3.6, a1, T + 6)
            + box(pos + 2, a0, T + 6 + h, pos + 3.6, a1, topo))
# (os rasgos do AHT ficam até y = 91; os trilhos de cima começam em y = 94)
solidos.append(trilho(2.0, 'y', 94, 2, 40)); pontos(46, 92.5, 1)
solidos.append(trilho(2.4, 'y', 94, 56, 96)); pontos(100, 92.5, 2)
solidos.append(trilho(2.8, 'x', 116, 66, 92)); pontos(112, 79, 3)

H2 = 96 + 2.0
m = box(0, 0, 0, W, H2, T)
for s in solidos:
    m += s
for f in furos:
    m -= f

mm = m.to_mesh()
t = trimesh.Trimesh(np.array(mm.vert_properties)[:, :3], np.array(mm.tri_verts))
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "imprimir", "teste_encaixes_completo.stl")
t.export(out)
print("fechado:", t.is_watertight, "medidas:", np.round(t.extents, 1))
