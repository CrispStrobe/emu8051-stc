#!/usr/bin/env python3
"""Batch Keil→SDCC translate + compile for rainbowpeee corpus.

For each project: translate, provide SDCC STC15 header, compile, report.
Handles multi-file projects, subdirectories, encoding issues.
"""

import os
import sys
import subprocess
import shutil
import re

CORPUS = "/mnt/volume1/code/emu8051-stc/corpus/rainbowpeee-stc15"
TOOLS = "/mnt/volume1/code/emu8051-stc/tools"
EMU = "/mnt/volume1/code/emu8051-stc/emu_trace"
HEADER = os.path.join(TOOLS, "stc15f2k60s2_sdcc.h")
TRANSLATOR = os.path.join(TOOLS, "keil2sdcc.py")
OUTDIR = "/tmp/rainbowpeee-sdcc"

SKIP = {"空程序", "流水灯", "LICENSE", "README.md", "LOCK"}


def find_c_files(d):
    """Find all .c files, recursing into subdirectories."""
    result = []
    for root, dirs, files in os.walk(d):
        for f in files:
            if f.lower().endswith('.c'):
                result.append(os.path.join(root, f))
    return result


def find_h_files(d):
    """Find all .h files, recursing into subdirectories."""
    result = []
    for root, dirs, files in os.walk(d):
        for f in files:
            if f.lower().endswith('.h'):
                result.append(os.path.join(root, f))
    return result


def translate_file(src, dst):
    """Run keil2sdcc.py on a single file."""
    r = subprocess.run(
        [sys.executable, TRANSLATOR, src, dst],
        capture_output=True, text=True, timeout=10
    )
    return r.returncode == 0


INTRINS_STUB = os.path.join(TOOLS, "intrins_sdcc.h")

def provide_headers(builddir):
    """Copy the master SDCC STC15 header under all expected names."""
    for name in [
        "stc15f2k60s2.h", "STC15F2K60S2.H", "STC15F2K60S2.h",
        "stc15fxxxx.h", "stc15f2k60s2_sdcc.h"
    ]:
        dst = os.path.join(builddir, name)
        if not os.path.exists(dst):
            shutil.copy(HEADER, dst)
    # intrins stub
    dst = os.path.join(builddir, "intrins_sdcc.h")
    if not os.path.exists(dst):
        shutil.copy(INTRINS_STUB, dst)

    # Fix case-sensitive header references: create symlinks for ALL
    # case variants (lower, upper, capitalized, and any include
    # patterns found in .c files)
    existing = set(os.listdir(builddir))
    headers = [f for f in existing if f.lower().endswith(('.h',))]
    for f in headers:
        variants = {
            f.lower(), f.upper(),
            f[0].upper() + f[1:],        # Ds1302.h
            f[0].upper() + f[1:].lower(), # Ds1302.h (same)
            # Full upper stem with .h extension
            os.path.splitext(f)[0].upper() + '.h',  # DS1302.h
            os.path.splitext(f)[0].upper() + '.H',  # DS1302.H
            os.path.splitext(f)[0].lower() + '.h',  # ds1302.h
        }
        for v in variants:
            if v != f and v not in existing:
                link = os.path.join(builddir, v)
                if not os.path.exists(link):
                    try:
                        os.symlink(f, link)
                        existing.add(v)
                    except OSError:
                        pass

    # Also scan all .c files for #include "xxx.h" and create any
    # missing case variants
    for cf in os.listdir(builddir):
        if not cf.lower().endswith('.c'):
            continue
        try:
            with open(os.path.join(builddir, cf), 'r', errors='replace') as fh:
                for line in fh:
                    m = re.search(r'#\s*include\s*"([^"]+)"', line)
                    if m:
                        inc = m.group(1)
                        if inc not in existing:
                            # Try to find a case-insensitive match
                            for h in headers:
                                if h.lower() == inc.lower():
                                    link = os.path.join(builddir, inc)
                                    if not os.path.exists(link):
                                        try:
                                            os.symlink(h, link)
                                            existing.add(inc)
                                        except OSError:
                                            pass
                                    break
        except Exception:
            pass


