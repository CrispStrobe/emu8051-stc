# Build provenance for emu8051.js + emu8051.wasm

## Files

| File | Size | SHA-256 |
|------|------|---------|
| `emu8051.js` | 17 KB | `5a008a67644f1b1bb45716c4fa67ff12bbd9d9e369e6a23cac2b0b88f3582faf` |
| `emu8051.wasm` | 55 KB | `6feddb71df1b37bbb4ef58c152684d6b2c40451dde3b52182e68ce388cb63a45` |

## How they were produced

```bash
source ~/emsdk/emsdk_env.sh
make -f Makefile.wasm
```

**Emscripten version:** emcc 6.0.6 (ce75e06884093bcefb86a6b8fd56a5d62a4cc245)

**Source commit:** `8c4a4fa` (emu8051-stc master, includes STC15 delta)

**Source files compiled:**
- `core.c` — 8051 emulator core (upstream jarikomppa/emu8051, MIT)
- `opcodes.c` — opcode handlers (upstream, with cycle count + bug fixes)
- `disasm.c` — disassembler (upstream)
- `stc12.c` — STC12/STC15 peripheral model (ours, MIT)
- `debug.c` — debug control interface (ours, MIT)
- `wasm_api.c` — WASM export layer (ours, MIT)

**Emscripten flags:** see `Makefile.wasm`. Key flags:
- `-O2`, `MODULARIZE=1`, `EXPORT_NAME="createEmu8051"`
- `ALLOW_TABLE_GROWTH=1`, `WASM_BIGINT=1`
- `ENVIRONMENT='web,worker,node'`

## Reproducibility

Emscripten embeds build paths; byte-identical reproduction is not
guaranteed. Verify against SHA-256 values above.

## Licence

MIT (emu8051 + STC12 extensions) + MIT/UIUC (Emscripten runtime).
No GPL code linked. See [THIRD-PARTY.md](../THIRD-PARTY.md).
