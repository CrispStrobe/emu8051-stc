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

**Version question:** The acceptance test requires byte-identical output
with the compiler that produced all existing measurements. Which SDCC
version is that?
- System: 4.2.0 (dpkg)
- stc-compiler binary: 4.0.0
- Requested: 4.5.0

The version must be pinned to whichever produced the reference .hex
files, or the byte-identity test is meaningless.

**Cleanup done:** /tmp/sdcc-4.5.0 and tarball removed. No processes
left running.
