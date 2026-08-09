# Build provenance for emu8051.js + emu8051.wasm

| File | Size | SHA-256 |
|------|------|---------|
| `emu8051.js` | 19 KB | `4a5d5ec4d48164e31c710e32ea12e495b2eae7729faa63dc82c018baa94172a8` |
| `emu8051.wasm` | 62 KB | `9572af95f1b2d9fb7afd9aadab9ea99a679ae84fd168cc4ba2ccdc40c8fb1757` |

**Emscripten:** emcc 6.0.6
**Source commit:** `3bf43e3`
**Build:** `make -f Makefile.wasm`
**Licence:** MIT (emu8051 + STC12/15) + MIT/UIUC (Emscripten). No GPL.

Peripherals: Timer 0/1 (1T/12T), ADC (10-bit, 8ch, ADRJ), PCA/PWM
(8 sources, 9-bit compare, double-buffered, pin output, toggle),
UART1/2, SPI, watchdog, dual DPTR, port modes, STC15 Timer 2.

Debug: run/halt/step/breakpoints/memory/registers/profiling/pin history.
Boundary A: push-mode pin callbacks, ADC in volts, advanceTo(ns).
Boundary D: capabilities(), consumes=[], version "emu8051-stc 1.0.0".

57 exported functions. Re-initialization crash fixed (3bf43e3).
