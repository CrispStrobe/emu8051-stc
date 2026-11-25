# bw8051_1: STC15 Timer 0 and part qualification

This change prepares the emulator for selected STC15lib and ElectronicHourGlassKit
guest firmware. It uses the manufacturer's register semantics as the expected
result. A passing HAL program is integration evidence, not a substitute for the
chip specification.

## Corrections and primary sources

The [STC15 series manual](https://stcmicro.com/datasheet/STC15F2K60S2-en.pdf),
printed page 441, section 7.2.1, defines Timer 0 mode 0 as **16-bit auto-reload**.
The earlier model applied the classic 8051's 13-bit mode to STC15 as well.

| Event | STC15 Timer 0 mode 0 behavior |
|---|---|
| Write TH0/TL0 with TR0 clear | Update the readable counter and hidden reload latch |
| Write TH0/TL0 with TR0 set | Update the hidden reload latch; leave the counter running |
| Counter overflows | Set TF0 and reload both bytes from the latch |
| AUXR.T0x12 = 1 / 0 | Count once per oscillator clock / once per 12 clocks |
| GATE = 1 | Count only while INT0 is high and TR0 is set |

The implementation is shared by `PART_STC15` (1) and `PART_STC15W` (3).
Classic mode 0 on STC12, STC12_16, and STC89 remains 13-bit. STC15 mode 1
continues to be a plain 16-bit counter. Mode 0 with external counter input is
outside this qualification.

The [W408AS product page](https://stcmicro.com/stc/stc15w408as.html) specifies
three PCA/CCP/PWM modules, one UART, two main timers (T0/T2), one DPTR, 8 KB
flash, 512 bytes SRAM, and 5 KB EEPROM. The corrected native part reflects this
identity. [Spec update 007](../spec-updates/007-stc15w408as-delta.md) records the
part delta and rejects the earlier no-PCA / two-UART / Timer-1 assumptions.

The same manual's P_SW1 table on printed page 692 supplies the PCA pin groups:

| P_SW1.CCP_S | CCP0 | CCP1 | CCP2 |
|---|---|---|---|
| 0 | P1.1 | P1.0 | P3.7 |
| 1 | P3.5 | P3.6 | P3.7 |
| 2 | P2.5 | P2.6 | P2.7 |

Group 3 is reserved and not qualified. These routes now apply to PWM, toggle,
and capture on both STC15 parts. STC12's default P1.3/P1.4 route is preserved.
Third-module capture now sets CCF2 instead of CCF1.

The ADC_RES/ADC_RESL table on printed page 664 defines ADRJ packing. STC15
selects ADRJ with `CLK_DIV.5`; W408AS previously took the STC12 `AUXR1.2` path.
For a 10-bit result `n`, ADRJ=0 stores `n >> 2` in ADC_RES and `n & 3` in
ADC_RESL. ADRJ=1 stores `n >> 8` in ADC_RES and `n & 255` in ADC_RESL.
The P1ASF register and initialization requirement appear on printed pages
662–663: firmware must enable the selected channel at 0x9D before converting.

SPI callback binding now selects the part's register map. STC12's SPSTAT at
0xCE must not intercept STC15's SPCTL at the same address. W408AS shares the
STC15 SPSTAT/SPCTL/SPDAT addresses 0xCD/0xCE/0xCF. SPI remains a flag model.

## Rewind contract

The visible TH0/TL0 pair cannot reconstruct the next reload after a running
write. The checkpoint now stores two additional little-endian uint16 fields,
`timer0_reload` and `timer0_counter`, after the UART receive state.

| Property | This layout |
|---|---|
| Major version | 1 |
| Build fingerprint | `0x80510103` |
| Exact byte count | `443561` |
| Payload change from `0x80510102` | 4 additional bytes |

Old layouts are rejected before live state changes. Consumers must query the
API size and match this build fingerprint. Native and WASM tests save while
the visible counter and next reload differ, then prove the same overflow
sequence after restore. This catches a snapshot that saves only the visible
registers.

`emu_set_sfr` remains a raw debugger-memory operation. It does not execute
firmware SFR callbacks. Tests of reload-buffer writes therefore execute real
MOV opcodes in the exact WASM artifact as well as testing the native callback
path. Debugger edits of TH0/TL0 are not a way to configure the hidden latch.

## Reset integration

A real Brickwright debug-runner test found that starting an already-configured
12MHz firmware target called debug reset and silently restored the native core's
11.0592MHz default. The adapter still advertised 12MHz. Three NOPs therefore took
271ns in the core instead of approximately 250ns. Reset also discarded configured
Vcc and serial callback bindings.

Hardware and debugger reset now call `stc12_reset`, which restarts volatile state
while retaining oscillator, supply, part, and external board/serial connections.
`stc12_init` keeps its existing fresh-target defaults. Native tests exercise four
resets per STC12, STC15F2K60S2, W408AS, and STC89 target. Exact-WASM tests measure
three NOPs at 12MHz and an actual 1.65V ADC conversion against a 3.3V reference,
then verify digital input, GPIO, analog, advance, and serial callback delivery
without re-registering them. They also distinguish cold code-memory wipe from
warm/debug reset and check fresh initialization's 11.0592MHz / 5V defaults.

Callback owners must unregister a binding before freeing its Emscripten function
table entry. The old smoke-test serial teardown relied on reset clearing a stale
pointer; it now unregisters explicitly, as the board callback teardown already did.

## Regression gates

Run these from this repository:

```sh
make test_stc15_fidelity test_reset test_checkpoint test_stc12 test_stc89_timers
./test_stc15_fidelity
./test_reset
./test_checkpoint
./test_stc12
./test_stc89_timers
node test_stc15_fidelity.mjs
node test_reset.mjs
node test_wasm.mjs
node check-exports.mjs "$PWD/build/emu8051.js"
```

`test_stc15_fidelity.c` checks independently calculated 4-, 48-, and
264-clock periods; repeat reloads; writes during counting and while stopped;
gating; classic-part mode preservation; W408AS resources; ADC packing; SPI
flags; and all nine documented PCA routes on both STC15 parts.

`test_stc15_fidelity.mjs` imports the actual `build/emu8051.js` / `.wasm` pair.
It executes firmware reload writes, checks repeated periods and rewind,
rejects old checkpoint layouts, inspects part capabilities, and observes all
three default PCA outputs. This test also runs after a source rebuild in CI.
CI pins Emscripten 6.0.11 and requires the rebuilt JS/WASM bytes to match the
committed files. `build/BUILD-INFO.md` identifies the source revision and the
SHA-256 of both deliverables.

The checkpoint proof harness uses the same `0x80510103` / 443561-byte layout,
including both hidden Timer0 fields in its independent native-state observation.
Its ABI smoke, phase census, continuation/refusal proof, receipt check, and five
allocation-refusal cases remain required; see
[`tools/CHECKPOINT-PROOF.md`](../tools/CHECKPOINT-PROOF.md). The focused STC15
tests above additionally prove rewind while the counter and next reload differ.

The existing instruction, debugger, integration, and STC89 timing suites
remain regression gates. Guest programs and board/circuit integration belong
to the `bw-board` bw8051_1 fixture suite; they consume an exact emulator commit.

## Features still outside this qualification

| Feature | Current behavior / boundary |
|---|---|
| STC15 Timer 2 | Counter advances using the correct family register map; hidden auto-reload and interrupt delivery are not implemented |
| STC89 Timer 2 | Counter/reload behavior exists; its interrupt vector is not delivered, so an original Timer-2-IRQ scheduler is not qualified |
| STC15 Timer 1 mode 0 and Timer 0 mode 3 | Family-specific semantics are not implemented by this change |
| External timer counters | External edge counting is not implemented |
| ADC | Results, flags, and polling work; conversion delay still uses the STC12 420/280/140/70-clock table instead of STC15's 540/360/180/90 clocks (manual p. 663); ADC interrupt dispatch is absent |
| PCA | Documented pin groups and 8-bit PWM/compare/capture are exercised; ECI clocking, CIDL idle gating, reserved routes, and extended-resolution PWM are not qualified |
| UART | TX is immediate; UART1 queued RX uses fixed 9600-baud character spacing, not configured baud-generator timing |
| SPI | Enabled writes complete immediately and set SPIF; there is no pin-level shifting or peer exchange |
| Watchdog | Feed/count/flag behavior exists; expiration does not reset the CPU |
| IAP/EEPROM, comparator, wake-up timer | No functional models |
| CLK_DIV | ADRJ is honored; CPU/peripheral clock division is not modeled |
| Package bond-out and memory bounds | Resource sizes are reported, but package-specific pin availability and strict part-sized memory bounds are not enforced |

These limitations are why the selected guests use Timer 0 and polling ADC,
and why EEPROM persistence, ADC IRQs, and complete original scheduler/clock
firmware require separate work. No new hardware measurement is claimed.
