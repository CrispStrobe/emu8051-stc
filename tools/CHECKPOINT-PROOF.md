# Independent 8051 checkpoint v1 proof tooling

Base: `47c504191a8420676415a1b86766621d6ea0de86`.
This branch owns tests only. Root owns the codec, native test probes, ABI exports,
builds and integration. No production implementation is included here.

## Current qualification status

**Native qualification pending.** The assigned base has no checkpoint exports.
Synthetic runner tests are not evidence of emulator checkpoint correctness.
The boundary smoke must fail on that old artifact, rather than skip to green.

```
node --test tools/checkpoint-proof.test.mjs
node tools/checkpoint-wasm-smoke.mjs /absolute/path/to/build/emu8051.js
node tools/checkpoint-proof.mjs /absolute/path/to/native-proof-adapter.mjs
```

The WASM binding uses exactly `emu_checkpoint_version`, `emu_checkpoint_build_id`,
`emu_checkpoint_size`, `emu_checkpoint_save`, `emu_checkpoint_restore`, and the
specified 0/-1..-8 codes. It returns owned copies, reacquires the heap after native
calls, and frees scratch allocations. The smoke checks selected length boundaries,
null pointers, uninitialized restore, deterministic bytes and restore round trips.
It does not claim fault injection, all phases, or peripheral replay coverage.

## Full proof adapter contract

Export a default object with `contractVersion: 1`, exact `identity: {schema, build}`,
`scenarios`, and `corrupt(checkpoint)` returning `{kind, blob}` mutations.
Required mutation kinds are bad-version, bad-header, bad-build, invalid-state.
Construct mutations from the actual documented codec layout, not guessed offsets.
Version is provisionally the schema identity; root must confirm this interpretation.
Valid RAM edits are not necessarily invalid blobs: there is no stated checksum.

Each scenario declares `name`, `points`, an explicit `expectedPhases` census,
`advance` action, a nonempty `continuation` action list, and `create()` returning:

- `identity()` returning actual runtime identity, not the expected constant;
- `save()` returning owned Uint8Array bytes and `restore(bytes)` returning boolean;
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

## Native integration checklist (all still pending)

| Proof | Required fixture/probe evidence |
| --- | --- |
| Reachable phases | Explicit instruction delay census, 1T/12T parts, interrupt entry/active/nested/return, idle/wakeup; do not infer coverage from PC alone |
| Timers/controller | Prescalers, overflow pending, enabled/masked and priority state; ADC/PCA/watchdog continuation where applicable |
| UART | RX latch/RI and UART2, serial_out contents/index/wrap, remaining TX bits and pending interrupt; clarify any host queues excluded from ABI |
| Time/inputs | Oscillator clocks, conversion remainder/configuration, port_ext and analog inputs, deterministic edge stimuli |
| Pins/history | Drive/mode shadows, callback binding survival, ring disabled/enabled/part-full/wrapped with head/count and timestamps |
| Memory/debug | Distinct code/IRAM/XRAM/SFR patterns, debugger breakpoint/watch/profiling presence and contents according to codec scope |
| Identity/errors | Actual header widths/offsets; exact -4/-5/-6/-7 mutations; allocation-failure injection is a separate native test |
| Atomicity | Independent probes and callback log unchanged on errors, then identical replay; include nonzero and in-flight source states |

Source inventory at baseline: `emu8051.h:69` (CPU, interrupt saved registers,
UART ring/bit state), `stc12.h:288` (timers, latches, pin history, time and shadows),
`debug.h` (debugger-owned state), `wasm_api.c:450` and `:736` (direct RX injection).
An adapter needs a read-only native probe for internal phases: public getters alone
cannot prove all of the assigned state. No probe or ABI changes are made here.

## Harness mutation receipts

The self-test injects synthetic identity mismatch, nondeterministic save,
round-trip loss, non-idempotent second restore, divergent continuation,
accepted corruption, refusal mutation, latent refusal damage, incomplete phase
census and vacuous time coverage. Each must fail at its intended assertion.
These are test-runner mutants, **not mutations of the native serializer**.
