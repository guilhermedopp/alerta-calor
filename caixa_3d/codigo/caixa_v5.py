"""
Caixa de alerta de calor — v5 (para ABS). Mesmo tamanho da v4 (70 x 69 x 40 mm).
mm. X = largura, Y = profundidade (0 = frente), Z = altura (0 = base).

Por que a v5: a v4b foi desenhada para PETG/Tritan e, impressa em ABS no IFAL, teve detalhes finos
falhando, paredes rachando, suportes difíceis de tirar e encaixes apertados (furos, placa, tampa).

Mudanças principais:
  - frente lisa (sem o rebaixo do painel): ela fica inteira na mesa e o corpo imprime sem suporte;
    o painel preto é colado por cima, alinhado pelos furos;
  - paredes de 3,0 mm (eram 2,4), divisória de 2,4 mm e bossas mais grossas;
  - folgas de 0,35–0,5 mm por lado em todos os furos e encaixes (o ABS encolhe);
  - sem presilhas finas nem nervuras de aperto: a placa de circuito desliza em trilhos na tampa;
  - furos de ventilação, do buzzer e do LED maiores; rasgo do USB-C maior;
  - tampa com ressalto que centraliza no corpo (folga de 0,4) e furos de parafuso maiores;
  - pés removidos (eram saliências laterais na impressão); use pés de silicone adesivos.

Peças: corpo, tampa, painel (preto, colado na frente) e disco (preto, colado no meio do anel).
"""
import os, sys
import numpy as np, trimesh
from manifold3d import Manifold, CrossSection, JoinType

AQUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, AQUI)
import bercos_v5 as bercos

W, H, D = 70.0, 69.0, 40.0
R = 12.0                 # raio dos cantos (vista de frente)
CH = 2.0                 # chanfro da borda da frente (45°, encosta na mesa)
T = 3.0                  # paredes laterais, topo e base (eram 2,4)
TF = 3.0                 # parede da frente
LID_T = 2.4
BODY_D = D - LID_T
PART_Z = 18.5            # divisória da câmara do sensor
PART_T = 2.4             # espessura da divisória (era 2,0)
FOLGA = 0.4              # folga geral por lado

OUT = os.path.join(AQUI, "..", "saida") + os.sep
IMP = os.path.join(AQUI, "..", "imprimir") + os.sep
for _p in (OUT, IMP):
    os.makedirs(_p, exist_ok=True)

PNL = dict(x0=6.0, x1=64.0, z0=19.5, z1=65.5, r=7.0, depth=1.2)   # painel preto (colado por cima)
RING = (24.0, 42.5, 8.5, 12.5)          # centro x, z, raio interno, raio externo
SKIN = 0.8                              # pele do anel de luz (era 0,6)
PIR = (51.0, 57.5, 12.8)                # furo da capa Ø12 do AM312 (era 12,4 com nervuras)
BUZ = (51.0, 43.5)
BTN = (51.0, 29.5)
B_TOPO, B_ABA, B_TOPO_H, B_ALT, B_SALTO = 10.0, 12.0, 3.0, 10.0, 0.5

# placa de circuito 40 x 30 na tampa. Desceu 1 mm em relação à v4b: na v4b o canto de cima
# da placa encostava na bossa do parafuso de cima à direita.
PB = dict(cx=46.0, cz=41.5, hx=20.0, hz=15.0, standoff=6.0)
USB = dict(y=16.4, z=PB['cz'], w=8.0, h=13.5)       # rasgo do plugue USB-C, cantos R1,5 (era 7 x 12,5 oval)
KEYHOLES = [(13.0, 38.5), (57.0, 38.5)]
SCREWS = [(8.0, 8.0), (W - 8, 8.0), (8.0, H - 8), (W - 8, H - 8)]
BOSS_R = 3.8             # bossa (era 3,5)
PILOT_R = 1.4            # furo guia para parafuso M3 autoatarraxante em ABS (Ø2,8; era Ø2,5)


def rrect(x0, z0, x1, z1, r, n=64):
    sq = CrossSection.square([x1 - x0 - 2 * r, z1 - z0 - 2 * r]).translate([x0 + r, z0 + r])
    return sq.offset(r, JoinType.Round, circular_segments=n)


