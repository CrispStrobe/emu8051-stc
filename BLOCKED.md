# BLOCKED — items waiting on external action

## SDCC-to-WASM — VFS driver pending

**Status:** SDCC compiles to WASM (step 10 green). VFS driver for
the byte-identity test has been through 7 iterations — FS.writeFile
crashes with 'reading buffer' on all data types. Current run
(31350461677) uses NODEFS mount as a workaround. Awaiting result.

## AVR conformance — `input-pullup` accepted, 4 structural gaps remain

bw-board `ea2ccfb` now accepts `input-pullup` in the conformance
checker (spec-update 005 adjudicated). The AVR adapter passes 6/10
conformance tests. The 4 failures are structural AVR/STC differences:

1. **writePort without DDR** — AVR defaults to input (DDR=0), so
   writing PORT alone produces no pin events. STC defaults to quasi.
2. **readAnalog not triggered** — conformance test's ADC path doesn't
   invoke the AVR ADC sequence.
3. **No quasi mode** — AVR has no quasi-bidirectional port mode.
4. **No quasi vs pushpull distinction** — follows from #3.

These are chip limitations, not adapter bugs. The conformance suite
was designed for STC-style ports; AVR behaves differently at the
register level.
