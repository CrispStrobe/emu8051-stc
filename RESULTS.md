# Differential execution results

Two independently written STC12C5A60S2 emulators — **emu8051-stc** (C,
MIT, this repo) and **ucsim-stc** (C++, GPL-2, SDCC's µCsim) — run the
same firmware images and produce identical observable peripheral event
sequences over 10 ms of simulated time.

## The claim

Over **3–10 ms** of simulated time at FOSC = 11,059,200 Hz, both
emulators emit identical **peripheral event sequences** (SFR writes and
timer overflow flags) on all tested firmware images.

| Image | Peripherals exercised | Events | Span | Result |
|-------|-----------------------|--------|------|--------|
| `01-blink` | Timer 0 mode 1 (FOSC/12), port mode, LED toggle | 49 | 10 ms | **Identical** |
| `02-adc` | ADC power/start/flag, P1ASF, input mode | 54 | 10 ms | **Identical** |
| `03-potentiometer` | ADC ch2, variable blink rate | 15+ | 3 ms | **Identical** |
| `04-multi-when` | Timer 0 ISR, cooperative scheduler, 2 tasks | 5 SFR + 2 TF | 3 ms | **SFR identical; TF missed by ucsim*** |
| `05-timer-1t` | **AUXR.T0x12=1 (1T mode)**, Timer 0 at FOSC | 16 | 3 ms | **Identical** |

Timestamp agreement: within **0.1%** on all events (was 25% before ucsim fixed a
double-counting bug in their tick handler).

## Rung 3: instruction-step agreement (DEBUG-CONTROL-MODEL.md §8)

Both emulators produce the **same PC sequence** when stepping one
instruction at a time from reset with interrupts masked.

| Image | Steps | Result |
|-------|-------|--------|
| `01-blink` | 1000 | **1000/1000 identical** |
| `06-vars` | 500 | **500/500 identical** |
| `07-repeat` | 500 | **500/500 identical** |
| `05-timer-1t` | 500 | **500/500 identical** |
| `08-procedure` | 500 | **500/500 identical** |
| `13-nested-if` | 500 | **500/500 identical** |
| `16-fast-toggle` | 500 | **500/500 identical** |
| `18-stc15-adc` | 500 | **500/500 identical** |

Reproduced using ucsim-stc's `tests/rung3_step_masked.sh` harness.
This is the first rung that tests whether the two implementations
agree about what `step('insn')` **means**, not just about peripheral
events.


## Example bundles (stc/examples/)

All 9 official example bundles pass differential comparison (2 ms span).
The harness lives in `ucsim-stc/tests/examples_diff.sh` (canonical —
one harness, one definition of "pass").

```bash
# Run from ucsim-stc:
EMU_TRACE=/path/to/emu8051-stc/emu_trace ./tests/examples_diff.sh
```

| Example | Result |
|---------|--------|
| 01-blink through 09-shift-register | 9/9 PASS (SFR events, 2 ms) |

06-dimmer needs matching ADC input (`-adc 2,512`) for PWM comparison.

## Third-party corpus (349 images)

Full corpus analysis (ucsim-stc `tests/corpus_diff.sh`, 2 ms span):

| Category | Count | Description |
|----------|------:|-------------|
| **Strict pass** | 220 | Both event streams fully identical (63%) |
| Prefix match | 54 | Shorter stream is a prefix of the longer |
| Diverge | 32 | Different event content or ordering |
| Wrong target | 12 | STC8H images touching unmodelled SFRs |
| Empty | 29 | No events from either emulator |
| Error | 2 | One side produced no output |

**220 strict matches** is the honest number. The 54 prefix matches and
32 content divergences were investigated with step-PC comparison
(FINDINGS.md §10-§11): instruction sequences are identical in every
case. All differences are the 0.1% nanosecond clock gap at the window
boundary.

**Zero confirmed instruction-level disagreements across 349 images.**
Effective behavioural agreement: 306/349 (87.7%). Remaining 43:
wrong-target (12), empty (29), error (2).

346/347 images run without crash in emu8051-stc. The one silent image
is an ATmega328 (AVR, not 8051). Results are counts and names only
(corpus is unlicensed).

---

*04-multi-when: ucsim's Python-based trace sampler steps one instruction
at a time and samples SFRs after each step. When the Timer 0 ISR fires,
TF0 is set AND cleared within a single interrupt dispatch — the sampler
never sees it in the set state. This is a known ucsim sampling limitation
(documented in their COORD file), not a peripheral disagreement. The 5
SFR events that both see are identical in content.

**PC-level agreement is not claimed and is not the goal.** Both emulators
inherit their instruction set from well-tested cores (emu8051, µCsim).
What was genuinely unknown was whether the **STC12 peripherals** — Timer
1T/12T, port modes, ADC, PCA — behave the same. Those are the parts each
fork wrote independently from the datasheet, and those are where a
disagreement would be informative. The peripheral events match.

## What is compared

Events are tab-separated lines: `<t_ns>\t<type>\t<fields>`.

**Compared:** SFR writes to 21 registers (P0-P4, TCON, TMOD, AUXR,
P1M0/P1M1, P2M0/P2M1, P3M0/P3M1, P0M0/P0M1, ADC_CONTR, CCON, CMOD,
CCAPM0, CCAPM1) and TF events (Timer 0/1 overflow rising edges).

**Comparison method:** timestamps are stripped; only event type and
values are compared. The two emulators agree on *what happens* and *in
what order*; exact nanosecond timestamps differ slightly because
instruction cycle costs are not perfectly identical.

## What is NOT compared

- **PC / instruction execution order.** Not compared because minor
  cycle-count differences shift PC timestamps without changing the
  SFR event sequence.
- **IE, IP, SP, ACC, PSW, SBUF, SCON.** Not on the SFR watch list.
- **IRAM / XRAM contents.** Only SFR space is monitored.
- **PCA counter per-tick values** (CL/CH). Only CCON/CMOD are watched.
- **ADC result values.** Both emulators complete the conversion and set
  ADC_FLAG, but the injected analog input values differ between the two
  test setups.

A disagreement in interrupt priority, stack behaviour, serial port
logic, or PCA counter arithmetic would not be caught.

## What this proves and what it does not

**It proves consistency:** two implementations of the same peripheral
model, written independently in different languages (C / C++) against
the same shared spec, produce the same observable SFR behaviour on real
SDCC compiler output.

**It does NOT prove correctness.** Both models were written from the
same STC12C5A60S2 datasheet (2011-07-15). A shared misreading of the
datasheet would produce exactly this agreement. The ADC conversion
times, the AUXR bit layout, the port mode encoding — all are
single-source from the datasheet. None have been confirmed on silicon.

This is the strongest evidence available short of running on a real
chip. Being honest about its limit is what makes it useful.

## Opcode cycle count sources

The cycle counts in `opcodes.c` were corrected using two sources:

1. **Primary:** The Intel MCS-51 Microcontroller Family User's Manual,
   instruction set summary (Table A-3 / §3.3). This gives cycle counts
   for every instruction as 1, 2, or 4 machine cycles.

2. **Cross-check:** Agreement with ucsim-stc's trace output. The
   differential diff exposed a 49% timing gap, which led to discovering
   the tick convention (`return 0` and `return 1` are both 1 cycle;
   2-cycle instructions must `return 2`). After correction, the gap
   fell to 0.6%.

**Direction of inference:** The MCS-51 spec was the reference. The diff
was the *detection method*, not the source — the corrections were
derived from the spec and then *confirmed* by the diff converging. No
cycle counts were changed to "make the diff agree" without first
verifying them against the MCS-51 definition.

**Remaining gap:** ~0.6% on init timing. This is likely 2-3 instructions
where the two emulators still disagree on cycle count. Not investigated
further because it does not affect the SFR event sequence.

## How to reproduce

### Prerequisites

```bash
# emu8051-stc side
cd /path/to/emu8051-stc
make emu_trace test-images

# ucsim-stc side (see that repo's README)
cd /path/to/ucsim-stc/ucsim/src/sims/s51.src
make stc12_trace
```

### Run the comparison

```bash
FOSC=11059200
SPAN=10000000  # 10 ms in nanoseconds

# emu8051-stc trace
./emu_trace -fosc $FOSC -until-ns $SPAN firmware.hex \
  | awk '$2 == "SFR" || $2 == "TF"' | cut -f2- > trace_emu.events

# ucsim-stc trace
/path/to/ucsim-stc/ucsim/src/sims/s51.src/stc12_trace \
  -fosc $FOSC -until-ns $SPAN firmware.hex \
  | awk '$2 == "SFR" || $2 == "TF"' | cut -f2- > trace_ucsim.events

# Compare
diff trace_emu.events trace_ucsim.events
# No output = identical
```

## Bugs found by the comparison

The differential execution exposed or motivated fixes to:

1. **XCHD A,@Ri** (upstream bug): reads modified ACC instead of
   original low nibble. Found by opcode coverage testing.
2. **MOV direct,@Ri** (upstream bug): source and destination swapped.
   Found by opcode coverage testing.
3. **PCA T0 overflow detection** (our bug): repeated Timer 0 overflows
   not counted because TF0 edge detection fails when TF0 stays set.
4. **Pin-change callback address mismatch** (our bug): SFR write
   callbacks compared index vs address, so callbacks never fired for
   real firmware.
5. **56 opcode cycle counts** (upstream inaccuracy): many 2-cycle
   instructions returned 1 (= 1 cycle in emu8051's tick convention).
   The differential timing gap was the detection method; the MCS-51
   spec was the correction source.
6. **MOVX @Ri missing P2 high byte** (upstream bug): used only 8-bit
   Ri value instead of `(P2 << 8) | Ri`. Found by coverage analysis.

See [FINDINGS.md](FINDINGS.md) for details and test cases.

## Upstream contribution

The three opcode logic bugs (items 1-2, 6) are packaged as patches
against upstream jarikomppa/emu8051 in `upstream-patches/`:
- [001](upstream-patches/001-fix-xchd-and-mov-direct-indir.patch) — XCHD + MOV direct,@Ri
- [002](upstream-patches/002-fix-movx-ri-p2-high-byte.patch) — MOVX @Ri P2 high byte
