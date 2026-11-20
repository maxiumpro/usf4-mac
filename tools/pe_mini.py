#!/usr/bin/env python3
# pe_mini.py: lector mínimo de PE32 (secciones, imports por IAT, exports),
# sin dependencias. Lo usan las herramientas de tools/ para leer el exe y las
# DLL del usuario; no escribe nada del juego.
import struct


class Pe:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        d = self.data
        e = struct.unpack_from('<I', d, 0x3C)[0]
        assert d[e:e + 4] == b'PE\0\0', path
        nsec, = struct.unpack_from('<H', d, e + 6)
        opt_size, = struct.unpack_from('<H', d, e + 20)
        opt = e + 24
        assert struct.unpack_from('<H', d, opt)[0] == 0x10B, 'solo PE32'
        self.base, = struct.unpack_from('<I', d, opt + 28)
        self.entry = struct.unpack_from('<I', d, opt + 16)[0] + self.base
        self.dirs = [struct.unpack_from('<II', d, opt + 96 + 8 * i) for i in range(16)]
        self.sections = []
        s = opt + opt_size
        for i in range(nsec):
            name = d[s:s + 8].rstrip(b'\0').decode('latin1')
            vsize, rva, rsize, raw = struct.unpack_from('<IIII', d, s + 8)
            chars, = struct.unpack_from('<I', d, s + 36)
            self.sections.append((name, rva, max(vsize, rsize), raw, rsize, chars))
            s += 40

    def off(self, rva):
        for _, srva, size, raw, rsize, _ in self.sections:
            if srva <= rva < srva + size:
                if rva - srva >= rsize:
                    return None
                return raw + rva - srva
        return None

    def u32(self, va):
        o = self.off(va - self.base)
        return None if o is None else struct.unpack_from('<I', self.data, o)[0]

    def u8(self, va):
        o = self.off(va - self.base)
        return None if o is None else self.data[o]

    def bytes_at(self, va, n):
        o = self.off(va - self.base)
        return b'' if o is None else self.data[o:o + n]

    def cstr(self, rva):
        o = self.off(rva)
        if o is None:
            return ''
        e = self.data.index(b'\0', o)
        return self.data[o:e].decode('latin1')

    def section(self, name):
        for sec in self.sections:
            if sec[0] == name:
                return sec
        return None

    def in_exec(self, va):
        rva = va - self.base
        for _, srva, size, _, _, chars in self.sections:
            if chars & 0x20000000 and srva <= rva < srva + size:
                return True
        return False

    def imports(self):
        """[(dll, name_or_#ord, iat_va)] del directorio de imports normal."""
        out = []
        rva, size = self.dirs[1]
        if not rva:
            return out
        o = self.off(rva)
        while True:
            oft, _, _, name_rva, ft = struct.unpack_from('<IIIII', self.data, o)
            if not name_rva:
                break
            dll = self.cstr(name_rva)
            thunks = oft or ft
            i = 0
            while True:
                t = struct.unpack_from('<I', self.data, self.off(thunks) + 4 * i)[0]
                if not t:
                    break
                if t & 0x80000000:
                    nm = '#%d' % (t & 0xFFFF)
                else:
                    nm = self.cstr(t + 2)
                out.append((dll, nm, self.base + ft + 4 * i))
                i += 1
            o += 20
        return out

    def delay_imports(self):
        """[(dll, name_or_#ord, iat_va)] del directorio de carga retardada."""
        out = []
        rva, size = self.dirs[13]
        if not rva:
            return out
        o = self.off(rva)
        while True:
            attrs, name, hmod, iat, int_, bound, unload, stamp = struct.unpack_from('<8I', self.data, o)
            if not name:
                break
            rel = 0 if attrs & 1 else self.base  # RVA (VC7+) o VA (VC6)
            dll = self.cstr(name - (0 if attrs & 1 else self.base))
            i = 0
            while True:
                t = struct.unpack_from('<I', self.data, self.off(int_ - (0 if attrs & 1 else self.base)) + 4 * i)[0]
                if not t:
                    break
                if t & 0x80000000:
                    nm = '#%d' % (t & 0xFFFF)
                else:
                    nm = self.cstr(t - (0 if attrs & 1 else self.base) + 2)
                out.append((dll, nm, (self.base if attrs & 1 else 0) + iat + 4 * i))
                i += 1
            o += 32
        return out

    def exports(self):
        """{nombre o '#ordinal'} del directorio de exports."""
        out = set()
        rva, size = self.dirs[0]
        if not rva:
            return out
        o = self.off(rva)
        base_ord, nfun, nnames, afun, anames, aords = struct.unpack_from('<6I', self.data, o + 16)
        for i in range(nfun):
            if struct.unpack_from('<I', self.data, self.off(afun) + 4 * i)[0]:
                out.add('#%d' % (base_ord + i))
        for i in range(nnames):
            nrva = struct.unpack_from('<I', self.data, self.off(anames) + 4 * i)[0]
            out.add(self.cstr(nrva))
        return out
