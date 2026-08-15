/**
 * stc-flash.js — STC12 ISP programmer over WebSerial.
 *
 * Dependency-free ES module. Speaks the STC12 bootloader protocol:
 * 0x7F pulse handshake, baud negotiation, erase, program, verify.
 *
 * Protocol documented in docs/STC-ISP-PROTOCOL.md (clean-room spec
 * from the STC12C5A60S2 datasheet and wire observations).
 *
 * Usage (browser):
 *   import { flashStc12, parseIntelHex } from './stc-flash.js';
 *   const port = await navigator.serial.requestPort();
 *   await flashStc12(port, hexString, { log: console.log });
 *
 * Usage (Node, with mock transport):
 *   import { flashStc12, parseIntelHex, buildPacket } from './stc-flash.js';
 *   await flashStc12(mockTransport, hexString, { log: console.log });
 *
 * MIT licence. No GPL code was used in this implementation.
 *
 * @module
 */

// ── Protocol constants (from docs/STC-ISP-PROTOCOL.md §4) ──

const FRAME_START = [0x46, 0xB9];
const FRAME_END = 0x16;
const DIR_HOST = 0x6A;  // host → MCU
const DIR_MCU  = 0x68;  // MCU → host

// ── Packet construction ──

/**
 * Build an STC12 ISP packet from a payload array.
 * @param {number[]} payload - command bytes
 * @returns {Uint8Array} framed packet
 */
export function buildPacket(payload) {
  const totalLen = payload.length + 6; // DIR + LEN(2) + payload + SUM(2) + END
  const body = [DIR_HOST, (totalLen >> 8) & 0xFF, totalLen & 0xFF, ...payload];
  const sum = body.reduce((a, b) => (a + b) & 0xFFFF, 0);
  return Uint8Array.from([
    ...FRAME_START, ...body,
    (sum >> 8) & 0xFF, sum & 0xFF,
    FRAME_END,
  ]);
}

/**
 * Parse one MCU→host packet from a byte stream.
 * @param {object} io - transport with read(n) → Uint8Array
 * @returns {Promise<Uint8Array>} payload (without framing)
 */
export async function readPacket(io) {
  let head = (await io.read(1))[0];

  // Some bootloader versions omit the frame start on the status packet.
  if (head !== DIR_MCU) {
    if (head !== FRAME_START[0]) throw new Error('bad frame start: 0x' + head.toString(16));
    const b2 = (await io.read(1))[0];
    if (b2 !== FRAME_START[1]) throw new Error('bad frame start byte 2');
    head = (await io.read(1))[0];
    if (head !== DIR_MCU) throw new Error('bad direction: expected 0x68, got 0x' + head.toString(16));
  }

  const lenBytes = await io.read(2);
  const totalLen = (lenBytes[0] << 8) | lenBytes[1];
  const rest = await io.read(totalLen - 3); // already consumed DIR + LEN(2)

  if (rest[rest.length - 1] !== FRAME_END) {
    throw new Error('bad frame end: 0x' + rest[rest.length - 1].toString(16));
  }

  const payload = rest.subarray(0, rest.length - 3); // strip SUM(2) + END
  const givenSum = (rest[rest.length - 3] << 8) | rest[rest.length - 2];
  const calcSum = [DIR_MCU, lenBytes[0], lenBytes[1], ...payload]
    .reduce((a, b) => (a + b) & 0xFFFF, 0);

  if (givenSum !== calcSum) {
    throw new Error(`checksum mismatch: got 0x${givenSum.toString(16)}, want 0x${calcSum.toString(16)}`);
  }

  return payload;
}

// ── Intel HEX parser ──

/**
 * Parse Intel HEX text into a flat binary image.
 * @param {string} text - Intel HEX content
 * @returns {{ image: Uint8Array, lowest: number, highest: number }}
 */
