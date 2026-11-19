#!/usr/bin/env python3
"""bmp2png.py -- the runtime's captures (24/32-bit BMP) as PNGs, optionally
scaled down by an integer factor: python tools/bmp2png.py [--scale 2] a.bmp ...
No dependencies (zlib). MIT."""
import struct
import sys
import zlib


def convert(path, scale):
    b = open(path, 'rb').read()
    off, = struct.unpack_from('<I', b, 10)
    w, h = struct.unpack_from('<ii', b, 18)
    bpp, = struct.unpack_from('<H', b, 28)
    bottom_up = h > 0
    h = abs(h)
    stride = ((w * bpp // 8) + 3) & ~3
    px = bpp // 8
    rows = []
    for y in range(0, h, scale):
        sy = (h - 1 - y) if bottom_up else y
        row = b[off + sy * stride: off + sy * stride + w * px]
        out = bytearray()
        for x in range(0, w, scale):
            p = row[x * px:x * px + 3]
            out += bytes((p[2], p[1], p[0]))
        rows.append(b'\0' + bytes(out))
    ow, oh = (w + scale - 1) // scale, len(rows)

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', ow, oh, 8, 2, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b'')
    dst = path[:-4] + '.png'
    open(dst, 'wb').write(png)
    return dst


def main():
    args = sys.argv[1:]
    scale = 1
    if args and args[0] == '--scale':
        scale = int(args[1])
        args = args[2:]
    for a in args:
        print(convert(a, scale))


if __name__ == '__main__':
    main()
