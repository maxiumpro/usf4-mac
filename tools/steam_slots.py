#!/usr/bin/env python3
# steam_slots.py: qué métodos de las interfaces de Steam llama el exe, por
# slot de vtable. Lee el desensamblado de objdump (-M intel
# --no-show-raw-insn) de .text y sigue, en línea recta, el valor que
# devuelve cada accesor (SteamUser(), SteamRemoteStorage()...) hasta el
# `call [vtable+N]`. También sigue los globales donde el juego guarda el
# puntero de interfaz. Es heurístico (no ve todo): el HLE falla con nombre
# en cualquier slot no implementado, así que esto sirve para priorizar.
#
#   steam_slots.py SSFIV.exe ssfiv.dis > steam_slots.tsv
import collections
import re
import sys

sys.path.insert(0, __import__('os').path.dirname(__file__))
from pe_mini import Pe  # noqa: E402

ACCESSORS = {
    'SteamUser': 'ISteamUser', 'SteamFriends': 'ISteamFriends', 'SteamUtils': 'ISteamUtils',
    'SteamUserStats': 'ISteamUserStats', 'SteamApps': 'ISteamApps',
    'SteamMatchmaking': 'ISteamMatchmaking', 'SteamNetworking': 'ISteamNetworking',
    'SteamRemoteStorage': 'ISteamRemoteStorage', 'SteamClient': 'ISteamClient',
}
REGS = ['eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp']
LINE = re.compile(r'^\s*([0-9a-f]+):\s+(\S+)\s*(.*)$')


def main():
    pe = Pe(sys.argv[1])
    iat = {va: name for dll, name, va in pe.imports() if dll.lower() == 'steam_api.dll'}
    acc_iat = {va: ACCESSORS[n] for va, n in iat.items() if n in ACCESSORS}
    insns = []
    for ln in open(sys.argv[2], encoding='latin1'):
        m = LINE.match(ln)
        if m:
            insns.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))

    def ds_addr(op):
        m = re.search(r'ds:0x([0-9a-f]+)', op)
        return int(m.group(1), 16) if m else None

    # Globales donde se guarda el resultado de un accesor.
    globals_ = {}
    for i, (va, mn, op) in enumerate(insns):
        if mn == 'call' and op.startswith('DWORD PTR ds:') and ds_addr(op) in acc_iat:
            for va2, mn2, op2 in insns[i + 1:i + 6]:
                if mn2 == 'mov' and op2.startswith('DWORD PTR ds:') and op2.endswith(',eax'):
                    globals_[ds_addr(op2)] = acc_iat[ds_addr(op)]
                if mn2 in ('call', 'ret', 'jmp'):
                    break

    hits = collections.defaultdict(list)
    acc_reg = {}  # registro que guarda la entrada de la IAT de un accesor
    for i, (va, mn, op) in enumerate(insns):
        src = None
        regs_i = {}
        dst0 = op.split(',', 1)[0]
        if mn.startswith('ret') or mn == 'jmp':
            acc_reg = {}
        elif mn == 'mov' and dst0 in REGS and ',DWORD PTR ds:' in op and ds_addr(op) in acc_iat:
            acc_reg[dst0] = acc_iat[ds_addr(op)]
            continue
        elif mn != 'call' and mn != 'push' and dst0 in acc_reg:
            acc_reg.pop(dst0)
        if mn == 'call' and op in acc_reg:
            src = acc_reg[op]
            regs_i = {'eax': src}
        elif mn == 'call' and op.startswith('DWORD PTR ds:') and ds_addr(op) in acc_iat:
            src = acc_iat[ds_addr(op)]
            regs_i = {'eax': src}
        elif mn == 'mov' and ',DWORD PTR ds:' in op and ds_addr(op) in globals_:
            dst = op.split(',')[0]
            if dst in REGS:
                src = globals_[ds_addr(op)]
                regs_i = {dst: src}
        if not src:
            continue
        regs_v = {}
        regs_m = {}  # registro con el puntero a un método: (interfaz, slot)
        for va2, mn2, op2 in insns[i + 1:i + 60]:
            parts = op2.split(',', 1)
            dst = parts[0]
            rhs = parts[1] if len(parts) > 1 else ''
            if mn2 == 'call':
                m = re.match(r'DWORD PTR \[(e[a-z]{2})(?:\+0x([0-9a-f]+))?\]$', op2)
                if m and m.group(1) in regs_v:
                    slot = int(m.group(2) or '0', 16) // 4
                    hits[(regs_v[m.group(1)], slot)].append(va2)
                elif op2 in regs_m:
                    hits[regs_m[op2]].append(va2)
                for r in ('eax', 'ecx', 'edx'):
                    regs_i.pop(r, None)
                    regs_v.pop(r, None)
                    regs_m.pop(r, None)
                if not regs_i and not regs_v and not regs_m:
                    break
                continue
            if mn2 == 'mov' and dst in REGS:
                m = re.fullmatch(r'DWORD PTR \[(e[a-z]{2})(?:\+0x([0-9a-f]+))?\]', rhs)
                if m and m.group(1) in regs_v:
                    regs_m[dst] = (regs_v[m.group(1)], int(m.group(2) or '0', 16) // 4)
                    regs_i.pop(dst, None)
                    regs_v.pop(dst, None)
                    continue
                regs_m.pop(dst, None)
            if mn2 in ('ret', 'jmp') or mn2.startswith('ret'):
                break
            if dst in REGS:
                if mn2 == 'mov' and rhs in regs_i:
                    regs_i[dst] = regs_i[rhs]
                    regs_v.pop(dst, None)
                elif mn2 == 'mov' and rhs in regs_v:
                    regs_v[dst] = regs_v[rhs]
                    regs_i.pop(dst, None)
                elif mn2 == 'mov' and re.fullmatch(r'DWORD PTR \[(e[a-z]{2})\]', rhs) and \
                        rhs[11:14] in regs_i:
                    regs_v[dst] = regs_i[rhs[11:14]]
                    regs_i.pop(dst, None)
                elif mn2 not in ('push', 'cmp', 'test'):
                    regs_i.pop(dst, None)
                    regs_v.pop(dst, None)
            if mn2 == 'pop' and dst in REGS:
                regs_i.pop(dst, None)
                regs_v.pop(dst, None)
            if not regs_i and not regs_v and not regs_m:
                break

    print('interfaz\tslot\tsitios\tejemplos')
    for (iface, slot), sites in sorted(hits.items()):
        print('%s\t%d\t%d\t%s' % (iface, slot, len(sites), ','.join('%x' % s for s in sites[:6])))
    print('# globales con interfaz: %d; accesores en la IAT: %d' % (len(globals_), len(acc_iat)),
          file=sys.stderr)


if __name__ == '__main__':
    main()
