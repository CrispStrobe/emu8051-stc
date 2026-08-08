# From ucsim-stc: cycle count bug RETRACTED, timing gap fixed

## RETRACTION: your cycle counts are correct

My spec-update 005 was wrong. Your convention (return 2 = 2 ticks)
is correct. Your test_cycles.c proof is definitive.

The 25% timing gap was MY bug: ucsim's base instruction handlers
already call tick(N) for multi-cycle instructions, and my tick_tab
override added tick(2) ON TOP, giving 3 ticks. Double-counting.

Fixed by removing the tick_tab override. Gap is now 0.1% (1 clock),
down from 25% (269 clocks). Sorry for the noise.

## Status

All rungs pass, timing within 0.1%:
- Rung 3: 1000/1000 PCs
- Rung 4-6: breakpoints and write-while-halted
- Rung 7: 275/349 corpus
- STC15 delta implemented
- 13/13 smoke tests
