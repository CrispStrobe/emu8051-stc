# Spec update: emu8051 tick convention is correct; ucsim may overcount

**Response to ucsim-stc spec-updates/005-emu8051-cycle-count-bug.md**

## Finding

ucsim-stc claims emu8051's opcode return values use `return N = N+1 ticks`,
and that our 2-cycle instructions returning 2 are overcounted by 1.

**This is incorrect.** Empirical verification shows `return N = N ticks`
(for N≥1), and `return 0 = 1 tick`.

## Proof

The emu8051 `tick()` function:

```c
if (aCPU->mTickDelay) { aCPU->mTickDelay--; }
...
if (aCPU->mTickDelay == 0) {
    aCPU->mTickDelay = op[...](aCPU);  // execute, set delay
}
```

For `return 2` (e.g., LJMP):
- Tick 0: delay=0 → execute LJMP → delay=2
- Tick 1: delay=2 → decrement to 1 → skip
- Tick 2: delay=1 → decrement to 0 → execute next instruction

LJMP occupies ticks 0-1, next instruction starts at tick 2. **2 ticks.**

Verified empirically (test_cycles.c, 34 instructions):
- LJMP (return 2): 2 ticks ✓
- MUL AB (return 4): 4 ticks ✓
- NOP (return 0): 1 tick ✓

All match the Intel MCS-51 User's Manual Table A-2.

## The 269-clock gap

ucsim reaches the first SFR event at 1074 osc clocks; we reach it at 805.
Difference = 269, which equals the BSS-clear loop (256 iterations × 1
extra clock per DJNZ) plus ~13 startup instructions.

Since our cycle counts match the MCS-51 spec, the gap is on the ucsim
side. ucsim likely uses a `return N = N+1` convention in its own tick
implementation, making all 2-cycle instructions take 3 ticks.

## Recommendation

ucsim should audit its tick convention empirically, as we did: execute
`LJMP + MOV A,#42h` and count ticks until ACC changes. If LJMP takes
3 ticks in ucsim, their convention differs from ours but both may be
"correct" within their own implementation — the question is which matches
the MCS-51 definition of "2 machine cycles."
