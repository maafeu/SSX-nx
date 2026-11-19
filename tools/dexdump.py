#!/usr/bin/env python3
"""dexdump.py -- list the classes, methods (with signatures and access flags)
and fields of an APK's classes.dex, for writing the port's JNI tables.

    python tools/dexdump.py <classes.dex> [prefix ...] > classes_dump.txt

Only classes whose name starts with one of the prefixes are listed (default:
all). Native methods (what libgame.so implements) are marked NATIVE; the rest
are the Java methods libgame.so may call back through JNI. MIT.
"""
import struct
import sys


def uleb(b, o):
    r = s = 0
    while True:
        c = b[o]
        o += 1
        r |= (c & 0x7F) << s
        s += 7
        if c < 0x80:
            return r, o


def main():
    path = sys.argv[1]
    prefixes = [p.replace('.', '/') for p in sys.argv[2:]]
    b = open(path, 'rb').read()
    (string_ids_size, string_ids_off, type_ids_size, type_ids_off,
     proto_ids_size, proto_ids_off, field_ids_size, field_ids_off,
     method_ids_size, method_ids_off, class_defs_size,
     class_defs_off) = struct.unpack_from('<12I', b, 0x38)

    def string(i):
        off = struct.unpack_from('<I', b, string_ids_off + 4 * i)[0]
        _, o = uleb(b, off)
        e = b.index(b'\0', o)
        return b[o:e].decode('utf-8', 'replace')

    def type_name(i):
        return string(struct.unpack_from('<I', b, type_ids_off + 4 * i)[0])

    def proto(i):
        shorty, ret, params = struct.unpack_from('<III', b, proto_ids_off + 12 * i)
        args = ''
        if params:
            n = struct.unpack_from('<I', b, params)[0]
            for k in range(n):
                args += type_name(struct.unpack_from('<H', b, params + 4 + 2 * k)[0])
        return '(' + args + ')' + type_name(ret)

    def method(i):
        cls, pr, name = struct.unpack_from('<HHI', b, method_ids_off + 8 * i)
        return type_name(cls), string(name), proto(pr)

    def field(i):
        cls, ty, name = struct.unpack_from('<HHI', b, field_ids_off + 8 * i)
        return type_name(cls), string(name), type_name(ty)

    for c in range(class_defs_size):
        (cidx, flags, superidx, ifaces, src, ann, data,
         statics) = struct.unpack_from('<8I', b, class_defs_off + 32 * c)
        name = type_name(cidx)[1:-1]
        if prefixes and not any(name.startswith(p) for p in prefixes):
            continue
        sup = type_name(superidx)[1:-1] if superidx != 0xFFFFFFFF else '-'
        print(f'class {name} : {sup}')
        if not data:
            continue
        o = data
        sf, o = uleb(b, o)
        inf, o = uleb(b, o)
        dm, o = uleb(b, o)
        vm, o = uleb(b, o)
        idx = 0
        for kind, n in (('static field', sf), ('field', inf)):
            idx = 0
            for _ in range(n):
                d, o = uleb(b, o)
                acc, o = uleb(b, o)
                idx += d
                _, fname, ftype = field(idx)
                print(f'  {kind} {fname} {ftype} flags=0x{acc:x}')
        for kind, n in (('direct', dm), ('virtual', vm)):
            idx = 0
            for _ in range(n):
                d, o = uleb(b, o)
                acc, o = uleb(b, o)
                _code, o = uleb(b, o)
                idx += d
                _, mname, sig = method(idx)
                tag = ' NATIVE' if acc & 0x100 else ''
                st = ' static' if acc & 0x8 else ''
                print(f'  {kind}{st} {mname}{sig}{tag}')


if __name__ == '__main__':
    main()
