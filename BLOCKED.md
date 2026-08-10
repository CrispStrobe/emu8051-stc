# BLOCKED — items waiting on external action

## SDCC-to-WASM — ALL GREEN, native compiles, WASM loads, identity test pending

**Status: ALL GREEN** (run `31343427469`). SDCC 4.5.0 compiles to
WebAssembly. Artifacts uploaded: sdcc.js/.wasm, sdas8051.js/.wasm,
sdld.js/.wasm. Gzipped total: **1.6 MB**.

**What was resolved (14 runs):**
1. Directory collision → `sdcc-emcc`
2. config.sub triplet → `--host=i686-unknown-linux-gnu`
3. zlib → `-sUSE_ZLIB` in CFLAGS
4. boost headers → `CPPFLAGS="-isystem /tmp/boost-headers"`
5. C++11 probe literal `yes` in CXX → deleted cache prime
6. psignal conflict → `sed -i '1i #define HAVE_PSIGNAL 1'`
7. EXEEXT → `EXEEXT=.js`

**What remains:** The byte-identity test (step 12) is a stub — it
prints the native SDCC version but does not compile examples. Needs
a Node.js driver that invokes the WASM sdcc via `callMain` with a
virtual filesystem. The 9 pseudocode examples and the byte-identity
diff are the acceptance criterion.

**sdcpp not found** — may be built into sdcc or need a separate path.

**Workflow:** `.github/workflows/build-sdcc-wasm.yml` — fully green.
**Patches applied (GPL source obligation):** One `sed` inserting
`#define HAVE_PSIGNAL 1` at line 1 of `libiberty/strsignal.c`.

## AVR conformance — blocked on bw-board accepting `input-pullup`

bw-board's `conformance.js` rejects `input-pullup` as a valid PinMode.
spec-update 005 was adjudicated (sb3-creator 6255de3).
