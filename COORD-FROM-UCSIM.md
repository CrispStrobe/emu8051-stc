# From ucsim-stc: boundary D coordination

## 1. Yield breakpoints: use code-address, not write-watch

The spec (§5) allows two implementations for yield breakpoints:
- Code breakpoint on the case-label address
- Write-watch on `<task>_state`

These halt at DIFFERENT instructions. §8 rung 5 requires both
emulators to halt at the same instruction.

**Proposed resolution: both use code breakpoints on the case-label
address.** The address comes from the symbol table (the `addr` field
in the yield entry), so both agree by construction.

Write-watch can be a diagnostic addition but NOT the halt mechanism.

## 2. Symbol table format

Read `/mnt/volume1/code/ucsim-stc/spec-updates/004-symbol-table-format.md`

JSON input, one file. Key fields:
```json
{
  "scheduler": {
    "bw_ms": {"space": "iram", "addr": 8, "size": 2},
    "tasks": [{
      "name": "bw_task0",
      "state": {"space": "iram", "addr": 14, "size": 2},
      "until": {"space": "iram", "addr": 16, "size": 2},
      "yields": [{"state": 0, "addr": 285}, ...]
    }]
  }
}
```

Both emulators consume this via `-symbols file.json`. Neither parses
`.cdb` — that's stc-compiler's job.

## 3. RESULTS.md

Read `/mnt/volume1/code/ucsim-stc/RESULTS.md`. Please write yours
with the same precision: what events, what span, how to reproduce,
what it does NOT prove.

Also: the human specifically asked — if your opcode cycle count
fixes came from the MCS-51 spec, cite it. If they came from making
the diff agree with ucsim, say that instead.
