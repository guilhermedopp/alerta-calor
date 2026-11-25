"""Peça de teste rápida (uns 20 min): os mesmos furos e rasgos da caixa v5, para conferir as folgas
no ABS antes de imprimir o corpo inteiro. Plaquinha de 74 x 34 x 3 mm.
  Ø12,8  furo da lente do PIR          Ø5,6  furo do LED
  10,9   furo quadrado do botão        Ø12,9 copo do buzzer (8 mm de altura)
  trilho da placa de circuito (rasgo de 2,4 para placa de 1,6), 14 mm de comprimento
  rasgo do AHT20 (2,3 para placa de 1,6)
"""
import os, numpy as np, trimesh
from manifold3d import Manifold
def box(x0,y0,z0,x1,y1,z1): return Manifold.cube([x1-x0,y1-y0,z1-z0]).translate([x0,y0,z0])
def cyl(cx,cy,r,z0,z1,n=64): return Manifold.cylinder(z1-z0,r,r,n).translate([cx,cy,z0])
m = box(0,0,0,74,34,3)
m -= cyl(10,10,6.4,-1,4)                      # PIR
m -= cyl(10,26,2.8,-1,4,32)                   # LED
m -= box(21.55,4.55,-1,32.45,15.45,4)         # botão 10,9
m += cyl(27,25,6.45+1.5,3,11) - cyl(27,25,6.45,2.9,12)   # copo do buzzer
m -= cyl(27,25,6.45,-1,4)
# trilho da placa: apoio de 6 + rasgo de 2,4 (1,6 + 0,4 + 0,4) + aba de 1,2; placa entra pelo lado
m += box(40,4,3,54,30,3+6+2.4+1.2) - box(39,4+2.0,3+6,55,30.01,3+6+2.4)
# rasgo do AHT20: 2,3 entre duas abas de 2 mm, 6 mm de altura
m += box(58,4,3,60,30,9) + box(62.3,4,3,64.3,30,9)
mm = m.to_mesh(); t = trimesh.Trimesh(np.array(mm.vert_properties)[:,:3], np.array(mm.tri_verts))
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "imprimir", "teste_encaixes_v5.stl")
t.export(out); print("teste fechado:", t.is_watertight, np.round(t.extents,1))
