# BLOCKED — items waiting on external action

## SDCC-to-WASM build — workflow ready, needs first run

**Status:** `.github/workflows/build-sdcc-wasm.yml` is pushed and
runnable via `workflow_dispatch`. Not yet triggered — the Emscripten
cross-compilation of SDCC may need patches (the `--host` flag and
`emconfigure` interaction is untested).

**Recommendation:** GitHub Actions runner. A workflow that downloads
SDCC source, configures for mcs51 only, cross-compiles with Emscripten,
runs byte-identity test, and uploads artifacts. This is reproducible,
doesn't compete for VPS memory, and produces a checked artifact.

**Version: SDCC 4.5.0** (confirmed by coordinator). The local sdcc that
built every reference .hex is 4.5.0. The hosted API runs 4.0.0 (forced
by Vercel's glibc 2.34 — 4.5.0 needs GLIBC_2.36). This means the web
page has been producing different firmware than `make` (996 vs 888 bytes
for 01-blink). WASM has no glibc, so building 4.5.0 ends this
divergence and makes the page agree with the repo for the first time.

**Acceptance:** byte-identical against native SDCC 4.5.0 for all 9
examples. A match against 4.0.0 would be a failure, not a curiosity.

**Handover note for bw-bundle:** swapping in the WASM compiler changes
the .hex the page produces. This is the intended fix (ending the
4.0.0/4.5.0 divergence), not a regression.

**Cleanup done:** /tmp/sdcc-4.5.0 and tarball removed. No processes
left running. Build moves to a GitHub Actions workflow.
