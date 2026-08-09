# 8051 emulator landscape — what exists and what we add

## The field

| Name | Lang | Browser | Licence | Peripherals | STC? |
|------|------|---------|---------|-------------|------|
| **SimulIDE** | C++ | No | **AGPLv3** | 8051 core + SPICE analog+digital, circuit editor, breakpoints, watch | No (generic) |
| **EdSim51** | Java | No | Free | ADC, DAC, 7-seg, LCD, UART, motor | No |
| **MCU 8051 IDE** | Tcl/C++ | No | GPL-2 | LEDs, LCD, 7-seg, keypad, UART, coverage | No |
| **µCsim (SDCC)** | C++ | No | GPL-2 | Timers, UART, interrupts, profiling | No |
| **PICSimLab** | C++ | No | GPL-2 | Oscilloscope, logic analyzer (uses ucsim) | No |
| **Wokwi** | TS | **Yes** | Commercial | AVR, ESP32, RP2040 — **no 8051** | No |
| **js51** | JS | Yes | Open | Core only (minimal) | No |
| **i8051emu** | Python | Yes | Open | Core registers, memory | No |
| **Keil µVision** | — | No | Commercial | Full peripherals, profiler, logic analyzer | SiLabs only |
| **Proteus VSM** | — | No | Commercial | Full analog+digital, SPICE, PCB co-sim | AT89 only |
| **emu8051-stc** | C | **Yes (WASM)** | **MIT** | **STC12/15 model, ADC, PCA, 1T/12T, port modes** | **Yes** |

## What we uniquely offer

1. **Only MIT-licensed 8051 emulator that runs in the browser.** SimulIDE
   is the closest open-source comparable — it has an 8051 core with a
   SPICE-style circuit simulator, breakpoints, and watch registers — but
   it is **AGPLv3** and desktop-only. AGPL is precisely why
   `brickwright-lite` (a fully-permissive browser bundle) can learn from
   it but never link it. js51 and i8051emu are browser-capable but have
   no peripheral simulation.
2. **Only emulator modelling STC12/STC15-specific features.** SimulIDE's
   8051 is generic (no 1T timer, no PCA, no port modes). Wokwi has no
   8051 at all — its Custom Chips API models peripherals, not cores,
   which is why this fork has to exist.
3. **Boundary A push-mode pin bus** — no other 8051 emulator exposes
   pin state as (mode, driveHigh) callbacks.
4. **Differential-verified peripheral model** — 220/349 corpus images
   agree with an independent implementation, 0 content disagreements.

## What we lack

| Feature | Who has it | Status |
|---------|-----------|--------|
| UART/serial simulation | µCsim, EdSim51, Keil | **Done** (TX callback + RX inject) |
| Code coverage / profiling | MCU 8051 IDE, Keil | **Done** (PC histogram) |
| GDB stub | avr8js | **Done** (gdb-stub.mjs, ~250 lines) |
| Watchdog timer | µCsim, Keil | **Done** (WDT_CONTR 0xC1) |
| Pin history / waveform data | Proteus, PICSimLab | **Done** (ring buffer, WASM exported) |
| Interrupt state query | MCU 8051 IDE | **Done** (emu_get_interrupt_active) |
| Visual debugging UI | EdSim51, MCU 8051 IDE | Front-end job (data exported) |
| Oscilloscope / logic analyzer | Proteus, PICSimLab | Board layer (pin callbacks done) |
| Built-in assembler | EdSim51, MCU 8051 IDE | Not needed (SDCC exists) |
| 8052 Timer 2 | µCsim | Not started (~50 lines) |

## Architecture advantage

Most 8051 simulators are monolithic: the CPU core, peripherals, and UI
are tangled together. Our architecture separates them through contracts:

- Boundary A (pin bus): MCU ⇄ board
- Boundary B (parts): board ⇄ UI
- Boundary D (debug): MCU ⇄ debugger

This means the board layer, circuit designer, and debugger can all be
built and tested independently — which is exactly what is happening
across bw-board, bw-circuit-ui, and this repo.