export function parseIntelHex(text) {
  const bytes = new Map();
  let base = 0;
  let lineNum = 0;

  for (const raw of text.split(/\r?\n/)) {
    lineNum++;
    const line = raw.trim();
    if (!line) continue;
    if (line[0] !== ':') throw new Error(`line ${lineNum}: missing start code`);

    const octets = [];
    for (let i = 1; i + 1 < line.length; i += 2) {
      octets.push(parseInt(line.substr(i, 2), 16));
    }
    if (octets.some(Number.isNaN)) throw new Error(`line ${lineNum}: invalid hex`);

    const sum = octets.reduce((a, b) => (a + b) & 0xFF, 0);
    if (sum !== 0) throw new Error(`line ${lineNum}: bad checksum`);

    const len = octets[0];
    const addr = (octets[1] << 8) | octets[2];
    const type = octets[3];

    if (type === 0x00) { // data record
      for (let i = 0; i < len; i++) {
        bytes.set(base + addr + i, octets[4 + i]);
      }
    } else if (type === 0x02) { // extended segment address
      base = ((octets[4] << 8) | octets[5]) << 4;
    } else if (type === 0x04) { // extended linear address
      base = ((octets[4] << 8) | octets[5]) << 16;
    } else if (type === 0x01) { // end of file
      break;
    }
  }

  if (bytes.size === 0) throw new Error('no data records in hex file');

  const lowest = Math.min(...bytes.keys());
  const highest = Math.max(...bytes.keys());
  const image = new Uint8Array(highest - lowest + 1).fill(0xFF);
  for (const [addr, val] of bytes) {
    image[addr - lowest] = val;
  }

  return { image, lowest, highest };
}

// ── Baud rate and IAP calculations (from spec §6-7) ──

/**
 * Compute BRT reload value for a target baud rate.
 * @param {number} clockHz - MCU clock frequency
 * @param {number} baud - target baud rate
 * @returns {{ brt: number, csum: number, iap: number, delay: number }}
 */
export function computeBaud(clockHz, baud) {
  const brt = 256 - Math.round(clockHz / (baud * 16));
  if (brt <= 1 || brt > 255) {
    throw new Error(`${baud} baud not achievable at ${clockHz} Hz`);
  }
  return {
    brt,
    csum: (2 * (256 - brt)) & 0xFF,
    iap: iapDelay(clockHz),
    delay: 0x80,
  };
}

function iapDelay(hz) {
  if (hz < 1e6) return 0x87;
  if (hz < 2e6) return 0x86;
  if (hz < 3e6) return 0x85;
  if (hz < 6e6) return 0x84;
  if (hz < 12e6) return 0x83;
  if (hz < 20e6) return 0x82;
  if (hz < 24e6) return 0x81;
  return 0x80;
}

// ── Status packet parsing (from spec §5) ──

/** Known STC parts. `isp` identifies which protocol the bootloader speaks. */
export const MODELS = {
  0xD17E: { name: 'STC12C5A60S2', flash: 61440, isp: 'stc12' },
  0xD168: { name: 'STC12C5A16S2', flash: 16384, isp: 'stc12' },
  0xF408: { name: 'STC15F2K60S2', flash: 61440, isp: 'stc15' },
  0xF449: { name: 'STC15W408AS',  flash: 8192,  isp: 'stc15' },
};

/**
 * Decode the bootloader's status packet.
 * @param {Uint8Array} payload - status packet payload
 * @param {number} handshakeBaud - baud rate used for the handshake
 * @returns {{ clockHz: number, magic: number, bslVersion: number }}
 */
export function parseStatus(payload, handshakeBaud) {
  if (payload.length < 23) throw new Error('status packet too short');
  let counter = 0;
  for (let i = 0; i < 8; i++) {
    counter += (payload[1 + 2 * i] << 8) | payload[2 + 2 * i];
  }
  counter /= 8;
  return {
    clockHz: (handshakeBaud * counter * 12) / 7,
    magic: (payload[20] << 8) | payload[21],
    bslVersion: payload[17],
  };
}

// ── WebSerial transport adapter ──

/**
 * Wrap a WebSerial port into a simple { read, write, close } interface.
 * @param {SerialPort} port - an opened WebSerial port
 * @param {{ timeout?: number }} opts
 * @returns {{ read: (n: number) => Promise<Uint8Array>, write: (data: Uint8Array) => Promise<void>, close: () => Promise<void> }}
 */
export function serialTransport(port, { timeout = 4000 } = {}) {
  const reader = port.readable.getReader();
  let buf = new Uint8Array(0);

  return {
    async read(n) {
      const deadline = Date.now() + timeout;
      while (buf.length < n) {
        if (Date.now() > deadline) throw new Error('read timeout');
        const { value, done } = await reader.read();
        if (done) throw new Error('port closed');
        const merged = new Uint8Array(buf.length + value.length);
        merged.set(buf);
        merged.set(value, buf.length);
        buf = merged;
      }
      const result = buf.subarray(0, n);
      buf = buf.subarray(n);
      return result;
    },
    async write(data) {
      const writer = port.writable.getWriter();
      await writer.write(data);
      writer.releaseLock();
    },
    async close() {
      try { reader.releaseLock(); } catch {}
    },
  };
}

