#!/usr/bin/env python3
"""Keil C51 → SDCC source translator for 8051 targets.

Handles the six constructs that block SDCC compilation of Keil-targeted
STC15/STC12 firmware:

  1. sfr NAME = 0xNN;          →  __sfr __at(0xNN) NAME;
  2. sbit NAME = SFR^N;        →  __sbit __at(base+N) NAME;
  3. bit (as type)              →  __bit
  4. code (memory qualifier)    →  __code
  5. interrupt N                →  __interrupt(N)
  6. #include <reg51.h>         →  #include <8051.h>
     #include <INTRINS.H>      →  #include <compiler.h>

Usage:
    python3 keil2sdcc.py INPUT.c [OUTPUT.c]
    python3 keil2sdcc.py --dir PROJECT_DIR --out OUTPUT_DIR

If OUTPUT is omitted, prints to stdout.  --dir mode translates all
.c and .h files in PROJECT_DIR into OUTPUT_DIR, preserving structure.
"""

import re
import sys
import os
import shutil
import argparse

# Known SFR base addresses for bit-addressable registers (8051 standard + STC15)
SFR_BASES = {
    'P0': 0x80, 'TCON': 0x88, 'P1': 0x90, 'SCON': 0x98,
    'P2': 0xA0, 'IE': 0xA8, 'P3': 0xB0, 'IP': 0xB8,
    'P4': 0xC0, 'P5': 0xC8, 'PSW': 0xD0, 'CCON': 0xD8,
    'ACC': 0xE0, 'P6': 0xE8, 'B': 0xF0, 'P7': 0xF8,
    # T2CON on classic 8051 is 0xC8 but STC15 uses that for P5
}

# Will be populated from sfr declarations in the source
local_sfr_map = {}


def translate_sfr(line):
    """sfr NAME = 0xNN;  →  __sfr __at(0xNN) NAME;"""
    m = re.match(r'^(\s*)sfr\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;', line)
    if m:
        indent, name, addr = m.group(1), m.group(2), m.group(3)
        addr_int = int(addr, 0)
        local_sfr_map[name] = addr_int
        return f'{indent}__sfr __at({addr}) {name};\n'
    return None


def translate_sbit(line):
    """sbit NAME = SFR^N;  →  __sbit __at(base+N) NAME;"""
    m = re.match(r'^(\s*)sbit\s+(\w+)\s*=\s*(\w+)\s*\^\s*(\d+)\s*;', line)
    if m:
        indent, name, sfr_name, bit_n = m.group(1), m.group(2), m.group(3), int(m.group(4))
        base = local_sfr_map.get(sfr_name, SFR_BASES.get(sfr_name))
        if base is not None:
            addr = base + bit_n
            return f'{indent}__sbit __at(0x{addr:02X}) {name};\n'
        else:
            return f'{indent}/* FIXME: unknown SFR base for {sfr_name} */ __sbit __at(0x00) {name};\n'
    return None


