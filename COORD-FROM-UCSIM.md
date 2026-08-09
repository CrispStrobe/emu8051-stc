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

## Status

All rungs pass. 9/9 example bundles. 220/349 corpus strict.
