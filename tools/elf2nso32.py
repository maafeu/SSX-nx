#!/usr/bin/env python3
"""elf2nso32.py -- an NSO from a 32-bit ARM ELF (switch-tools' elf2nso takes
AArch64 only; the vita2hos toolchain image carried a 32-bit capable one).

    python tools/elf2nso32.py <in.elf> <out.nso> [--lz4]

The NSO: the 0x100-byte header, then the three PT_LOAD segments -- text (R-X),
rodata (R--), data (RW-) -- each with its SHA-256 in the header, the data
segment's .bss size, and the module id from .note.gnu.build-id. Segments are
stored uncompressed (valid for the loader, Atmosphere and the emulators), or
LZ4-compressed with --lz4 when the lz4 module is installed. Header layout as
switch-tools' elf2nso.c writes it. MIT.
"""
import hashlib
import struct
import sys

PT_LOAD = 1
PT_NOTE = 4


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    use_lz4 = '--lz4' in sys.argv
    src, dst = args[0], args[1]
    elf = open(src, 'rb').read()
    if elf[:4] != b'\x7fELF' or elf[4] != 1 or struct.unpack_from('<H', elf, 18)[0] != 40:
        sys.exit(f'{src}: not a 32-bit ARM ELF')
    e_phoff, = struct.unpack_from('<I', elf, 28)
    e_phentsize, e_phnum = struct.unpack_from('<HH', elf, 42)
    phdrs = []
    for i in range(e_phnum):
        p = struct.unpack_from('<8I', elf, e_phoff + i * e_phentsize)
        phdrs.append(dict(type=p[0], offset=p[1], vaddr=p[2], filesz=p[4], memsz=p[5], flags=p[6]))

    def seg(flags):
        for p in phdrs:
            if p['type'] == PT_LOAD and p['flags'] == flags:
                return p
        sys.exit(f'{src}: no PT_LOAD with flags {flags}')

    segs = [seg(5), seg(4), seg(6)]

    build_id = b''
    for p in phdrs:
        if p['type'] != PT_NOTE:
            continue
        o, end = p['offset'], p['offset'] + p['filesz']
        while o + 12 <= end:
            namesz, descsz, ntype = struct.unpack_from('<3I', elf, o)
            name = elf[o + 12:o + 12 + namesz]
            desc_o = o + 12 + ((namesz + 3) & ~3)
            if ntype == 3 and name[:3] == b'GNU':
                build_id = elf[desc_o:desc_o + descsz]
            o = desc_o + ((descsz + 3) & ~3)
    if not build_id:
        # no PT_NOTE (the runtime's dcr32.ld lists its own PHDRS): the section
        e_shoff, = struct.unpack_from('<I', elf, 32)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', elf, 46)
        shdrs = [struct.unpack_from('<10I', elf, e_shoff + i * e_shentsize) for i in range(e_shnum)]
        strtab = shdrs[e_shstrndx][4] if e_shstrndx < e_shnum else 0
        for sh in shdrs:
            nm = elf[strtab + sh[0]:elf.index(b'\0', strtab + sh[0])]
            if nm == b'.note.gnu.build-id':
                o = sh[4]
                namesz, descsz, _ = struct.unpack_from('<3I', elf, o)
                d = o + 12 + ((namesz + 3) & ~3)
                build_id = elf[d:d + descsz]
    build_id = (build_id + b'\0' * 0x20)[:0x20]

    lz4 = None
    if use_lz4:
        try:
            import lz4.block as lz4  # noqa
        except ImportError:
            print('elf2nso32: no lz4 module, writing uncompressed', file=sys.stderr)
            lz4 = None

    blobs, hashes, flags = [], [], 0x38  # hash checks for the three segments
    for i, p in enumerate(segs):
        data = elf[p['offset']:p['offset'] + p['filesz']]
        hashes.append(hashlib.sha256(data).digest())
        if lz4:
            data = lz4.compress(data, store_size=False)
            flags |= 1 << i
        blobs.append(data)

    hdr = bytearray(0x100)
    hdr[0:4] = b'NSO0'
    struct.pack_into('<III', hdr, 4, 0, 0, flags)
    off = 0x100
    for i, p in enumerate(segs):
        extra = (p['memsz'] - p['filesz']) if i == 2 else 1  # .bss size; else 1 (as elf2nso)
        struct.pack_into('<4I', hdr, 0x10 + i * 0x10, off, p['vaddr'], p['filesz'], extra)
        off += len(blobs[i])
    hdr[0x40:0x60] = build_id
    struct.pack_into('<3I', hdr, 0x60, *[len(b) for b in blobs])
    for i in range(3):
        hdr[0xA0 + i * 0x20:0xC0 + i * 0x20] = hashes[i]
    with open(dst, 'wb') as f:
        f.write(hdr)
        for b in blobs:
            f.write(b)


if __name__ == '__main__':
    main()
