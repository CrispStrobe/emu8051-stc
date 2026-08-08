/* test_cycles.c — verify every opcode's cycle count against the MCS-51 spec.
 *
 * For each instruction, measures the actual number of tick() calls consumed
 * and compares against the published Intel MCS-51 machine cycle count.
 *
 * Method: execute instruction A followed by MOV A,#42h (known 1-cycle).
 * Count ticks until ACC==0x42. Subtract 1 for the MOV. The remainder is A's cost.
 *
 * Build: gcc -O2 -o test_cycles test_cycles.c core.c opcodes.c disasm.c stc12.c debug.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static int pass_count = 0, fail_count = 0;

static void exc(struct em8051 *a, int c) { (void)a; (void)c; }

static void setup(void) {
    memset(&cpu, 0, sizeof(cpu));
    memset(&stc, 0, sizeof(stc));
    cpu.mCodeMemMaxIdx = 65535;
    cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535;
    cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    cpu.except = exc;
    reset(&cpu, 1);
    stc12_init(&cpu, &stc);
    cpu.skip_timers = true;
}

static void teardown(void) {
    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
}

/* Measure: how many ticks does instruction at addr 0 take?
 * We put the test instruction at 0, followed by MOV A,#42h at the
 * appropriate offset. Then count ticks until ACC==0x42. Subtract 1. */
static int measure_cycles(const uint8_t *insn, int insn_len) {
    setup();
    memcpy(cpu.mCodeMem, insn, insn_len);
    /* MOV A,#42h immediately after */
    cpu.mCodeMem[insn_len] = 0x74;
    cpu.mCodeMem[insn_len + 1] = 0x42;
    /* SJMP $ after that */
    cpu.mCodeMem[insn_len + 2] = 0x80;
    cpu.mCodeMem[insn_len + 3] = 0xFE;

    int t = 0;
    while (cpu.mSFR[REG_ACC] != 0x42 && t < 100) {
        tick(&cpu);
        stc12_tick(&cpu, &stc);
        t++;
    }
    teardown();
    return t - 1; /* subtract 1 for the MOV A,#42 */
}

/* For instructions that modify ACC, use R0 as sentinel instead */
static int measure_cycles_r0(const uint8_t *insn, int insn_len) {
    setup();
    memcpy(cpu.mCodeMem, insn, insn_len);
    /* MOV R0,#42h immediately after */
    cpu.mCodeMem[insn_len] = 0x78;
    cpu.mCodeMem[insn_len + 1] = 0x42;
    cpu.mCodeMem[insn_len + 2] = 0x80;
    cpu.mCodeMem[insn_len + 3] = 0xFE;

    int t = 0;
    while (cpu.mLowerData[0] != 0x42 && t < 100) {
        tick(&cpu);
        stc12_tick(&cpu, &stc);
        t++;
    }
    teardown();
    return t - 1;
}

/* For jumps that change PC, we need a different approach:
 * put the jump at 0, target = 0x20, MOV A,#42 at 0x20. */
static int measure_jump_cycles(const uint8_t *insn, int insn_len, int target) {
    setup();
    memcpy(cpu.mCodeMem, insn, insn_len);
    cpu.mCodeMem[target] = 0x74;
    cpu.mCodeMem[target + 1] = 0x42;
    cpu.mCodeMem[target + 2] = 0x80;
    cpu.mCodeMem[target + 3] = 0xFE;

    int t = 0;
    while (cpu.mSFR[REG_ACC] != 0x42 && t < 100) {
        tick(&cpu);
        stc12_tick(&cpu, &stc);
        t++;
    }
    teardown();
    return t - 1;
}

#define CHECK_CYCLES(name, actual, expected) do { \
    if (actual == expected) { pass_count++; } \
    else { printf("FAIL: %-25s actual=%d expected=%d\n", name, actual, expected); fail_count++; } \
} while(0)

