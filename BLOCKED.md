# Where this repo stands — 2026-08-11

## SDCC WASM: demonstrated sdld placement bug (run 31464841382)

Same `.rel` files, same `.lk`, two sdld binaries:

    native sdld:  HOME  00000000  0000004C = 76. bytes (REL,CON,CODE)
    wasm   sdld:  HOME  00000001  0000004C = 76. bytes (REL,CON,CODE)

Identical input, identical arguments, different placement. This is a
bug in the Emscripten-compiled sdld binary, proven by observation.

## SDCC WASM byte-identity (the main open item)

**Status:** Four-stage pipeline works. Code generation matches native SDCC
4.5.0 (verified by content comparison under address shift). A 1-byte origin
shift remains: native image at 0000-00B0, WASM at 0001-00B1.

**One problem remains: +1 origin shift.**
WASM image at 0001-00B1, native at 0000-00B0.

**The six shifted-diff bytes are ALL relocated address operands
with delta=+1.** This is one bug, not six:

    native[0002]=0x4C  wasm[0003]=0x4D  (LJMP target, +1)
    native[004B]=0xA8  wasm[004C]=0xA9  (+1)
    native[0051]=0xAD  wasm[0052]=0xAE  (+1)
    native[0058]=0x49  wasm[0059]=0x4A  (+1)
    native[0064]=0xB1  wasm[0065]=0xB2  (+1)
    native[00A7]=0x49  wasm[00A8]=0x4A  (+1)

Code generation is identical. Fix the origin, all 6 vanish.

**The three injections (.optsdcc, O record, area declarations) are
irrelevant to the origin.** They did not change the offset on any
run. They compensate for WASM-build defects (confirmed: native 4.5.0
--c1mode DOES emit .optsdcc), but they are not the cause of the +1
shift and removing them would not change the image placement.

**RETRACTED (a2c68b3): injection 3 wrong SSEG flags was NOT the cause.**
Run 93d95ea reverted injections 2+3 entirely. Shift unchanged at +1.
The SSEG OVR/CON mechanism is real (OVR does not consume CODE space,
CON does) but it is not what produces the +1 here — the shift
predates the injection and persists without it.

**What has been eliminated:**
- .optsdcc injection (no effect on origin)
- O record injection (no effect on origin)
- Area declaration injection (no effect on origin — reverted, shift persists)
- Linker script segment bases (tried multiple times, no effect)
- Library set (lib/small vs small-stack-auto, no effect)
- Preprocessor defines (no effect)

**Eliminated by experiment (run 31463763972):**
- Missing `addr 0` on A records: appended it, shift unchanged. Not the cause.
- The `addr` field IS a real input difference (WASM sdas8051 omits it,
  native includes it) but sdld handles both formats identically.

**Build flags match (from build log):**
- sdas8051: `-DSDCDB -DNOICE -DINDEXLIB` — identical native vs WASM
- sdld: `-DINDEXLIB -DUNIX` — identical native vs WASM
- The `(Linux)` vs `(UNIX)` version string is in sdcc, not sdas/sdld.

**What remains after elimination:**
Every input we can compare matches (preprocessed code, flags, area
names/sizes/flags). The `.rel` differs in two ways: missing `O` record
and missing `addr` field — both confirmed NOT to cause the shift.
The only remaining variable is the **sdld binary itself**: same source,
same `-D` flags, native gcc vs Emscripten. If the binary differs in
behaviour, it is an Emscripten miscompilation — and that is now the
only hypothesis left standing, reached by elimination rather than
assertion.

**What is ruled out (each tested, none moved the offset):**
- Linker script segment bases (`-b HOME = 0x0000` matches native)
- Missing 4.5.0 preprocessor defines (added, no change)
- Library set (`lib/small` vs `lib/small-stack-auto`, both tried)
- Linker itself (both maps say HOME=0x0000 but WASM sdld places it at 0x0001)
- Record-boundary artefact (decoded address maps show real +1 shift)

**What the last run established:**
- `rel: differ` — divergence is upstream of the linker
- `asm: differ` — divergence is in codegen or earlier
- `i: differ` — preprocessed files differ (but `.i` carries `#line` paths)

**Run 31421530762 result:**
- `i-code: identical` — preprocessed code is byte-identical. Stage 1 exonerated.
- `flags: differ` — the cause is in our stage-2 argv, not in defines.

**Cause found (two argv bugs, fixed in fd91210):**
1. `-mmcs51 --model-small` not reaching `--c1mode` — native .rel has
   `O -mmcs51 --model-small`, ours did not. Different memory model.
