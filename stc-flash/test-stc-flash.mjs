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
import * as fs from 'fs';

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

// ── Test 5: parseIntelHex rejects bad checksums ──
{
  let threw = false;
  try { parseIntelHex(':03000000020006F4\n:00000001FF'); } catch { threw = true; }
  check('parseIntelHex: rejects bad checksum', threw);
}

// ── Test 6: parseIntelHex rejects empty ──
{
  let threw = false;
  try { parseIntelHex(':00000001FF'); } catch { threw = true; }
  check('parseIntelHex: rejects empty (EOF only)', threw);
}

// ── Test 7: Flash a larger program (02-adc.hex) ──
{
  const fs = await import('fs');
  const adcHex = fs.readFileSync(new URL('../test_images/02-adc.hex', import.meta.url), 'utf8');
  const { hostTransport, bootloader } = createMockBootloader({ magic: 0xD17E, clockHz: 11059200 });
  try {
    const result = await flashStc12(null, adcHex, {
      transport: hostTransport, handshakeBaud: 2400, transferBaud: 115200,
      log: () => {},
    });
    check('flashStc12 02-adc: completed', true);
    check('flashStc12 02-adc: bytes > 01-blink', result.bytes > 100, `got ${result.bytes}`);

    const parsed = parseIntelHex(adcHex);
    let match = true;
    for (let i = 0; i < parsed.image.length; i++) {
      if (bootloader.written[i] !== parsed.image[i]) { match = false; break; }
    }
    check('flashStc12 02-adc: written image matches', match);
  } catch (e) {
    check('flashStc12 02-adc: completed', false, e.message);
  }
}

// ── Test 8: Unknown part magic still programs ──
{
  const { hostTransport, bootloader } = createMockBootloader({
    magic: 0xBEEF, clockHz: 11059200, flashSize: 8192,
  });
  const simpleHex = ':03000000020006F5\n:00000001FF';
  // Parse will give 3 bytes; mock should still work
  try {
    const result = await flashStc12(null, simpleHex, {
      transport: hostTransport, handshakeBaud: 2400, transferBaud: 115200,
      log: () => {},
    });
    check('flashStc12 unknown part: completed', true);
    check('flashStc12 unknown part: model is null', result.model === null);
  } catch (e) {
    check('flashStc12 unknown part: completed', false, e.message);
  }
}

// ── Test 9: Flash multiple hex files (batch consistency) ──
{
  const fs = await import('fs');
  const path = await import('path');
  const dir = new URL('../test_images/', import.meta.url);
  const hexFiles = ['05-scheduler.hex', '07-buzzer.hex', '09-shift-register.hex'];
  let allOk = true;
  for (const name of hexFiles) {
    try {
      const hex = fs.readFileSync(new URL(name, dir), 'utf8');
      const { hostTransport, bootloader } = createMockBootloader({ magic: 0xD17E, clockHz: 11059200 });
      const result = await flashStc12(null, hex, {
        transport: hostTransport, handshakeBaud: 2400, transferBaud: 115200, log: () => {},
      });
      const parsed = parseIntelHex(hex);
      let match = true;
      for (let i = 0; i < parsed.image.length; i++) {
        if (bootloader.written[i] !== parsed.image[i]) { match = false; break; }
      }
      if (!match || !result.bytes) { allOk = false; check(`flash ${name}`, false, 'image mismatch'); }
    } catch (e) {
      allOk = false;
      check(`flash ${name}`, false, e.message);
    }
  }
  check('flashStc12 batch: 3 additional hex files', allOk);
}

// ── Test 10: Packet round-trip (build + parse) ──
{
  const payload = [0x84, 0xFF, 0x00, 0x02, 0x00, 0x00, 0x04];
  const pkt = buildPacket(payload);
  // Simulate reading it back as an MCU packet by swapping direction
  const mcuPkt = new Uint8Array(pkt);
  mcuPkt[2] = 0x68; // change DIR to MCU
  // Recompute checksum
  const body = Array.from(mcuPkt.subarray(2, mcuPkt.length - 3));
  const sum = body.reduce((a, b) => (a + b) & 0xFFFF, 0);
  mcuPkt[mcuPkt.length - 3] = (sum >> 8) & 0xFF;
  mcuPkt[mcuPkt.length - 2] = sum & 0xFF;

  let pos = 0;
  const mockIo = {
    async read(n) {
      const slice = mcuPkt.subarray(pos, pos + n);
      pos += n;
      return slice;
    }
  };
  try {
    const got = await readPacket(mockIo);
    check('packet round-trip: payload matches',
      got.length === payload.length && got.every((b, i) => b === payload[i]));
  } catch (e) {
    check('packet round-trip: payload matches', false, e.message);
  }
}

// ── Test 11: STC12C5A16S2 model identification ──
{
  const { hostTransport, bootloader } = createMockBootloader({
    magic: 0xD168, clockHz: 11059200, flashSize: 16384,
  });
  const hex = fs.readFileSync(new URL('../test_images/01-blink.hex', import.meta.url), 'utf8');
  try {
    const result = await flashStc12(null, hex, {
      transport: hostTransport, handshakeBaud: 2400, transferBaud: 115200, log: () => {},
    });
    check('flashStc12 STC12C5A16S2: model name', result.model === 'STC12C5A16S2',
      `got ${result.model}`);
  } catch (e) {
    check('flashStc12 STC12C5A16S2: completed', false, e.message);
  }
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