def apply_source_patches(name, builddir, c_files):
    """Apply project-specific source patches for known bugs.

    These are genuine source bugs in the original Keil code, not
    translator issues. Each patch is documented with the root cause.
    """
    import re as _re

    def patch_file(fname, old, new):
        path = os.path.join(builddir, fname)
        if os.path.exists(path):
            with open(path, 'r', errors='replace') as f:
                text = f.read()
            if old in text:
                text = text.replace(old, new, 1)
                with open(path, 'w') as f:
                    f.write(text)

    def patch_file_re(fname, pattern, repl):
        path = os.path.join(builddir, fname)
        if os.path.exists(path):
            with open(path, 'r', errors='replace') as f:
                text = f.read()
            text = _re.sub(pattern, repl, text)
            with open(path, 'w') as f:
                f.write(text)

    # 温度: ex6.c has display_num definition; display.c should have extern
    if '温度' in name and '温度计' not in name:
        path = os.path.join(builddir, 'display.c')
        if os.path.exists(path):
            with open(path, 'r', errors='replace') as f:
                text = f.read()
            # Don't change extern→definition; leave it as extern
            # (ex6.c has the real definition with initializer)
            if 'extern' in text and 'display_num' in text:
                pass  # already extern, good
            elif 'unsigned char display_num' in text:
                text = text.replace(
                    'unsigned char display_num',
                    'extern unsigned char display_num')
                with open(path, 'w') as f:
                    f.write(text)
        # Also need extern unsigned int num
        path = os.path.join(builddir, 'display.c')
        if os.path.exists(path):
            with open(path, 'r', errors='replace') as f:
                text = f.read()
            if 'extern unsigned int num' in text:
                pass  # already extern, keep it
            with open(path, 'w') as f:
                f.write(text)

    # 温度: ex6.c missing variable declarations (temperature, time0_num)
    if '温度' in name and '温度计' not in name:
        patch_file('ex6.c',
                   '\twhile(1)',
                   '\tunsigned int temperature;\n\twhile(1)')
        # time0_num used in ISR but never declared
        path = os.path.join(builddir, 'ex6.c')
        if os.path.exists(path):
            with open(path, 'r', errors='replace') as f:
                text = f.read()
            if 'time0_num' in text and 'unsigned int time0_num' not in text:
                # Add global declaration at top
                text = 'static unsigned int time0_num;\n' + text
                with open(path, 'w') as f:
                    f.write(text)

    # 红外人体感应灯: key.h declares Key_Scan as static (breaks linkage)
    if '红外人体感应灯' in name:
        patch_file('key.h', 'static unsigned char Key_Scan',
                   'unsigned char Key_Scan')

    # LED汉字显示/03: exclude 1.c (scratch file with duplicate definitions)
    if '03-几个汉字滚动显示' in name:
        bad = os.path.join(builddir, '1.c')
        if os.path.exists(bad):
            os.rename(bad, bad + '.excluded')
        # Also remove from c_files list
        c_files[:] = [f for f in c_files if os.path.basename(f) != '1.c']

    # 按键数码管测试 and 数码管按键测试/按键测试: key_value used before declaration
    if '按键数码管' in name or ('数码管按键' in name and '按键' in name):
        patch_file('main.c',
                   'while(1)',
                   'unsigned char key_value;\n\twhile(1)')

    # 11、PCF8591: includes stc15f104.h which doesn't exist; should be stc15fxxxx.h
    if 'PCF8591' in name:
        patch_file_re('main.c',
                      r'#\s*include\s*"stc15f104\.h"',
                      '#include "stc15fxxxx.h"')

    # Projects with display_num extern but never defined
    for proj in ('开发板点阵测试', '测试按键', '点阵测试 - 1616',
                 '1302数码管显示', '数码管测试'):
        if proj in name:
            path = os.path.join(builddir, 'display.c')
            if os.path.exists(path):
                with open(path, 'r', errors='replace') as f:
                    text = f.read()
                if 'extern' in text and 'display_num' in text:
                    text = text.replace(
                        'extern unsigned char display_num[]',
                        'unsigned char display_num[4]', 1)
                if 'extern unsigned int num' in text:
                    text = text.replace(
                        'extern unsigned int num',
                        'unsigned int num', 1)
                with open(path, 'w') as f:
                    f.write(text)
            break

    # 按键数码管测试/数码管按键测试: dis_num defined in display.c with initializer,
    # main.c also defines it → make main.c's version extern
    if '按键数码管' in name or ('数码管按键' in name and '按键' in name):
        patch_file('main.c', 'unsigned char dis_num[]',
                   'extern unsigned char dis_num[]')
        # display.c has 'extern unsigned char dis_num[]={...}' — remove extern
        patch_file('display.c', 'extern unsigned char dis_num[]',
                   'unsigned char dis_num[]')
        path = os.path.join(builddir, 'main.c')
        if os.path.exists(path):
            with open(path, 'r', errors='replace') as f:
                text = f.read()
            text = text.replace(
                '  dis_num[0]=0;\n  unsigned char key_value;',
                '  unsigned char key_value;\n  dis_num[0]=0;')
            with open(path, 'w') as f:
                f.write(text)

    # General: if function.c is a stub (< 50 bytes), add missing definitions
    func_path = os.path.join(builddir, 'function.c')
    if os.path.exists(func_path):
        with open(func_path, 'r', errors='replace') as f:
            text = f.read()
        if len(text.strip()) < 80 and 'void' not in text:
            # It's a stub — add delay() and num definitions
            additions = '\n'
            # Check if delay/num is needed AND not already defined elsewhere
            needs_delay = False
            has_delay_def = False
            needs_num = False
            for cf in os.listdir(builddir):
                if cf.lower().endswith('.c') and cf != 'function.c':
                    with open(os.path.join(builddir, cf), 'r', errors='replace') as fh:
                        ct = fh.read()
                    if 'delay()' in ct or 'delay (' in ct:
                        needs_delay = True
                    if 'void delay(' in ct:
                        has_delay_def = True
                    if _re.search(r'\bnum\b', ct) and 'extern' in ct:
                        needs_num = True
            if needs_delay and not has_delay_def:
                additions += 'void delay(void) { unsigned char i,j; for(i=0;i<200;i++) for(j=0;j<200;j++); }\n'
            if needs_num:
                additions += 'unsigned int num;\n'
            text += additions
            with open(func_path, 'w') as f:
                f.write(text)

    # 数码管测试: two delay() functions with different signatures (source bug)
    if name == '数码管测试':
        # ex6.c has void delay(void), ds1302.c has void delay(u8 i)
        # Rename ex6.c's version to delay_display
        path = os.path.join(builddir, 'ex6.c')
        if os.path.exists(path):
            with open(path, 'r', errors='replace') as f:
                text = f.read()
            text = text.replace('void delay(void)', 'void delay_display(void)')
            text = text.replace('delay();', 'delay_display();')
            with open(path, 'w') as f:
                f.write(text)
        # display.c also calls delay() — should call delay_display()
        path = os.path.join(builddir, 'display.c')
        if os.path.exists(path):
            with open(path, 'r', errors='replace') as f:
                text = f.read()
            text = text.replace('delay();', 'delay_display();')
            with open(path, 'w') as f:
                f.write(text)

    # 按键数码管测试: display.h may define dis_num without size
    if '按键数码管' in name or ('数码管按键' in name and '按键' in name):
        for hf in ('display.h', 'Display.h'):
            hp = os.path.join(builddir, hf)
            if os.path.exists(hp):
                with open(hp, 'r', errors='replace') as f:
                    ht = f.read()
                if 'unsigned char dis_num[]' in ht:
                    ht = ht.replace('unsigned char dis_num[]',
                                    'extern unsigned char dis_num[4]')
                    with open(hp, 'w') as f:
                        f.write(ht)

    # LED汉字显示/01一个汉字显示: missing sbit defs, typos, undeclared vars
    if '01一个汉字显示' in name:
        path = os.path.join(builddir, 'led.c')
        if os.path.exists(path):
            with open(path, 'r', errors='replace') as f:
                text = f.read()
            if 'DS' in text and '__sbit' not in text:
                text = text.replace(
                    '#include <8051.h>',
                    '#include <8051.h>\n'
                    '__sbit __at(0x91) SHCP;\n'
                    '__sbit __at(0x92) STCP;\n'
                    '__sbit __at(0x93) DS;\n')
            # Fix typo: SHCP=I → SHCP=1 (capital I instead of 1)
            text = text.replace('SHCP=I;', 'SHCP=1;')
            # Fix: undeclared variable x → add int x declaration
            text = text.replace('void diaplay(', 'void display(')
            if 'for(x=' in text and 'int x' not in text:
                text = text.replace('void display(unsigned char *p)\n{',
                                    'void display(unsigned char *p)\n{\n    int x;')
            with open(path, 'w') as f:
                f.write(text)

    if '01一个汉字显示' in name:
        patch_file('main.c', 'diaplay', 'display')
        path = os.path.join(builddir, 'main.c')
        if os.path.exists(path):
            with open(path, 'r', errors='replace') as f:
                text = f.read()
            # Fix: display(table0)//comment → display(table0);//comment
            text = _re.sub(r'display\(table0\)\s*//', 'display(table0);//', text)
            # Fix: array closing }(newline) without semicolon → };
            text = _re.sub(r'(0x[0-9A-Fa-f]+)\s*}\s*\n', r'\1};\n', text)
            # Add prototype if missing
            if 'void display(' not in text and 'extern' not in text:
                text = text.replace('#include', 'extern void display(unsigned char *p);\n#include', 1)
            with open(path, 'w') as f:
                f.write(text)


