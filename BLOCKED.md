# Where this repo stands — 2026-08-10 session end

## SDCC WASM byte-identity (the main open item)

**Status:** Four-stage pipeline works. Code generation matches native SDCC
4.5.0 (verified by content comparison under address shift). A 1-byte origin
shift remains: native image at 0000-00B0, WASM at 0001-00B1.

**One problem remains: +1 origin shift.**
WASM image at 0001-00B1, native at 0000-00B0. Under the +1 shift,
166/172 bytes match and the 6 that differ are ALL relocated address
operands with delta=+1 (LJMP/LCALL targets). Code generation is
identical. Fixing the origin fixes all 6.

Root cause: two WASM-build defects, both workarounds:
1. WASM sdcc --c1mode does not emit `.optsdcc` (native 4.5.0 does).
   Workaround: inject `.optsdcc` into the .asm after codegen.
2. WASM sdas8051 ignores `.optsdcc` and does not emit the `O` record.
   Workaround: inject `O -mmcs51 --model-small` into the .rel.

**The claim, stated precisely:** "Native and WASM SDCC 4.5.0 produce
byte-identical firmware once we inject two records the WASM-compiled
tools fail to emit." This is not "produce identical firmware
unassisted." Both injections compensate for defects in how SDCC was
cross-compiled to WASM, not for missing driver logic.

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
