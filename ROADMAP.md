# Roadmap: from emulator core to full debugger-in-browser

## Where each feature lives

```
  emu8051-stc (this repo)          bw-board              bw-circuit-ui / brickwright-lite
  ─────────────────────────        ──────────            ──────────────────────────────────
  CPU core + STC12/15 model        Board solver          Circuit designer UI
  Boundary A (pin bus)       ←→    Pin resolution        Component drag & wire
  Boundary D (debug)               LED brightness        Debugger panel
  UART model                       Buzzer tone           Serial terminal
  PC histogram profiler            Oscilloscope data     Oscilloscope view
  GDB stub (future)                Logic analyzer        Logic analyzer view
  Pin history ring buffer          Multimeter            Register/interrupt viewer
                                                         Code coverage heatmap
                                                         Stack viewer
```

---

## Status of emulator-side features

| Feature | Status | WASM exported | Test coverage |
|---------|--------|---------------|---------------|
| CPU core (all 8051 opcodes) | ✅ Done | ✅ | 136 assertions (test_suite) |
| STC12 SFR set (66 registers) | ✅ Done | ✅ | 12 + 103 assertions |
| Timer 0/1 with 1T/12T | ✅ Done | ✅ | 14 assertions |
| Port modes (PxM0/PxM1) | ✅ Done | ✅ | 11 assertions |
| 10-bit ADC with ADRJ | ✅ Done | ✅ | 18 assertions |
| PCA / PWM | ✅ Done | ✅ | 12 assertions |
| STC15 delta (ADRJ trap, Timer 2) | ✅ Done | ✅ | 9 assertions |
| Boundary A pin bus (push mode) | ✅ Done | ✅ | 13 assertions |
| Boundary D debug control | ✅ Done | ✅ (26 functions) | 37 assertions |
| Level 1 position (task state) | ✅ Done | ✅ | 4 assertions |
| UART serial TX/RX | ✅ Done | ✅ | 5 assertions |
| PC histogram profiling | ✅ Done | ✅ (4 functions) | 2 assertions |
| Interrupt state query | ✅ Done | ✅ | — |
| MCS-51 cycle count verification | ✅ Done | — | 34 assertions |
| Differential trace emitter | ✅ Done | — | — |
| Pin history ring buffer | ✅ Done | ✅ (5 functions) | 2 assertions |
| GDB remote protocol stub | ✅ Done (Node.js) | — | 6 assertions |
| UART2 (STC12/15) | ✅ Done | ✅ | 2 assertions |
| SPI peripheral | ✅ Done | ✅ | 4 assertions |
| Watchdog | ✅ Done | ✅ | 3 assertions |
| 8052 Timer 2 | N/A (STC remaps 0xC8 to P5) | — | — |

---

## Plan: what to build next, and how

### Phase 1: make the debugger panel possible (front-end unblocking) — DONE

All WASM exports needed by the debugger panel are implemented and tested.

- **Profiling:** `emu_dbg_profile_start/stop/get/total` — 4 WASM exports,
  tested in test_wasm.mjs. Front-end reads the histogram to color-code
  lines by execution count.
- **Pin history ring buffer:** `emu_pin_history_enable/count/head/get` +
  `emu_pin_event_size` — 5 WASM exports, tested in test_wasm.mjs.
  4096-entry circular buffer, written by pin-change callback.
  Front-end renders as a logic analyzer / oscilloscope trace.
- **Stack viewer:** no emulator changes needed — front-end reads
  `emu_dbg_read_mem(SPACE_IRAM, sp, depth)` and `emu_dbg_sp()`.

### Phase 2: serial terminal

**Status: TX and RX are implemented.** The front-end needs:
- A `<textarea>` or terminal component (xterm.js or similar)
- Register `emu_set_serial_callback` via `addFunction` to receive TX bytes
- Call `emu_serial_write(byte)` when the user types into the terminal
- SBUF writes from firmware appear as characters in the terminal

For `printf` support: SDCC's `putchar` writes to SBUF. Any program
using `printf` will produce output through this path automatically.

### Phase 3: GDB remote protocol stub — DONE

**Implemented in `gdb-stub.mjs`** (~250 lines, Node.js). TCP server
speaking GDB Remote Serial Protocol (RSP) over the WASM debug API.
6 tests in `test_gdb.mjs`.

Note: no upstream mcs51 GDB target exists, so this is a transport layer
below boundary D — useful for custom GDB-compatible front-ends, not
for `target remote` with stock GDB.

