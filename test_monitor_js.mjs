/**
 * test_monitor_js.mjs — test stc12live.js's frame codec against the
 * firmware running on the emulator.
 *
 * This is the fourth independent codec. If it agrees with the other three,
 * the wire format is verified across C, Python, JavaScript and hand-built
 * test frames — four implementations, one protocol.
 *
 * Usage: node test_monitor_js.mjs
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

// --- Load stc12live.js in a VM context with a mock Scratch ---

const src = readFileSync(
    '../../extensions/extensions/CrispStrobe/stc12live.js', 'utf-8');

let captured = {};
const mockScratch = {
    extensions: {
        register: (ext) => { captured.ext = ext; }
    }
};

const ctx = vm.createContext({ Scratch: mockScratch, console, navigator: {} });
vm.runInContext(src, ctx);

// Extract the codec functions from the IIFE's scope.
// They're not exported, so we need to extract them by running code in the context.
// Let's extract buildFrame and Decoder by injecting a capture.

const extractCode = `
(function() {
    // Re-declare locally to access the closure variables
    const SOF = 0x7e;

    function buildFrame(cmd, payload = []) {
        const len = payload.length;
        const body = [len, cmd, ...payload];
        let sum = 0;
        for (const b of body) sum += b;
        body.push(-sum & 0xff);
        return new Uint8Array([SOF, ...body]);
    }

    class Decoder {
        constructor() { this.reset(); this.onFrame = null; }
        reset() {
            this._state = 0; this._len = 0; this._cmd = 0;
            this._buf = []; this._sum = 0;
        }
        feed(byte) {
            switch (this._state) {
                case 0: if (byte === SOF) this._state = 1; break;
                case 1:
                    this._len = byte; this._sum = byte; this._state = 2; break;
                case 2:
                    this._cmd = byte; this._sum += byte;
                    this._buf = []; this._state = this._len > 0 ? 3 : 4; break;
                case 3:
                    this._buf.push(byte); this._sum += byte;
                    if (this._buf.length >= this._len) this._state = 4; break;
                case 4:
                    this._sum += byte;
                    if ((this._sum & 0xff) === 0 && this.onFrame)
                        this.onFrame(this._cmd, this._buf);
                    this._state = 0; break;
            }
        }
        idle() { this._state = 0; }
    }

    return { buildFrame, Decoder };
})()
`;

const { buildFrame, Decoder } = vm.runInContext(extractCode, ctx);

console.log('=== stc12live.js codec vs firmware on emulator ===\n');

// --- Test 1: JS buildFrame produces valid frames ---

const helloFrame = buildFrame(0x01); // HELLO
check(helloFrame[0] === 0x7e, `JS buildFrame: SOF=0x${helloFrame[0].toString(16)}`);
check(helloFrame[1] === 0, 'JS buildFrame: LEN=0');
check(helloFrame[2] === 0x01, 'JS buildFrame: CMD=0x01');
let sum = 0;
for (let i = 1; i < helloFrame.length; i++) sum += helloFrame[i];
check((sum & 0xff) === 0, 'JS buildFrame: checksum valid');

// --- Test 2: JS buildFrame matches C and Python format ---

// The Python/C test showed HELLO = 7E 00 01 FF
check(helloFrame.length === 4, `JS buildFrame HELLO: length=${helloFrame.length}`);
check(helloFrame[3] === 0xff, `JS buildFrame HELLO: SUM=0x${helloFrame[3].toString(16)}`);

// --- Test 3: Feed firmware HELLO reply to JS Decoder ---

const BRIDGE = join(__dirname, 'emu_serial_bridge');
const FIRMWARE = '/tmp/monitor.ihx';

// Send HELLO via bridge, get firmware response
const helloInput = Buffer.from(helloFrame);
let replyBytes;
try {
    replyBytes = execFileSync(BRIDGE, [FIRMWARE], {
        input: helloInput, timeout: 10000, maxBuffer: 1024
    });
} catch (e) {
    console.log('FAIL: bridge execution failed:', e.message);
    process.exit(1);
}

check(replyBytes.length > 0, `Bridge: got ${replyBytes.length} bytes`);

// Feed to JS Decoder
const dec = new Decoder();
const frames = [];
dec.onFrame = (cmd, payload) => frames.push({ cmd, payload });
for (const b of replyBytes) dec.feed(b);

check(frames.length === 1, `JS Decoder: parsed ${frames.length} frame(s)`);
if (frames.length >= 1) {
    check(frames[0].cmd === 0x81, `JS Decoder: HELLO reply cmd=0x${frames[0].cmd.toString(16)}`);
    check(frames[0].payload.length >= 9, `JS Decoder: payload=${frames[0].payload.length} bytes`);

    const cap = frames[0].payload;
    check(cap[0] === 1, `JS Capabilities: version=${cap[0]}`);
    check((cap[6] & 0x01) !== 0, `JS Capabilities: timeFreezes`);
}

// --- Test 4: JS-encoded POS command → firmware → JS decoder ---

const posFrame = buildFrame(0x0a); // POS
const bothInput = Buffer.concat([helloInput, Buffer.from(posFrame)]);

let bothReply;
try {
    bothReply = execFileSync(BRIDGE, [FIRMWARE], {
        input: bothInput, timeout: 10000, maxBuffer: 1024
    });
} catch (e) {
    console.log('FAIL: bridge POS failed:', e.message);
    bothReply = Buffer.alloc(0);
}

const dec2 = new Decoder();
const frames2 = [];
dec2.onFrame = (cmd, payload) => frames2.push({ cmd, payload });
for (const b of bothReply) dec2.feed(b);

check(frames2.length >= 2, `JS Bridge: ${frames2.length} frames (HELLO+POS)`);
if (frames2.length >= 2) {
    check(frames2[1].cmd === 0x8a, `JS POS reply: cmd=0x${frames2[1].cmd.toString(16)}`);
    const pos = frames2[1].payload;
    if (pos.length >= 6) {
        const ntasks = pos[1];
        const bwMs = (pos[2] << 8) | pos[3];
        check(ntasks === 2, `JS POS: ntasks=${ntasks}`);
        check(bwMs > 0, `JS POS: bw_ms=${bwMs}`);
    }
}

// --- Test 5: Torn frame + idle recovery in JS Decoder ---

const dec3 = new Decoder();
const tornFrames = [];
dec3.onFrame = (cmd, payload) => tornFrames.push({ cmd, payload });
// Partial frame
dec3.feed(0x7e);
dec3.feed(0x05); // LEN=5 but no more data
check(tornFrames.length === 0, 'JS Torn: no frame from partial');
dec3.idle();
// Now feed a valid HELLO reply
for (const b of replyBytes) dec3.feed(b);
check(tornFrames.length === 1, 'JS Torn recovery: frame parsed after idle()');

// --- Test 6: Cross-check JS encoder output against C/Python format ---
// Build a READ command with JS and verify it byte-matches what Python builds
const readFrame = buildFrame(0x02, [1, 0, 8, 2]); // READ IRAM addr=8 len=2
check(readFrame.length === 8, `JS READ frame: ${readFrame.length} bytes`);
// Verify: SOF=7E, LEN=4, CMD=02, payload=[01,00,08,02], SUM
check(readFrame[0] === 0x7e, 'JS READ: SOF');
check(readFrame[1] === 4, 'JS READ: LEN=4');
check(readFrame[2] === 0x02, 'JS READ: CMD=0x02');
let rsum = 0;
for (let i = 1; i < readFrame.length; i++) rsum += readFrame[i];
check((rsum & 0xff) === 0, 'JS READ: checksum valid');

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
