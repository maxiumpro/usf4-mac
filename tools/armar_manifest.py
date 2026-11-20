#!/usr/bin/env python3
# armar_manifest.py: escribe el lifter.manifest de un módulo a partir del
# binario del usuario y de las listas de análisis (Ember, S0). Lo del juego
# que entra al manifest son solo direcciones y nombres de Ember (MIT).
#
#   armar_manifest.py --pe SSFIV.exe [--ember ember_names.tsv]
#       [--s0 candidatos_funcion.tsv] [--eh eh_entradas.tsv
#        --funcs functions.tsv] --titulo TEXTO > usf4.manifest
#
# Las directivas de la CRT de VS2010 (setjmp, longjmp, SEH4, C++ EH) se
# buscan por patrón de bytes, no por dirección fija: así sirven igual para
# la CRT estática de cryptopp.dll.
import argparse
import csv
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from pe_mini import Pe  # noqa: E402

# (directiva, desplazamiento dentro del patrón, nombre, regex de bytes, nota)
CRT = [
    ('setjmp', 0, '__setjmp3',
     rb'\x8b\x54\x24\x04\x89\x2a\x89\x5a\x04\x89\x7a\x08\x89\x72\x0c\x89\x62\x10'
     rb'\x8b\x04\x24\x89\x42\x14\xc7\x42\x20\x30\x32\x43\x56',
     'setjmp de la CRT: cada retorno de una llamada directa es punto de reanudación'),
    ('resume', 6, '', rb'\x8b\x62\x10\x83\xc4\x04\xff\x62\x14',
     'longjmp: jmp [jmp_buf+14h] con ebp/esp del llamante de setjmp ya restaurados'),
    ('resume', 10, '', rb'\x33\xc0\x33\xdb\x33\xc9\x33\xd2\x33\xff\xff\xe6',
     '_EH4_TransferToHandler: jmp esi al bloque __except, ebp = marco dueño'),
    ('resume', 6, '', rb'\x8b\x6d\xfc\x8b\x63\xfc\xff\xe0',
     '_JumpToContinuation: jmp eax a la continuación del catch, ebp = marco dueño'),
    # HIPÓTESIS (ESTADO.md §SEH): el lifter solo envuelve en try/catch a las
    # funciones que escriben fs:[0] ellas mismas; en VS2010 lo hacen estos
    # ayudantes por su llamante. Declararlos `setjmp` hace reanudable a cada
    # llamante (y parte su cuerpo en el retorno de la llamada).
    ('setjmp', 0, '__SEH_prolog4',
     rb'\x68....\x64\xff\x35\x00\x00\x00\x00\x8b\x44\x24\x10\x89\x6c\x24\x10\x8d\x6c\x24\x10',
     'HIPOTESIS: prólogo SEH4 (registra el marco del llamante)'),
    ('setjmp', 0, '__EH_prolog3',
     rb'\x50\x64\xff\x35\x00\x00\x00\x00\x8d\x44\x24\x0c\x2b\x64\x24\x0c\x53\x56\x57\x89\x28\x8b\xe8',
     'HIPOTESIS: prólogo C++ EH3 (registra el marco del llamante)'),
]


def crt_lines(pe):
    text = pe.section('.text')
    base = pe.base + text[1]
    blob = pe.data[text[3]:text[3] + text[4]]
    out = []
    for kind, delta, name, pat, note in CRT:
        hits = [base + m.start() for m in re.finditer(pat, blob, re.S)]
        out.append('# %s (%d en este módulo)' % (note, len(hits)))
        for va in hits:
            out.append(('%s %x %s' % (kind, va + delta, name)).rstrip())
    return out


def count_calls(pe, targets):
    text = pe.section('.text')
    base = pe.base + text[1]
    blob = pe.data[text[3]:text[3] + text[4]]
    n = dict.fromkeys(targets, 0)
    for m in re.finditer(rb'\xe8(....)', blob, re.S):
        t = base + m.start() + 5 + int.from_bytes(m.group(1), 'little', signed=True)
        if t in n:
            n[t] += 1
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pe', required=True)
    ap.add_argument('--titulo', required=True)
    ap.add_argument('--ember')
    ap.add_argument('--s0')
    ap.add_argument('--eh')
    ap.add_argument('--funcs')
    a = ap.parse_args()
    pe = Pe(a.pe)
    L = ['# %s: manifest del lifter (generado por tools/armar_manifest.py; no editar a mano)' % a.titulo,
         '#',
         '# entry <va> [nombre]   función que Ghidra no encontró (destino indirecto)',
         '# setjmp <va> [nombre]  función tipo setjmp: sus llamantes son reanudables',
         '# resume <va>           call/jmp que abandona la cadena de llamadas (GuestResume)',
         '']
    L.append('## CRT de VS2010 (por patrón de bytes)')
    crt = crt_lines(pe)
    L += crt
    setjmp_vas = [int(x.split()[1], 16) for x in crt if not x.startswith('#') and x.startswith('setjmp')]
    calls = count_calls(pe, setjmp_vas)
    L.append('# llamadas directas a cada función setjmp: ' +
             ', '.join('%x=%d' % (v, calls[v]) for v in setjmp_vas))
    L.append('')
    known = set()
    if a.funcs:
        for r in csv.DictReader(open(a.funcs), delimiter='\t'):
            known.add(int(r['entry'], 16))
    if a.ember:
        L.append('## Ember: funciones con nombre (kind=func)')
        for r in sorted(csv.DictReader(open(a.ember), delimiter='\t'), key=lambda r: int(r['va'], 16)):
            if r['kind'] == 'func':
                L.append('entry %x %s' % (int(r['va'], 16), r['name']))
        L.append('')
    if a.s0:
        L.append('## S0: candidatos de confianza ALTA (punteros a .text fuera de toda función)')
        alta = sorted({int(r['destino'], 16) for r in csv.DictReader(open(a.s0), delimiter='\t')
                       if r['categoria'] == 'ALTA'})
        for va in alta:
            L.append('entry %x' % va)
        L.append('')
    if a.eh:
        extra = []
        for r in csv.DictReader((l for l in open(a.eh) if not l.startswith('#')), delimiter='\t'):
            if r['tipo'].startswith('prologo'):
                continue
            va = int(r['va'], 16)
            if va not in known:
                extra.append((va, r['tipo']))
        L.append('## Tablas de EH (tools/eh_entradas.py) que no son funciones de Ghidra: %d' % len(extra))
        L.append('# (fijan como entrada fuerte lo que el barrido de punteros ya encontraba)')
        for va, t in sorted(set(extra)):
            L.append('entry %x  # %s' % (va, t))
        L.append('')
    sys.stdout.write('\n'.join(L) + '\n')


if __name__ == '__main__':
    main()
