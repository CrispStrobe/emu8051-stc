#!/usr/bin/env python3
"""Fail if a .wasm still carries DWARF custom sections.

SDCC's own build puts -ggdb ahead of whatever configure was given, so debug
info returns quietly whenever a make-time CFLAGS override is dropped. It cost
sdcc.wasm 11 MB against a 1.2 MB code section once, and that ships to browsers.
This is the check that makes it loud.

Usage: python3 assert-stripped.py <file.wasm> ...
Exit 0 = clean, exit 1 = debug sections present (or the file is not a wasm).
"""
import sys


def leb(data, i):
    result = shift = 0
    while True:
        byte = data[i]
        i += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, i
        shift += 7


def debug_sections(path):
    data = open(path, 'rb').read()
    if data[:4] != b'\x00asm':
        raise ValueError(f'{path} is not a WebAssembly module')
    found = []
    i = 8
    while i < len(data):
        section_id = data[i]
        i += 1
        size, i = leb(data, i)
        if section_id == 0:
            length, j = leb(data, i)
            name = data[j:j + length].decode('utf8', 'replace')
            if name.startswith('.debug_'):
                found.append((name, size))
        i += size
    return found


if __name__ == '__main__':
    bad = False
    for path in sys.argv[1:]:
        sections = debug_sections(path)
        if sections:
            bad = True
            total = sum(size for _, size in sections)
            print(f'{path}: {len(sections)} debug sections, {total} bytes')
            for name, size in sections:
                print(f'    {name}  {size}')
    sys.exit(1 if bad else 0)
