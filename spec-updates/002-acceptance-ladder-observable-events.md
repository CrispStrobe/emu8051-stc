# Spec update: acceptance ladder should test observable events, not PC streams

**Proposed change to DEBUG-CONTROL-MODEL.md §8**

## The problem

Rung 3 as written requires "step('insn') × N from reset produces the
same PC sequence on both emulators." This is a harness property, not a
product property. The two emulators sample PC at different moments in
the tick cycle, so they can disagree on PC observation timing while
producing identical peripheral behaviour. Chasing PC-step alignment
has no payoff — the valuable claim is peripheral agreement.

## Proposed rewrite of rungs 3-6

Replace PC-stream comparison with observable-event comparison at each
step granularity:

| Rung | Original | Proposed |
|------|----------|----------|
| 3 | step(insn) × N → same PC sequence | step(insn) × N → same **SFR write and TF event sequence** |
| 4 | code BP halts at same PC with same A,B,DPTR,SP,PSW and IRAM/XRAM digest | code BP halts at same PC **and same SFR state** for the watched set |
| 5 | yield BP halts at same (task,state) and same bw_ms | unchanged — this is already an observable-event test |
| 6 | write variable while halted, resume → same subsequent trace | unchanged — already tests peripheral trace |

## Rationale

PC agreement between two independently written cores is expected (both
implement the same instruction set from the same spec) and uninformative
when it holds. Peripheral agreement is the part each fork wrote
independently from a datasheet, and the only part where a disagreement
would reveal a misreading or a bug.

The differential execution results (RESULTS.md) already demonstrate
peripheral agreement: 49/49 SFR+TF events on blink, 54/54 on adc,
37/37 on scheduler, all identical. This is the claim that matters.

## What the current results establish

- **Rung 1**: capabilities answered, state tracks run/halt. ✓
- **Rung 2**: Level 1 position implemented (needs real symbol table). ✓
- **Rung 3 (revised)**: SFR+TF event sequences identical over 10 ms
  on 3 firmware images. ✓
- **Rungs 4-6**: need ucsim to implement DebugTarget.
- **Rung 7**: needs real hardware.
