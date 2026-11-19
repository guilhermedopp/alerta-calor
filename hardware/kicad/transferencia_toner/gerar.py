"""Gera o PDF A4 para transferência térmica de toner da placa do Alerta de Calor.

Lê o .kicad_pcb, desenha o cobre de cada face (trilhas, ilhas, vias e o plano de GND
preenchido de novo aqui, com folga de 0,3 mm e alívios térmicos), e monta 4 cópias da
frente (F.Cu, espelhada) e 4 do verso (B.Cu, sem espelhar) numa folha A4, em escala 1:1.
"""
import math, sys
from shapely.geometry import Point, LineString, box, Polygon, MultiPolygon
from shapely.ops import unary_union
from shapely import affinity
from reportlab.pdfgen import canvas
from reportlab.lib.units import mm

sys.path.insert(0, __import__('os').path.dirname(__file__))
from sexpr import parse, find

PCB, OUT = sys.argv[1], sys.argv[2]
board = parse(open(PCB, encoding='utf-8').read())

GND = '/GND'
CLEAR, TGAP, TBRIDGE, MINT = 0.3, 0.5, 0.5, 0.2
EDGE = (100.0, 80.0, 140.0, 110.0)
DOT = 0.5   # furinho no centro das ilhas, para centralizar a broca

def f(v): return float(v)

def val(node, key, i=1):
    r = find(node, key)
    return r[0][i] if r else None

