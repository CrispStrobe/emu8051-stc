# Build provenance for emu8051.js + emu8051.wasm

| File | Size | SHA-256 |
|------|------|---------|
| `emu8051.js` | 19 KB | `5c70f2a0e3075ef28a998c13f1f3cb5e19a669cc5919db9735c35d3845b1cf25` |
| `emu8051.wasm` | 56 KB | `6546370bf643cd0e78012e96926bfe6d7a83ca34d10d73ea3612779e9bc0b85e` |

**Emscripten:** emcc 6.0.6
**Source commit:** `0455a54`
**Build:** `make -f Makefile.wasm`
**Licence:** MIT (emu8051 + STC12/15) + MIT/UIUC (Emscripten). No GPL.

40+ exported functions: CPU core, boundary A (pin bus), boundary D
(debug control), UART TX/RX, PC histogram profiling, pin history
ring buffer, watchdog, STC15 delta.
