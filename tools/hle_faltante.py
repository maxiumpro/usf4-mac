#!/usr/bin/env python3
# hle_faltante.py: lo que falta de HLE, medido sin ejecutar nada.
#
# En x86recomp un import sin manejador NO es un error de enlace: al cargar el
# exe se ata a un thunk que detiene la corrida con su nombre la primera vez
# que se llama. Esta herramienta da esa lista de antemano: cruza los imports
# de cada módulo (exe y DLL liftadas) con los manejadores que registran las
# bibliotecas que el ejecutable enlaza de verdad (leídas del CMakeLists del
# toolkit) más el HLE del proyecto. También cruza los slots de Steam medidos
# (tools/steam_slots.py) con los "ISteamX0NN::#n" implementados.
#
#   hle_faltante.py --toolkit DIR --libs recomp_runtime,recomp_d3d9,...
#       --hle-extra src/ [--hle-extra OTRO_DIR] --modulo SSFIV.exe
#       --modulo cryptopp.dll --modulo curllib.dll [--steam-slots TSV]
# Aproximación: lee las macros HLE*/HLE_NAMED*/Registrar/RECOMP_WS2 por
# regex; no ve manejadores registrados en bucles o con macros nuevas.
import argparse
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
from pe_mini import Pe  # noqa: E402

STEAM_VERSIONS = {
    'ISteamUser': 'ISteamUser017', 'ISteamFriends': 'ISteamFriends014', 'ISteamUtils': 'ISteamUtils006',
    'ISteamUserStats': 'ISteamUserStats011', 'ISteamApps': 'ISteamApps006',
    'ISteamMatchmaking': 'ISteamMatchmaking009', 'ISteamNetworking': 'ISteamNetworking005',
    'ISteamRemoteStorage': 'ISteamRemoteStorage012', 'ISteamClient': 'ISteamClient012',
}


def lib_sources(toolkit, libs):
    """Archivos .cpp de cada biblioteca, según add_library/target_sources."""
    cm = open(os.path.join(toolkit, 'CMakeLists.txt'), encoding='utf8').read()
    files = set()
    for m in re.finditer(r'(add_library|target_sources)\(\s*(\w+)\s+(.*?)\)', cm, re.S):
        if m.group(2) not in libs:
            continue
        for tok in m.group(3).split():
            if tok.endswith('.cpp') and not tok.startswith('$'):
                files.add(os.path.join(toolkit, tok))
    return sorted(files)


def handlers(paths):
    reg = set()
    pat = re.compile(r'\bHLE(?:64|32)?\(\s*("([^"]+)"|kDll)\s*,\s*([A-Za-z0-9_]+)\s*,')
    patn = re.compile(r'\bHLE_NAMED[A-Z0-9_]*\(\s*("([^"]+)"|kDll)\s*,\s*"([^"]+)"')
    patr = re.compile(r'Registrar\s*\w*\s*[\{\(]\s*"([^"]+)"\s*,\s*"([^"]+)"')
    patw = re.compile(r'^RECOMP_WS2(?:_32)?\(\s*(\d+)\s*,\s*(\w+)\s*,', re.M)
    pata = re.compile(r'(?:RECOMP|USF4)_STEAM_ACCESSOR\((\w+),')
    for f in paths:
        t = open(f, errors='ignore').read()
        kd = re.search(r'kDll\s*=\s*"([^"]+)"', t)
        kd = kd.group(1).lower() if kd else ''
        for m in pat.finditer(t):
            reg.add(((m.group(2) or kd).lower(), m.group(3)))
        for m in patn.finditer(t):
            reg.add(((m.group(2) or kd).lower(), m.group(3).split('@')[0] if not m.group(3).startswith('_') else m.group(3)))
            reg.add(((m.group(2) or kd).lower(), m.group(3)))
        for m in patr.finditer(t):
            reg.add((m.group(1).lower(), m.group(2)))
        for m in patw.finditer(t):
            reg.add(('ws2_32.dll', m.group(2)))
            reg.add(('ws2_32.dll', '#' + m.group(1)))
        for m in pata.finditer(t):
            reg.add(('steam_api.dll', m.group(1)))
    return reg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--toolkit', required=True)
    ap.add_argument('--libs', required=True)
    ap.add_argument('--hle-extra', action='append', default=[])
    ap.add_argument('--modulo', action='append', required=True,
                    help='PE; el primero es el exe, los demás DLL liftadas')
    ap.add_argument('--steam-slots')
    a = ap.parse_args()
    libs = set(a.libs.split(','))
    paths = lib_sources(a.toolkit, libs)
    for d in a.hle_extra:
        for root, _, fs in os.walk(d):
            paths += [os.path.join(root, f) for f in fs if f.endswith('.cpp')]
    reg = handlers(paths)
    mods = [(os.path.basename(p).lower(), Pe(p)) for p in a.modulo]
    lifted = {n: pe for n, pe in mods[1:]}
    print('manejadores registrados (en lo que se enlaza): %d, en %d fuentes' % (len(reg), len(paths)))
    total = 0
    for name, pe in mods:
        imps = pe.imports() + [(d, n, v) for d, n, v in pe.delay_imports()]
        by = collections.defaultdict(lambda: [0, []])
        for dll, fn, _ in imps:
            d = dll.lower()
            by[d][0] += 1
            if d in lifted:
                # El runtime para al arrancar si la DLL liftada no lo exporta.
                if fn not in lifted[d].exports():
                    by[d][1].append(fn + '(no exportado)')
                continue
            if (d, fn) not in reg:
                by[d][1].append(fn)
        miss = sum(len(v[1]) for v in by.values())
        total += miss
        print('\n== %s: %d imports, %d sin HLE' % (name, len(imps), miss))
        for d, (n, m) in sorted(by.items(), key=lambda kv: -len(kv[1][1])):
            tag = 'liftada' if d in lifted else '%d/%d' % (n - len(m), n)
            print('  %-16s %-8s %s' % (d, tag, ' '.join(sorted(m)) if m else ''))
    if a.steam_slots:
        print('\n== steam_api.dll: slots de vtable medidos (steam_slots.py) sin manejador')
        n = 0
        for ln in open(a.steam_slots):
            if ln.startswith(('interfaz', '#')):
                continue
            iface, slot, sites = ln.split('\t')[:3]
            key = ('steam_api.dll', '%s::#%s' % (STEAM_VERSIONS.get(iface, iface), slot))
            if key not in reg:
                n += 1
                print('  %s (%s sitios)' % (key[1], sites))
        print('  total: %d' % n)
        total += n
    print('\nTOTAL sin HLE: %d' % total)


if __name__ == '__main__':
    main()
