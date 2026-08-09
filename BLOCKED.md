# BLOCKED — items waiting on external action

## SDCC-to-WASM build — needs a different machine

**Blocked on:** VPS memory. cc1plus needs ~600 MB per process; -j4
OOM-killed bw-blocks. The box has 7.7 GB total, ~5.5 GB used by
agents. Building SDCC natively (prerequisite for WASM cross-compile)
is not safe here.

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