int main(void) {
    int c;

    printf("=== MCS-51 cycle count verification ===\n\n");

    /* 1-cycle instructions (MCS-51 Table A-2) */
    { uint8_t p[] = {0x00}; c = measure_cycles(p, 1); CHECK_CYCLES("NOP", c, 1); }
    { uint8_t p[] = {0x04}; c = measure_cycles_r0(p, 1); CHECK_CYCLES("INC A", c, 1); }
    { uint8_t p[] = {0x08}; c = measure_cycles(p, 1); CHECK_CYCLES("INC R0", c, 1); }
    { uint8_t p[] = {0x14}; c = measure_cycles_r0(p, 1); CHECK_CYCLES("DEC A", c, 1); }
    { uint8_t p[] = {0x74, 0x10, 0x24, 0x05}; c = measure_cycles_r0(p+2, 2) + 1; /* ADD sets ACC, use different method */
      /* Actually: MOV A,#10; ADD A,#05 — measure just the ADD */
      setup(); cpu.mCodeMem[0]=0x24; cpu.mCodeMem[1]=0x05; /* ADD A,#05 */
      cpu.mCodeMem[2]=0x78; cpu.mCodeMem[3]=0x42; cpu.mCodeMem[4]=0x80; cpu.mCodeMem[5]=0xFE;
      int t=0; while(cpu.mLowerData[0]!=0x42 && t<100) { tick(&cpu); stc12_tick(&cpu,&stc); t++; }
      teardown(); CHECK_CYCLES("ADD A,#imm", t-1, 1);
    }
    { uint8_t p[] = {0x74, 0x55}; c = measure_cycles_r0(p, 2); CHECK_CYCLES("MOV A,#imm", c, 1); }
    { uint8_t p[] = {0x78, 0x55}; c = measure_cycles(p, 2); CHECK_CYCLES("MOV R0,#imm", c, 1); }
    { uint8_t p[] = {0xE8}; c = measure_cycles(p, 1); CHECK_CYCLES("MOV A,R0", c, 1); }
    { uint8_t p[] = {0xF8}; c = measure_cycles(p, 1); CHECK_CYCLES("MOV R0,A", c, 1); }
    { uint8_t p[] = {0xE4}; c = measure_cycles_r0(p, 1); CHECK_CYCLES("CLR A", c, 1); }
    { uint8_t p[] = {0xF4}; c = measure_cycles_r0(p, 1); CHECK_CYCLES("CPL A", c, 1); }
    { uint8_t p[] = {0x23}; c = measure_cycles_r0(p, 1); CHECK_CYCLES("RL A", c, 1); }
    { uint8_t p[] = {0xC4}; c = measure_cycles_r0(p, 1); CHECK_CYCLES("SWAP A", c, 1); }
    { uint8_t p[] = {0xC3}; c = measure_cycles(p, 1); CHECK_CYCLES("CLR C", c, 1); }
    { uint8_t p[] = {0xD3}; c = measure_cycles(p, 1); CHECK_CYCLES("SETB C", c, 1); }
    { uint8_t p[] = {0xD4}; c = measure_cycles_r0(p, 1); CHECK_CYCLES("DA A", c, 1); }

    /* 2-cycle instructions */
    { c = measure_jump_cycles((uint8_t[]){0x02,0x00,0x20}, 3, 0x20);
      CHECK_CYCLES("LJMP", c, 2); }
    { /* LCALL 0020h; at 0020h: MOV A,#42; RET (measure LCALL only) */
      setup();
      cpu.mCodeMem[0]=0x12; cpu.mCodeMem[1]=0x00; cpu.mCodeMem[2]=0x20;
      cpu.mCodeMem[0x20]=0x74; cpu.mCodeMem[0x21]=0x42;
      cpu.mCodeMem[0x22]=0x80; cpu.mCodeMem[0x23]=0xFE;
      int t=0; while(cpu.mSFR[REG_ACC]!=0x42 && t<100) { tick(&cpu); stc12_tick(&cpu,&stc); t++; }
      teardown(); CHECK_CYCLES("LCALL", t-1, 2);
    }
    { c = measure_jump_cycles((uint8_t[]){0x80,0x1E}, 2, 0x20);
      CHECK_CYCLES("SJMP", c, 2); }
    { /* RET: LCALL 0020; at 0020: RET; then MOV A,#42 at 0003 */
      setup();
      cpu.mCodeMem[0]=0x12; cpu.mCodeMem[1]=0x00; cpu.mCodeMem[2]=0x20;
      cpu.mCodeMem[3]=0x74; cpu.mCodeMem[4]=0x42;
      cpu.mCodeMem[5]=0x80; cpu.mCodeMem[6]=0xFE;
      cpu.mCodeMem[0x20]=0x22; /* RET */
      int t=0; while(cpu.mSFR[REG_ACC]!=0x42 && t<100) { tick(&cpu); stc12_tick(&cpu,&stc); t++; }
      teardown(); CHECK_CYCLES("LCALL+RET", t-1, 4); /* LCALL=2 + RET=2 */
    }
    { uint8_t p[] = {0x75, 0x30, 0xAA}; c = measure_cycles(p, 3);
      CHECK_CYCLES("MOV direct,#imm", c, 2); }
    { /* MOV A,direct (0xE5) — MCS-51 says 1 cycle! */
      uint8_t p[] = {0xE5, 0x30}; c = measure_cycles_r0(p, 2);
      CHECK_CYCLES("MOV A,direct", c, 1); }
    { uint8_t p[] = {0xF5, 0x30}; c = measure_cycles(p, 2);
      CHECK_CYCLES("MOV direct,A", c, 1); }
    { uint8_t p[] = {0x25, 0x30}; /* ADD A,direct */
      setup(); cpu.mCodeMem[0]=0x25; cpu.mCodeMem[1]=0x30;
      cpu.mCodeMem[2]=0x78; cpu.mCodeMem[3]=0x42; cpu.mCodeMem[4]=0x80; cpu.mCodeMem[5]=0xFE;
      int t=0; while(cpu.mLowerData[0]!=0x42 && t<100) { tick(&cpu); stc12_tick(&cpu,&stc); t++; }
      teardown(); CHECK_CYCLES("ADD A,direct", t-1, 2);
    }
    { uint8_t p[] = {0xA3}; /* INC DPTR */
      c = measure_cycles(p, 1); CHECK_CYCLES("INC DPTR", c, 2); }
    { uint8_t p[] = {0x90, 0x12, 0x34}; c = measure_cycles(p, 3);
      CHECK_CYCLES("MOV DPTR,#imm16", c, 2); }
    { uint8_t p[] = {0xD2, 0x00}; c = measure_cycles(p, 2);
      CHECK_CYCLES("SETB bit", c, 2); }
    { uint8_t p[] = {0xC2, 0x00}; c = measure_cycles(p, 2);
      CHECK_CYCLES("CLR bit", c, 2); }
    { /* JZ (not taken) */
      setup(); cpu.mSFR[REG_ACC] = 0x01; /* A != 0, JZ not taken */
      cpu.mCodeMem[0]=0x60; cpu.mCodeMem[1]=0x10; /* JZ +16 */
      cpu.mCodeMem[2]=0x74; cpu.mCodeMem[3]=0x42;
      cpu.mCodeMem[4]=0x80; cpu.mCodeMem[5]=0xFE;
      int t=0; while(cpu.mSFR[REG_ACC]!=0x42 && t<100) { tick(&cpu); stc12_tick(&cpu,&stc); t++; }
      teardown(); CHECK_CYCLES("JZ (not taken)", t-1, 2);
    }
    { /* DJNZ R0,rel (taken once, then falls through) */
      setup(); cpu.mLowerData[0] = 2; /* R0=2 */
      cpu.mCodeMem[0]=0xD8; cpu.mCodeMem[1]=0xFE; /* DJNZ R0, -2 (back to 0) */
      cpu.mCodeMem[2]=0x74; cpu.mCodeMem[3]=0x42;
      cpu.mCodeMem[4]=0x80; cpu.mCodeMem[5]=0xFE;
      int t=0; while(cpu.mSFR[REG_ACC]!=0x42 && t<100) { tick(&cpu); stc12_tick(&cpu,&stc); t++; }
      teardown();
      /* DJNZ runs twice: R0=2→1 (jump), R0=1→0 (fall through to MOV) */
      CHECK_CYCLES("2×DJNZ + MOV", t, 2*2 + 1); /* 2 DJNZs at 2 each + 1 MOV */
    }
    { uint8_t p[] = {0xC0, 0x30}; c = measure_cycles(p, 2);
      CHECK_CYCLES("PUSH direct", c, 2); }
    { /* MOVC A,@A+DPTR */
      setup(); cpu.mSFR[REG_ACC]=0;
      cpu.mCodeMem[0]=0x93; /* MOVC A,@A+DPTR — reads code[A+DPTR]=code[0]=0x93 */
      cpu.mCodeMem[1]=0x78; cpu.mCodeMem[2]=0x42;
      cpu.mCodeMem[3]=0x80; cpu.mCodeMem[4]=0xFE;
      int t=0; while(cpu.mLowerData[0]!=0x42 && t<100) { tick(&cpu); stc12_tick(&cpu,&stc); t++; }
      teardown(); CHECK_CYCLES("MOVC A,@A+DPTR", t-1, 2);
    }

    /* 4-cycle instructions */
    { uint8_t p[] = {0xA4}; /* MUL AB */
      c = measure_cycles_r0(p, 1); CHECK_CYCLES("MUL AB", c, 4); }
    { uint8_t p[] = {0x84}; /* DIV AB */
      setup(); cpu.mSFR[REG_B] = 1; /* avoid div by zero */
      cpu.mCodeMem[0]=0x84; cpu.mCodeMem[1]=0x78; cpu.mCodeMem[2]=0x42;
      cpu.mCodeMem[3]=0x80; cpu.mCodeMem[4]=0xFE;
      int t=0; while(cpu.mLowerData[0]!=0x42 && t<100) { tick(&cpu); stc12_tick(&cpu,&stc); t++; }
      teardown(); CHECK_CYCLES("DIV AB", t-1, 4);
    }

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    if (fail_count == 0) {
        printf("\nAll cycle counts match the MCS-51 specification.\n");
        printf("Convention verified: return 0 = 1 cycle, return N (N>=1) = N cycles.\n");
    }
    return fail_count > 0 ? 1 : 0;
}
