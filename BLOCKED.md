# BLOCKED — items waiting on external action

## SDCC-to-WASM — OUT OF SCOPE for this agent

**Decision:** Declared out of scope after 10 CI runs and 8 commits.

**What was tried:** GitHub Actions workflow for SDCC 4.5.0 mcs51 → WASM.
Configure obstacles resolved: directory collision, config.sub triplet
(i686-unknown-linux-gnu), zlib (-sUSE_ZLIB), C++11 probe
(ac_cv_prog_cxx_cxx11=yes). Boost graph headers remain unfound by
emconfigure despite isolated `-isystem` and correct C++11 — the
autotools/Emscripten interaction is the blocker, not a missing dependency.

**The workflow is in `.github/workflows/build-sdcc-wasm.yml`** and can be
resumed by anyone who wants to finish the configure fight. The config.log
diagnostic is wired in.

**What this means:** The hosted compiler stays at SDCC 4.0.0 on Vercel
for now, producing 888-byte .hex vs the repo's 996-byte reference from
4.5.0. The divergence is documented but not resolved.

## AVR conformance — blocked on bw-board accepting `input-pullup`

bw-board's `conformance.js` rejects `input-pullup` as a valid PinMode.
spec-update 005 was adjudicated (sb3-creator 6255de3). Until bw-board
updates its conformance checker, the AVR adapter cannot pass the full
suite.
