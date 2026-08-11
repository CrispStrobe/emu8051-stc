# 010 — sdld `newarea()` reads absent field via `eval()` (upstream defect report)

For the SDCC maintainers. Ready to file; filing is the owner's decision.

## Summary

`sdld`'s `newarea()` in `lkarea.c` calls `eval()` to read a trailing
`addr` field from area (A) records unconditionally when targeting 8051.
The field is present only when the assembler runs in sdas mode
(`is_sdas() == true`). When the field is absent, `eval()` reads past
the end of the line — undefined behaviour. The result is used as the
area's starting address, producing a silently misplaced code origin.

## Version

SDCC 4.5.0, revision #15242. Source tarball SHA-256:
`d5030437fb436bb1d93a8dbdbfb46baaa60613318f4fb3f5871d72815d1eed80`.

## Reproduction

1. Produce a `.rel` file whose A records lack the `addr` field.
   This happens when `sdas8051` runs with `is_sdas() == false`
   (e.g. when `argv[0]` does not start with `"sdas"`).

   A record with field:    `A HOME size 4C flags 20 addr 0`
   A record without field: `A HOME size 4C flags 20`

2. Link with `sdld -nf test.lk` where the `.lk` specifies
   `-b HOME = 0x0000`.

3. Check the map file for the HOME area address.

**Expected:** `HOME 00000000` (the `-b HOME = 0x0000` directive
should place HOME at address 0).

**Actual:** Depends on host. On native x86-64 Linux, `eval()`
happens to return 0 from the absent field — correct by accident.
On WASM (Emscripten wasm32), `eval()` returns 1 — HOME placed at
0x0001, all code shifted by one byte, all absolute jump targets
off by one.

## Demonstrated

Run 31464841382: fed byte-identical `.rel` and `.lk` files to both
native sdld and WASM sdld. Same input, same arguments, different output:

    native sdld:  HOME  00000000  0000004C = 76. bytes (REL,CON,CODE)
    wasm   sdld:  HOME  00000001  0000004C = 76. bytes (REL,CON,CODE)

## Mechanism

`lkarea.c`, function `newarea()`, lines 153-158 (SDCC 4.5.0):

```c
if (is_sdld() && !(TARGET_IS_Z80 || TARGET_IS_GB)) {
    /*
     * Evaluate area address
     */
    skip(-1);
    axp->a_addr = eval();
}
```

This block runs unconditionally for 8051 targets. `skip(-1)` skips
whitespace. `eval()` parses a hex value. When the A record line ends
after `flags XX` with no `addr` field, `eval()` reads whatever follows
the current parse position — either a newline, the start of the next
record, or uninitialised buffer content. The return value is used
directly as the area address.

## What we are NOT claiming

- We are NOT claiming `eval()` is broken. It parses what it is given.
  The defect is the caller assuming a field is present without
  checking.
- We are NOT claiming the native x86-64 result is correct. It is
  correct by accident — the UB happens to yield 0, which matches
  the desired placement. A different native toolchain, optimisation
  level, or input alignment could yield a different value.
- We are NOT claiming all A records must carry `addr`. The sdas
  assembler emits it when `is_sdas()` is true; the generic ASxxxx
  assembler does not. The linker should handle both formats.

## Suggested fix

Check whether the `addr` field is present before calling `eval()`.
If absent, default to 0 (matching the area initialisation in
`lkparea()` at line 247: `ap->a_addr = 0`). Alternatively, emit
a diagnostic when `is_sdld()` expects a field the input does not
provide.

## Our workaround

Set `thisProgram` in the Emscripten Module config to `"sdas8051"`,
making `is_sdas()` return true and the assembler emit the `addr`
field. The linker then receives well-formed input. The upstream
defect is no longer reachable from our pipeline but is not fixed.
Commit `3d2b3b1` in `CrispStrobe/emu8051-stc`.
