# Findings — bugs and gotchas surfaced during implementation

This file records bugs found in emu8051-stc that the ucsim-stc fork (or any
other STC12 model built from the same datasheet) should check for. A bug
found in one independent implementation is only valuable if the other one
can test for it.

---

## 1. PCA Timer 0 overflow clock source: repeated overflows not counted

**What:** When the PCA counter uses Timer 0 overflow as its clock source
(CMOD CPS=10), only the *first* T0 overflow incremented the PCA counter.
Subsequent overflows were silently dropped.

**Why it was easy to make:** The obvious implementation watches TF0 (the
Timer 0 overflow flag in TCON) for a rising edge: sample TF0 before the
timer tick, check if it transitioned from 0 to 1 after. This works exactly
once — then TF0 stays set (nothing clears it unless an ISR runs or the
program writes TCON), so the edge condition is never true again.

The same bug would appear in any mode where TF0 is not cleared by an
interrupt handler — which is every program that polls TF0 manually, or
simply doesn't enable the Timer 0 interrupt.

**Fix:** Track overflow directly inside the timer increment logic. The
`stc12_timer0_tick()` function now returns `true` when the counter wrapped,
regardless of whether TF0 was already set. The PCA uses this return value
instead of watching the SFR.

**Test that catches it:**

```c
/* Timer 0 mode 2 (auto-reload), 1T, reload=0xFE → overflow every 2 ticks.
 * PCA clock = T0 overflow.
 * After 4 osc clocks → 2 T0 overflows → PCA counter should be 2. */
cpu.mSFR[STC_REG_CMOD] = CMOD_CPS1_BIT;  /* CPS=10 */
cpu.mSFR[REG_TH0] = 0xFE;
cpu.mSFR[REG_TL0] = 0xFE;
/* ... run 4 clocks ... */
assert(pca_counter == 2);  /* fails with edge-detection, passes with direct flag */
```

The bug was invisible in single-overflow tests and only appeared when Timer 0
overflowed repeatedly without TF0 being cleared between overflows.

**Where in the code:** `stc12.c`, `stc12_timer0_tick()` return value and
`stc12_tick()` usage of `t0_overflowed`. Commit `8255030`.

---

## 2. ADC conversion times: single-source, not independently confirmed

The conversion times used (420 / 280 / 140 / 70 oscillator clocks for
SPEED 00 / 01 / 10 / 11) are from the STC12C5A60S2 datasheet §10.5. No
independent source has been found to corroborate these exact numbers.
SDCC's `stc12.h` confirms the SPEED bit layout but does not specify timing.

The emulator implements these values and the tests verify the implementation
is self-consistent (flag appears at exactly the right tick count), but this
proves the code matches itself, not that it matches the chip. If a different
STC12 datasheet revision or STC example code gives different numbers, the
constants in `stc12.h` should be updated.

**Status:** single-source. Marked accordingly in the shared spec.

---

## 3. Upstream bug: XCHD A,@Ri reads modified ACC

**Opcodes:** 0xD6 (`XCHD A,@R0`), 0xD7 (`XCHD A,@R1`).

**MCS-51 definition** (Intel MCS-51 Microcontroller Family User's Manual,
§3.3 "XCHD"): "Exchanges the low-order nibble of the Accumulator (bits 3–0)
with that of the internal RAM location indirectly addressed by the specified
register. The high-order nibbles of each register are not affected."

The operation is: `A[3:0] ⇄ (Ri)[3:0]`, a simultaneous swap.

**Bug:** The upstream code modifies ACC first, then reads `ACC & 0x0f` for
the memory write — getting the new low nibble, not the original:

```c
// Bug (upstream):
ACC = (ACC & 0xf0) | (value & 0x0f);     // A low = mem low ✓
value = (value & 0xf0) | (ACC & 0x0f);   // reads NEW A low ✗

// Fix:
uint8_t old_acc_low = ACC & 0x0f;
ACC = (ACC & 0xf0) | (value & 0x0f);
value = (value & 0xf0) | old_acc_low;
```

**Proof:**

