# Independent 8051 checkpoint v1 proof tooling

Base: `47c504191a8420676415a1b86766621d6ea0de86`.
This branch owns tests only. Root's accepted codec and artifact head
`0ad7215dfe6fd5d3191dcd6f82b7b7bf67651be4` was merged forward (source `a519b0d`).
Root owns production implementation and landing. Our live-state probes are
test-only; they include the unchanged wasm_api.c in a separate WASM build.

## Current qualification status

**Real integration exists; hosted acceptance is pending the repaired run.** The
committed checkpoint-capable WASM passes the boundary smoke and targeted semantic
corruptions locally. Hosted run `34694012099` built both WASM variants and passed
synthetic tests, then correctly failed the IRQ fixture's phase census. It must
not be cited as a full proof pass. Synthetic tests are never emulator evidence.

```
node --test tools/checkpoint-proof.test.mjs
node tools/checkpoint-wasm-smoke.mjs /absolute/path/to/build/emu8051.js
CHECKPOINT_PROBE_WASM=/absolute/path/to/build-proof/emu8051.js node tools/checkpoint-phase-census.mjs
CHECKPOINT_PROBE_WASM=/absolute/path/to/build-proof/emu8051.js node tools/checkpoint-proof.mjs tools/checkpoint-real-adapter.mjs
```

The WASM binding uses exactly `emu_checkpoint_version`, `emu_checkpoint_build_id`,
`emu_checkpoint_size`, `emu_checkpoint_save`, `emu_checkpoint_restore`, and the
specified 0/-1..-8 codes. It returns owned copies, reacquires the heap after native
calls, and frees scratch allocations. The smoke checks selected length boundaries,
null pointers, uninitialized restore, deterministic bytes and restore round trips.
The separate real adapter adds independent live-state probes, real pin/UART
callback transcripts, exact-status semantic corruptions, and 195 checkpoint
positions across eight scenarios. CI rejects empty receipts and missing mutations.

### Red-1 evidence repair

The downloaded probe artifact from run `34694012099` shows all five part MUL
censuses were correct. The failing scenario was `nested-interrupts`, not the first
part. Unconditional RX before every single-tick advance continuously reasserted
the high-priority serial interrupt. This kept the checkpoint trajectory out of
the intended foreground/T0 ISR and produced only delay 0/1/2 and IRQ states 0/2.

The repair stops per-tick RX injection only in that fixture. Its Timer0 ISR itself
transmits UART1 to trigger nested high-priority serial service. RX still arrives
in continuation actions; the other fixtures keep per-tick RX. UART2 output is
required in foreground/wrapped fixtures, not in the IRQ-priority fixture whose
ISR transmits only UART1. The exact phase equality gate is unchanged.

Source-derived expectations, independently measured on the hosted probe artifact:

| Scenario | Required checkpoint delay set | Evidence |
| --- | --- | --- |
| STC12/STC15/STC15W/STC12_16 and classic | 0..4 | mul_ab returns 4; scale=1 preserves it, then tick counts down |
| STC89 | 0..59 | tick assigns `(4+1)*12-1 = 59`; 12T countdown reaches zero |
| Repaired nested IRQ | 0..4; active IRQ states 0,1,2,3 | Foreground MUL, low T0, high serial and nested high-on-low observed at checkpoint positions |
| Wrapped history/UART | 1..4 | Warm foreground loop has no reset boundary; actual 24-position trace matches |

`checkpoint-phase-census.mjs` compares observations against these explicit sets,
never learns its expectations from the current execution. Its local run against
the downloaded artifact passes all eight scenarios, including real event/output
non-vacuity assertions. It is fast reachability evidence, not full replay proof.

## Full proof adapter contract

