# 005 — Add `input-pullup` pin mode to boundary A

**Date:** 2026-08-09
**Status:** Proposed. Awaiting coordinator adjudication.
**Affects:** simulation-contract.md (boundary A PinMode), bw-board pin-model.js

## Problem

AVR's `INPUT_PULLUP` has no clean mapping to boundary A's four modes:

| Current mode | Electrical model | Problem for AVR pullup |
|---|---|---|
| `input` | high-Z | `driveHigh` is ignored → pull-up vanishes |
| `quasi` | weak pull-up + strong sink | AVR input never sinks; reusing would import 25 Ω sink |
| `pushpull` | strong both ways | AVR input is not driving |
| `opendrain` | strong sink, no pull-up | opposite of what's needed |

`pinMode(pin, INPUT_PULLUP)` with a button to ground is the standard
Arduino input idiom. Under the current mapping the pin floats, the
button does not work, and the learner gets no explanation.

## Proposal

Add a fifth `PinMode`:

```ts
type PinMode = 'quasi' | 'pushpull' | 'input' | 'opendrain' | 'input-pullup';
```

Thévenin model in bw-board `pin-model.js`:

```js
case 'input-pullup':
    return { vTh: vcc, rTh: R_PULLUP };  // R_PULLUP ≈ 35 kΩ (AVR: 20–50 kΩ)
```

- `input` continues to return high-Z and ignore `driveHigh`
- `input-pullup` returns a weak Thévenin source (VCC through ~35 kΩ)
- The STC12's `quasi` mode is unchanged (its pull-up is ~21.7 kΩ with a
  strong sink — a different device)

## Why not reuse `quasi`

`quasi` driving LOW is a strong 25 Ω sink (STC12: 20 mA / 0.5 V). An
AVR input pin in INPUT_PULLUP mode never sinks — it is always high
impedance with a resistor to VCC. Reusing `quasi` would make a button
press look like a short through 25 Ω instead of pulling a 35 kΩ
resistor to ground.

## Tolerance

The AVR datasheet gives 20–50 kΩ. Use 35 kΩ as the nominal, state it
is a range, and cite the source. `pin-model.js` already sets this
standard in its header.

## Impact

- **Adapter change:** `avr8js-adapter.mjs` maps `PinState.InputPullUp`
  to `mode: 'input-pullup'` instead of `mode: 'input', driveHigh: true`
- **Board change:** `pin-model.js` adds a case for `'input-pullup'`
- **Contract change:** `simulation-contract.md` PinMode type gains one value
- **Existing modes:** unchanged, no breakage
- **Test:** button-to-ground through pull-up reads HIGH when open, LOW
  when pressed, with node voltage asserted
