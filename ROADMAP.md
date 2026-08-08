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
| PC histogram profiling | ✅ Done | ❌ (C only, needs WASM export) | — |
| Interrupt state query | ✅ Done | ✅ | — |
| MCS-51 cycle count verification | ✅ Done | — | 34 assertions |
| Differential trace emitter | ✅ Done | — | — |
| Pin history ring buffer | ❌ Not started | ❌ | — |
| GDB remote protocol stub | ❌ Not started | ❌ | — |
| UART2 (STC12/15) | ❌ Stub only | — | — |
| SPI peripheral | ❌ Stub only | — | — |
| Watchdog | ❌ Stub only | — | — |
| 8052 Timer 2 | ❌ Not implemented | — | — |

---

## Plan: what to build next, and how

### Phase 1: make the debugger panel possible (front-end unblocking)

The debugger panel in brickwright-lite needs these WASM exports. Most
already exist; the gaps are small.

**1a. Export profiling to WASM** (~10 lines)
```c
EMSCRIPTEN_KEEPALIVE void emu_dbg_profile_start(void);
EMSCRIPTEN_KEEPALIVE void emu_dbg_profile_stop(void);
EMSCRIPTEN_KEEPALIVE uint32_t emu_dbg_profile_get(uint16_t addr);
```
Front-end reads the histogram to color-code lines by execution count.

**1b. Pin history ring buffer** (~60 lines in stc12.c)
```c
struct pin_event { uint64_t t_ns; uint8_t port; uint8_t bit; uint8_t mode; uint8_t drive; };
#define PIN_HISTORY_SIZE 4096
static struct pin_event pin_history[PIN_HISTORY_SIZE];
```
Stored in a circular buffer, written by the existing pin-change callback.
WASM export: `emu_pin_history_get(index)` returns a pointer to the event.
Front-end renders as a logic analyzer / oscilloscope trace.

**1c. Stack viewer data** (already possible, no emulator changes)
Front-end reads `emu_dbg_read_mem(SPACE_IRAM, sp, depth)` and
`emu_dbg_sp()`. No new exports needed.

### Phase 2: serial terminal

**Status: TX and RX are implemented.** The front-end needs:
- A `<textarea>` or terminal component (xterm.js or similar)
- Register `emu_set_serial_callback` via `addFunction` to receive TX bytes
- Call `emu_serial_write(byte)` when the user types into the terminal
- SBUF writes from firmware appear as characters in the terminal

For `printf` support: SDCC's `putchar` writes to SBUF. Any program
using `printf` will produce output through this path automatically.

### Phase 3: GDB remote protocol stub

**What it is:** a WebSocket server that speaks the GDB Remote Serial
Protocol (RSP). A standard debugger (GDB, VS Code + cortex-debug,
or any GDB-compatible client) connects and gets full debug access.

**What it maps to:**

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

**Implementation:** ~300 lines. The protocol is text-based, well-documented,
and the mapping to our debug API is 1:1. Can live as a Node.js wrapper
around the WASM module, or as a standalone C server.

**What it enables:**
- VS Code debugging with source-level stepping
- GDB command-line debugging
- Any tool that speaks GDB RSP (Eclipse, CLion, etc.)

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

### Phase 5: additional peripherals

**UART2** (STC12/15): second serial port on S2CON/S2BUF (0x9A/0x9B).
Same model as UART1 — intercept S2BUF writes, callback for TX.
~30 lines on top of the UART1 model.

**SPI:** SPCTL/SPDAT/SPSTAT (0x85/0x86/0xCE on STC12, 0xCD/0xCE/0xCF
on STC15). Master mode: shift register, clock generation, chip select.
~100 lines. Enables communication with SPI peripherals (OLED displays,
flash memory, DACs).

**Watchdog:** WDT_CONTR (0xC1). Count-down timer, reset on overflow.
~20 lines. Mostly a guard against infinite loops in firmware.

**8052 Timer 2:** T2CON/T2MOD/RCAP2L/RCAP2H/TL2/TH2. Auto-reload and
capture modes. ~80 lines. Standard 8052 feature, different from STC15's
Timer 2.

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

Current: 381 assertions across 11 test suites, 21 firmware images,
275/349 third-party corpus agreement, rung 3 verified on 8 images.
