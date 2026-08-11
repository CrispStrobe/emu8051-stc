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
    print("shifted:       identical")
    print("origin-delta:  0")
    sys.exit(0)

# Compute origin delta and shifted comparison.
# Two images may have identical content but different start addresses.
native_min = min(native)
wasm_min = min(wasm)
origin_delta = wasm_min - native_min

# shifted: compare native[addr] against wasm[addr + delta] for all native addresses
shifted_diffs = 0
shifted_total = 0
for addr in sorted(native.keys()):
    shifted_addr = addr + origin_delta
    if shifted_addr in wasm:
        shifted_total += 1
        if native[addr] != wasm[shifted_addr]:
            shifted_diffs += 1

shifted_status = "identical" if shifted_diffs == 0 and shifted_total == len(native) else "differ (%d)" % shifted_diffs

print("shifted:       %s" % shifted_status)
print("origin-delta:  %+d" % origin_delta)

if origin_delta != 0 and shifted_diffs == 0 and shifted_total == len(native):
    print("CODE-IDENTICAL under +%d shift. Only the origin address differs." % origin_delta)
    sys.exit(1)  # still a failure — origin must match

print("MISMATCH: %d byte(s) differ at matching addresses:" % len(diffs))
for a, nb, wb in diffs[:10]:
    ns = "0x%02X" % nb if nb is not None else "absent"
    ws = "0x%02X" % wb if wb is not None else "absent"
    print("  %04X: native=%s  wasm=%s" % (a, ns, ws))
if len(diffs) > 10:
    print("  ... and %d more" % (len(diffs) - 10))
sys.exit(1)
