# BLOCKED — items waiting on external action

## SDCC-to-WASM — configure passes, build fails in sdbinutils

**Status:** Configure now passes (run `31339842453`). `emmake make`
fails in the sdbinutils sub-build (the assembler/linker component).

**What was resolved (11 runs):**
1. Directory collision: `sdcc-wasm/` existed in repo → `sdcc-emcc`
2. config.sub: `wasm32-unknown-emscripten` not recognized → `i686-unknown-linux-gnu`
3. Endianness: `ppc` is big-endian → `i686` (little-endian = wasm32)
4. zlib: Emscripten sysroot → `-sUSE_ZLIB` in CFLAGS
5. boost headers: isolated copy → `CPPFLAGS="-isystem /tmp/boost-headers"`
6. C++11 probe: `ac_cv_prog_cxx_cxx11=yes` injected literal `yes` into CXX →
   deleted, CXXFLAGS already has `-std=c++11`

**What blocks now:** `emmake make` fails in `sdcc-sdbinutils` (Error 2).
The sdbinutils sub-tree is a binutils fork with its own build system.
This is the first real Emscripten compilation failure (all prior were
configure environment issues).

**The workflow is at `.github/workflows/build-sdcc-wasm.yml`** and
configure is now green. The next person needs to diagnose the sdbinutils
compilation error.

## AVR conformance — blocked on bw-board accepting `input-pullup`

bw-board's `conformance.js` rejects `input-pullup` as a valid PinMode.
spec-update 005 was adjudicated (sb3-creator 6255de3). Until bw-board
updates its conformance checker, the AVR adapter cannot pass the full
suite.