2. Source at `/test.c` yields module `_test` (leading slash sanitised).
   Moved to `/work/test.c` so module name matches native's `test`.
Known benign: version comment says (UNIX) vs (Linux).

3. `.optsdcc` directive missing from WASM `--c1mode` output (2044aa9).
   Native sdcc 4.2.0 `--c1mode -mmcs51 --model-small` DOES emit
   `.optsdcc` — verified locally. This means emitting it is the
   **compiler's** job, not the driver's. The WASM 4.5.0 build omits
   it, which is a **defect in our WASM build**, and the injection is
   a **workaround**, not faithful driver reconstruction.
   Version-matched confirmation (run 31448759565): native 4.5.0
   `--c1mode -mmcs51 --model-small` DOES emit `.optsdcc`. The WASM
   omission is confirmed as a defect in our WASM-compiled sdcc.
   Additionally, WASM sdas8051 appears to ignore `.optsdcc` even when
   injected into the .asm — the O record is still missing from the
   .rel. This is a second WASM-build defect in the assembler.
   If byte-identity passes, the claim is "WASM SDCC with
   driver-equivalent pipeline produces identical firmware", not
   "WASM SDCC produces identical firmware unassisted."

## Open: WASM build may silently omit other directives

`.optsdcc` was caught only because it changed a byte we were comparing.
No audit has been done of what else the WASM `--c1mode` build omits.
A WASM build that silently drops one directive is not proven to emit
all the others.

## Cycle-count fix (done, downstream pending)

`6cb9bc7`: CLR/SETB/CPL/MOV-C bit took 2 MC, should be 1 MC (MCS-51 spec).
Found by diffing steveschnepp/emu8051 (MIT). Confirmed by ucsim-stc 8350048.
Post-fix corpus traces generated (318/347). Cross-emulator comparison pending.

## UART contract (done)

`docs/UART-ENTRY-POINTS.md` — SFR story, buffer ownership, idle-timeout
resync needs `-inject` flag (not a model limit). bw-board unblocked.

## PCA interrupt (done)

PCA uses CCAPMn.ECCF not IE.6. Datasheet Ch. 6 p. 138. Servo confirmed.

## Path sweep (done)

0 `/mnt/volume1` references remaining (grep verified). `../X` relative paths.

## The 169-byte figure is the shift, not the content divergence

Recorded at session end, after the weekly quota ran out — this was sent to the
session but never processed, so it is written here instead of being lost.

Two fixes in a row produced byte-identical failure output:

    9a1124a  module name + model flags   -> 169 bytes differ, 0000-00B0 vs 0001-00B1
    2044aa9  .optsdcc injection          -> 169 bytes differ, 0000-00B0 vs 0001-00B1

Identical count, identical offsets. The usual reading is that the fixes never
reached the build. The likelier reading here: **the metric is saturated.**

Both images are 172 bytes and the WASM one starts one byte later. Compare two
byte strings offset by one and nearly every position mismatches — 169 of 172,
the three matches being what chance gives you. So 169 measures the one-byte
shift, not content divergence, and it will read 169 however much content is
fixed, until the origin is fixed, at which point it drops to 0.

This is consistent with what this file already said above: *"Code generation
matches native SDCC 4.5.0 (verified by content comparison under address
shift)."* If that holds, the content fixes may have been landing correctly and
invisibly.

**Do this before fixing anything else** — the summary block cannot currently
show whether a fix worked:

    shifted:       identical | differ     (native[0..n] vs wasm[1..n+1])
    origin-delta:  +1

Then the block shows what is actually left: one address, not 169 bytes.

**Then the one question worth a run:** why does the image start at 0x0001?
Read the raw `.ihx` records from both sides and establish whether the WASM
image carries an extra leading byte in its first data record, or simply
declares a different load address. Those are different bugs — an emitted byte
too many, versus a placement directive. The decoded address maps say "real +1
shift", which points at the first; the record itself will say plainly.

## Spec-update scan (2026-08-11)

Adopting bw-parts a6f9240 convention. Scanned sibling repos' spec-updates/.

| Source | Number | Status |
|--------|--------|--------|
| ucsim-stc | 005 (cycle count bug) | Handled (6cb9bc7, retracted by ucsim) |
| ucsim-stc | 012 (emu_trace -part flag) | Already implemented in trace.c |
| bw-parts | 006 (stale gearmotor refs) | For bw-circuit-ui, not us |

Last scanned number: ucsim-stc/017, bw-parts/006, bw-board/vsource-current-limit.
