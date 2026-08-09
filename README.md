emu8051-stc
===========

Fork of [jarikomppa/emu8051](https://github.com/jarikomppa/emu8051) (MIT)
with **STC12C5A60S2 / STC15F2K60S2** peripheral models, a **debug control
interface**, and a **WASM build** (19K + 62K) for use in the browser.

The only MIT-licensed 8051 emulator that runs in a browser with
STC-specific hardware support. See [LANDSCAPE.md](LANDSCAPE.md) for the
competitive analysis.

**Peripherals:** Timer 0/1 (1T/12T), port modes, 10-bit ADC, PCA/PWM
(8 clock sources, 9-bit compare, double-buffered pin output, toggle),
UART1/2, SPI, watchdog, dual DPTR, STC15 Timer 2.

**Debug:** run/halt/step (5 kinds), breakpoints (code + yield),
memory access (5 address spaces), registers, PC histogram profiling,
pin history ring buffer, Level 1 position for cooperative scheduler.

**Verified:** 430+ test assertions (native + WASM + GDB), 30 firmware
images. Third-party corpus: 220/349 produce byte-identical event
streams; a further 86 differ only in how far each model advances in a
2 ms window (zero instruction-level disagreements).
See [RESULTS.md](RESULTS.md).

**API:** 57 WASM exports. See [API.md](API.md).

What changed from upstream
--------------------------

| Area | What |
|------|------|
| **SFR set** | 44 STC12-specific registers added (`stc12.h`): AUXR, port mode (PxM1/PxM0), ADC, PCA/PWM, SPI/UART2/WDT stubs |
| **1T/12T timers** | AUXR.7 (T0x12) and AUXR.6 (T1x12) switch Timer 0/1 between FOSC/12 and FOSC/1. Both states tested explicitly. |
| **Port modes** | PxM1/PxM0 implement quasi-bidirectional, push-pull, input-only, and open-drain per pin |
| **ADC** | 10-bit, 8 channels, 4 speed settings, ADRJ justification. Register sequence from datasheet (not confirmed on silicon). |
| **PCA** | 16-bit counter, FOSC/12 / FOSC/2 / T0-overflow clock sources, compare/match, 8-bit PWM |
| **WASM** | `Makefile.wasm` builds `emu8051.js` + `emu8051.wasm` (12 K + 49 K) via Emscripten. Modularized as `createEmu8051()`. |
| **`-stc12` flag** | Runtime switch; without it behaviour is identical to upstream emu8051 |

Build
-----

### Native (curses TUI)

```bash
sudo apt-get install libncurses5-dev
make
./emu -stc12 firmware.hex
```

### WASM (browser / Node)

```bash
# one-time: install Emscripten
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
source ~/emsdk/emsdk_env.sh

# build
make -f Makefile.wasm
# -> build/emu8051.js + build/emu8051.wasm
```

### Test images (requires SDCC)

```bash
sudo apt-get install sdcc
make test-images          # builds test_images/01-blink.hex, 02-adc.hex
```

### Run all tests

```bash
make test                 # unit tests + integration tests with real firmware
```

WASM API
--------

```js
import createEmu8051 from './build/emu8051.js';
const Module = await createEmu8051();

const init     = Module.cwrap('emu_init',     null,     ['number']);
const tick     = Module.cwrap('emu_tick',      'number', []);
const run      = Module.cwrap('emu_run',       'number', ['number']);
const loadHex  = Module.cwrap('emu_load_hex',  'number', ['string','number']);
const getSfr   = Module.cwrap('emu_get_sfr',   'number', ['number']);
const setSfr   = Module.cwrap('emu_set_sfr',   null,     ['number','number']);
const getPC    = Module.cwrap('emu_get_pc',    'number', []);
const disasm   = Module.cwrap('emu_disasm',    'string', ['number']);
const setAdc   = Module.cwrap('emu_set_adc_input', null, ['number','number']);
const setPort  = Module.cwrap('emu_set_port_input', null, ['number','number']);

init(1);                         // 1 = STC12 mode
loadHex(hexString, hexString.length);
run(1000000);                    // run 1M oscillator clocks
console.log('PC:', getPC());
console.log('P1:', getSfr(0x90).toString(16));
```

Full export list: `emu_init`, `emu_reset`, `emu_tick`, `emu_run`,
`emu_load_hex`, `emu_get/set_sfr`, `emu_get/set_iram`, `emu_get_code`,
`emu_get/set_xdata`, `emu_get/set_pc`, `emu_disasm`,
`emu_set_adc_input`, `emu_set_port_input`, `emu_set_fosc`.

### Boundary A (pin bus)

For integration with the board layer (`CrispStrobe/bw-board`), use the push
API instead of polling:

```js
// These replace polling emu_get_sfr(P1) every cycle:
const getPinMode  = Module.cwrap('emu_get_pin_mode',  'number', ['number','number']);
const getPinDrive = Module.cwrap('emu_get_pin_drive',  'number', ['number','number']);
const setPinInput = Module.cwrap('emu_set_pin_input',  null,     ['number','number','number']);
const setAdcVolt  = Module.cwrap('emu_set_adc_voltage', null,    ['number','number']); // volts, not counts
const advanceTo   = Module.cwrap('emu_advance_to_ns',  'number', ['number','number']);
const setVcc      = Module.cwrap('emu_set_vcc',         null,    ['number']);
```

In native C, register a `stc12_pin_callback` via `stc12_set_board_callbacks()`
to receive `(port, bit, mode, driveHigh)` on every pin change — including mode
register writes, not just port data writes. The board never calls the MCU; it
only answers `readPin` and `readAnalog` when asked. See
[simulation-contract.md](https://github.com/CrispStrobe/sb3-creator/blob/main/reference/simulation-contract.md).

Differential trace
------------------

For cross-checking against the ucsim-stc fork:

```bash
make emu_trace
./emu_trace -fosc 11059200 -cycles 1000000 firmware.hex > trace.tsv
```

Emits one tab-separated event per line: `PC`, `SFR`, `PIN`, `TF` (timer
overflow), `ADC`. Format spec in `spec-updates/001-differential-trace-format.md`.

Tests
-----

| Suite | What it covers |
|-------|----------------|
| `test_stc12` | 12 unit tests: Timer 0/1 in 1T and 12T modes, auto-reload overflow, all four port modes, ADC with both ADRJ settings |
| `test_blink` | Loads `01-blink.hex`, verifies init (AUXR.T0x12=0, TMOD=mode1, P1M0 push-pull), confirms LED toggle after 150 ms |
| `test_adc` | Loads `02-adc.hex`, verifies P1ASF routing + input mode, ADC conversion with known input, blink at correct rate |
| `test_integration` | 52 tests: button input with debounce, potentiometer ADC, 1T/12T synthetic, PCA (counter, compare, PWM, T0 clock), open-drain, ADC edge cases, 13-bit timer, hex loader |
| `test_wasm.mjs` | 14 tests: init, hex load, execution, SFR access, disassembly, boundary A (pin mode/drive, advanceTo, ADC voltage) |

Caveats
-------

- The **ADC register sequence** is from the datasheet (sections 10.x) and is
  self-consistent, but has **not been confirmed on silicon**. The `02-adc`
  test image exists precisely to verify it on a real chip.
- **PCA PWM output** is computed internally but not yet wired to a visible
  port pin.
- **BRT** (independent baud rate timer) is stubbed — the counter runs but
  has no UART baud rate effect.
- **SPI, UART2, watchdog** have SFR storage but no logic.
- **ADC conversion times** (420/280/140/70 clocks) are single-source (datasheet
  §10.5). No independent corroboration found. See `FINDINGS.md` §2.

Spec updates
------------

Changes proposed to the shared peripheral spec live in `spec-updates/` as
standalone documents (not edits to the read-only mirror at
`/mnt/volume1/code/stc/docs/`). Current patches:

- `001-differential-trace-format.md` — trace format for cross-checking
  against ucsim-stc.

See also `FINDINGS.md` for bugs found during implementation that the ucsim
fork should check for.

Licence
-------

MIT. Original emulator core copyright 2006/2022 Jari Komppa. STC12
extensions copyright 2024 CrispStrobe. See [LICENSE](LICENSE) and
[THIRD-PARTY.md](THIRD-PARTY.md).

No code from copyleft sources (ucsim, QEMU, SimulIDE, circuitjs, Verilator)
was used. SDCC (GPL-2+) is a build-time dependency for compiling test images
only and is not linked into the emulator.
