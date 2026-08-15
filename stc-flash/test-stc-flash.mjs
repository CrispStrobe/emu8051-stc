/**
 * test-stc-flash.mjs — test stc-flash.js against the mock bootloader.
 *
 * Both sides are written from docs/STC-ISP-PROTOCOL.md.
 * Agreement = internal consistency of the spec.
 *
 * Usage: node test-stc-flash.mjs
 */

import { flashStc12, parseIntelHex, buildPacket, readPacket, computeBaud } from './stc-flash.js';
import { createMockBootloader } from './mock-bootloader.js';

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { console.log(`PASS: ${name}`); pass++; }
  else { console.log(`FAIL: ${name}${detail ? ' — ' + detail : ''}`); fail++; }
}

// ── Test 1: Intel HEX parsing ──
{
  const hex = `:03000000020006F5
:0300030002000BED
:01000B0022D2
:00000001FF`;
  const { image, lowest, highest } = parseIntelHex(hex);
  check('parseIntelHex: lowest address', lowest === 0, `got ${lowest}`);
  check('parseIntelHex: highest address', highest === 0x0B, `got 0x${highest.toString(16)}`);
  check('parseIntelHex: image length', image.length === 12, `got ${image.length}`);
  check('parseIntelHex: byte at 0', image[0] === 0x02, `got 0x${image[0].toString(16)}`);
  check('parseIntelHex: byte at 0x0B', image[0x0B] === 0x22, `got 0x${image[0x0B].toString(16)}`);
}

// ── Test 2: Packet construction and parsing ──
{
  const payload = [0x50, 0x00, 0x01];
  const pkt = buildPacket(payload);
  check('buildPacket: starts with 0x46 0xB9', pkt[0] === 0x46 && pkt[1] === 0xB9);
  check('buildPacket: direction byte', pkt[2] === 0x6A);
  check('buildPacket: ends with 0x16', pkt[pkt.length - 1] === 0x16);

  // Verify checksum
  const len = (pkt[3] << 8) | pkt[4];
  const body = Array.from(pkt.subarray(2, 2 + len - 2)); // DIR through payload
  const sum = body.reduce((a, b) => (a + b) & 0xFFFF, 0);
  const givenSum = (pkt[pkt.length - 3] << 8) | pkt[pkt.length - 2];
  check('buildPacket: checksum', sum === givenSum, `calc=${sum}, given=${givenSum}`);
}

// ── Test 3: Baud rate calculation ──
{
  const { brt, csum } = computeBaud(11059200, 115200);
  check('computeBaud: BRT for 115200 at 11.0592MHz', brt === 250, `got ${brt}`);
  check('computeBaud: csum', csum === ((2 * (256 - 250)) & 0xFF), `got ${csum}`);

  let threw = false;
  try { computeBaud(11059200, 2400); } catch { threw = true; }
  check('computeBaud: 2400 baud throws (cannot fit in BRT)', threw);
}

// ── Test 4: Full flash session against mock bootloader ──
{
  // Use the real 01-blink.hex from test_images
  const fs = await import('fs');
  const testHex = fs.readFileSync(new URL('../test_images/01-blink.hex', import.meta.url), 'utf8');

  const { hostTransport, bootloader } = createMockBootloader({
    magic: 0xD17E,
    clockHz: 11059200,
  });

  const logs = [];
  try {
    const result = await flashStc12(null, testHex, {
      transport: hostTransport,
      handshakeBaud: 2400,
      transferBaud: 115200, // mock transport handles this without port reopen
      log: (msg) => logs.push(msg),
    });

    check('flashStc12: completed without error', true);
    check('flashStc12: model identified', result.model === 'STC12C5A60S2',
      `got ${result.model}`);
    check('flashStc12: bytes > 0', result.bytes > 0, `got ${result.bytes}`);
    check('flashStc12: bootloader erased', bootloader.erased);
    check('flashStc12: bootloader state is done', bootloader.state === 'done',
      `got ${bootloader.state}`);

    // Verify the written bytes match the hex
    const parsed = parseIntelHex(testHex);
    let mismatch = -1;
    for (let i = 0; i < parsed.image.length; i++) {
      if (bootloader.written[i] !== parsed.image[i]) { mismatch = i; break; }
    }
    check('flashStc12: written image matches hex', mismatch === -1,
      mismatch >= 0 ? `first mismatch at 0x${mismatch.toString(16)}` : '');

    check('flashStc12: log includes bootloader greeting',
      logs.some(l => l.includes('bootloader')));
    check('flashStc12: log includes done',
      logs.some(l => l.includes('done')));
  } catch (e) {
    check('flashStc12: completed without error', false, e.message);
  }
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
