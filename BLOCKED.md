# BLOCKED — items waiting on external action

## SDCC-to-WASM — configure passes, build fails in sdcc-libs

**Status:** Configure green (run `31339842453`). sdbinutils skipped
(psignal conflict). Build now fails in `sdcc-libs` with
`em++: error: no input files` in `all-gcc` (run `31340277027`).

**What was resolved (12 runs):**
1. Directory collision: `sdcc-wasm/` in repo → renamed to `sdcc-emcc`
2. config.sub triplet: → `--host=i686-unknown-linux-gnu` (LE 32-bit)
3. zlib: → `-sUSE_ZLIB` in CFLAGS only (not CXXFLAGS, avoids shadow)
4. boost headers: → `CPPFLAGS="-isystem /tmp/boost-headers"` (isolated)
5. C++11 probe: `ac_cv_prog_cxx_cxx11=yes` injected literal `yes`
   into CXX → deleted, CXXFLAGS has `-std=c++11`
6. sdbinutils psignal: → `--disable-sdbinutils`

**What blocks now:** `sdcc-libs` target tries to build `all-gcc`
(GCC support library sub-build). Likely fix: also disable sdcc-libs
or build only the `sdcc-cc` make target.

**Route 2 (recommended):** Build only `sdcc-cc` (compiler + preprocessor)
and `sdas` (assembler + linker) with Emscripten. Take device libraries
from the native build (they are target artifacts, host-independent).

**Workflow:** `.github/workflows/build-sdcc-wasm.yml` — configure green.

## AVR conformance — blocked on bw-board accepting `input-pullup`

bw-board's `conformance.js` rejects `input-pullup` as a valid PinMode.
spec-update 005 was adjudicated (sb3-creator 6255de3).
