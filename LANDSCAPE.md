# What this emulator offers and what it does not

## What is here

- **MIT-licensed 8051 emulator that runs in the browser.** 21 KB + 65 KB
  WASM, single-threaded, no SharedArrayBuffer. Modularized as
  `createEmu8051()`.
- **STC12/STC15/STC89-specific peripheral models.** 1T/12T timer, port
  modes, ADC, PCA, UART, dual DPTR — not a generic 8051 core.
- **Boundary A push-mode pin bus.** Pin state as `(mode, driveHigh)`
  callbacks, not polling. The board layer receives events.
- **Boundary D debug control.** Run/halt/step, breakpoints, memory access,
  profiling, pin history.
- **Differential verification.** 220/349 third-party corpus images produce
  byte-identical event streams against an independent implementation.
  Evidence category 2b (no silicon).
- **UART entry points.** TX callback + RX inject. Baud timing not modelled.
- **AVR adapter.** Boundary A wrapper around avr8js (MIT) for ATmega328P
  family. 7/10 conformance tests pass.

## What is NOT here

| Feature | Status |
|---------|--------|
| Baud-rate timing (UART) | Not modelled. Bytes are instant. |
| SPI, watchdog | SFR storage only, no logic. |
| PCA PWM pin output | Computed but not wired to port pin. |
| Visual debugging UI | Data is exported; UI is the front-end's job. |
| Oscilloscope / logic analyzer | Pin callbacks provide the data; rendering is elsewhere. |
| Silicon verification | Nothing has run on real hardware. |

## Architecture

The CPU core, peripherals, and UI are separated through contracts:

- **Boundary A** (pin bus): MCU <-> board
- **Boundary B** (parts): board <-> UI
- **Boundary D** (debug): MCU <-> debugger

The board layer, circuit designer, and debugger are built and tested
independently across separate repositories.
