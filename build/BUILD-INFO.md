# Build provenance for emu8051.js + emu8051.wasm

| File | Size | SHA-256 |
|------|------|---------|
| `emu8051.js` | 19 KB | `a41313458b36960171888878675708048f9fd32cb5bb6e9305b5b2c4eaa6a0ef` |
| `emu8051.wasm` | 56 KB | `6632ae7fbcd10b26d9e2cd8d40bb76e2aff08208db5af515cd0542265a51782a` |

**Emscripten:** emcc 6.0.6
**Source commit:** `ed47735`
**Build:** `make -f Makefile.wasm`
**Licence:** MIT (emu8051 + STC12/15) + MIT/UIUC (Emscripten). No GPL.

Peripherals: Timer 0/1 (1T/12T), ADC, PCA/PWM (8 sources), UART1/2,
SPI, watchdog, port modes, STC15 Timer 2, STC15 ADRJ trap.
Debug: run/halt/step/breakpoints/memory/registers/profiling/pin history.
Boundary A: push-mode pin callbacks, ADC in volts, advanceTo(ns).
Boundary D: consumes=[] (takes nothing).