def extrude_y(cs, y0, y1):
    m = Manifold.extrude(cs, y1 - y0).rotate([90, 0, 0])
    return m.translate([0, y1, 0])


def box(x0, y0, z0, x1, y1, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def cyl_y(cx, cz, r, y0, y1, n=64):
    return Manifold.cylinder(y1 - y0, r, r, n).rotate([-90, 0, 0]).translate([cx, y0, cz])


# ------------------------------------------------ CORPO
outer = extrude_y(rrect(0, 0, W, H, R), CH, BODY_D) + extrude_y(rrect(CH, CH, W - CH, H - CH, R - CH), 0, CH + 0.01)
outer = Manifold.batch_hull([outer])
cavity = extrude_y(rrect(T, T, W - T, H - T, R - T), TF, BODY_D + 1)
body = outer - cavity

# anel de luz: canal por dentro, deixando 0,8 mm de pele na frente
rx, rz, ri, ro = RING
body -= cyl_y(rx, rz, ro, SKIN, TF + 0.01) - cyl_y(rx, rz, ri, -1, TF + 1)
# copo de luz em funil a 45° (o fundo plano da v4b era uma ponte de Ø25 mm na impressão)
F0 = TF + 2.0                       # início do funil
F1 = F0 + (ro - 2.8)                # 45°: o raio cai de 12,5 para 2,8
cup = Manifold.hull(cyl_y(rx, rz, ro + 2.0, TF, F0) + cyl_y(rx, rz, 4.8, F1, F1 + 2.5))
cup -= Manifold.hull(cyl_y(rx, rz, ro, TF - 0.1, F0) + cyl_y(rx, rz, 2.8, F1, F1 + 0.01))
cup -= cyl_y(rx, rz, 2.8, F0, F1 + 3)                               # furo do LED Ø5,6 (era 5,2)
body += cup

# PIR: furo liso Ø12,8 (a aba Ø13,34 encosta atrás da parede)
body -= cyl_y(PIR[0], PIR[1], PIR[2] / 2, -1, TF + 1)
# buzzer: 9 furos Ø2,4 (eram Ø1,8) + copo liso
bx, bz = BUZ
BUZ_HOLES = [(bx + 3.6 * i, bz + 3.6 * j) for i in (-1, 0, 1) for j in (-1, 0, 1)]
for hx, hz in BUZ_HOLES:
    body -= cyl_y(hx, hz, 1.2, -1, TF + 1, 24)
body += bercos.buzzer_copo(bx, bz, TF)
# espaço para a aba Ø13,34 do PIR atrás da parede (o copo do buzzer chegava nela)
body -= cyl_y(PIR[0], PIR[1], 7.1, TF, TF + 1.6)

# botão TS-1212F com capa quadrada
bx, bz = BTN
y_topo = -B_SALTO
y_aba = y_topo + B_TOPO_H
y_fundo = y_topo + B_ALT
hf = B_TOPO / 2 + 0.45                      # furo quadrado: folga 0,45 por lado (era 0,25)
body -= box(bx - hf, -1, bz - hf, bx + hf, TF + 1, bz + hf)
ha = B_ABA / 2 + 0.5
body -= box(bx - ha, y_aba, bz - ha, bx + ha, TF + 1, bz + ha)
GI = B_ABA / 2 + 0.5
GO = GI + 2.0                               # paredes da guia de 2 mm (eram 1,5)
yt = y_fundo + 0.3
GO_T = GI + 1.4                             # parede de cima mais fina: logo acima fica o buzzer
cr = box(bx - GO, TF, bz + GI, bx + GO, yt + 2.4, bz + GO_T) + box(bx - GO, TF, bz - GO, bx + GO, yt + 2.4, bz - GI)
cr += box(bx - GO, TF, bz - GO, bx - GI, y_fundo - 1.2, bz + GO_T) + box(bx + GI, TF, bz - GO, bx + GO, y_fundo - 1.2, bz + GO_T)
cr += box(bx - 3.0, yt, bz - GO, bx + 3.0, yt + 2.4, bz + GO_T)    # travessa
body += cr

# câmara do sensor: divisória + ventilação
part = box(T, TF, PART_Z, W - T, BODY_D - 0.4, PART_Z + PART_T) - box(36, 20, PART_Z - 1, 46, 28, PART_Z + PART_T + 1)
body += part
VENT_X = np.arange(14.0, 57.0, 5.0)
for z in (7.5, 12.5):
    for x in VENT_X:
        body -= cyl_y(x, z, 1.4, -1, TF + 1, 24)         # furos Ø2,8 (eram Ø2,2)
for z in (9.0, 14.0):                                    # rasgos laterais de 2,6 mm
    body -= box(-1, 9, z - 1.3, T + 1, BODY_D - 9, z + 1.3)
    body -= box(W - T - 1, 9, z - 1.3, W + 1, BODY_D - 9, z + 1.3)
SX = 35.0
body += bercos.aht_trilho(SX - 7.5, SX + 7.5, T, PART_Z, 12.0)

# USB-C na lateral direita: rasgo arredondado 8 x 13,5
_r = 1.5
_dy, _dz = USB['w'] / 2 - _r, USB['h'] / 2 - _r
_c = lambda y, z: Manifold.cylinder(T + 2, _r, _r, 24).rotate([0, 90, 0]).translate([W - T - 1, y, z])
body -= Manifold.batch_hull([_c(USB['y'] + a * _dy, USB['z'] + b * _dz) for a in (-1, 1) for b in (-1, 1)])

# bossas dos parafusos
for cx, cz in SCREWS:
    body += cyl_y(cx, cz, BOSS_R, TF, BODY_D)
    body -= cyl_y(cx, cz, PILOT_R, 8, BODY_D + 1, 32)

# ------------------------------------------------ TAMPA (Y local: 0 = face interna, negativo = para dentro)
lid = extrude_y(rrect(0, 0, W, H, R), 0, LID_T)
# ressalto que centraliza a tampa no corpo: 1,6 de largura x 1,5 de altura, 0,4 de folga da parede,
# só nos trechos retos (as bossas ocupam os cantos)
LIP_H, LIP_W = 1.5, 1.6
li = T + FOLGA
lid += box(13, -LIP_H, li, W - 13, 0.01, li + LIP_W)                   # base
lid += box(13, -LIP_H, H - li - LIP_W, W - 13, 0.01, H - li)            # topo
lid += box(li, -LIP_H, 13, li + LIP_W, 0.01, H - 13)                    # esquerda
lid += box(W - li - LIP_W, -LIP_H, 13, W - li, 0.01, H - 13)            # direita
# recorte no ressalto onde passa a divisória
lid -= box(-1, -LIP_H - 1, PART_Z - 0.5, W + 1, 0.0, PART_Z + PART_T + 0.5)
for cx, cz in SCREWS:
    lid -= cyl_y(cx, cz, 1.8, -LIP_H - 1, LID_T + 1, 32)                # Ø3,6 (era 3,4); parafuso de cabeça panela, sem rebaixo
# trilhos da placa de circuito
bx0, bx1 = PB['cx'] - PB['hx'], PB['cx'] + PB['hx']
bz0, bz1 = PB['cz'] - PB['hz'], PB['cz'] + PB['hz']
TRILHOS = bercos.placa_trilhos(bx0, bx1, bz0, bz1, x_fim_cima=57.0, x_fim_baixo=W - li - LIP_W - 0.6,
                               back=-PB['standoff'])
lid += TRILHOS
# fechaduras para pendurar
for kx, kz in KEYHOLES:
    lid += box(kx - 6, -3, kz - 5.5, kx + 6, 0.01, kz + 14.5)
    lid -= cyl_y(kx, kz, 4, -4, LID_T + 1)
    lid -= box(kx - 1.8, -1, kz, kx + 1.8, LID_T + 1, kz + 9)
    lid -= box(kx - 4, -4, kz, kx + 4, 0.6, kz + 9)
    lid -= cyl_y(kx, kz + 9, 4, -4, 0.6)

# ------------------------------------------------ PAINEL PRETO + DISCO (colados por cima da frente)
panel = extrude_y(rrect(PNL['x0'], PNL['z0'], PNL['x1'], PNL['z1'], PNL['r']), -PNL['depth'], 0)
panel -= cyl_y(rx, rz, ro, -3, 1)
panel -= cyl_y(PIR[0], PIR[1], PIR[2] / 2, -3, 1)
for hx, hz in BUZ_HOLES:
    panel -= cyl_y(hx, hz, 1.2, -3, 1, 24)
panel -= box(BTN[0] - hf, -3, BTN[1] - hf, BTN[0] + hf, 1, BTN[1] + hf)
disc = cyl_y(rx, rz, ri - 0.3, -PNL['depth'], 0)

# ------------------------------------------------ MODELOS DOS COMPONENTES (só para conferir)
placa = box(bx0, BODY_D + (-PB['standoff'] - 1.6), bz0, bx1, BODY_D - PB['standoff'], bz1)
# lado de solda: pontas e solda até 2 mm atrás da placa
solda = box(bx0 + 1.5, BODY_D - PB['standoff'], bz0 + 1.5, bx1 - 1.5, BODY_D - PB['standoff'] + 2.0, bz1 - 1.5)
# ESP32-C3 SuperMini em soquete: 18 x 22,5, ~14 mm à frente da placa; USB-C sai pela direita
esp = box(bx1 - 22.5, BODY_D - PB['standoff'] - 1.6 - 14.0, PB['cz'] - 9, bx1, BODY_D - PB['standoff'] - 1.6, PB['cz'] + 9)
plug_usb = box(bx1 - 1, USB['y'] - 3.2, USB['z'] - 5.5, W + 15, USB['y'] + 3.2, USB['z'] + 5.5)  # capa do plugue


def tm(m):
    mm = m.to_mesh()
    return trimesh.Trimesh(np.array(mm.vert_properties)[:, :3], np.array(mm.tri_verts))


if __name__ == "__main__":
    lid_asm = lid.translate([0, BODY_D, 0])
    asm = {"corpo": body, "tampa": lid_asm, "painel": panel, "disco": disc,
           "placa": placa, "solda": solda, "esp32": esp}
    for k, m in asm.items():
        tm(m).export(OUT + f"v5_asm_{k}.stl")

    # conferências de colisão
    def vol(m):
        return m.volume() if hasattr(m, "volume") else m.get_volume()
    print("--- colisões (volume em mm³; 0 = não encosta)")
    pares = [("corpo x tampa", body, lid_asm), ("corpo x placa", body, placa), ("corpo x solda", body, solda),
             ("corpo x ESP32", body, esp), ("tampa x solda", lid_asm, solda), ("tampa x ESP32", lid_asm, esp),
             ("corpo x plugue USB", body, plug_usb)]
    for nome, a, b in pares:
        print(f"{nome:22s}", round(vol(a ^ b), 3))
    # trilhos: a placa encosta só nas faces (folga) — confere que não sobrepõe
    print(f"{'tampa x placa':22s}", round(vol(lid_asm ^ placa), 3))
    # tampa deslizando: a placa entra pela direita sem bater (varre 30 mm em X)
    pior = 0.0
    for dx in np.arange(0, 30.1, 1.0):
        pior = max(pior, vol(lid_asm ^ placa.translate([dx, 0, 0])))
    print(f"{'placa deslizando':22s}", round(pior, 3))

    rot = {"corpo": np.pi / 2, "tampa": -np.pi / 2, "painel": -np.pi / 2, "disco": -np.pi / 2}
    parts = {"corpo": body, "tampa": lid, "painel": panel, "disco": disc}
    for k, m in parts.items():
        t = tm(m)
        t.apply_transform(trimesh.transformations.rotation_matrix(rot[k], [1, 0, 0]))
        t.apply_translation(-t.bounds[0])
        t.export(IMP + f"caixa_alerta_{k}_v5.stl")
        print(k, "fechado:", t.is_watertight, "dims:", np.round(t.extents, 1), "vol cm3:", round(t.volume / 1000, 1))
