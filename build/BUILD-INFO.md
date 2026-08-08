# Build provenance for emu8051.js + emu8051.wasm

| File | Size | SHA-256 |
|------|------|---------|
| `emu8051.js` | 18 KB | `5c70f2a0e3075ef28a998c13f1f3cb5e19a669cc5919db9735c35d3845b1cf25` |
| `emu8051.wasm` | 57 KB | `c6b3d6e42f7f7f9f67fbc7b9b14ac44cf2e1839eee265a5a7fb6f00c5abfdc41` |

**Emscripten:** emcc 6.0.6
**Source commit:** `b81e8ea`
**Build:** `make -f Makefile.wasm`
**Licence:** MIT (emu8051 + STC12) + MIT/UIUC (Emscripten). No GPL.

40+ exported functions: CPU, debug, serial TX/RX, profiling, pin history,
boundary A push callbacks, boundary D debug control.
