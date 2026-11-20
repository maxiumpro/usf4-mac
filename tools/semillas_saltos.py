#!/usr/bin/env python3
# semillas_saltos.py: mete en el lift lo que W1 midio de los saltos
# indirectos (`jmp reg` y `jmp [mem]` del exe), sin tocar el codigo generado:
#
#   1. jumptables.tsv: une las tablas de Ghidra con los destinos medidos por
#      W1 (mismo formato `jmp<TAB>destinos`, base 0x400000). Un sitio que
#      esta en las dos fuentes recibe la union. El lifter emite entonces un
#      `switch` (destinos conocidos) y `BadJump` para cualquier otro: un
#      destino que W1 no vio es un error con nombre, no un despacho mudo.
#   2. manifest: cada destino medido que no es una funcion de Ghidra sale como
#      `entry` (los "candidatos a entry" de W1), con el sitio de origen en el
#      comentario.
#
# Los sitios que W1 no vio ejecutar (327) NO se tocan: siguen por despacho en
# tiempo de ejecucion y, si alguno llega a una direccion que no es funcion
# liftada, la corrida se detiene con su nombre.
#
#   semillas_saltos.py --w1 saltos_destinos.tsv --ghidra-jumptables J.tsv
#       --funcs functions.tsv --out-jumptables OUT.tsv --out-manifest OUT.inc
import argparse
import sys


def leer_tsv(path):
    """Filas de un TSV con encabezado; las lineas que empiezan con # son comentarios."""
    filas = []
    encabezado = True
    for line in open(path, encoding='utf8'):
        line = line.rstrip('\r\n')
        if line.startswith('#') or not line.strip():
            continue
        if encabezado:  # la primera linea que no es comentario
            encabezado = False
            continue
        filas.append(line.split('\t'))
    return filas


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--w1', required=True, action='append',
                    help='w1/saltos_destinos.tsv; se puede repetir (p. ej. tools/saltos_extra.tsv: destinos vistos al correr el port)')
    ap.add_argument('--ghidra-jumptables', required=True)
    ap.add_argument('--funcs', required=True, help='symbols/usf4/functions.tsv (entry en la primera columna)')
    ap.add_argument('--out-jumptables', required=True)
    ap.add_argument('--out-manifest', required=True)
    a = ap.parse_args()

    tablas = {}  # sitio -> lista ordenada de destinos (sin repetir)
    for c in leer_tsv(a.ghidra_jumptables):
        if len(c) < 2 or not c[1]:
            continue
        tablas[int(c[0], 16)] = [int(x, 16) for x in c[1].split(',')]
    n_ghidra = len(tablas)
    nuevos_sitios = mezclados = 0
    w1_destinos = {}  # destino -> primer sitio que lo uso
    for c in (c for ruta in a.w1 for c in leer_tsv(ruta)):
        if len(c) < 2 or not c[1]:
            continue
        sitio = int(c[0], 16)
        dest = [int(x, 16) for x in c[1].split(',')]
        for d in dest:
            w1_destinos.setdefault(d, sitio)
        if sitio in tablas:
            mezclados += 1
            actual = tablas[sitio]
            for d in dest:
                if d not in actual:
                    actual.append(d)
        else:
            nuevos_sitios += 1
            tablas[sitio] = list(dest)

    funcs = set()
    for c in leer_tsv(a.funcs):
        funcs.add(int(c[0], 16))
    sin_funcion = sorted(d for d in w1_destinos if d not in funcs)

    with open(a.out_jumptables, 'w', encoding='utf8', newline='\n') as f:
        f.write('jmp\ttargets\n')
        for sitio in sorted(tablas):
            f.write('%08X\t%s\n' % (sitio, ','.join('%08X' % d for d in tablas[sitio])))
    with open(a.out_manifest, 'w', encoding='utf8', newline='\n') as f:
        f.write('\n## Destinos de saltos indirectos medidos por W1 que Ghidra no tiene como funcion\n')
        f.write('# (tools/semillas_saltos.py; el sitio de origen va en el comentario)\n')
        for d in sin_funcion:
            f.write('entry %x  # destino de un salto en %08X\n' % (d, w1_destinos[d]))
    print('semillas_saltos: tablas de Ghidra %d, sitios nuevos de W1 %d, sitios con union %d, '
          'total %d; destinos de W1 %d, sin funcion de Ghidra %d (entry)' % (
              n_ghidra, nuevos_sitios, mezclados, len(tablas), len(w1_destinos), len(sin_funcion)))


if __name__ == '__main__':
    sys.exit(main())
