/**
 * mock-bootloader.js — simulated STC12 ISP bootloader for testing.
 *
 * Implements the MCU side of the STC12 ISP protocol as documented in
 * docs/STC-ISP-PROTOCOL.md. Both sides (stc-flash.js and this mock)
 * are written from the same spec = internal consistency test.
 *
 * Usage:
 *   import { createMockBootloader } from './mock-bootloader.js';
 *   const { hostTransport, bootloader } = createMockBootloader({ ... });
 *   await flashStc12(null, hexText, { transport: hostTransport });
 *   console.log(bootloader.written); // the bytes that were "flashed"
 *
 * @module
 */

const FRAME_START = [0x46, 0xB9];
const FRAME_END = 0x16;
const DIR_HOST = 0x6A;
const DIR_MCU  = 0x68;

function buildMcuPacket(payload) {
  const totalLen = payload.length + 6;
  const body = [DIR_MCU, (totalLen >> 8) & 0xFF, totalLen & 0xFF, ...payload];
  const sum = body.reduce((a, b) => (a + b) & 0xFFFF, 0);
  return Uint8Array.from([
    ...FRAME_START, ...body,
    (sum >> 8) & 0xFF, sum & 0xFF,
    FRAME_END,
  ]);
}

function parseHostPacket(data) {
  let i = 0;
  if (data[i] === FRAME_START[0] && data[i + 1] === FRAME_START[1]) i += 2;
  if (data[i] !== DIR_HOST) throw new Error('not a host packet');
  i++; // skip DIR
  const len = (data[i] << 8) | data[i + 1];
  i += 2;
  const payload = data.subarray(i, i + len - 6 + 1);
  return payload;
}

/**
 * Create a mock STC12 bootloader and a transport pair.
 *
 * @param {object} opts
 * @param {number} [opts.magic=0xD17E] - part magic (STC12C5A60S2)
 * @param {number} [opts.clockHz=11059200] - simulated clock
 * @param {number} [opts.bslVersion=0x72] - bootloader version
 * @param {number} [opts.flashSize=61440] - flash size in bytes
 * @returns {{ hostTransport: object, bootloader: object }}
 */
export function createMockBootloader({
  magic = 0xD17E,
  clockHz = 11059200,
  bslVersion = 0x72,
  flashSize = 61440,
} = {}) {
  // Bi-directional byte queues
  let toMcu = [];     // bytes from host → MCU
  let toHost = [];    // bytes from MCU → host

  const written = new Uint8Array(flashSize).fill(0xFF);
  let erased = false;
  let state = 'idle'; // idle → detected → negotiated → erasing → programming → done

  // Generate the status packet the bootloader sends on detection
  function statusPacket(handshakeBaud) {
    const counter = Math.round((clockHz * 7) / (handshakeBaud * 12));
    const payload = new Uint8Array(23);
    payload[0] = 0x00; // status byte
    for (let i = 0; i < 8; i++) {
      payload[1 + 2 * i] = (counter >> 8) & 0xFF;
      payload[2 + 2 * i] = counter & 0xFF;
    }
    payload[17] = bslVersion;
    payload[20] = (magic >> 8) & 0xFF;
    payload[21] = magic & 0xFF;
    payload[22] = 0x00;
    return buildMcuPacket([...payload]);
  }

  // Process a complete host packet
  function handleHostPacket(payload) {
    const cmd = payload[0];

    if (cmd === 0x50 && state === 'detected') {
      // Handshake command
      toHost.push(...buildMcuPacket([0x8F]));
      return;
    }
    if (cmd === 0x8F && state === 'detected') {
      // Baud set command
      toHost.push(...buildMcuPacket([0x8F]));
      return;
    }
    if (cmd === 0x8E && state === 'detected') {
      // Baud switch command
      state = 'negotiated';
      toHost.push(...buildMcuPacket([0x84]));
      return;
    }
    if (cmd === 0x84 && state === 'negotiated') {
      // Erase command
      erased = true;
      written.fill(0xFF);
      state = 'programming';
      toHost.push(...buildMcuPacket([0x00]));
      return;
    }
    if (cmd === 0x00 && state === 'programming') {
      // Write command
      const addrHi = payload[3], addrLo = payload[4];
      const addr = (addrHi << 8) | addrLo;
      const lenHi = payload[5], lenLo = payload[6];
      const blockLen = (lenHi << 8) | lenLo;
      for (let i = 0; i < blockLen && (addr + i) < flashSize; i++) {
        written[addr + i] = payload[7 + i];
      }
      toHost.push(...buildMcuPacket([0x00]));
      return;
    }
    if (cmd === 0x69 && state === 'programming') {
      // Finish command
      state = 'done';
      toHost.push(...buildMcuPacket([0x8D]));
      return;
    }
    if (cmd === 0x82 && state === 'done') {
      // Reset command — no response needed
      return;
    }

    throw new Error(`unexpected command 0x${cmd.toString(16)} in state ${state}`);
  }

  // Try to extract and process a complete host packet from toMcu
  function processInput() {
    // Look for frame start
    let startIdx = -1;
    for (let i = 0; i < toMcu.length - 1; i++) {
      if (toMcu[i] === FRAME_START[0] && toMcu[i + 1] === FRAME_START[1]) {
        startIdx = i;
        break;
      }
    }
    if (startIdx < 0) return false;

    // Check if we have enough bytes for the length field
    if (startIdx + 4 >= toMcu.length) return false;
    const len = (toMcu[startIdx + 3] << 8) | toMcu[startIdx + 4];
    const packetEnd = startIdx + 2 + len; // START(2) + body(len)
    if (packetEnd > toMcu.length) return false;

    const raw = Uint8Array.from(toMcu.slice(startIdx, packetEnd));
    toMcu = toMcu.slice(packetEnd);

    const payload = parseHostPacket(raw);
    handleHostPacket(payload);
    return true;
  }

  // The transport the host side uses
  const hostTransport = {
    async read(n) {
      // Feed any 0x7F bytes to the bootloader first
      while (toMcu.length > 0 && toMcu[0] === 0x7F) {
        toMcu.shift();
        if (state === 'idle') {
          state = 'detected';
          toHost.push(...statusPacket(2400));
        }
      }
      // Process any queued host packets
      while (processInput()) {}

      // Wait for enough bytes in toHost
      const deadline = Date.now() + 4000;
      while (toHost.length < n) {
        if (Date.now() > deadline) throw new Error('mock: read timeout');
        await new Promise(r => setTimeout(r, 1));
        // Process any new input
        while (toMcu.length > 0 && toMcu[0] === 0x7F) {
          toMcu.shift();
          if (state === 'idle') {
            state = 'detected';
            toHost.push(...statusPacket(2400));
          }
        }
        while (processInput()) {}
      }

      const result = Uint8Array.from(toHost.slice(0, n));
      toHost = toHost.slice(n);
      return result;
    },
    async write(data) {
      toMcu.push(...data);
    },
    async close() {},
  };

  return {
    hostTransport,
    bootloader: {
      get written() { return written; },
      get erased() { return erased; },
      get state() { return state; },
    },
  };
}