def compile_project(name, c_files, builddir):
    """Compile .c files individually then link. Returns (ok, hex_bytes, error)."""
    rel_files = []
    for cfile in c_files:
        base = os.path.splitext(os.path.basename(cfile))[0]
        translated = os.path.join(builddir, os.path.basename(cfile))
        if not os.path.exists(translated):
            translated = cfile  # fallback

        rel = os.path.join(builddir, base + ".rel")
        r = subprocess.run(
            ["sdcc", "-mmcs51", "--model-small", f"-I{builddir}", "-c",
             translated, "-o", rel],
            capture_output=True, text=True, timeout=30
        )
        if r.returncode != 0:
            # Extract first error line
            err = ""
            for line in (r.stdout + r.stderr).splitlines():
                if "error" in line.lower():
                    err = line.strip()[:120]
                    break
            if not err:
                err = (r.stderr or r.stdout)[:120].strip()
            return False, 0, f"cc-fail ({base}): {err}"
        rel_files.append(rel)

    # Link
    ihx = os.path.join(builddir, "out.ihx")
    r = subprocess.run(
        ["sdcc", "-mmcs51", "--model-small"] + rel_files + ["-o", ihx],
        capture_output=True, text=True, timeout=30
    )
    if r.returncode != 0:
        err = ""
        for line in (r.stdout + r.stderr).splitlines():
            if "error" in line.lower() or "Warning" in line:
                err = line.strip()[:120]
                break
        if not err:
            err = (r.stderr or r.stdout)[:120].strip()
        return False, 0, f"link-fail: {err}"

    # Count hex bytes
    hexbytes = 0
    with open(ihx) as f:
        for line in f:
            line = line.strip()
            if line.startswith(':') and line[7:9] == '00':
                hexbytes += int(line[1:3], 16)

    return True, hexbytes, ihx


