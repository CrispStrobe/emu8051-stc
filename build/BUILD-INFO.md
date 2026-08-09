# Build provenance for emu8051.js + emu8051.wasm

| File | Size | SHA-256 |
|------|------|---------|
| `emu8051.js` | 19 KB | `a41313458b36960171888878675708048f9fd32cb5bb6e9305b5b2c4eaa6a0ef` |
| `emu8051.wasm` | 58 KB | `4d66e6e8f2e20c5e5ff115b27d67e2a2f50a2c7acbde2a504ff8c8cf954f4ff9` |

**Emscripten:** emcc 6.0.6
**Source commit:** `e455ad8`
**Build:** `make -f Makefile.wasm`
**Licence:** MIT (emu8051 + STC12/15) + MIT/UIUC (Emscripten). No GPL.

50+ exported functions. `emu_capabilities()` returns full spec-compliant
JSON. `emu_version()` returns "emu8051-stc 1.0.0".
