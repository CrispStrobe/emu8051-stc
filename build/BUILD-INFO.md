# Build provenance for emu8051.js + emu8051.wasm

| File | Size | SHA-256 |
|------|------|---------|
| `emu8051.js` | 19 KB | `2afecbab7bf5fef74786c4060b7e67b93b6e41ff53fd363e9e363456fb23b674` |
| `emu8051.wasm` | 62 KB | `c21d57743caac777cfdaef0d2b223969a091a9c3a0f25edbb3b841f1f32037d0` |

**Emscripten:** emcc 6.0.6
**Source commit:** `1383b08`
**Build:** `make -f Makefile.wasm`
**Licence:** MIT (emu8051 + STC12/15) + MIT/UIUC (Emscripten). No GPL.

Peripherals: Timer 0/1 (1T/12T), ADC (10-bit, 8ch, ADRJ), PCA/PWM
(8 sources, 9-bit compare, double-buffered, pin output, toggle),
UART1/2, SPI, watchdog, dual DPTR, port modes, STC15 Timer 2.

Debug: run/halt/step/breakpoints/memory/registers/profiling/pin history.
Boundary A: push-mode pin callbacks, ADC in volts, advanceTo(ns).
Boundary D: capabilities(), consumes=[], version string.

50+ exported functions. `emu_capabilities()` returns spec-compliant JSON.
