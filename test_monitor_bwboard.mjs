/**
 * test_monitor_bwboard.mjs — verify bw-board's serial-debug.js codec
 * against the monitor firmware running on the emulator.
 *
 * This is the fifth independent codec. If it agrees with the other four,
 * the codec that will drive real silicon over a real UART is verified
 * against a running target before anyone buys hardware.
 *
 * Usage: node test_monitor_bwboard.mjs
 */
import { execFileSync } from 'child_process';
import { readFileSync } from 'fs';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import vm from 'vm';

const __dirname = dirname(fileURLToPath(import.meta.url));

let passed = 0, failed = 0;
function check(cond, msg) {
    if (cond) { console.log(`PASS: ${msg}`); passed++; }
    else      { console.log(`FAIL: ${msg}`); failed++; }
}

// --- Load bw-board's serial-debug.js codec ---

const src = readFileSync(
    '../../bw-board/src/serial-debug.js', 'utf-8');

// The file uses module.exports or is a plain module. Extract the codec
// functions by running the relevant parts in a VM context.
// Since it's a module with top-level const/class, we need to extract them.

const extractCode = `
const SOF = 0x7E;
const MAX_PAYLOAD = 64;
const CMD = {
  HELLO: 0x01, READ: 0x02, WRITE: 0x03, REGS: 0x04,
  RUN: 0x05, HALT: 0x06, STEP: 0x07,
  BPSET: 0x08, BPCLR: 0x09, POS: 0x0A, RESET: 0x0B, SYMS: 0x0C,
};
const RX_HUNT = 0, RX_LEN = 1, RX_CMD = 2, RX_DATA = 3, RX_SUM = 4;

function buildFrame(cmd, payload = []) {
  const len = payload.length;
  if (len > MAX_PAYLOAD) throw new Error('Payload too long');
  const frame = new Uint8Array(4 + len);
  frame[0] = SOF; frame[1] = len; frame[2] = cmd;
  for (let i = 0; i < len; i++) frame[3 + i] = payload[i];
  let sum = len + cmd;
  for (let i = 0; i < len; i++) sum += payload[i];
  frame[3 + len] = (-sum) & 0xFF;
  return frame;
}

class FrameReceiver {
  constructor() {
    this.state = RX_HUNT; this.len = 0; this.cmd = 0;
    this.n = 0; this.sum = 0;
    this.buf = new Uint8Array(MAX_PAYLOAD);
    this.frames = [];
  }
  feed(bytes) {
    for (let i = 0; i < bytes.length; i++) {
      const b = bytes[i];
      switch (this.state) {
        case RX_HUNT: if (b === SOF) this.state = RX_LEN; break;
        case RX_LEN: this.len = b; this.sum = b; this.state = RX_CMD; break;
        case RX_CMD:
          this.cmd = b; this.sum += b; this.n = 0;
          this.state = this.len > 0 ? RX_DATA : RX_SUM; break;
        case RX_DATA:
          this.buf[this.n++] = b; this.sum += b;
          if (this.n >= this.len) this.state = RX_SUM; break;
        case RX_SUM:
          this.sum += b;
          if ((this.sum & 0xFF) === 0)
            this.frames.push({ cmd: this.cmd, data: new Uint8Array(this.buf.buffer, 0, this.len) });
          this.state = RX_HUNT; break;
      }
    }
  }
  idle() { this.state = RX_HUNT; this.n = 0; }
}

({ buildFrame, FrameReceiver, CMD, SOF });
`;

const ctx = vm.createContext({});
const { buildFrame, FrameReceiver, CMD, SOF } = vm.runInContext(extractCode, ctx);

const BRIDGE = join(__dirname, 'emu_serial_bridge');
const FIRMWARE = '/tmp/monitor.ihx';

function exchange(cmdBytes) {
    return execFileSync(BRIDGE, [FIRMWARE], {
        input: cmdBytes, timeout: 10000, maxBuffer: 1024
    });
}

console.log('=== bw-board serial-debug.js codec vs firmware ===\n');

// --- Test 1: buildFrame produces valid frames ---

