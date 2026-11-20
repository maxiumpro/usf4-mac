#!/usr/bin/env python3
# saltos_virtuales.py: separa de la lista de saltos sin medir (USF4_BRANCH_TRAPS)
# los saltos de VTABLE: `jmp [reg+k]` o `mov r,[r2+k]; (epílogo); jmp r` (un
# método virtual llamado como cola). Su destino depende del objeto y no hay tabla que medir; ni
# W1 ni nadie lo puede enumerar. Tomarlos NO es "despachar a ciegas": Dispatch
# solo llega a una función liftada y cualquier otro destino termina en un fatal
# con nombre. Se quitan de las trampas y quedan en un archivo aparte (que no se
# pierde: la corrida de B1 ronda 2 chocó con uno, 006B0BBC -> 006BA6A0).
#
#   saltos_virtuales.py --dis ssfiv.dis --sin-medir saltos_sin_medir.txt \
#       --trampas saltos_trampas.txt --virtuales saltos_virtuales.txt
import argparse
import re
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dis', required=True)
    ap.add_argument('--sin-medir', required=True)
    ap.add_argument('--trampas', required=True, help='salida: sin medir menos los virtuales')
    ap.add_argument('--virtuales', required=True, help='salida: los virtuales')
    a = ap.parse_args()
    order, ins = [], {}
    for line in open(a.dis, errors='replace'):
        m = re.match(r'^  ([0-9a-f]+):\t(.*)$', line.rstrip('\n'))
        if m:
            v = int(m.group(1), 16)
            order.append(v)
            ins[v] = m.group(2)
    idx = {v: i for i, v in enumerate(order)}
    sitios = [int(x, 16) for x in open(a.sin_medir).read().split()]
    mem = re.compile(r'(jmp|call)\s+DWORD PTR \[e[a-d]x\+0x[0-9a-f]+\]$')
    reg = re.compile(r'jmp\s+(e[a-d]x)$')
    # `mov r,[r2+k]` o `mov r,[r2]`: la carga del método desde la vtable.
    load = re.compile(r'mov\s+(e[a-d]x),DWORD PTR \[e[a-d]x(\+0x[0-9a-f]+)?\]$')
    # Instrucciones que pueden ir entre la carga y el salto (epílogo).
    epilogo = re.compile(r'(pop\s+e[a-ds][xip]|leave|mov\s+esp,ebp|add\s+esp,0x[0-9a-f]+)$')
    trampas, virt = [], []
    for s in sitios:
        i = idx.get(s)
        es_virtual = False
        if i is not None:
            t = ins[s]
            if mem.match(t):
                es_virtual = True
            else:
                m = reg.match(t)
                if m:
                    k = i - 1
                    while k >= 0 and i - k <= 4 and epilogo.match(ins[order[k]]):
                        k -= 1
                    p = load.match(ins[order[k]]) if k >= 0 else None
                    es_virtual = bool(p) and p.group(1) == m.group(1)
        (virt if es_virtual else trampas).append(s)
    for path, lst in ((a.trampas, trampas), (a.virtuales, virt)):
        with open(path, 'w', newline='\n') as f:
            for v in lst:
                f.write('%08X\n' % v)
    print('saltos sin medir: %d -> trampas %d, virtuales (vtable) %d' % (len(sitios), len(trampas), len(virt)))


if __name__ == '__main__':
    sys.exit(main())