def pad_shape(shape, cx, cy, w, h, ang, rr=0.25):
    if shape == 'circle':
        g = Point(cx, cy).buffer(w / 2, 64)
    elif shape == 'oval':
        if w >= h:
            d = (w - h) / 2
            g = LineString([(cx - d, cy), (cx + d, cy)]).buffer(h / 2, 64) if d > 0 else Point(cx, cy).buffer(h / 2, 64)
        else:
            d = (h - w) / 2
            g = LineString([(cx, cy - d), (cx, cy + d)]).buffer(w / 2, 64)
    elif shape == 'roundrect':
        r = min(w, h) * rr
        g = box(cx - w / 2 + r, cy - h / 2 + r, cx + w / 2 - r, cy + h / 2 - r).buffer(r, 32)
    else:
        g = box(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
    # ângulo do KiCad: anti-horário na tela (y para baixo) = horário na matemática
    return affinity.rotate(g, -ang, origin=(cx, cy)) if ang else g

pads, tracks, vias, zones = [], [], [], []
for it in board[1:]:
    if not isinstance(it, list):
        continue
    k = it[0]
    if k == 'segment':
        s, e = find(it, 'start')[0], find(it, 'end')[0]
        tracks.append(dict(layer=val(it, 'layer').strip('"'), net=val(it, 'net').strip('"'),
                           g=LineString([(f(s[1]), f(s[2])), (f(e[1]), f(e[2]))]).buffer(f(val(it, 'width')) / 2, 32)))
    elif k == 'via':
        a = find(it, 'at')[0]
        vias.append(dict(net=val(it, 'net').strip('"'), x=f(a[1]), y=f(a[2]),
                         g=Point(f(a[1]), f(a[2])).buffer(f(val(it, 'size')) / 2, 64), drill=f(val(it, 'drill'))))
    elif k == 'zone':
        pts = [(f(p[1]), f(p[2])) for p in find(find(find(it, 'polygon')[0], 'pts')[0], 'xy')]
        zones.append(dict(layer=val(it, 'layer').strip('"'), net=val(it, 'net').strip('"'), poly=Polygon(pts)))
    elif k == 'footprint':
        a = find(it, 'at')[0]
        fx, fy = f(a[1]), f(a[2])
        fa = f(a[3]) if len(a) > 3 else 0.0
        t = math.radians(fa)
        for p in find(it, 'pad'):
            pa = find(p, 'at')[0]
            rx, ry = f(pa[1]), f(pa[2])
            ang = f(pa[3]) if len(pa) > 3 else 0.0
            # posição do pad: girada pelo ângulo do footprint (anti-horário na tela)
            x = fx + rx * math.cos(t) + ry * math.sin(t)
            y = fy - rx * math.sin(t) + ry * math.cos(t)
            sz = find(p, 'size')[0]
            rr = val(p, 'roundrect_rratio')
            net = find(p, 'net')
            layers = [l.strip('"') for l in find(p, 'layers')[0][1:]]
            pads.append(dict(net=net[0][1].strip('"') if net else '', x=x, y=y, ang=ang, layers=layers,
                             drill=f(val(p, 'drill')) if find(p, 'drill') else 0,
                             g=pad_shape(p[3], x, y, f(sz[1]), f(sz[2]), ang, f(rr) if rr else 0.25)))

def on(layer, layers):
    return layer in layers or '*.Cu' in layers

def copper(layer):
    lt = [t for t in tracks if t['layer'] == layer]
    lp = [p for p in pads if on(layer, p['layers'])]
    items = [t['g'] for t in lt] + [p['g'] for p in lp] + [v['g'] for v in vias]
    fills = []
    for z in [z for z in zones if z['layer'] == layer]:
        other = [t['g'] for t in lt if t['net'] != z['net']] + [p['g'] for p in lp if p['net'] != z['net']] \
              + [v['g'] for v in vias if v['net'] != z['net']]
        same_pads = [p for p in lp if p['net'] == z['net']]
        fill = z['poly'].difference(unary_union([g.buffer(CLEAR, 32) for g in other]))
        # alívio térmico: anel de 0,5 mm em volta do pad e 4 raios de 0,5 mm
        for p in same_pads:
            ring = p['g'].buffer(TGAP, 32)
            fill = fill.difference(ring)
            L = 10
            spokes = unary_union([box(p['x'] - L, p['y'] - TBRIDGE / 2, p['x'] + L, p['y'] + TBRIDGE / 2),
                                  box(p['x'] - TBRIDGE / 2, p['y'] - L, p['x'] + TBRIDGE / 2, p['y'] + L)])
            spokes = affinity.rotate(spokes, -p['ang'], origin=(p['x'], p['y']))
            fill = fill.union(spokes.intersection(ring).difference(unary_union([g.buffer(CLEAR, 32) for g in other])))
        fill = fill.buffer(-MINT / 2, 16).buffer(MINT / 2, 16)
        anchors = unary_union([p['g'] for p in same_pads] + [t['g'] for t in lt if t['net'] == z['net']]
                              + [v['g'] for v in vias if v['net'] == z['net']])
        geoms = list(fill.geoms) if isinstance(fill, MultiPolygon) else [fill]
        kept = [g for g in geoms if g.intersects(anchors)]   # remove ilhas soltas
        fills += kept
    cu = unary_union(items + fills)
    holes = [Point(p['x'], p['y']).buffer(DOT / 2, 24) for p in lp if p['drill']] + \
            [Point(v['x'], v['y']).buffer(DOT / 2, 24) for v in vias]
    return cu.difference(unary_union(holes))

CU = {'F.Cu': copper('F.Cu'), 'B.Cu': copper('B.Cu')}

# ---------- PDF A4 ----------
W, H = 210, 297
c = canvas.Canvas(OUT, pagesize=(W * mm, H * mm))
c.setTitle('Alerta de Calor - transferência de toner (A4, escala 1:1)')

def P(x, y):           # mm do topo-esquerda da folha -> pontos do PDF
    return x * mm, (H - y) * mm

def draw_board(layer, ox, oy, mirror):
    x0, y0, x1, y1 = EDGE
    def tr(x, y):
        u = (x1 - x) if mirror else (x - x0)
        return P(ox + u, oy + (y - y0))
    geom = CU[layer]
    polys = list(geom.geoms) if hasattr(geom, 'geoms') else [geom]
    c.setFillColorRGB(0, 0, 0)
    for poly in polys:
        path = c.beginPath()
        for ring in [poly.exterior] + list(poly.interiors):
            pts = list(ring.coords)
            path.moveTo(*tr(*pts[0]))
            for q in pts[1:]:
                path.lineTo(*tr(*q))
            path.close()
        c.drawPath(path, stroke=0, fill=1, fillMode=0)   # 0 = par-ímpar: furos ficam vazados
    # contorno da placa (linha de corte)
    c.setLineWidth(0.2 * mm)
    c.setStrokeColorRGB(0, 0, 0)
    a, b = P(ox, oy), P(ox + 40, oy + 30)
    c.rect(a[0], b[1], 40 * mm, 30 * mm, stroke=1, fill=0)

c.setFont('Helvetica-Bold', 13)
c.drawString(*P(10, 16), 'Alerta de Calor v0.1 - placa para transferência de toner')
c.setFont('Helvetica', 8.5)
for i, t in enumerate([
    'Imprimir em TAMANHO REAL (100%, sem "ajustar à página"), na impressora LASER, no lado brilhante do papel.',
    'Confira com a régua abaixo: a barra tem de medir exatamente 50 mm. Placa: 40 x 30 mm, trilhas de 0,2 mm.',
    'Preto = cobre que fica. O furinho branco no meio de cada ilha guia a broca.']):
    c.drawString(*P(10, 22.5 + i * 4.2), t)

GAP = 7.5
X0 = (W - (4 * 40 + 3 * GAP)) / 2
rows = [('F.Cu', True, 'FRENTE (F.Cu, lado dos componentes) - JÁ ESPELHADA', 45),
        ('B.Cu', False, 'VERSO (B.Cu, lado da solda) - NÃO espelhar', 95)]
for layer, mir, title, y in rows:
    c.setFont('Helvetica-Bold', 9.5)
    c.drawString(*P(X0, y - 3), title)
    c.setFont('Helvetica', 7)
    for i in range(4):
        ox = X0 + i * (40 + GAP)
        draw_board(layer, ox, y, mir)
        c.drawString(*P(ox, y + 34), f"{'F' if mir else 'V'}{i + 1}  {'frente' if mir else 'verso'}")

# régua de 50 mm
ry = 150
c.setFont('Helvetica', 8)
a = P(10, ry)
c.setLineWidth(0.25 * mm)
c.line(a[0], a[1], a[0] + 50 * mm, a[1])
for i in range(0, 51, 5):
    hgt = 3 if i % 10 == 0 else 1.6
    c.line(a[0] + i * mm, a[1], a[0] + i * mm, a[1] + hgt * mm)
    if i % 10 == 0:
        c.drawCentredString(a[0] + i * mm, a[1] + 4 * mm, str(i))
c.drawString(*P(64, ry), 'régua: de 0 a 50 tem de dar 50 mm')

c.setFont('Helvetica', 8.5)
notes = [
    'Como as faces se encaixam: a frente (F) e o verso (V) de mesmo número formam uma placa.',
    'Furos de referência: os furos do ESP32 e dos conectores aparecem nas duas faces, no mesmo lugar.',
    'Dica para dupla face: transfira e corroa o verso primeiro, fure 2 ou 3 ilhas dos cantos, use',
    'esses furos (contra a luz ou com alfinetes) para alinhar a frente e só então transfira a frente.',
    'As 2 vias (ponte de GND) viram furos com um pedaço de fio soldado nas duas faces.',
]
for i, t in enumerate(notes):
    c.drawString(*P(10, 162 + i * 4.4), t)
c.showPage()
c.save()

for k, g in CU.items():
    print(k, 'área de cobre %.1f mm²' % g.area)
