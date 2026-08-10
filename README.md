emu8051-stc
===========

Fork of [jarikomppa/emu8051](https://github.com/jarikomppa/emu8051) (MIT,
copyright 2006/2022 Jari Komppa) with STC12/STC15/STC89 peripheral models,
a debug control interface, UART entry points, and a WASM build for the
browser. All additions are MIT; no copyleft code was used.

This is the **bundleable** 8051 emulator for
[brickwright](https://github.com/CrispStrobe/bw-board) — MIT-licensed,
21 KB + 65 KB gzipped, runs in a browser without a server. The sibling
project [ucsim-stc](https://github.com/CrispStrobe/ucsim-stc) (GPL-2,
part of SDCC) is the accurate oracle; this one is the shippable core.

What is in it
-------------

**5 MCU parts** behind a runtime switch (`emu_set_part`):

| Part | Core | Flash | SRAM | PCA modules |
|------|------|-------|------|-------------|
| STC12C5A60S2 | 1T | 60 KB | 1280 B | 2 |
| STC12C5A16S2 | 1T | 16 KB | 1280 B | 2 |
| STC15F2K60S2 | 1T | 60 KB | 2048 B | 3 |
| STC15W408AS | 1T | 8 KB | 512 B | 0 |
| STC89C52RC | 12T | 8 KB | 512 B | 0 (8052 Timer 2) |

**Peripherals:** Timer 0/1 (1T and 12T via AUXR.7/AUXR.6), port modes
(quasi-bidirectional / push-pull / input-only / open-drain per pin), 10-bit
ADC (8 channels, 4 speed settings, ADRJ justification), PCA (16-bit counter,
8 clock sources, compare/match, 8-bit PWM, capture, toggle), UART1/2 with
TX callback and RX inject, dual DPTR, STC15 Timer 2, STC89 8052 Timer 2.

**Debug control (boundary D):** run / halt / step (5 kinds: instruction,
tick, over, out, ns-target), breakpoints (code address, yield-point, SFR
write), memory access (5 address spaces), register read, PC histogram
profiling, pin history ring buffer, cooperative-scheduler position tracking.

**WASM API:** 75 exports. `createEmu8051()` factory, modularized, no
SharedArrayBuffer required (single-threaded, runs on GitHub Pages).

**AVR adapter** (`avr-adapter/`): boundary-A adapter around
[avr8js](https://github.com/nickolaj/avr8js) (MIT) for ATmega328P,
ATmega168P, ATtiny85. 7/10 conformance tests pass; 3 are honest structural
gaps (AVR lacks quasi-bidirectional and open-drain modes, ADC needs
firmware-driven conversion). AVR compilation to WASM is deferred
(stc d49d0c0); AVR emulation via avr8js is not affected.

**UART entry points** for serial DebugTarget integration: TX callback fires
synchronously on SBUF write, RX inject via `emu_serial_write`. Baud timing
is NOT modelled — bytes are instant. See `docs/UART-ENTRY-POINTS.md` for
the full contract including SFR story and the trap about untimed models.

**SDCC 4.5.0 as WASM** (`.github/workflows/build-sdcc-wasm.yml`): four-stage
pipeline (cc1 preprocess, sdcc --c1mode codegen, sdas8051 assemble, sdld
link) running in MEMFS with no fork/exec. Code generation matches native
SDCC 4.5.0 byte-for-byte; link-layout alignment is in progress (1-byte
origin difference as of 2026-08-10, from library set mismatch). Specified
and partially built, not yet shipping.

What is verified
----------------

**417 test assertions, 0 failures** across 9 native test suites plus WASM
tests. 32 firmware test images.

| Suite | Count | What it covers |
|-------|-------|----------------|
| `test_stc12` | 12 | Timer 0/1 1T/12T, port modes, ADC ADRJ |
| `test_suite` | 136 | Firmware-level: blink, ADC, button, pot, scheduler, PCA, serial |
| `test_integration` | 171 | PCA (counter, compare, PWM, T0 clock, capture), open-drain, ADC edges, 13-bit timer, hex loader |
| `test_debug` | 37 | Boundary D: run/halt/step, breakpoints, memory spaces |
| `test_cycles` | 34 | MCS-51 instruction cycle counts |
| `test_mass` | 39 | 32 firmware images, pass/fail per image |

**Differential trace:** `emu_trace` emits per-event traces for cross-checking
against ucsim-stc. Third-party corpus: 220/349 byte-identical event streams;
86 timing-only (zero instruction disagreements). Format spec in
`spec-updates/001-differential-trace-format.md`.

**PCA interrupt vector (0x3B):** confirmed by servo pulse measurement at
three angles (0°=501 us, 90°=1502 us, 180°=2502 us, all under 0.3% error)
and by datasheet citation (Ch. 6, p. 138). IE.6 is ELVD (LVD enable), not
PCA enable — PCA interrupt is gated by ECCFn in CCAPMn. Evidence category
2b: two emulators agree, no silicon. See `spec-updates/008-stc12-interrupt-vectors.md`.

**Nothing in this repo has run on real silicon.** The ADC register sequence,
PCA timing, and UART model are all from the datasheet and are self-consistent
(category 2b). They have not been confirmed on hardware. The test images
(`test_images/02-adc.hex`) exist precisely to verify on a real chip and have
not been run.

What is NOT done
----------------

- **SDCC WASM byte-identity:** code generation matches native; link layout
  has a 1-byte origin shift (lib/small vs lib/small-stack-auto). Specified,
  partially built.
- **Baud-rate timing:** UART TX and RX are instant. Baud mismatch is
  undetectable in emulation. Idle-timeout resync needs a `-inject` flag in
  the trace harness (specified in `docs/UART-ENTRY-POINTS.md` §9, not built).
- **SPI, watchdog:** SFR storage only, no logic.
- **PCA PWM pin output:** computed internally, not wired to observable port pin.
- **UART mode 0 (synchronous):** not implemented.
- **Framing errors, parity, multi-processor communication:** not modelled.
- **AVR conformance:** 3/10 tests fail (structural gaps, not bugs).
- **Silicon verification:** nothing has been tested on hardware.

Build
-----

### Native (curses TUI)

```bash
sudo apt-get install libncurses5-dev
make
./emu firmware.hex          # upstream 8051
./emu firmware.hex          # STC12 mode via emu_set_part()
```

### WASM (browser / Node)

```bash
source /path/to/emsdk/emsdk_env.sh
make -f Makefile.wasm
# -> build/emu8051.js (21 KB) + build/emu8051.wasm (65 KB)
```

### Tests

```bash
sudo apt-get install sdcc   # for test image compilation
make test-images
make test                   # 417 assertions, 9 suites
```

### Differential trace

```bash
make emu_trace
./emu_trace -fosc 11059200 -part stc12 -until-ns 200000000 firmware.hex
```

Parts: `stc12`, `stc15`, `stc89`, `stc15w`. Flags: `-adc CH,VAL`,
`-bp ADDR`, `-read SPACE,ADDR,LEN`, `-write SPACE,ADDR,VAL`.

WASM API (quick start)
----------------------

```js
import createEmu8051 from './build/emu8051.js';
const Module = await createEmu8051();

const init    = Module.cwrap('emu_init',    null,     ['number']);
const run     = Module.cwrap('emu_run',     'number', ['number']);
const loadHex = Module.cwrap('emu_load_hex','number', ['string','number']);
const getSfr  = Module.cwrap('emu_get_sfr', 'number', ['number']);

init(1);  // 1 = STC12
loadHex(hexString, hexString.length);
run(1000000);
console.log('P1:', getSfr(0x90).toString(16));
```

75 exports total. See `Makefile.wasm` for the full list. Boundary A
(pin bus), boundary D (debug control), UART, and pin history are all
available. See `docs/UART-ENTRY-POINTS.md` for serial integration.

Licence
-------

MIT. Original emulator core copyright 2006/2022 Jari Komppa. STC12/15/89
extensions copyright 2024-2026 CrispStrobe. See [LICENSE](LICENSE) and
[THIRD-PARTY.md](THIRD-PARTY.md).

No code from copyleft-licensed emulators or simulators was used. SDCC
(GPL-2+) is a build-time dependency for test images and for the WASM
compiler pipeline; it is not linked into the emulator.
See [THIRD-PARTY.md](THIRD-PARTY.md) for the full dependency list.