def translate_line(line):
    """Translate a single line of Keil C to SDCC C."""

    # 1. sfr declaration
    result = translate_sfr(line)
    if result:
        return result

    # 2. sbit declaration
    result = translate_sbit(line)
    if result:
        return result

    # 3. bit type → __bit
    # Only match 'bit' at start of declaration or after common type contexts,
    # not in comments or inside words. Skip lines that are comments.
    stripped = line.lstrip()
    if not stripped.startswith('//') and not stripped.startswith('/*') and not stripped.startswith('*'):
        # Match 'bit' as a type: at line start, after (, after comma, or after keywords
        # But not after alphanumeric (e.g. 'sbit', 'bit-addressable')
        line = re.sub(r'(?<![a-zA-Z0-9_-])\bbit\b(?=\s+\w)', '__bit', line)
        # Match (bit) cast → (__bit)
        line = re.sub(r'\(\s*bit\s*\)', '(__bit)', line)

    # 4. code memory qualifier → __code
    # Match 'code' as memory qualifier, not in strings or comments
    # Common patterns: "unsigned char code table[]" or "const code char"
    line = re.sub(r'\bcode\b(?=\s+\w)', '__code', line)

    # 4b. xdata memory qualifier → __xdata
    line = re.sub(r'\bxdata\b', '__xdata', line)

    # 4c. idata memory qualifier → __idata
    line = re.sub(r'\bidata\b', '__idata', line)

    # 4d. data memory qualifier → __data (only after type specifiers)
    # Careful: 'data' is very common in identifiers
    line = re.sub(r'(?<=\bchar\s)\bdata\b', '__data', line)
    line = re.sub(r'(?<=\bint\s)\bdata\b', '__data', line)
    line = re.sub(r'(?<=\blong\s)\bdata\b', '__data', line)

    # 4d. extern declarations with no return type → add int
    # e.g. "extern func(u8 x);" → "extern int func(u8 x);"
    m_extern = re.match(r'^(\s*extern\s+)(\w+\s*\()', line)
    if m_extern:
        # Check if the token after extern is a type or directly a function name
        after = m_extern.group(2).strip()
        # If it starts with a function name followed by '(', no type present
        if re.match(r'^[a-zA-Z_]\w*\s*\(', after):
            # Check it's not already a known type
            types = {'void', 'int', 'char', 'short', 'long', 'unsigned', 'signed',
                     'float', 'double', '__bit', 'u8', 'u16', 'u32', 'uint8',
                     'uint16', 'uint32', 'int8', 'int16', 'int32', 'uchar',
                     'uint', 'BYTE', 'WORD', 'DWORD', 'bit', '__bit'}
            first_word = re.match(r'^(\w+)', after).group(1)
            if first_word not in types:
                line = m_extern.group(1) + 'int ' + m_extern.group(2) + line[m_extern.end():]

    # 5. interrupt N → __interrupt(N)
    line = re.sub(r'\binterrupt\s+(\d+)', r'__interrupt(\1)', line)

    # 6. using N (register bank) → __using(N)
    line = re.sub(r'\busing\s+(\d+)', r'__using(\1)', line)

    # 7. System header replacements (both <> and "" forms)
    line = re.sub(r'#\s*include\s*[<"]\s*reg51\.h\s*[>"]', '#include <8051.h>', line, flags=re.IGNORECASE)
    line = re.sub(r'#\s*include\s*[<"]\s*reg52\.h\s*[>"]', '#include <8052.h>', line, flags=re.IGNORECASE)
    # intrins.h → stub that provides _nop_() as inline asm; do NOT use
    # compiler.h because it redefines NOP and conflicts with corpus headers
    line = re.sub(r'#\s*include\s*[<"]\s*intrins\.h\s*[>"]', '#include "intrins_sdcc.h"', line, flags=re.IGNORECASE)

    # 8. _nop_() → __asm__("nop")  (Keil intrinsic)
    line = re.sub(r'\b_nop_\s*\(\s*\)', '__asm__("nop")', line)

    return line


def translate_file(text):
    """Translate an entire file's contents."""
    global local_sfr_map
    local_sfr_map = dict(SFR_BASES)  # start with known bases

    # Two-pass: first collect all sfr declarations to build the map,
    # then translate everything
    for line in text.splitlines():
        m = re.match(r'^\s*sfr\s+(\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;', line)
        if m:
            local_sfr_map[m.group(1)] = int(m.group(2), 0)

    # Now translate
    result = []
    for line in text.splitlines(True):
        result.append(translate_line(line))
    return ''.join(result)


def translate_dir(src_dir, dst_dir):
    """Translate all .c and .h files from src_dir to dst_dir."""
    os.makedirs(dst_dir, exist_ok=True)
    translated = []
    for fname in os.listdir(src_dir):
        src = os.path.join(src_dir, fname)
        dst = os.path.join(dst_dir, fname)
        if os.path.isdir(src):
            continue
        if fname.lower().endswith(('.c', '.h')):
            with open(src, 'r', encoding='utf-8', errors='replace') as f:
                text = f.read()
            out = translate_file(text)
            with open(dst, 'w', encoding='utf-8') as f:
                f.write(out)
            translated.append(fname)
        else:
            # Copy non-C files as-is
            shutil.copy2(src, dst)
    return translated


def main():
    parser = argparse.ArgumentParser(description='Keil C51 → SDCC translator')
    parser.add_argument('input', nargs='?', help='Input .c or .h file')
    parser.add_argument('output', nargs='?', help='Output file (default: stdout)')
    parser.add_argument('--dir', help='Translate all files in directory')
    parser.add_argument('--out', help='Output directory (with --dir)')
    args = parser.parse_args()

    if args.dir:
        if not args.out:
            print('--out required with --dir', file=sys.stderr)
            sys.exit(1)
        files = translate_dir(args.dir, args.out)
        print(f'Translated {len(files)} files: {", ".join(files)}')
    elif args.input:
        with open(args.input, 'r', encoding='utf-8', errors='replace') as f:
            text = f.read()
        out = translate_file(text)
        if args.output:
            with open(args.output, 'w', encoding='utf-8') as f:
                f.write(out)
        else:
            print(out, end='')
    else:
        parser.print_help()
        sys.exit(1)


if __name__ == '__main__':
    main()
