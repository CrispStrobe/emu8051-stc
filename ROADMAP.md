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
  GDB stub (optional)              Logic analyzer        Logic analyzer view
                                   Multimeter            Register/interrupt viewer
```

## What we need to add (in this repo)

### 1. UART simulation — HIGH PRIORITY
**Status:** upstream has basic serial TX (bit-by-bit, Timer 1 baud rate).
Our STC12 timer path bypasses it.

**What to do:**
- Wire serial_tx into stc12_tick (call it when Timer 1 overflows)
- Add a serial output callback: `on_serial_tx(uint8_t byte, void *ud)`
- Add serial RX: write to SBUF sets RI, firmware reads it
- WASM export: `emu_serial_write(byte)` for input, callback for output
- ~150 lines of C

**What it enables:** programs that do `printf` or `SBUF = 'A'` produce
visible output in the browser's serial terminal.

### 2. Oscilloscope/logic analyzer data — MEDIUM PRIORITY
**Status:** boundary A pin-change callbacks already fire on every pin
toggle with nanosecond timestamps.

**What to do:**
- Nothing in the emulator. bw-board already receives pin changes.
- The board layer computes toggle frequency (buzzerTone) and
  integrates duty cycle (ledBrightness).
- The UI renders it.

**What we could add:** a `pin_history` ring buffer that stores the
last N pin-change events with timestamps, for the UI to render as
a waveform. ~50 lines.

### 3. Register/interrupt visualization data — LOW PRIORITY (already exposed)
**Status:** the debug API already exposes:
- All registers: A, B, DPTR, SP, PSW, R0-R7 (with bank)
- All SFR space (0x80-0xFF)
- All IRAM (0x00-0xFF)
- All XRAM (0x0000-0xFFFF)
- IE, IP (interrupt enable/priority)
- PC, step state, breakpoints

**What to do:** nothing in the emulator. The front-end reads these
through the WASM debug API and renders them.

### 4. GDB stub — LOW PRIORITY
**Status:** not started.

**What to do:**
- Implement the GDB remote serial protocol over WebSocket
- Map GDB commands to our debug API (step, continue, breakpoint,
  register read/write, memory read/write)
- ~300 lines, well-documented protocol

**What it enables:** connect VS Code, GDB, or any GDB-compatible
debugger to the emulated chip.

### 5. Code coverage — DONE (PC histogram)
**Status:** implemented in debug.c. `dbg_profile_start/stop/get/total`.
Front-end can read the histogram and highlight hot/cold code.

## What the front-end needs from us (WASM API)

Already exported:
- ✅ Run/halt/step/reset
- ✅ Breakpoints (code, yield)
- ✅ Memory read/write (all 5 spaces)
- ✅ Register read (A, B, DPTR, SP, PSW, R0-R7)
- ✅ Pin state (mode + drive per pin)
- ✅ ADC input (volts)
- ✅ Time tracking (nanoseconds)
- ✅ Level 1 position (bw_ms, task_state, task_until)
- ✅ Disassembly
- ✅ Pin-change push callback
- ✅ Profiling (PC histogram)

Needed:
- ❌ Serial TX callback (byte → JS)
- ❌ Serial RX input (JS → byte)
- ❌ Pin history ring buffer (for waveform rendering)
- ❌ Interrupt state query (which interrupts pending/active)

## Priority order

1. **UART** — unblocks serial terminal in the UI
2. **Interrupt state query** — 2 lines, exposes mInterruptActive
3. **Pin history** — 50 lines, unblocks waveform view
4. **GDB stub** — nice-to-have, not blocking anything
