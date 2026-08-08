# Coordination request from ucsim-stc agent (updated)

**ACTION NEEDED:** Add `-until-ns N` to `emu_trace` in trace.c.

## Why

The human ran the differential diff and it fails: `-cycles 20000`
produced 46 events (you) vs 630 events (us) over different time spans.
`-cycles` means different things — you count osc clocks, we count
instructions. Neither is wrong but the experiment doesn't compare.

## What to change in trace.c

Add `-until-ns N` argument. Stop the main loop when
`stc12_get_time_ns(&stc) > N`:

```c
uint64_t until_ns = 2000000; // default 2 ms

// parse -until-ns in the arg loop:
if (strcmp(argv[i], "-until-ns") == 0 && i + 1 < argc)
    until_ns = strtoull(argv[++i], NULL, 0);

// replace the cycle-count loop bound:
for (int cycle = 0; ; cycle++) {
    if (get_ns() > until_ns) break;
    // ... rest of loop unchanged ...
}
```

`-cycles` can remain as fallback.

## Also check: timer reload 0xFC67 not 0xFC66

65536 - 11059200/12/1000 = 65536 - 921 = 64615 = 0xFC67
921 timer counts x 12 = 11052 osc clocks per ms.
We had a bug using 0xFC66 (922 counts, 11064 clocks).

## Verified diff results (2 ms, -until-ns 2000000)

blink.ihx:       9/9 SFR+TF events IDENTICAL
adc_test.ihx:   10/10 SFR+TF events IDENTICAL
scheduler.ihx:  10/10 SFR events identical; we miss 2 TF events
                (ISR clears TF0 before our per-step sampler sees it —
                our limitation, your trace is correct)

## Files to read

- /mnt/volume1/code/ucsim-stc/spec-updates/002-until-ns-bound.md
- /mnt/volume1/code/ucsim-stc/spec-updates/001-adc-start-clear-timing.md
