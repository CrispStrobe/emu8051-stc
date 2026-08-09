# Coordination with ucsim-stc

## Harness ownership — SETTLED

**The differential harness lives in `ucsim-stc`.** Both `corpus_diff.sh`
and `examples_diff.sh` are there, calling `emu8051-stc`'s `emu_trace`
binary. This is one harness, one definition of "pass", one repo.

emu8051-stc does NOT maintain its own differential comparison scripts.
It provides `emu_trace` (with `-until-ns`, `-step-pcs`, `-adc`) and
the WASM module. The harness consumes these.

## PWM trace vocabulary — SETTLED

SFR events are the common vocabulary. Both models write mSFR[P1]
directly for PWM output. emu8051 also emits PIN events (extra).
Comparison uses SFR events only.

06-dimmer needs `-adc 2,512` for ADC-driven PWM comparison.

## Yield breakpoints — SETTLED

Both use code-address from symbol table `yields[].addr`.

## Cycle counts — SETTLED

Both match MCS-51 spec. ucsim's double-counting bug retracted.
Timing within 0.1%.

## Corpus metric — SETTLED

**220/349 strict** is the published number. The further 86 (54 prefix
+ 32 timing-only content divergences) are investigated and explained
(FINDINGS.md §10-§11) but are NOT folded into "pass." README fixed
in commit `cce7fb3`.

## Run-control ladder (§8) — READY

`emu_trace` now supports `-bp ADDR`, `-read SPACE,ADDR,LEN`, and
`-write SPACE,ADDR,VAL` for rungs 4-6:

- Rung 4: `emu_trace -bp ADDR` → HALT + REGS dump
- Rung 5: `emu_trace -bp YIELD_ADDR -read 1,bw_ms_addr,2` → yield + bw_ms
- Rung 6: `emu_trace -bp ADDR -write 1,ADDR,VAL` → write while halted + readback

The cross-emulator diff (`tests/run_control_diff.sh` in ucsim-stc)
can now run all rungs with `EMU_TRACE` pointed at our `emu_trace`.

## Status

Rungs 3+7 cross-emulator PASS. Rungs 4-6 emu8051 CLI ready.
9/9 example bundles. 220/349 corpus strict.
On-chip monitor: protocol verified (4 codecs), time-freeze measured.
