# Where this repo stands — 2026-08-10 session end

## SDCC WASM byte-identity (the main open item)

**Status:** Four-stage pipeline works. Code generation matches native SDCC
4.5.0 (verified by content comparison under address shift). A 1-byte origin
shift remains: native image at 0000-00B0, WASM at 0001-00B1.

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
   `.optsdcc` — verified locally. The WASM 4.5.0 build does not, for
   unknown reasons (possibly a WASM-specific omission). The injection
   is faithful driver reconstruction: it supplies what the compiler
   should emit but doesn't under WASM. If byte-identity passes, the
   claim is "WASM SDCC with driver-equivalent pipeline produces
   identical firmware", not "WASM SDCC produces identical firmware
   unassisted."

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
