# Trace vocabulary — RESOLVED

## PWM pin changes

Both models write mSFR[P1] directly when PCA PWM toggles a pin.
This produces SFR events (`SFR 90 XX`) in both traces.

emu8051 additionally emits PIN events (`PIN 1.3 PP H/L`) via
emit_pin_changes. ucsim's trace.sh does not emit PIN events
(its C++ hook does, but the Python sampler doesn't).

**Differential comparison uses SFR events only.** Both traces
agree on SFR content for 06-dimmer when ADC inputs match:

```bash
./emu_trace -fosc 11059200 -until-ns 2000000 -adc 2,512 06-dimmer.hex
```

Verified: SFR events for P1 (0x90) are identical between both
models — F7/FF toggling at ~139µs (50% duty, FOSC/12 PCA).

## Status

All rungs pass, timing within 0.1%.
06-dimmer: SFR events IDENTICAL with matching ADC input.
