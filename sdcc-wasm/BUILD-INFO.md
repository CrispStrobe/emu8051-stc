# SDCC WASM build provenance

**Status:** In progress

## Source

- **SDCC version:** 4.5.0
- **Source tarball:** sdcc-src-4.5.0.tar.bz2
- **SHA-256:** d5030437fb436bb1d93a8dbdbfb46baaa60613318f4fb3f5871d72815d1eed80
- **Ports enabled:** mcs51 only
- **Emscripten:** emcc 6.0.6

## Target

The mcs51 toolchain as WASM modules:
- `sdcc.wasm` — the compiler
- `sdcpp.wasm` — the preprocessor (or bundled with sdcc)
- `sdas8051.wasm` — the assembler
- `sdld.wasm` — the linker

Plus the minimum headers/libraries from `share/sdcc/`:
- `include/mcs51/*.h` — register headers (8051.h, 8052.h, stc12.h)
- `lib/mcs51/` — the model-small library

## Acceptance

For all 9 pseudocode examples in stc/examples/:
the .hex from WASM SDCC must be BYTE-IDENTICAL to native SDCC 4.5.0.

## Scope

8051 only. AVR (avr-gcc) is out of scope and stays server-side.
