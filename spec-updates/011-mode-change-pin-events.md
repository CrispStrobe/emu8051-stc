# Spec update 011: PxM0/PxM1 writes are pin-level events

**Date:** 2026-08-18
**Scope:** STC12-PERIPHERAL-MODEL.md §3, §7

## Question

When firmware writes PxM0 or PxM1 (changing a pin's mode without changing
its latch value), should the emulator fire a boundary-A `setPin` callback?

## Ruling: YES — mode changes are pin events

A PxM0/PxM1 write that changes any pin's mode fires `on_pin_change` with:
- The **new mode** (quasi/pushpull/input/opendrain)
- The **current latch value** (unchanged)

### Rationale

1. **The board's Thévenin model depends on mode.** A quasi-bidirectional pin
   sourcing a `1` through a 20 kΩ pull-up (230 µA) produces a completely
   different voltage at a load than a push-pull pin sourcing the same `1`
   through a 25 Ω driver (20 mA). Without the mode event, the board
   integrator would use the stale mode until the next latch write —
   potentially hundreds of milliseconds later.

2. **The adapter already consumes both fields.** `emu8051-adapter.js` calls
   `board.setPin(pinId, mode, driveHigh)` on every callback. The mode is
   not informational — it determines the source/sink impedance the board
   applies to the pin node.

3. **Real firmware writes PxM0/PxM1 during init, before any port writes.**
   Without mode events, the board would not know the pins are push-pull
   until the first port write. On programs that only configure mode and
   never write the port (e.g., input-only ADC pins), the board would never
   learn the mode at all.

### Cross-emulator convention

ucsim does not emit mode-change events in its trace format. This is a
known, documented difference. When cross-checking:
- Strip leading mode events from emu8051's stream before comparison
- The remaining data events match exactly (67/67 programs verified)
- The mode-event count equals the number of PxM0/PxM1 SFR writes that
  changed at least one pin's mode — fully deterministic, not random

### Addition to §7 (acceptance ladder)

> **7.2** `PxM1`/`PxM0` writes fire `setPin` callbacks with the new mode
> and the current latch value. The board's circuit model uses both.
> A mode change without a latch change is still a pin event.
