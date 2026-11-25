"""
Encaixes da caixa v5 (ABS). Mesmo sistema de coordenadas da v4b:
X largura, Y profundidade (0 = frente), Z altura (0 = base). Tampa em Y local: 0 = face interna,
negativo = para dentro da caixa.

O que muda em relação à v4b (impressa em ABS no IFAL: encaixes apertados, presilhas quebrando):
  - folgas de 0,35 a 0,5 mm por lado (o ABS encolhe e os furos fecham uns 0,2–0,3 mm);
  - nada de nervuras de esmagamento nem presilhas finas: o que precisar firmar leva uma gota de cola quente;
  - peças com no mínimo 2 mm de espessura.
"""
import numpy as np
from manifold3d import Manifold


def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cyl_y(cx, cz, r, y0, y1, n=64):
    return Manifold.cylinder(y1 - y0, r, r, n).rotate([-90, 0, 0]).translate([cx, y0, cz])


def buzzer_copo(bx, bz, TF, prof=8.0, r_in=6.45, parede=1.5):
    """Copo liso para o buzzer Ø12 (furo Ø12,9). Se ficar folgado, uma gota de cola quente atrás."""
    return cyl_y(bx, bz, r_in + parede, TF, TF + prof) - cyl_y(bx, bz, r_in, TF - 1, TF + prof + 1)


def aht_trilho(x0, x1, T, part_z, floor_x0, folga=0.35):
    """Trilho do AHT20 (placa 15 x 15 x 1,6 em pé, sensores para a frente).
    A placa entra em pé na área livre da esquerda e desliza para a direita até o batente.
    Rasgo de 1,6 + 2 x folga; abas de 2 mm; rampas de 45° para imprimir de frente para a mesa."""
    yf0, yf1 = 12.0 - 0.8 - folga, 12.0 + 0.8 + folga   # placa centrada em y = 12
    A = 2.0                                             # espessura das abas
    zb = T + 0.5                                        # fundo do rasgo = base da placa
    xs = x1 + 0.5                                       # batente da direita
    m = box(floor_x0, yf0 - A, T, xs + A, yf1 + A, zb)               # piso
    m += box(x0 - 2.5, yf1, T, xs + A, yf1 + A, T + 3.0)              # aba de trás (3 mm: saliência curta na impressão)
    m += box(x0 - 2.5, yf0 - A, T, xs + A, yf0, T + 1.5)              # aba da frente baixa (chips a 1,9 mm do fundo)
    m += box(x1 - 0.7, yf0 - A, T, xs + A, yf0, T + 6.4)              # canto direito da frente mais alto
    m += box(xs, yf0 - A, T, xs + A, yf1 + A, T + 6.4)                # batente
    top = zb + 15.0 + folga                                           # garfo do topo, preso na divisória
    m += box(x1 - 1.6, yf0 - A, top - 1.0, xs + A, yf0, part_z + 0.01)
    m += box(x1 - 1.6, yf1, top - 1.0, xs + A, yf1 + A, part_z + 0.01)
    m += box(xs, yf0 - A, top - 1.0, xs + A, yf1 + A, part_z + 0.01)
    # rampas de 45° (sem suporte: o corpo imprime de frente para a mesa, Y é a altura)
    yr = yf0 - A
    m += Manifold.hull(box(x1 - 0.7, yr - 0.01, T, xs + A, yr, T + 6.4) +
                       box(x1 - 0.7, yr - 6.4, T, xs + A, yr - 6.39, T + 0.01))
    h = part_z - (top - 1.0)
    m += Manifold.hull(box(x1 - 1.6, yr - 0.01, top - 1.0, xs + A, yr, part_z) +
                       box(x1 - 1.6, yr - h, part_z - 0.01, xs + A, yr - h + 0.01, part_z))
    # chanfro de entrada na esquerda
    m -= Manifold.hull(box(x0 - 2.6, yf0 - 0.6, zb, x0 - 2.5, yf1 + 0.6, T + 3.1) +
                       box(x0 - 1.0, yf0, zb, x0 - 0.99, yf1, T + 3.1))
    return m


def placa_trilhos(bx0, bx1, bz0, bz1, x_fim_cima, x_fim_baixo, back=-6.0, esp=1.6, folga=0.4,
                  apoio=1.2, aba=1.2, parede=2.0):
    """Trilhos em U na tampa para a placa de circuito (40 x 30, borda livre de 1,5 mm).
    A placa entra deslizando pela direita (lado do USB) até o batente da esquerda.
    Com a tampa fechada, a parede do corpo impede que ela saia.
    back = Y local da face de trás da placa (apoio). As abas cobrem 1,2 mm da borda (fora das ilhas)."""
    y_apoio = back                       # face de trás da placa
    y_rasgo = back - esp - folga         # até onde vai o rasgo (frente da placa + folga)
    y_aba = y_rasgo - aba                # frente da aba
    x0 = bx0 - folga                     # batente
    m = Manifold()
    for edge, s, xf in ((bz0, -1, x_fim_baixo), (bz1, +1, x_fim_cima)):
        zin = edge - s * apoio           # até onde o apoio/aba entra sobre a placa
        zout = edge + s * (folga + parede)
        z_lo, z_hi = min(zin, zout), max(zin, zout)
        trilho = box(x0 - parede, y_aba, z_lo, xf, 0.01, z_hi)
        # rasgo: da borda (com folga) até 1,2 mm para dentro
        zr0, zr1 = min(edge + s * folga, zin), max(edge + s * folga, zin)
        trilho -= box(x0, y_rasgo, zr0 - 0.001, xf + 1, back, zr1 + 0.001)
        # entrada alargada na ponta da direita (boca de 0,6 mm a mais, em rampa de 3 mm)
        boca = Manifold.hull(box(xf - 0.01, y_rasgo - 0.6, zr0, xf + 0.5, back + 0.6, zr1) +
                             box(xf - 3.0, y_rasgo, zr0, xf - 2.99, back, zr1))
        trilho -= boca
        m += trilho
    return m
