# Build provenance for emu8051.js + emu8051.wasm

| File | Size | SHA-256 |
|------|------|---------|
| `emu8051.js` | 18 KB | `5c70f2a0e3075ef28a998c13f1f3cb5e19a669cc5919db9735c35d3845b1cf25` |
| `emu8051.wasm` | 57 KB | `44a8b855aeaa7f7ada2e43a47be22d67535030fc57c0d028cf082d692a06abe0` |

**Emscripten:** emcc 6.0.6
**Source commit:** `9791323`
**Build:** `make -f Makefile.wasm`
**Licence:** MIT (emu8051 + STC12) + MIT/UIUC (Emscripten). No GPL.

Includes: UART TX/RX, profiling, pin history, PCA 8 clock sources,
STC15 delta, boundary A/D, 36+ debug exports.
