import numpy as np, subprocess, sys
d=np.load('teste_mlp.npz'); X=d['X']; Y=d['Y']
open('in.txt','w').write("\n".join(" ".join(f"{v:.9g}" for v in r) for r in X))
open('t.cpp','w').write('''#include <cstdio>
#include "modelo_risco.h"
int main(){ float x[12], y[2]; while(true){ for(int i=0;i<12;i++) if(scanf("%f",&x[i])!=1) return 0; mr_prever(x,y); printf("%.6f %.6f\\n",y[0],y[1]); } }''')
subprocess.check_call(["g++","-O2","-o","t","t.cpp"])
out=subprocess.run(["./t"],stdin=open('in.txt'),capture_output=True,text=True).stdout
C=np.array([list(map(float,l.split())) for l in out.strip().splitlines()])
print("dif max dcore", np.abs(C[:,0]-Y[:,0]).max(), "dif max suor", np.abs(C[:,1]-Y[:,1]).max())
