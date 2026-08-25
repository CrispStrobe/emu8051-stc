/* The PCON.IDL fast-forward exercised THROUGH the WASM boundary.
 *
 * test_idle_fastforward.c proves the semantics natively. This proves the
 * BUILD: a symbol in the export table is not a working call, and the native
 * oracle cannot see a marshalling mistake. Writing it caught two of mine —
 * calling emu_init() with no argument (stc12_mode defaults to 0, so the
 * guard correctly refused and the skip looked broken), and treating
 * emu_get_code(addr) as a pointer when it is a byte accessor, which left the
 * CPU executing zeros through empty memory while the test read back its own
 * pokes from WASM address 0 and believed them.
 *
 * Run: node test_wasm_idle.mjs   (after make -f Makefile.wasm)
 */
import createEmu8051 from './build/emu8051.js';
const Module = await createEmu8051();
const load_hex = Module.cwrap('emu_load_hex', 'number', ['string', 'number']);

/* Intel HEX for: 0000 SJMP 30 | 000B INC 30h, RETI | 0030 ORL PCON,#01, SJMP 30 */
function hexRecord(addr, bytes) {
  const b = [bytes.length, (addr >> 8) & 0xff, addr & 0xff, 0x00, ...bytes];
  const sum = (0x100 - (b.reduce((a, v) => a + v, 0) & 0xff)) & 0xff;
  return ':' + [...b, sum].map(v => v.toString(16).padStart(2, '0').toUpperCase()).join('');
}
const HEX = [
  hexRecord(0x0000, [0x80, 0x2E]),
  hexRecord(0x000B, [0x05, 0x30, 0x32]),
  hexRecord(0x0030, [0x43, 0x87, 0x01, 0x80, 0xFB]),
  ':00000001FF',
].join('\n');

const NS = 100_000_000n;
let sawIdle = false;

function build() {
  Module._emu_init(1);
  Module._emu_set_part(0);
  Module._emu_reset(1);
  load_hex(HEX, HEX.length);
  Module._emu_set_sfr(0x8E, Module._emu_get_sfr(0x8E) | 0x80);            /* AUXR.T0x12 */
  Module._emu_set_sfr(0x89, (Module._emu_get_sfr(0x89) & 0xF0) | 0x01);   /* TMOD T0 mode 1 */
  const rl = 65536 - 11059;
  Module._emu_set_sfr(0x8A, rl & 0xff);
  Module._emu_set_sfr(0x8C, rl >> 8);
  Module._emu_set_sfr(0x88, Module._emu_get_sfr(0x88) | 0x10);            /* TR0 */
  Module._emu_set_sfr(0xA8, Module._emu_get_sfr(0xA8) | 0x82);            /* EA | ET0 */
  sawIdle = false;
}
function run() {
  for (let t = 1_000_000n; t <= NS; t += 1_000_000n) {
    Module._emu_advance_to_ns(Number(t & 0xFFFFFFFFn), Number(t >> 32n));
    if (Module._emu_core_is_idle() === 1) sawIdle = true;
  }
}
const skipped = () =>
  (BigInt(Module._emu_get_idle_skipped_hi() >>> 0) << 32n) | BigInt(Module._emu_get_idle_skipped_lo() >>> 0);
const ticks = () => Module._emu_get_iram(0x30);

let fail = 0;
const check = (c, msg) => { if (!c) { console.log('FAIL:', msg); fail++; } else console.log('ok:', msg); };

Module._emu_set_idle_fastforward(0);
build(); const t0 = Date.now(); run(); const slowMs = Date.now() - t0;
const slowTicks = ticks(), slowSkip = skipped(), slowIdle = sawIdle;

Module._emu_set_idle_fastforward(1);
build(); const t1 = Date.now(); run(); const fastMs = Date.now() - t1;
const fastTicks = ticks(), fastSkip = skipped();

check(slowIdle && sawIdle, 'the core actually parks on PCON.IDL in both runs');
check(slowSkip === 0n, `off switch works through WASM (skipped ${slowSkip})`);
check(fastSkip > 0n, `fast-forward fires through WASM (skipped ${fastSkip})`);
check(slowTicks === fastTicks, `same ISR count both ways: ${slowTicks} vs ${fastTicks}`);
check(fastTicks > 0, `the firmware actually ran (${fastTicks} ISR entries)`);
console.log(`\nwall: ${slowMs} ms without, ${fastMs} ms with, for 100 ms of sim`);
console.log(fail ? `FAILED (${fail})` : 'PASSED');
process.exit(fail ? 1 : 0);
