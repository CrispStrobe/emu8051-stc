/**
 * test-avr-debug.mjs — test step-over/step-out with a call/ret program.
 *
 * Program (ATmega328P):
 *   0x0000: LDI R16, 0xAA     ; E0AA
 *   0x0001: RCALL +2          ; D002 (calls 0x0004)
 *   0x0002: LDI R16, 0xBB     ; E0BB (return lands here)
 *   0x0003: RJMP -1           ; CFFF (infinite loop)
 *   0x0004: LDI R16, 0xCC     ; E0CC (subroutine)
 *   0x0005: RET               ; 9508
 */

import { createAvrAdapter } from './avr8js-adapter.mjs';

let pass = 0, fail = 0;
function check(name, ok, detail) {
  if (ok) { console.log(`PASS: ${name}`); pass++; }
  else { console.log(`FAIL: ${name}${detail ? ' — ' + detail : ''}`); fail++; }
}

// Build the test program
const flash = new Uint16Array(16384);
// LDI Rd,K encoding: 1110 KKKK dddd KKKK (d=Rd-16, K split across nibbles)
flash[0] = 0xEA0A;  // LDI R16, 0xAA
flash[1] = 0xD002;  // RCALL +2 → calls address 0x0004
flash[2] = 0xEB0B;  // LDI R16, 0xBB (return point)
flash[3] = 0xCFFF;  // RJMP -1 (infinite loop)
flash[4] = 0xEC0C;  // LDI R16, 0xCC (subroutine body)
flash[5] = 0x9508;  // RET

// ── Test capabilities ──
{
  const adapter = createAvrAdapter({ flash, clockHz: 16_000_000 });
  const caps = adapter.capabilities();
  check('capabilities: steps includes insn', caps.steps.includes('insn'));
  check('capabilities: steps includes over', caps.steps.includes('over'));
  check('capabilities: steps includes out', caps.steps.includes('out'));
  check('capabilities: debugTarget is true', caps.debugTarget === true);
}

// ── Test stepInsn ──
{
  const adapter = createAvrAdapter({ flash: new Uint16Array(flash), clockHz: 16_000_000 });
  check('stepInsn: initial PC is 0', adapter.getPC() === 0);

  adapter.stepInsn(); // LDI R16, 0xAA
  check('stepInsn: PC after LDI', adapter.getPC() === 1);

  adapter.stepInsn(); // RCALL +2 → pushes return, jumps to 0x0004
  check('stepInsn: PC after RCALL', adapter.getPC() === 4,
    `got ${adapter.getPC()}`);
}

// ── Test stepOver ──
{
  const adapter = createAvrAdapter({ flash: new Uint16Array(flash), clockHz: 16_000_000 });

  adapter.stepOver(); // LDI R16, 0xAA — not a call, just steps once
  check('stepOver: PC after LDI (no call)', adapter.getPC() === 1);

  adapter.stepOver(); // RCALL +2 — this IS a call; stepOver should run through the subroutine and stop after RET
  check('stepOver: PC after RCALL (stepped over subroutine)', adapter.getPC() === 2,
    `got ${adapter.getPC()}`);

  // R16 should be 0xCC (set inside the subroutine)
  check('stepOver: subroutine executed (R16=0xCC)',
    adapter.cpu.data[16] === 0xCC, `R16=0x${adapter.cpu.data[16].toString(16)}`);
}

// ── Test stepOut ──
{
  const adapter = createAvrAdapter({ flash: new Uint16Array(flash), clockHz: 16_000_000 });

  // Step into the subroutine first
  adapter.stepInsn(); // LDI R16, 0xAA → PC=1
  adapter.stepInsn(); // RCALL +2 → PC=4 (inside subroutine)
  check('stepOut setup: inside subroutine', adapter.getPC() === 4);

  adapter.stepOut(); // Should run until RET, landing at PC=2
  check('stepOut: returned from subroutine', adapter.getPC() === 2,
    `got ${adapter.getPC()}`);
}

// ── Test getSP changes across call/ret ──
{
  const adapter = createAvrAdapter({ flash: new Uint16Array(flash), clockHz: 16_000_000 });

  const spInit = adapter.getSP();
  adapter.stepInsn(); // LDI — SP unchanged
  check('getSP: unchanged after LDI', adapter.getSP() === spInit);

  adapter.stepInsn(); // RCALL — SP decreases (push return address)
  check('getSP: decreased after RCALL', adapter.getSP() < spInit,
    `sp=${adapter.getSP()}, was ${spInit}`);

  const spInCall = adapter.getSP();
  adapter.stepInsn(); // LDI inside subroutine — SP unchanged
  adapter.stepInsn(); // RET — SP increases back
  check('getSP: restored after RET', adapter.getSP() === spInit,
    `sp=${adapter.getSP()}, want ${spInit}`);
}

console.log(`\n${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