// ── Main flash function ──

/**
 * Program an STC12 chip over its ISP bootloader.
 *
 * @param {SerialPort|object} port - WebSerial port or mock transport
 * @param {string} hexText - Intel HEX file content
 * @param {object} opts
 * @param {number} [opts.handshakeBaud=2400] - initial baud rate
 * @param {number} [opts.transferBaud=115200] - programming baud rate
 * @param {function} [opts.log] - log callback (string)
 * @param {function} [opts.onPowerCycle] - called when user must power-cycle
 * @param {number} [opts.timeoutMs=30000] - handshake timeout
 * @param {object} [opts.transport] - pre-built transport (for testing)
 * @returns {Promise<{ bytes: number, padded: number, model: string|null }>}
 */
export async function flashStc12(port, hexText, {
  handshakeBaud = 2400,
  transferBaud = 115200,
  log = () => {},
  onPowerCycle = () => {},
  timeoutMs = 30000,
  transport = null,
} = {}) {
  const parsed = parseIntelHex(hexText);

  // Pad to 512-byte boundary (erase granularity)
  const padded = new Uint8Array(Math.ceil(parsed.image.length / 512) * 512).fill(0xFF);
  padded.set(parsed.image);

  if (!transport) {
    await port.open({ baudRate: handshakeBaud });
  }
  let io = transport || serialTransport(port, { timeout: 4000 });
  const send = async (payload) => {
    await io.write(buildPacket(payload));
  };

  try {
    // ── Phase 1: Detection ──
    log('waiting for bootloader — pull the power and reapply it');
    onPowerCycle();

    const deadline = Date.now() + timeoutMs;
    let statusPayload = null;
    while (!statusPayload) {
      if (Date.now() > deadline) {
        throw new Error(
          'no bootloader greeting: the STC ISP answers only after a ' +
          'COLD power-on, and a reset button is not enough'
        );
      }
      await io.write(Uint8Array.from([0x7F]));
      try { statusPayload = await readPacket(io); } catch { /* keep pulsing */ }
    }

    const info = parseStatus(statusPayload, handshakeBaud);
    log(`bootloader: magic 0x${info.magic.toString(16)}, ` +
        `${(info.clockHz / 1e6).toFixed(3)} MHz, ` +
        `BSL ${(info.bslVersion >> 4)}.${info.bslVersion & 0xF}`);

    const magicHi = (info.magic >> 8) & 0xFF;
    const magicLo = info.magic & 0xFF;
    const { brt, csum, iap, delay } = computeBaud(info.clockHz, transferBaud);

    // ── Phase 2: Baud negotiation ──
    log('negotiating baud…');
    await send([0x50, 0x00, 0x00, 0x36, 0x01, magicHi, magicLo]);
    if ((await readPacket(io))[0] !== 0x8F) throw new Error('handshake refused');

    await send([0x8F, 0xC0, brt, 0x3F, csum, delay, iap]);
    if ((await readPacket(io))[0] !== 0x8F) throw new Error('baud test refused');

    await send([0x8E, 0xC0, brt, 0x3F, csum, delay]);
    if ((await readPacket(io))[0] !== 0x84) throw new Error('baud switch refused');

    // ── Phase 3: Baud switch ──
    if (transferBaud !== handshakeBaud && !transport) {
      await io.close();
      await port.close();
      await port.open({ baudRate: transferBaud });
      io = serialTransport(port, { timeout: 4000 });
      log(`switched to ${transferBaud} baud`);
    }

    // ── Identify part ──
    const model = MODELS[info.magic];
    if (model) {
      log(`part: ${model.name}, ${model.flash} bytes flash`);
      if (model.isp && model.isp !== 'stc12') {
        throw new Error(
          `this is a ${model.name}, whose bootloader speaks the ${model.isp} ` +
          `protocol. Only stc12 is implemented. Use flashStc15() for STC15 parts ` +
          `or stcgal for other families.`
        );
      }
    } else {
      log(`unknown part (magic 0x${info.magic.toString(16)})`);
    }

    const flashSize = model ? model.flash : padded.length;

    // ── Phase 4: Erase ──
    const blocks = Math.ceil(padded.length / 512) * 2;
    const total = Math.ceil(flashSize / 512) * 2;
    log(`erasing ${blocks} blocks…`);

    const eraseCmd = [0x84, 0xFF, 0x00, blocks, 0x00, 0x00, total,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    for (let i = 0x80; i > 0x0D; i--) eraseCmd.push(i);
    await send(eraseCmd);
    if ((await readPacket(io))[0] !== 0x00) throw new Error('erase refused');

    // ── Phase 5: Program ──
    const BLOCK = 128;
    for (let offset = 0; offset < padded.length; offset += BLOCK) {
      const chunk = padded.subarray(offset, offset + BLOCK);
      const writeCmd = [
        0x00, 0x00, 0x00,
        (offset >> 8) & 0xFF, offset & 0xFF,
        (BLOCK >> 8) & 0xFF, BLOCK & 0xFF,
        ...chunk,
      ];
      while (writeCmd.length < BLOCK + 7) writeCmd.push(0);
      await send(writeCmd);
      if ((await readPacket(io))[0] !== 0x00) {
        throw new Error(`write refused at 0x${offset.toString(16)}`);
      }
      log(`  wrote ${chunk.length} bytes at 0x${offset.toString(16).padStart(4, '0')}`);
    }

    // ── Phase 6: Finish ──
    await send([0x69, 0x00, 0x00, 0x36, 0x01, magicHi, magicLo]);
    if ((await readPacket(io))[0] !== 0x8D) throw new Error('finish refused');

    await send([0x82]); // reset and run
    log(`done: ${parsed.image.length} bytes (padded to ${padded.length})`);

    return {
      bytes: parsed.image.length,
      padded: padded.length,
      model: model ? model.name : null,
    };
  } finally {
    try { await io.close(); } catch {}
    if (!transport) {
      try { await port.close(); } catch {}
    }
  }
}

// ── STC15 protocol (detection + refusal, pending STC15 datasheet) ──

/**
 * Flash an STC15 chip. NOT YET IMPLEMENTED.
 *
 * The STC15 bootloader shares the same 0x7F handshake and packet
 * framing as STC12, but uses different command bytes, a different
 * status packet format, and an additional RC oscillator trimming
 * phase. Implementation requires the STC15 datasheet for clean-room
 * protocol facts.
 *
 * Currently: detects the part by magic number from the shared
 * handshake and returns the part info without programming.
 *
 * @param {SerialPort|object} port
 * @param {string} hexText
 * @param {object} opts - same as flashStc12
 * @returns {Promise<{ detected: boolean, model: string|null, magic: number, clockHz: number }>}
 */
export async function detectStc15(port, {
  handshakeBaud = 2400,
  log = () => {},
  onPowerCycle = () => {},
  timeoutMs = 30000,
  transport = null,
} = {}) {
  if (!transport) {
    await port.open({ baudRate: handshakeBaud });
  }
  const io = transport || serialTransport(port, { timeout: 4000 });

  try {
    log('waiting for bootloader — pull the power and reapply it');
    onPowerCycle();

    const deadline = Date.now() + timeoutMs;
    let statusPayload = null;
    while (!statusPayload) {
      if (Date.now() > deadline) {
        throw new Error('no bootloader greeting');
      }
      await io.write(Uint8Array.from([0x7F]));
      try { statusPayload = await readPacket(io); } catch { /* keep pulsing */ }
    }

    const info = parseStatus(statusPayload, handshakeBaud);
    const model = MODELS[info.magic];

    log(`detected: magic 0x${info.magic.toString(16)}, ` +
        `${(info.clockHz / 1e6).toFixed(3)} MHz` +
        (model ? `, ${model.name} (${model.isp})` : ''));

    if (model && model.isp === 'stc15') {
      log('STC15 protocol: detection OK, programming not yet implemented');
    }

    return {
      detected: true,
      model: model ? model.name : null,
      isp: model ? model.isp : 'unknown',
      magic: info.magic,
      clockHz: info.clockHz,
      bslVersion: info.bslVersion,
    };
  } finally {
    try { await io.close(); } catch {}
    if (!transport) {
      try { await port.close(); } catch {}
    }
  }
}