| GDB command | Our API |
|-------------|---------|
| `g` (read registers) | `emu_dbg_acc/b/dptr/sp/psw/rn` |
| `G` (write registers) | `emu_dbg_write_mem(SPACE_SFR, ...)` |
| `m` (read memory) | `emu_dbg_read_mem(space, addr, len)` |
| `M` (write memory) | `emu_dbg_write_mem(space, addr, len)` |
| `s` (step) | `emu_dbg_step(STEP_INSN, 1)` |
| `c` (continue) | `emu_dbg_run()` |
| `Z0` (set breakpoint) | `emu_dbg_set_bp_code(addr)` |
| `z0` (clear breakpoint) | `emu_dbg_clear_bp(handle)` |
| `?` (halt reason) | `emu_dbg_state()` + last halt reason |

### Phase 4: oscilloscope and logic analyzer

**Where it lives:** entirely in bw-board + bw-circuit-ui. The emulator's
job is done — boundary A push callbacks already provide:
- Pin ID (port.bit)
- Mode (quasi/pushpull/input/opendrain)
- Drive level (high/low)
- Nanosecond timestamp

The board layer:
- Stores pin-change history (ring buffer)
- Computes derived quantities: toggle frequency (buzzerTone),
  PWM duty cycle (ledBrightness), rise/fall timing
- Exposes `getWaveform(pin, startNs, endNs)` for the UI

The UI:
- Renders waveform traces (Canvas or WebGL)
- Time axis with zoom/pan
- Measurement cursors (frequency, period, duty cycle)
- Trigger (rising/falling edge on a selected pin)

### Phase 5: additional peripherals — mostly DONE

**UART2** ✅ Done. S2CON/S2BUF (0x9A/0x9B), TX callback, WASM export
`emu_serial2_write` / `emu_set_serial2_callback`. Tested.

**SPI** ✅ Done. SPCTL/SPDAT/SPSTAT, master mode shift register.
Tested in test_integration.c (4 assertions).

**Watchdog** ✅ Done. WDT_CONTR (0xC1), count-down with prescaler,
reset on overflow. Tested (3 assertions).

**8052 Timer 2:** N/A for STC12/15. The standard 8052 T2CON (0xC8) is
remapped to P5 on STC12. STC15's Timer 2 at T2H/T2L (0xD6/0xD7) IS
implemented. Only needed if we add a generic 8052 target.

### Phase 6: more MCU targets

**AVR (Arduino):** avr8js (MIT, wokwi) already exists and runs in the
browser. Integration path: same boundary A/B/D contracts, different CPU
core. The board layer and circuit designer work unchanged.

**micro:bit (Cortex-M0):** tier 1 only (JavaScript simulation, no CPU
emulation) until an MIT-licensed Cortex-M emulator is available.

---

## What the front-end builds (not our job, but we must expose the data)

| Front-end component | Reads from our WASM API |
|---------------------|------------------------|
| **Register viewer** | `emu_dbg_acc/b/dptr/sp/psw/rn`, `emu_dbg_read_mem(SFR)` |
| **Register bank highlight** | `emu_dbg_psw() >> 3 & 3` (RS1:RS0) |
| **Interrupt viewer** | `emu_get_sfr(IE)`, `emu_get_sfr(IP)`, `emu_get_interrupt_active()` |
| **Stack viewer** | `emu_dbg_sp()`, `emu_dbg_read_mem(IRAM, sp-depth, depth)` |
| **Memory editor** | `emu_dbg_read_mem/write_mem` across all 5 spaces |
| **Serial terminal** | `emu_set_serial_callback` (TX), `emu_serial_write` (RX) |
| **Code coverage heatmap** | `emu_dbg_profile_get(addr)` for each address |
| **Disassembly view** | `emu_disasm(addr)` per line |
| **Breakpoint gutter** | `emu_dbg_set_bp_code(addr)` / `emu_dbg_clear_bp(handle)` |
| **Step controls** | `emu_dbg_step(kind, count)`, `emu_dbg_run()`, `emu_dbg_halt()` |
| **Block highlight** | `emu_dbg_task_state(idx)` → symbol table → source block |
| **Waveform viewer** | Pin history from bw-board (which gets it from boundary A) |
| **LED/buzzer/pot** | bw-board's `ledBrightness`, `buzzerTone`, `setControl` |

---

## Testing strategy for new features

Each new feature gets:
1. A native C test in `test_integration.c` or a new `test_<feature>.c`
2. A WASM test in `test_wasm.mjs` for the JS-facing API
3. A firmware image that exercises it (in `test_images/`)
4. Differential comparison against ucsim where applicable

Current: 347 assertions across 5 test suites (test_suite 136,
test_integration 140, test_cycles 34, test_wasm 31, test_gdb 6),
30 firmware images. Third-party corpus: 220/349 strict byte-identical,
86 timing-only divergence (zero instruction disagreements).
