/* The stc firmware corpus as a regression canary for the PCON.IDL
 * fast-forward — this project's blinkenrocket equivalent.
 *
 * bw-board's avr8js sleep fast-forward shipped a first cut that fell through a
 * sleep at a slice boundary, and it was REAL THIRD-PARTY FIRMWARE that caught
 * it, not the unit tests. The 8051 equivalent is the hand-written firmware in
 * the `stc` sibling: every built .hex runs twice, with and without the
 * fast-forward, and the two runs must agree on the PC, elapsed simulated time,
 * all 128 SFRs and all 128 bytes of IRAM.
 *
 * None of this firmware sets PCON.IDL, so the expected result is that the
 * optimisation stays completely inert over all of it — which is the claim
 * worth having: existing firmware is untouched.
 *
 * Measured 2026-08-25 over 28 images (the nine shipped example .hex files plus
 * the rest of src/ built from source, including the A2 examples 22-26 that had
 * landed days earlier): 143-3975 bytes of code each, every PC well past the
 * reset vector, 0 divergent, slept=0 on every one. Build them with
 * `make EXAMPLE=<dir>` in the stc repo — it writes to build/, which is
 * gitignored there.
 *
 * THE FIRST RUN OF THIS PROVED NOTHING and said so confidently: it reported
 * "12 images, 0 divergent" while emu_load_hex's return of 0 — which is
 * SUCCESS, not failure — had been misread as a failed load. It would have
 * reported the same clean zero over twelve empty emulators. It now counts
 * non-zero code bytes and checks the PC left the reset vector, so the
 * comparison is over firmware that demonstrably ran.
 *
 * Run: node test_corpus_canary.mjs   (needs the `stc` sibling checked out)
 */
import { createRequire } from 'node:module';
import { readFileSync } from 'node:fs';
import { execSync } from 'node:child_process';
const require = createRequire(import.meta.url);
const createEmu8051 = require('./build/emu8051.js');

import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
const HERE = dirname(fileURLToPath(import.meta.url));
const STC = process.env.BW_STC || join(HERE, '..', 'stc');
if (!existsSync(STC)) {
  console.log(`SKIP: the stc firmware sibling is not at ${STC}. This canary is ` +
    'the only thing here that runs REAL hand-written firmware; a skip is not a pass.');
  process.exit(0);
}
const hexes = execSync(`find ${STC} -name "*.hex"`, { encoding: 'utf8' })
  .trim().split('\n').filter(Boolean);
if (hexes.length < 8) {
  console.log(`SKIP: only ${hexes.length} built images found — the corpus is not built. ` +
    'Build with `make EXAMPLE=<dir>` per example in the stc repo.');
  process.exit(0);
}
console.log(`${hexes.length} images found under ${STC}`);

const SIM_NS = 20_000_000;   /* 20 ms of sim per image */
let fails = 0, idlers = 0;

for (const path of hexes) {
  const hex = readFileSync(path, 'utf8');
  const snap = [];
  for (const ff of [0, 1]) {
    const M = await createEmu8051();
    const loadHex = M.cwrap('emu_load_hex', 'number', ['string', 'number']);
    M._emu_init(1);
    M._emu_set_part(/stc15/.test(path) ? 1 : 0);
    M._emu_reset(1);
    M._emu_set_idle_fastforward(ff);
    const ok = loadHex(hex, hex.length);   /* 0 == success, per test_wasm.mjs */
    /* Did anything actually get loaded? A canary over empty memory compares
       two runs of nothing and reports a confident zero. */
    let codeBytes = 0;
    for (let a = 0; a < 4096; a++) if (M._emu_get_code(a) !== 0) codeBytes++;
    M._emu_advance_to_ns(SIM_NS & 0xFFFFFFFF, 0);
    const sfr = [];
    for (let a = 0x80; a <= 0xff; a++) sfr.push(M._emu_get_sfr(a));
    const iram = [];
    for (let a = 0; a < 128; a++) iram.push(M._emu_get_iram(a));
    const skipped = (M._emu_get_idle_skipped_hi() >>> 0) * 4294967296 + (M._emu_get_idle_skipped_lo() >>> 0);
    snap.push({ ok, codeBytes, pc: M._emu_get_pc(),
      t: (BigInt(M._emu_get_time_ns_hi() >>> 0) << 32n) | BigInt(M._emu_get_time_ns_lo() >>> 0),
      sfr: sfr.join(','), iram: iram.join(','), skipped });
  }
  const [off, on] = snap;
  const name = path.replace(STC + '/', '');
  const diffs = [];
  if (off.pc !== on.pc) diffs.push(`PC ${off.pc} vs ${on.pc}`);
  if (off.t !== on.t) diffs.push(`time ${off.t} vs ${on.t}`);
  if (off.sfr !== on.sfr) diffs.push('SFRs differ');
  if (off.iram !== on.iram) diffs.push('IRAM differs');
  if (off.skipped !== 0) diffs.push(`reference run skipped ${off.skipped}`);
  if (on.ok !== 0) diffs.push(`load failed (${on.ok})`);
  if (on.codeBytes < 16) diffs.push(`only ${on.codeBytes} non-zero code bytes — nothing to compare`);
  if (on.pc === 0) diffs.push('PC never left the reset vector — the image did not run');
  if (on.skipped > 0) idlers++;
  if (diffs.length) { fails++; console.log(`DIFF ${name}: ${diffs.join('; ')}`); }
  else console.log(`ok   ${name.padEnd(52)} code=${String(on.codeBytes).padStart(4)}B pc=${String(on.pc).padStart(5)} slept=${on.skipped}`);
}
console.log(`\n${hexes.length} images, ${idlers} that idle, ${fails} divergent`);
process.exit(fails ? 1 : 0);