def run_emulator(ihx):
    """Run through emu_trace, return PIN count."""
    try:
        r = subprocess.run(
            [EMU, "-fosc", "11059200", "-part", "stc15",
             "-until-ns", "2000000000", ihx],
            capture_output=True, text=True, timeout=60
        )
        pins = sum(1 for line in r.stdout.splitlines() if "\tPIN\t" in line)
        return pins
    except subprocess.TimeoutExpired:
        return -1


def main():
    os.makedirs(OUTDIR, exist_ok=True)

    results = []
    projects = sorted(os.listdir(CORPUS))

    print("| Project | Compile | Hex bytes | Pins (2s) | Verdict |")
    print("|---------|---------|-----------|-----------|---------|")

    for pname in projects:
        pdir = os.path.join(CORPUS, pname)
        if not os.path.isdir(pdir) or pname in SKIP:
            continue

        # Handle projects with subdirectories (e.g. LED汉字显示/01.../LED/)
        # If project has subdirs with .c files, process each subdir separately
        subdirs = []
        direct_c = find_c_files(pdir)

        # Check if .c files are at top level or in subdirs
        top_c = [f for f in direct_c if os.path.dirname(f) == pdir]
        if top_c:
            subdirs.append((pname, pdir, top_c))
        else:
            # Group by immediate subdirectory
            sub_groups = {}
            for f in direct_c:
                rel = os.path.relpath(f, pdir)
                parts = rel.split(os.sep)
                if len(parts) > 1:
                    subname = parts[0]
                    if subname not in sub_groups:
                        sub_groups[subname] = []
                    sub_groups[subname].append(f)
                else:
                    sub_groups["."] = sub_groups.get(".", []) + [f]

            for subname, files in sorted(sub_groups.items()):
                full_name = f"{pname}/{subname}" if subname != "." else pname
                subpath = os.path.join(pdir, subname) if subname != "." else pdir
                subdirs.append((full_name, subpath, files))

        for full_name, srcdir, c_files in subdirs:
            builddir = os.path.join(OUTDIR, full_name.replace("/", "_"))
            os.makedirs(builddir, exist_ok=True)

            # Translate all .c and .h files from the source directory
            h_files = find_h_files(srcdir)
            # Also grab .h from parent if subdir
            if srcdir != pdir:
                h_files += find_h_files(pdir)

            for f in c_files + h_files:
                dst = os.path.join(builddir, os.path.basename(f))
                translate_file(f, dst)

            provide_headers(builddir)
            apply_source_patches(full_name, builddir, c_files)

            ok, hexbytes, result = compile_project(full_name, c_files, builddir)
            if ok:
                pins = run_emulator(result)
                verdict = "boots" if pins >= 0 else "timeout"
                if pins == 0:
                    verdict = "boots (no pins)"
                print(f"| {full_name} | OK | {hexbytes} | {pins} | {verdict} |")
                results.append((full_name, "OK", hexbytes, pins, verdict))
            else:
                print(f"| {full_name} | FAIL | - | - | {result[:80]} |")
                results.append((full_name, "FAIL", 0, 0, result[:80]))

    # Summary
    total = len(results)
    compiled = sum(1 for r in results if r[1] == "OK")
    booted = sum(1 for r in results if r[4].startswith("boots"))
    failed = total - compiled
    print()
    print(f"Total: {total} | Compiled: {compiled} | Booted: {booted} | Still failing: {failed}")


if __name__ == "__main__":
    main()
