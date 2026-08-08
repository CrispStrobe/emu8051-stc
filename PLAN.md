# emu8051-stc — implementation plan

## Goal

Fork emu8051 (MIT, jarikomppa) and extend it with an STC12C5A60S2 model so it
can serve as the MIT-licensed, WASM-buildable simulation core for
brickwright-lite. The sibling ucsim-stc builds the GPL-2 oracle; this repo is
the shippable track.

## Status

All phases complete and tested.

| Phase | Status | Commit |
|-------|--------|--------|
| 0. Baseline import | Done | `fdbc893` |
| 1. SFR set (44 registers) | Done | `049b87f` |
| 2. Timer 0/1 with AUXR.7/AUXR.6 1T/12T | Done | `049b87f` |
| 3. Port modes (PxM1/PxM0) | Done | `049b87f` |
| 4. ADC (10-bit, 8ch, 4 speeds, ADRJ) | Done | `049b87f` |
| 5. PCA/PWM (counter, compare, 8-bit PWM) | Done | `049b87f` |
| 6. WASM build (emcc, 12K+49K) | Done | `0920749` |
| 7. Test images (SDCC, 01-blink, 02-adc) | Done | `9211d4a` |

## Architecture

- `em8051.mSFR[128]` holds SFR space (0x80-0xFF), indexed as `addr - 0x80`.
- Per-register `sfrread[128]` / `sfrwrite[128]` callback arrays intercept
  SFR access for port modes, ADC, and PCA.
- `tick()` executes one machine cycle. In STC12 mode (`-stc12` flag),
  `cpu.skip_timers = true` disables the upstream `timer_tick()`, and
  `stc12_tick()` handles timers with 1T/12T prescaling.
- The WASM build (`Makefile.wasm`) compiles only the core files
  (core.c, opcodes.c, disasm.c, stc12.c, wasm_api.c) with Emscripten,
  exporting a flat C API via `EMSCRIPTEN_KEEPALIVE`.

## What remains

- **PCA PWM pin output**: the compare logic runs but the result isn't
  reflected on port pins yet.
- **BRT**: counter runs but doesn't feed UART baud rate.
- **SPI, UART2, watchdog**: SFR storage only, no logic.
- **ADC silicon verification**: the register sequence matches the datasheet
  but has not been confirmed on hardware. `02-adc` is the test for that.

## Licence constraints

- emu8051 is MIT. All additions are MIT.
- No code from copyleft sources (ucsim, QEMU, SimulIDE, circuitjs).
- SDCC is GPL-2+ but is build-time only for test images; it is not linked
  into the emulator or the WASM output.