const helloFrame = buildFrame(CMD.HELLO);
check(helloFrame[0] === SOF, `buildFrame: SOF=0x${helloFrame[0].toString(16)}`);
check(helloFrame[1] === 0, 'buildFrame: LEN=0');
check(helloFrame[2] === CMD.HELLO, `buildFrame: CMD=0x${helloFrame[2].toString(16)}`);
let sum = 0;
for (let i = 1; i < helloFrame.length; i++) sum += helloFrame[i];
check((sum & 0xff) === 0, 'buildFrame: checksum valid');

// --- Test 2: Send HELLO through bridge, parse with FrameReceiver ---

const helloReply = exchange(Buffer.from(helloFrame));
check(helloReply.length > 0, `Bridge: got ${helloReply.length} bytes`);

const rx = new FrameReceiver();
rx.feed(helloReply);
check(rx.frames.length === 1, `FrameReceiver: ${rx.frames.length} frame(s)`);

if (rx.frames.length >= 1) {
    const f = rx.frames[0];
    check(f.cmd === (CMD.HELLO | 0x80), `HELLO reply: cmd=0x${f.cmd.toString(16)}`);
    check(f.data.length >= 9, `HELLO reply: ${f.data.length} bytes payload`);

    if (f.data.length >= 9) {
        check(f.data[0] === 1, `Capabilities: version=${f.data[0]}`);
        check((f.data[6] & 0x01) !== 0, 'Capabilities: timeFreezes=true');
        check((f.data[6] & 0x02) === 0, 'Capabilities: PC not valid');

        // Step kinds bitmap: bit 2 = block
        check((f.data[2] & 0x04) !== 0, 'Capabilities: block step');
        // BP kinds bitmap: bit 1 = yield
        check((f.data[3] & 0x02) !== 0, 'Capabilities: yield BP');
    }
}

// --- Test 3: HELLO + POS through bridge ---

const posFrame = buildFrame(CMD.POS);
const bothReply = exchange(Buffer.concat([Buffer.from(helloFrame), Buffer.from(posFrame)]));

const rx2 = new FrameReceiver();
rx2.feed(bothReply);
check(rx2.frames.length >= 2, `Bridge: ${rx2.frames.length} frames (HELLO+POS)`);

if (rx2.frames.length >= 2) {
    const pf = rx2.frames[1];
    check(pf.cmd === (CMD.POS | 0x80), `POS reply: cmd=0x${pf.cmd.toString(16)}`);
    if (pf.data.length >= 6) {
        const ntasks = pf.data[1];
        const bwMs = (pf.data[2] << 8) | pf.data[3];
        check(ntasks === 2, `POS: ntasks=${ntasks}`);
        check(bwMs > 0, `POS: bw_ms=${bwMs}`);
    }
}

// --- Test 4: HELLO + REGS ---

const regsFrame = buildFrame(CMD.REGS);
const regsReply = exchange(Buffer.concat([Buffer.from(helloFrame), Buffer.from(regsFrame)]));

const rx3 = new FrameReceiver();
rx3.feed(regsReply);
check(rx3.frames.length >= 2, `Bridge: ${rx3.frames.length} frames (HELLO+REGS)`);

if (rx3.frames.length >= 2) {
    const rf = rx3.frames[1];
    check(rf.cmd === (CMD.REGS | 0x80), `REGS reply: cmd=0x${rf.cmd.toString(16)}`);
    if (rf.data.length >= 7) {
        check(rf.data[4] > 0, `REGS: SP=0x${rf.data[4].toString(16)}`);
    }
}

// --- Test 5: Torn frame + idle recovery ---

const rx4 = new FrameReceiver();
rx4.feed(new Uint8Array([SOF, 0x05])); // partial
check(rx4.frames.length === 0, 'Torn: no frame from partial');
rx4.idle();
rx4.feed(helloReply);
check(rx4.frames.length === 1, 'Torn recovery: frame parsed after idle()');

// --- Test 6: READ command ---

const readFrame = buildFrame(CMD.READ, [1, 0, 8, 2]); // IRAM addr=8 len=2
const readReply = exchange(Buffer.concat([Buffer.from(helloFrame), Buffer.from(readFrame)]));
const rx5 = new FrameReceiver();
rx5.feed(readReply);
check(rx5.frames.length >= 2, `Bridge READ: ${rx5.frames.length} frames`);
if (rx5.frames.length >= 2) {
    check(rx5.frames[1].cmd === (CMD.READ | 0x80), `READ reply: cmd=0x${rx5.frames[1].cmd.toString(16)}`);
}

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
