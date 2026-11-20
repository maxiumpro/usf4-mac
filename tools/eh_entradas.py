#!/usr/bin/env python3
# eh_entradas.py: puntos de entrada que el manejo de excepciones de la CRT de
# VS2010 alcanza solo por tabla (nunca por una llamada directa), leídos del
# exe del usuario:
#  - SEH (EH4): por cada `push <scopetable>; call __SEH_prolog4[_GS]` y cada
#    marco en línea `push -2; push <scopetable>; push __except_handler4`, los
#    filtros y los bloques __except/__finally de su tabla de ámbitos;
#  - C++ (FuncInfo 0x1993052x en .rdata): las acciones de desenrollado, los
#    catch (funclets) y las continuaciones que cada catch devuelve en EAX
#    (`mov eax, imm32 ... ret`).
# También busca los prólogos (`push __except_handler4; push fs:[0]` y
# `push eax; push fs:[0]; lea eax,[esp+0Ch]`) para encontrar variantes sin
# nombre de FID.
#
#   eh_entradas.py EXE --handler4 VA [--prolog VA,...] [--eh3 VA,...]
#       > salida TSV: tipo<TAB>va<TAB>origen
import argparse
import struct
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
from pe_mini import Pe  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('--handler4', required=True, help='VA de __except_handler4')
    ap.add_argument('--seh-prolog', default='', help='VAs de __SEH_prolog4 y _GS')
    args = ap.parse_args()
    pe = Pe(args.exe)
    h4 = int(args.handler4, 16)
    prologs = {int(x, 16) for x in args.seh_prolog.split(',') if x}
    d = pe.data
    out = []  # (tipo, va, origen)

    text = pe.section('.text')
    t_rva, t_size, t_raw = text[1], text[2], text[3]
    t_va = pe.base + t_rva
    t_bytes = d[t_raw:t_raw + t_size]

    def ex(va):
        return va and pe.in_exec(va)

    # --- prólogos: variantes por patrón ---------------------------------
    pat_h4 = b'\x68' + struct.pack('<I', h4) + b'\x64\xff\x35\x00\x00\x00\x00'
    pat_eh3 = b'\x50\x64\xff\x35\x00\x00\x00\x00\x8d\x44\x24\x0c'
    for name, pat in (('prologo_seh4', pat_h4), ('prologo_eh3', pat_eh3)):
        i = t_bytes.find(pat)
        while i >= 0:
            out.append((name, t_va + i, 'patron'))
            i = t_bytes.find(pat, i + 1)

    # --- SEH4: tablas de ámbitos ---------------------------------------
    tables = {}
    for i in range(len(t_bytes) - 10):
        if t_bytes[i] == 0xE8:
            tgt = t_va + i + 5 + struct.unpack_from('<i', t_bytes, i + 1)[0]
            if tgt in prologs and i >= 5 and t_bytes[i - 5] == 0x68:
                tables[struct.unpack_from('<I', t_bytes, i - 4)[0]] = t_va + i
    pat_inline = b'\x6a\xfe\x68'
    i = t_bytes.find(pat_inline)
    while i >= 0:
        if t_bytes[i + 7] == 0x68 and struct.unpack_from('<I', t_bytes, i + 8)[0] == h4:
            tables[struct.unpack_from('<I', t_bytes, i + 3)[0]] = t_va + i
        i = t_bytes.find(pat_inline, i + 1)
    nrec = 0
    for tbl, site in sorted(tables.items()):
        k = 0
        while k < 64:
            rec = tbl + 16 + 12 * k
            lvl, flt, hnd = (pe.u32(rec), pe.u32(rec + 4), pe.u32(rec + 8))
            if lvl is None or hnd is None:
                break
            slvl = lvl - (1 << 32) if lvl & 0x80000000 else lvl
            if not (slvl == -2 or 0 <= slvl < k) or not ex(hnd) or (flt and not ex(flt)):
                break
            if flt:
                out.append(('seh_filtro', flt, 'tabla %x (sitio %x)' % (tbl, site)))
                out.append(('seh_except', hnd, 'tabla %x (sitio %x)' % (tbl, site)))
            else:
                out.append(('seh_finally', hnd, 'tabla %x (sitio %x)' % (tbl, site)))
            k += 1
            nrec += 1

    # --- C++ EH: FuncInfo ------------------------------------------------
    rdata = pe.section('.rdata')
    r_rva, r_size, r_raw = rdata[1], rdata[2], rdata[3]
    r_va = pe.base + r_rva

    def in_rdata(va):
        return r_va <= va < r_va + r_size

    nfi = 0
    for o in range(0, rdata[4] - 36, 4):
        magic, maxs, punw, ntry, ptry, nip, pip = struct.unpack_from('<7I', d, r_raw + o)
        if magic not in (0x19930520, 0x19930521, 0x19930522):
            continue
        if maxs > 100000 or ntry > 10000 or (maxs and not in_rdata(punw)) or (ntry and not in_rdata(ptry)):
            continue
        fi = r_va + o
        nfi += 1
        for s in range(maxs):
            act = pe.u32(punw + 8 * s + 4)
            if act and ex(act):
                out.append(('cxx_unwind', act, 'funcinfo %x' % fi))
        for t in range(ntry):
            e = ptry + 20 * t
            ncatch, parr = pe.u32(e + 12), pe.u32(e + 16)
            if ncatch is None or ncatch > 1000 or not in_rdata(parr or 0):
                continue
            for c in range(ncatch):
                fn = pe.u32(parr + 16 * c + 12)
                if not ex(fn):
                    continue
                out.append(('cxx_catch', fn, 'funcinfo %x' % fi))
                body = pe.bytes_at(fn, 0x400)
                end = body.find(b'\xcc\xcc')
                body = body[:end if end > 0 else len(body)]
                j = body.find(b'\xb8')
                while 0 <= j <= len(body) - 6:
                    imm = struct.unpack_from('<I', body, j + 1)[0]
                    if body[j + 5] == 0xC3 and ex(imm):
                        out.append(('cxx_continuacion', imm, 'catch %x' % fn))
                    j = body.find(b'\xb8', j + 1)

    seen = set()
    print('tipo\tva\torigen')
    for kind, va, why in out:
        if (kind, va) in seen:
            continue
        seen.add((kind, va))
        print('%s\t%x\t%s' % (kind, va, why))
    print('# tablas seh4 %d, registros %d, funcinfo %d' % (len(tables), nrec, nfi), file=sys.stderr)


if __name__ == '__main__':
    main()
