/**
 * test_gdb.mjs — verify GDB stub protocol handling without a real GDB.
 * Simulates a GDB client connecting and sending commands.
 */
import { createServer, createConnection } from 'net';
import { fileURLToPath } from 'url';
import { dirname, join } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Load WASM
const createEmu8051 = (await import(join(__dirname, 'build', 'emu8051.js'))).default;
const Module = await createEmu8051();

// Load GDB stub
const { createGdbStub } = await import(join(__dirname, 'gdb-stub.mjs'));

const PORT = 13333 + Math.floor(Math.random() * 1000);
const stub = createGdbStub(Module, { port: PORT });

// Load a tiny program: MOV A,#42h; SJMP $
const hex = ':04000000744280FE44\n:00000001FF\n';
stub.loadHex(hex);
stub.start();

let passed = 0, failed = 0;
function assert(cond, msg) {
    if (cond) { passed++; } else { console.log(`FAIL: ${msg}`); failed++; }
}

// Wait for server to start
await new Promise(r => setTimeout(r, 200));

// Connect as a GDB client
const client = createConnection({ port: PORT });
let response = '';

function sendAndWait(data, ms = 200) {
    return new Promise((resolve) => {
        response = '';
        client.write(data);
        setTimeout(() => resolve(response), ms);
    });
}

client.on('data', (data) => {
    response += data.toString();
});

await new Promise(r => client.on('connect', r));

// Test 1: halt reason query
let resp = await sendAndWait('$?#3f');
assert(resp.includes('S05'), `Halt reason: ${resp.slice(0,20)}`);

// Test 2: read registers
resp = await sendAndWait('$g#67');
assert(resp.includes('$') && resp.length > 10, `Read regs: ${resp.length} chars`);

// Test 3: step
resp = await sendAndWait('$s#73');
assert(resp.includes('S05'), `Step: halted after step`);

// Test 4: read memory
resp = await sendAndWait('$m0000,4#64');
// Should return the hex bytes of our program
assert(resp.includes('7442'), `Read mem: contains 7442 (MOV A,#42h)`);

// Test 5: set breakpoint
resp = await sendAndWait('$Z0,0002,1#67');
assert(resp.includes('OK'), `Set BP: ${resp.slice(0,20)}`);

// Test 6: continue
resp = await sendAndWait('$c#63', 500);
assert(resp.includes('S05'), `Continue: halted at BP`);

client.end();
stub.stop();

console.log(`\nGDB stub: ${passed} passed, ${failed} failed`);
process.exit(failed > 0 ? 1 : 0);