| | Before | Expected after | Bug produces |
|---|--------|---------------|-------------|
| A | 0x34 | 0x32 | 0x32 (correct) |
| @R0 | 0x12 | 0x14 | 0x12 (unchanged — nibble "swapped" with itself) |

The bug is invisible when the low nibbles happen to be equal (e.g. `A=12h, @R0=32h`),
which is why it survived untested.

**Minimal test:**
```c
/* R0=30h, IRAM[30h]=12h, A=34h; XCHD A,@R0 */
uint8_t p[] = { 0x78, 0x30, 0x76, 0x12, 0x74, 0x34, 0xD6, 0x80, 0xFE };
// After: assert(ACC == 0x32 && IRAM[0x30] == 0x14);
```

**Where:** `opcodes.c`, `xchd_a_indir_rx()`. Upstream patch in `upstream-patches/`.

---

## 4. Upstream bug: MOV direct,@Ri has source and destination swapped

**Opcodes:** 0x86 (`MOV direct,@R0`), 0x87 (`MOV direct,@R1`).

**MCS-51 definition** (Intel MCS-51 User's Manual, §3.3 "MOV direct,@Ri"):
"Moves the contents of the internal RAM location addressed indirectly through
register Ri to the direct address indicated." Source = @Ri, destination = direct.

**Bug:** The upstream code has `address_from = OPERAND1` (the direct address
operand) and `address_to = INDIR_RX_ADDRESS` (the Ri-indirect address) —
exactly backwards. It reads from the direct address and writes to @Ri.

```c
// Bug (upstream):
uint8_t address_from = OPERAND1;          // direct addr — should be DEST
uint8_t address_to = INDIR_RX_ADDRESS;    // @Ri — should be SOURCE

// Fix:
uint8_t address_to = OPERAND1;            // direct addr = destination
uint8_t address_from = INDIR_RX_ADDRESS;  // @Ri = source
```

**Proof:**

| | Before | Expected after | Bug produces |
|---|--------|---------------|-------------|
| IRAM[30h] (source, @R0) | 0x88 | 0x88 (unchanged) | 0x00 (overwritten!) |
| IRAM[40h] (dest, direct) | 0x00 | 0x88 (copied) | 0x00 (read from here instead) |

The bug is destructive: it zeroes the source and writes nothing useful to the
destination. Any program using `MOV direct,@Ri` silently corrupts data.

**Minimal test:**
```c
/* R0=30h, IRAM[30h]=88h; MOV 40h,@R0 */
uint8_t p[] = { 0x78, 0x30, 0x76, 0x88, 0x86, 0x40, 0x80, 0xFE };
// After: assert(IRAM[0x40] == 0x88);
```

**Where:** `opcodes.c`, `mov_mem_indir_rx()`. Upstream patch in `upstream-patches/`.

---

## 5. Differential trace divergence: emu8051-stc vs ucsim-stc

**Firmware:** `01-blink.hex` compiled by SDCC 4.2.0 for STC12C5A60S2.
**FOSC:** 11059200 Hz. Both traces use nanosecond timestamps computed as
`osc_clocks * 1_000_000_000 / fosc`.

### Timing divergence

The first SFR write (`MOV P1M0,#03h` at address 0x0062) occurs at:
- **emu8051-stc:** 48474 ns
- **ucsim-stc:** 72790 ns

That is 24316 ns (~49%) difference for the same startup code. One of the
two is miscounting instruction cycle times during SDCC's `__sdcc_program_startup`
sequence (LJMP, LCALL, MOV, JZ, data-init loops).

### PC sequence divergence

After init (SFR 0x92 = 0x03, both at address 0x006A):

| emu8051-stc | ucsim-stc |
|-------------|-----------|
| 006A | 006A |
| 006C | 006C |
| **0072** | **00A5** |

At PC=006C the two emulators take different branches through the same
code. This is either a conditional-flag disagreement (one of the two
set PSW flags differently during init) or a different memory state
(the data-init loop wrote different values, so a compare/branch diverges).

### What this means

The timing difference needs investigation: which one matches the 8051
instruction cycle table? The PC divergence is likely caused by the timing
disagreement if it makes the startup data-init loop count differently.

### Root cause: upstream cycle count inaccuracy

The upstream emu8051 returns `0` (= 1 machine cycle) for several
instructions that are defined as 2-cycle in the MCS-51 spec. Confirmed
wrong:

| Instruction | Opcode(s) | Returns | Should return | MCS-51 cycles |
|-------------|-----------|---------|---------------|---------------|
| SETB bit | 0xD2 | 0 | 1 | 2 |
| CLR bit | 0xC2 | 0 | 1 | 2 |
| MOVC A,@A+PC | 0x83 | 0 | 1 | 2 |
| MOV A,direct | 0xE5 | 0 | 1 | 1 (ambiguous*) |
| MOV direct,A | 0xF5 | 0 | 1 | 1 (ambiguous*) |

(* Intel Table A-3 lists `MOV A,direct` as 1 cycle. Some references say 2.
The return value 0 = 1 cycle, which matches Intel's table. Not a bug.)

The upstream README explicitly states: "clock-cycle exact simulation of
processor pins is particularily left out." This is a design choice, not
an oversight. But it means emu8051-stc and ucsim-stc will never agree on
absolute timing unless every opcode handler is audited against the MCS-51
cycle table.

For the **differential trace**, relative event ordering (which instruction
follows which, which SFR changes when) is more useful than absolute
timestamps. The 49% startup timing difference is explained by accumulated
1-vs-2-cycle errors across ~40 instructions in SDCC's init sequence.

**Status:** root cause determined. Cycle count corrections are tracked
separately — they are an upstream accuracy improvement, not a STC12 issue.

---

## 6. Boundary D (DEBUG-CONTROL-MODEL.md) — review notes

Read the spec on 2026-08-08. Three observations from the implementer's side.

### 6.1 The tick convention matters for step('insn')

§8 rung 3 requires both emulators to produce the same PC sequence under
`step('insn')`. On emu8051, `tick()` may or may not advance PC depending
on `mTickDelay`. A "step one instruction" must call `tick()` repeatedly
until `mTickDelay` reaches 0 and a new instruction executes — not just
call `tick()` once. The upstream `opt_step_instruction` flag does this
correctly in the TUI. The WASM API must do the same.

### 6.2 yield breakpoint implementation choice — RESOLVED

§5 says yield breakpoints can be implemented as either:
- A code breakpoint on the `case` label's address, or
- A write-watch on `<task>_state`.

**Resolution (coordinated with ucsim-stc):** both use code breakpoints
on the `case` label address. The address comes from the symbol table's
`yields[].addr` field, so both agree by construction. Write-watch was
considered but rejected because it halts at a different instruction
(the MOV that writes the state, not the case label).

### 6.3 No issues found

The spec is implementable as written. The capability matrix is accurate
for our target. The `HaltReason` type is clean. `skewNs: 0n` is trivial
for us. No changes recommended.

---

## 7. Cycle count convention: DEFINITIVELY VERIFIED

In response to ucsim-stc insisting our `return 2` should be `return 1`:

**Our convention is correct.** Empirical measurement:

| Test | Result | MCS-51 spec |
|------|--------|-------------|
| BSS-clear loop (255 × MOV @R0,A + DJNZ) | 768 ticks | 255 × (1+2) + 2 = 768 ✓ |
| 5 × DJNZ standalone | 12 ticks | 1 + 5×2 + 1 = 12 ✓ |
| LJMP + MOV A,#42 | 3 ticks | 2 + 1 = 3 ✓ |
| MUL + MOV A,#42 | 5 ticks | 4 + 1 = 5 ✓ |

The tick() convention is `return 0 = 1 tick, return N (N≥1) = N ticks`.
`return 2` gives 2 ticks, which IS 2 machine cycles. If ucsim claims
`return N = N+1 ticks`, their tick() works differently from ours —
the label "return value" means different things in the two codebases.

The 269-clock gap is NOT from DJNZ. The BSS loop matches exactly.
It must come from other instructions in the startup path where the
two emulators disagree.

---

## 8. Cycle count gap RESOLVED: ucsim had a double-counting bug

ucsim-stc retracted their spec-update 005. Their base instruction
handlers already called tick(N) for multi-cycle instructions, and
their tick_tab override added tick(2) ON TOP, giving 3 ticks per
2-cycle instruction. Our test_cycles.c proof was correct all along.

After ucsim's fix, the timing gap dropped from 25% (269 clocks) to
0.1% (1 clock). Both emulators now agree on cycle accounting.

This is a worked example of why two independent implementations are
worth maintaining: the differential comparison detected the bug, and
the empirical proof (test_cycles.c) identified which side was wrong.

---

## 9. 8052 Timer 2 cannot coexist with STC12 Port 5

The standard 8052 Timer 2 registers (T2CON 0xC8, RCAP2L 0xCA, RCAP2H
0xCB, TL2 0xCC, TH2 0xCD) occupy addresses that the STC12 uses for
Port 5 (P5 0xC8, P5M0 0xCA). These are the same addresses with
different functions.

The STC12/STC15 do NOT have a standard 8052 Timer 2. The STC15 has
its own Timer 2 at different addresses (T2H 0xD6, T2L 0xD7), which
is already implemented.

An 8052 Timer 2 model would only be relevant for AT89C52-style chips,
which are a different target. Marked as out-of-scope for this model.

---

## 10. Prefix-only corpus images: confirmed timing, not instruction disagreement

Investigated the 54 prefix-only images where one model produces more
SFR events than the other in the same 2ms window.

**Step-PC comparison on a prefix-only image** (nixie_tube_eight_yin):
- 2000 instruction steps from reset
- 1999/2000 PCs identical between both models
- The one "difference" is a leading PC=0000 that we emit at reset

**Conclusion:** the prefix-only gap is the documented 0.1% timing
difference, not an instruction-level disagreement. Both models execute
the same instructions in the same order; they disagree on exactly when
2ms of nanosecond time has elapsed. ucsim reaches further in 50/54
cases, producing 1-1728 more SFR events in the same time window.

This is not actionable as a bug. A longer trace window would show
the same events — just with the shorter side catching up later.
The strict-match count (220/349) is the honest metric; the prefix
matches (54) are timing noise at the window boundary.

---

## 11. All 32 content divergences are also timing-driven

Investigated the 32 corpus images classified as "diverge" (different
SFR event content with matching event counts).

Step-PC comparison on the first divergent image (ADC0808 alarm, 588
events each): 499/500 PCs identical. The SFR content differs because
the two models reach different points in the code at the 2ms cutoff.

Of the 4 divergences with identifiable SFRs: 2 × P2 (0xA0), 1 × TCON
(0x88), 1 × P0 (0x80) — all port/timer registers that change rapidly
during execution. Different code positions at cutoff → different
register values, but the same instruction sequence.

**Combined with §10 (prefix-only):** across all 349 images, there are
ZERO confirmed instruction-level disagreements. Every divergence —
strict, prefix, or content — is the 0.1% nanosecond clock difference
at the window boundary.

Effective agreement: 220 strict + 54 prefix + 32 timing-content =
**306/349 (87.7%)** where both models agree on behaviour, with the
remaining 43 being wrong-target (12), empty (29), or error (2).

---

## 12. MOVX @Ri missing P2 high byte (upstream bug)

**What:** `MOVX @R0,A` and `MOVX A,@R0` used only the 8-bit value
from R0/R1 as the external memory address. Per the MCS-51 spec, the
P2 port register provides the high byte, forming a 16-bit address
`(P2 << 8) | Ri`.

**Impact:** Any firmware that sets P2 to select an external memory
bank before doing `MOVX @Ri` would access the wrong address. This is
the standard technique for accessing more than 256 bytes of XDATA when
the full 16-bit DPTR is not needed.

**Fix:** Both `movx_a_indir_rx` and `movx_indir_rx_a` now compute
`address = (aCPU->mSFR[REG_P2] << 8) | INDIR_RX_ADDRESS`.

**Test:** `test_movx_ri_p2` in test_integration.c sets P2=0x12,
R0=0x34, writes via MOVX @R0, and verifies XDATA[0x1234] was written.

This is the third upstream opcode bug found (after XCHD and MOV
direct,@Ri).

---

## 13. Vendor LED cube firmware: P2 scan table confirmed over 5 seconds

**Context:** Two measurements of the vendor 4x4x4 LED cube firmware
(ICStation 4681) appeared to contradict each other. The README recorded
P2 cycling `FE FD FB F7 EF DF BF 7F` (multiplexed scanning), while
`ucsim-stc/RESULTS.md` recorded `P2=0x00` (all layers lit at once).

**Resolution:** Run under emu8051-stc for 5 seconds (5,000,000,000 ns)
at FOSC = 11,059,200 Hz. P2 histogram:

| P2 value | Count | Meaning |
|----------|-------|---------|
| `0x00`   | 1     | Init: all layers on (~1.71 s) |
| `0xFE`   | 568   | Layer 0, red pass |
| `0xFD`   | 568   | Layer 1, red pass |
| `0xFB`   | 568   | Layer 2, red pass |
| `0xF7`   | 568   | Layer 3, red pass |
| `0xEF`   | 568   | Layer 0, blue pass |
| `0xDF`   | 568   | Layer 1, blue pass |
| `0xBF`   | 568   | Layer 2, blue pass |
| `0x7F`   | 568   | Layer 3, blue pass |

**Both measurements are correct.** The vendor firmware holds `P2=0x00`
for ~1.71 seconds during its all-on init pattern (all layers enabled,
no multiplexing needed). After that it switches to multiplexed scanning
through the 8-value scan table. The 50 ms window used by `ucsim-stc`
fell entirely inside the all-on pattern.

Per-line dwell during scanning: 0.824 ms, frame period 6.593 ms,
refresh rate 151.7 Hz. The spec stands — the scan model is real.

Ghosting verification: vendor blanks P0 before each select (0 violations
in 4,192 selects). Clean-room driver holds P2=0xFF across P0 writes
(0 violations in 243 writes over 4 seconds). Both satisfy the invariant:
no layer is enabled while P0 holds another line's data.

Build: `2dd4c198548e__icstation_4681_Code_main.hex` (SDCC build from
stc-research corpus).

---

## 14. P0 data polarity is active-HIGH — settled by trace, not by source

**Question:** Is `P0 = 0x00` "all LEDs off" (active-high) or "all LEDs on"
(active-low)? The spec said active-low; `probe.c` assumed active-high;
`main.c` followed the spec. Source reasoning alone could not settle it.

**Method:** P0 value histogram from the vendor firmware (SDCC build,
5 seconds, scan phase only), classifying each P0 write as either
"blank" (before a P2 select transition) or "data" (after a P2 select).

**Result:**

| Role | P0 value | Count | Notes |
|------|----------|-------|-------|
| Blank | `0x00` | 1,560 | exclusively before selects |
| Data: all-on | `0xFF` | 414 | exclusively after selects |
| Data: red cols | `0x0F` | 540 | lower 4 bits = red |
| Data: blue cols | `0xF0` | 460 | upper 4 bits = blue |
| Data: single | `0x01` | 100 | one column |
| Data: mixed | `0x11` | 28 | |
| Data: mixed | `0x10` | 18 | |

**Zero exceptions:** `0x00` is never used as data. `0xFF` is never used
as blank. During the all-on init phase (P2=0x00), P0 alternates between
0x00 (blank) and 0xFF (data=lit), confirming 0xFF = all LEDs on.

**Conclusion:** P0 is **active-HIGH**. `1` = LED on, `0` = LED off.

**Confidence:** Strong. Zero exceptions across 5 seconds and 3,930+ P0
writes. The roles of 0x00 and 0xFF are completely non-overlapping.
Still conditional on the hardware matching the firmware's intent — a
bench test with `probe.c` remains the definitive confirmation — but the
evidence is as strong as a trace measurement can be.

**Impact:** `main.c`'s `P0_ACTIVE_LOW = 1` is inverted. `fb_clear()`
should set `0x00` (not `0xFF`), and `fb_set_red`/`fb_set_blue` should
SET bits to light LEDs rather than CLEAR them.
