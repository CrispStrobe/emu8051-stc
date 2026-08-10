#!/usr/bin/env python3
"""Compare two Intel HEX files by decoded memory image, not text.

Intel HEX record boundaries are free to differ between builds while
encoding the same memory image. This script decodes both files into
address→byte maps and compares the maps.

Usage: python3 compare-ihx.py native.ihx wasm.ihx
Exit 0 = identical images, exit 1 = mismatch.
"""
import sys

def decode_ihx(path):
    """Parse Intel HEX into {address: byte} map."""
    mem = {}
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line.startswith(':'):
                continue
            length = int(line[1:3], 16)
            addr = int(line[3:7], 16)
            rtype = int(line[7:9], 16)
            if rtype != 0:  # only data records
                continue
            for i in range(length):
                mem[addr + i] = int(line[9 + i*2:11 + i*2], 16)
    return mem

if len(sys.argv) != 3:
    print("Usage: compare-ihx.py <native.ihx> <wasm.ihx>", file=sys.stderr)
    sys.exit(2)

native = decode_ihx(sys.argv[1])
wasm = decode_ihx(sys.argv[2])

all_addrs = sorted(set(native.keys()) | set(wasm.keys()))
diffs = []
for a in all_addrs:
    nb = native.get(a)
    wb = wasm.get(a)
    if nb != wb:
        diffs.append((a, nb, wb))

print("Native image: %d bytes at %04X-%04X" % (len(native), min(native), max(native)))
print("WASM image:   %d bytes at %04X-%04X" % (len(wasm), min(wasm), max(wasm)))

if not diffs:
    print("IMAGE-IDENTICAL: same bytes at every address.")
    print("Record layout differs but the firmware is the same.")
    sys.exit(0)
else:
    print("MISMATCH: %d byte(s) differ:" % len(diffs))
    for a, nb, wb in diffs[:20]:
        ns = "0x%02X" % nb if nb is not None else "absent"
        ws = "0x%02X" % wb if wb is not None else "absent"
        print("  %04X: native=%s  wasm=%s" % (a, ns, ws))
    if len(diffs) > 20:
        print("  ... and %d more" % (len(diffs) - 20))
    sys.exit(1)
