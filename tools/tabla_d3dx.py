#!/usr/bin/env python3
# tabla_d3dx.py: arma la tabla que lee el HLE de D3DXAssembleShader
# (GameConfig::shader_asm_dir) a partir de lo que W1 midio en Windows:
#
#   <origen>/<sha256 del texto>.bin   bytecode que devolvio el D3DX real
#   -> <destino>/d3dx_asm/<sha256>_00000001.bin
#
# El juego llama D3DXAssembleShader siempre con Flags = 1 (D3DXSHADER_DEBUG),
# sin defines ni include (W1: 41 llamadas, 39 textos); el HLE busca los
# bytecodes con Flags distintos de 0 como `<sha256>_<flags en 8 hex>.bin`, asi
# que un texto con otras flags, o uno que no esta, es fatal con su hash.
# Antes de copiar se comprueba que cada nombre sea de verdad el SHA-256 del
# texto exacto que el juego paso (los asmsrc_*.bin del censo, si se dan con
# --textos) y que cada bytecode sea una version de shader (token 0xFFFE/FFFF)
# terminada en END (0x0000FFFF). La tabla es derivada del juego: nunca va a git.
#
#   tabla_d3dx.py --origen w1/tabla_d3dx_juego --destino DIR [--textos w1/corridas/C1/censo/bin]
import argparse
import glob
import hashlib
import os
import struct
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--origen', required=True)
    ap.add_argument('--destino', required=True)
    ap.add_argument('--textos', help='carpeta con los asmsrc_*.bin del censo (los textos exactos)')
    a = ap.parse_args()

    archivos = sorted(glob.glob(os.path.join(a.origen, '*.bin')))
    if not archivos:
        print('tabla_d3dx: no hay .bin en %s' % a.origen, file=sys.stderr)
        return 1
    hashes = set()
    for f in archivos:
        nombre = os.path.basename(f)[:-4]
        if len(nombre) != 64 or any(ch not in '0123456789abcdef' for ch in nombre):
            print('tabla_d3dx: %s no se llama como un SHA-256' % f, file=sys.stderr)
            return 1
        d = open(f, 'rb').read()
        if len(d) < 8 or len(d) % 4:
            print('tabla_d3dx: %s no mide un multiplo de 4 bytes' % f, file=sys.stderr)
            return 1
        v = struct.unpack('<I', d[:4])[0]
        end = struct.unpack('<I', d[-4:])[0]
        if (v >> 16) not in (0xFFFE, 0xFFFF) or end != 0x0000FFFF:
            print('tabla_d3dx: %s no es bytecode de shader (version %08X, fin %08X)' % (f, v, end),
                  file=sys.stderr)
            return 1
        hashes.add(nombre)
    if a.textos:
        textos = {}
        for f in glob.glob(os.path.join(a.textos, 'asmsrc_*.bin')):
            textos[hashlib.sha256(open(f, 'rb').read()).hexdigest()] = f
        faltan = sorted(hashes - set(textos))
        sobran = sorted(set(textos) - hashes)
        if faltan or sobran:
            print('tabla_d3dx: la tabla y los textos del censo no coinciden: sin texto %s, sin bytecode %s' % (
                faltan[:3], sobran[:3]), file=sys.stderr)
            return 1
        print('tabla_d3dx: %d bytecodes, cada nombre es el SHA-256 de un texto del censo' % len(hashes))
    dest = os.path.join(a.destino, 'd3dx_asm')
    os.makedirs(dest, exist_ok=True)
    for f in archivos:
        nombre = os.path.basename(f)[:-4]
        with open(os.path.join(dest, nombre + '_00000001.bin'), 'wb') as o:
            o.write(open(f, 'rb').read())
    print('tabla_d3dx: %d archivos en %s' % (len(archivos), dest))
    return 0


if __name__ == '__main__':
    sys.exit(main())
