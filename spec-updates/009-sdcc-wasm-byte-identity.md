# 009 — SDCC 4.5.0 WASM byte-identity (for VERIFICATION-LEDGER)

## Claim

Native SDCC 4.5.0 and the WASM four-stage pipeline (cc1 preprocess,
sdcc --c1mode codegen, sdas8051 assemble, sdld link) produce
**byte-identical** firmware for this source:

    #include <8051.h>
    void main(void) { P1 = 0xAA; while(1); }

Verified across 10 programs (172-356 bytes), all byte-identical,
same origin, no injected records. Programs exercise: timer ISR,
ADC polling, UART, switch/case jump table, unsigned long library
calls, code-memory arrays, function pointers, multi-interrupt,
xdata struct pointer arithmetic.

The `.optsdcc` directive is injected into the `.asm` (WASM sdcc
`--c1mode` omits it), but with `is_sdas()` true the assembler
processes it natively and produces identical `.rel` output.

## Category

**2** (same-source) — two builds of the same compiler (native gcc vs
Emscripten), same SDCC 4.5.0 source (pinned, SHA-256 verified), same
test input. The cross-check is the compiler against itself via a
different build route, which tests the pipeline and cross-compilation
rather than the compiler's correctness. Not category 1 (the two sides
are not independent). Not category 3 (two distinct binaries do
produce the same output). No silicon involved.

## What this does NOT cover

- 10 programs, 172-356 bytes each. Larger programs, different memory models
  (`--model-large`, `--stack-auto`), or programs that exercise
  more of the C runtime are untested.
- Only SDCC 4.5.0. A different release may diverge.
- Only mcs51 target. No other SDCC ports were built.
- The `.optsdcc` workaround means the WASM `sdcc --c1mode` binary
  has a known defect (omits the directive). The workaround is in
  the JS driver, not in the SDCC source.

## Defects found

**1. `thisProgram` unset under Emscripten MODULARIZE (ours, fixed).**
`sdas_init()` (`sdas.c:84`) checks `argv[0]` starts with `"sdas"`
to enable sdas-specific behaviour (addr field on A records, `.optsdcc`
processing). Under Emscripten MODULARIZE, `argv[0]` defaults to
`thisProgram` which was not set, so `is_sdas()` returned false and
all sdas-specific output was disabled. Fixed by setting
`thisProgram: name` in the Module config (`3d2b3b1`).

This single defect explains all three injections that were tried
during the investigation (.optsdcc into .asm, O record into .rel,
area declarations into .rel) — each was compensating for one
disabled sdas feature.

**2. sdld `newarea()` trusts `eval()` on an absent field (upstream,
latent).** `lkarea.c:156` calls `eval()` to read the `addr` field
from A records unconditionally for 8051 targets. When the field is
absent (from a non-sdas assembler), `eval()` reads past the line
end — undefined behaviour. On native x86-64 the UB yielded 0
(correct by accident). On WASM the UB yielded 1 (the +1 origin
shift). Demonstrated directly in run 31464841382: same `.rel` files,
same `.lk`, two sdld binaries, different HOME address. This bug
survives our fix — it is no longer reachable from our pipeline but
exists in sdld's source. Worth reporting upstream.

## Verification

- Run 31466174965: `origin-delta: 0`, `verdict: PASS`, `is_sdas()`
  indicators both present. Single program.
- Gate positive control (run 31466945986): corrupted one byte of the
  WASM `.ihx`, comparison returned exit 1 (FAIL). Gate verified.
- Suite (run 31516634687): 10/10 programs pass, 0 fail. Programs
  range from 172 to 356 bytes, exercising timer ISRs, ADC, UART,
  jump tables, long arithmetic, code arrays, function pointers,
  multi-interrupt, and xdata structs. Gate verified on same run.

## History

Five root causes were proposed during this investigation; four were
retracted after experiments contradicted them:

1. SSEG flags OVR vs CON (`a2c68b3`) — retracted: shift predated injection
2. sdld off-by-one on zero-size .ABS. area — retracted: native has same .ABS.
3. Missing `addr 0` on A records — retracted: experiment showed no effect
4. Missing area declarations — retracted: experiment showed no effect
5. `thisProgram` / `is_sdas()` (`3d2b3b1`) — **confirmed**

The surviving cause was reached by elimination (not assertion) and
confirmed by a direct demonstration with identical inputs to both
linker binaries.
