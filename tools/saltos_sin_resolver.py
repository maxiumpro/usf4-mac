#!/usr/bin/env python3
# saltos_sin_resolver.py: cuántos de los 514 saltos indirectos del exe (lista
# de W1, sitios_saltos.tsv) siguen yendo por despacho en tiempo de ejecucion
# en el codigo liftado. Un salto con tabla (de Ghidra, del lifter o de las
# semillas de W1) sale como `switch` con `BadJump`; uno sin tabla sale como
# `Dispatch(c, t, <va>); return;` (o `Flow(...)`). Solo mide: no cambia nada.
#
#   saltos_sin_resolver.py --generated DIR --sitios sitios_saltos.tsv [--lista]
import argparse
import glob
import re
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--generated', required=True)
    ap.add_argument('--sitios', required=True)
    ap.add_argument('--lista', action='store_true', help='imprime los VA sin resolver')
    ap.add_argument('--salida', help='escribe los VA sin resolver, uno por linea (USF4_BRANCH_TRAPS)')
    a = ap.parse_args()
    sitios = []
    for i, line in enumerate(open(a.sitios, encoding='utf8')):
        if i == 0 or not line.strip():
            continue
        sitios.append(int(line.split('\t')[0], 16))
    sitios = set(sitios)
    # Despacho sin tabla: `Dispatch(c, t, 0x401014u)` o `Flow(c, t, R, 0x...)`
    # con el VA del salto como ultimo argumento y `return;` despues.
    pat = re.compile(r'::recomp::(?:Dispatch|Flow)\(c, t,(?: R,)? 0x([0-9A-Fa-f]+)u?\);\s*return;')
    badjump = re.compile(r'::recomp::BadJump\(c, 0x([0-9A-Fa-f]+)u?,')
    sin_tabla, con_tabla = set(), set()
    for f in sorted(glob.glob(a.generated + '/gen_[0-9]*.cpp')):
        txt = open(f, encoding='utf8', errors='replace').read()
        for m in pat.finditer(txt):
            sin_tabla.add(int(m.group(1), 16))
        for m in badjump.finditer(txt):
            con_tabla.add(int(m.group(1), 16))
    sin = sorted(sitios & sin_tabla)
    con = sorted(sitios & con_tabla)
    ni = sorted(sitios - sin_tabla - con_tabla)
    print('sitios de la lista (W1): %d' % len(sitios))
    print('  con tabla (switch): %d' % len(con))
    print('  sin resolver (despacho en tiempo de ejecucion): %d' % len(sin))
    print('  no aparecen como salto en el codigo liftado: %d %s' % (
        len(ni), ' '.join('%08X' % v for v in ni[:8])))
    if a.lista:
        for v in sin:
            print('%08X' % v)
    if a.salida:
        with open(a.salida, 'w', encoding='utf8', newline='\n') as f:
            for v in sin:
                f.write('%08X\n' % v)


if __name__ == '__main__':
    sys.exit(main())
