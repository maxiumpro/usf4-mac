#!/usr/bin/env python3
# tablas_dll.py: tablas de salto de una DLL que no pasó por Ghidra (cryptopp):
# todo `jmp [reg*4+TABLA]` con TABLA dentro de la imagen. Las entradas son los
# dwords consecutivos de TABLA que apuntan a codigo (.text) de la misma DLL,
# hasta el primero que no, o hasta el inicio de otra tabla de la lista. Sale en
# el formato de jumptables.tsv (`jmp<TAB>destinos`, VA con la base de la DLL).
# Heuristica: un destino de mas no cuesta nada (el lifter solo emite un caso
# extra), uno de menos termina en un fatal con nombre en tiempo de ejecución.
#
#   tablas_dll.py --pe cryptopp.dll > jumptables.tsv
import argparse
import re
import struct
import subprocess
import sys

sys.path.insert(0, __import__('os').path.dirname(__file__))
from pe_mini import Pe  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pe', required=True)
    ap.add_argument('--objdump', default='objdump')
    a = ap.parse_args()
    pe = Pe(a.pe)
    dis = subprocess.run([a.objdump, '-d', '-M', 'intel', '--no-show-raw-insn', '-j', '.text', a.pe],
                         capture_output=True, text=True, check=True).stdout
    pat = re.compile(r'^\s*([0-9a-f]+):\s+jmp\s+DWORD PTR \[[a-z]{3}\*4\+0x([0-9a-f]+)\]', re.M)
    sitios = [(int(m.group(1), 16), int(m.group(2), 16)) for m in pat.finditer(dis)]
    inicios = sorted(set(t for _, t in sitios))
    sec = pe.section('.text')
    lo = pe.base + sec[1]
    hi = lo + sec[2]
    print('jmp\ttargets')
    for sitio, tabla in sitios:
        sig = [t for t in inicios if t > tabla]
        tope = sig[0] if sig else tabla + 4 * 256
        dest = []
        va = tabla
        # La CRT (memcpy) pone la tabla encima del codigo y su indice 0 nunca se
        # usa: si la primera entrada no apunta a codigo, se parte de la segunda.
        v0 = pe.u32(va)
        if v0 is None or not (lo <= v0 < hi):
            va += 4
        while va < tope and len(dest) < 256:
            v = pe.u32(va)
            if v is None or not (lo <= v < hi):
                break
            if v not in dest:  # el lifter no admite dos `case` iguales
                dest.append(v)
            va += 4
        if not dest:
            # memcpy: `sub ecx,4; jb` deja el indice en -4..-1, asi que la tabla
            # empieza 16 bytes antes de la direccion del salto.
            va = tabla - 16
            while va < tabla and len(dest) < 4:
                v = pe.u32(va)
                if v is None or not (lo <= v < hi):
                    dest = []
                    break
                if v not in dest:
                    dest.append(v)
                va += 4
        if dest:
            print('%08X\t%s' % (sitio, ','.join('%08X' % d for d in dest)))


if __name__ == '__main__':
    main()
