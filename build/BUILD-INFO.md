# Build provenance for emu8051.js + emu8051.wasm

## Files

| File | Size | SHA-256 |
|------|------|---------|
| `emu8051.js` | 17 KB | `5a008a67644f1b1bb45716c4fa67ff12bbd9d9e369e6a23cac2b0b88f3582faf` |
| `emu8051.wasm` | 55 KB | `14d427b5375e810f1ee2d29407c6612364dab4a46f801d4de1e6f32433915929` |

## How they were produced

```bash
source ~/emsdk/emsdk_env.sh
make -f Makefile.wasm
```

**Emscripten version:** emcc 6.0.6 (ce75e06884093bcefb86a6b8fd56a5d62a4cc245)

**Source commit:** `95d0a18` (emu8051-stc master)

**Source files compiled:**
- `core.c` — 8051 emulator core (upstream jarikomppa/emu8051, MIT)
- `opcodes.c` — opcode handlers (upstream, with cycle count fixes)
- `disasm.c` — disassembler (upstream)
- `stc12.c` — STC12C5A60S2 peripheral model (ours, MIT)
- `debug.c` — debug control interface (ours, MIT)
- `wasm_api.c` — WASM export layer (ours, MIT)

**Emscripten flags:** see `Makefile.wasm` for the full command. Key flags:
- `-O2` optimization
- `MODULARIZE=1`, `EXPORT_NAME="createEmu8051"`
- `ALLOW_TABLE_GROWTH=1`, `WASM_BIGINT=1`
- `ENVIRONMENT='web,worker,node'`

## Reproducibility

Emscripten embeds build paths and timestamps in the JS glue, so a
rebuild from the same sources may not produce byte-identical output.
The WASM binary itself is more likely to be reproducible. Verify
against the SHA-256 values above.

## Licence

The WASM binary contains:
- **emu8051** core: MIT (© 2006/2022 Jari Komppa)
- **STC12 extensions**: MIT (© 2024 CrispStrobe)
- **Emscripten runtime glue**: MIT / University of Illinois Open Source
  License (part of the LLVM project)

All components are permissive. No GPL code is linked or included.
See [THIRD-PARTY.md](../THIRD-PARTY.md).
