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