Export a default object with `contractVersion: 1`, exact `identity: {schema, build}`,
`scenarios`, and `corrupt(checkpoint)` returning `{kind, blob, expectedCode}` mutations.
Required mutation kinds are bad-version, bad-header, bad-build, invalid-state.
Construct mutations from the actual documented codec layout, not guessed offsets.
Version 1 and layout fingerprint 0x80510101 are the exact schema/build identity.
The fixed v1 layout is 443483 bytes, with a 20-byte header and FNV-1a payload
checksum. Semantic mutations recompute the checksum so malformed boolean and
invariant checks are actually reached; a separate mutant checks checksum failure.
Header/version/build errors require exact -4/-5/-6 and state invariants exact -7.

Each scenario declares `name`, `points`, an explicit `expectedPhases` census,
`advance` action, a nonempty `continuation` action list, and `create()` returning:

- `identity()` returning actual runtime identity, not the expected constant;
- `save()` returning owned Uint8Array bytes and `restore(bytes)` returning boolean;
- `restoreRaw(bytes)` returning the exact native status for malformed proofs;
- `observe()` returning `{phase, state}` with independent state probes for cpu,
  timers, interrupts, uartInput, uartOutput, time, inputs, pins, history;
- `apply(action)` returning that action's events/output, with deterministic stimuli;
- optional `dispose()` releasing per-scenario resources.

All operations may be async. Probes must not derive state by decoding save().
Callback event capture is host-owned: clear it per action, do not rewind it by
pretending host logs are part of a native checkpoint. Include callback silence
during save/restore in observations, or assert it inside the adapter.
All returned observations must be structured-cloneable; state probe values must
also be JSON-serializable for the non-vacuity census.

The runner compares exact checkpoint bytes and independent observations; restores
twice; compares continuation after restoration including every action's events;
and checks byte/observation equality after each malformed refusal plus continuation
after the refusal sequence. Every required probe group must vary over the suite.
That guard detects empty exercises, not semantic completeness of fixture design.
All-prefix length testing is opt-in via `exhaustiveLengths: true`; default probes
boundary lengths to avoid quadratic copying of large fixed memory/histogram blobs.

## Scope and remaining qualification limits

| Proof | Required fixture/probe evidence |
| --- | --- |
| Reachable phases | All delay values in owned MUL/IRQ fixtures; not all opcode sequences or idle/power-down wakeup combinations |
| Timers/controller | Running T0/T1 and prescalers, interrupt priority/nesting observed. No exhaustive ADC/PCA/watchdog mode matrix |
| UART | Accepted RX latches/RI, both UART callbacks, UART1 ring/index/wrap, pending interrupt. Future JS input queues intentionally external. Classic WASM has no path starting bitwise TX; remaining bits are observed but not claimed nonzero-reachable |
| Time/inputs | Independent clock/ns/quantum, port_ext and ADC input probes, deterministic replayed stimuli |
| Pins/history | Full enabled ring event slots, head/count/timestamps and live shadows, real pin callback replay; disabled/enabled/wrapped fixtures |
| Memory/debug | Code/XRAM probe hashes, full IRAM/SFR, live debug counters and full serialized histogram. No exhaustive breakpoint/watch/task matrix |
| Identity/errors | Exact layout/status assertions with recomputed checksum. Allocation failure -8 still needs a separate allocation-fault test |
| Atomicity | Byte equality and independent probes after every refusal, callback silence, identical continuation and restore-twice |

Source inventory at baseline: `emu8051.h:69` (CPU, interrupt saved registers,
UART ring/bit state), `stc12.h:288` (timers, latches, pin history, time and shadows),
`debug.h` (debugger-owned state), `wasm_api.c:450` and `:736` (direct RX injection).
The test-only `checkpoint-probe.c` observes these live structures directly and
cross-checks public getters. It does not decode save() output, change core state,
or add production ABI exports. Hosted CI builds it; no heavy local build is needed.

## Harness mutation receipts

The self-test injects synthetic identity mismatch, nondeterministic save,
round-trip loss, non-idempotent second restore, divergent continuation,
accepted corruption, refusal mutation, latent refusal damage, incomplete phase
census and vacuous time coverage. Each must fail at its intended assertion.
These are test-runner mutants, **not mutations of the native serializer**.
