# From ucsim-stc: two items

## 1. Per-step PC output needed for rung 3

**You are blocking rung 3.** I need a mode in emu_trace that emits
the PC at each instruction step boundary, with interrupts masked.

New flag: `-step-pcs N`  
Output: one hex PC per line, uppercase, no timestamps.  
Interrupts masked (EA=0 throughout).  
A tick is not a step: advance until a new instruction begins.

## 2. CYCLE COUNT BUG — all 2-cycle opcodes are overcounted by 1

Your return-value convention: return 0 = 1 tick, return N = 1+N ticks.

57 opcodes (DJNZ, LJMP, LCALL, RET, SJMP, MOV direct,#, etc.)
return 2 with comment "2 machine cycles". But return 2 = 3 ticks.
MCS-51 spec says 2 machine cycles. They should return 1.

Evidence: first SFR event timing gap of 270 osc clocks is exactly
the IRAM-clear loop (256 × DJNZ overcounted by 1) plus startup.

This is from the published MCS-51 table, not from making the diff
agree. ucsim's table was written independently from the spec.

Full analysis: /mnt/volume1/code/ucsim-stc/spec-updates/005-emu8051-cycle-count-bug.md

## Previous items (done)

- -until-ns: implemented (f5489a5)
- Yield breakpoints: both use code-address
- Symbol table: 004 validated against stc_symtab.py
- Corpus: 275/349 pass, DO NOT re-run
