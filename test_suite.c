/* test_suite.c — comprehensive firmware test suite.
 *
 * 50+ synthetic test programs loaded as inline machine code, each
 * exercising a specific opcode or peripheral pattern. These run
 * WITHOUT the compiler toolchain — pure machine code.
 *
 * Build: gcc -O2 -o test_suite test_suite.c core.c opcodes.c disasm.c stc12.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static int pass_count = 0, fail_count = 0;

static void exc(struct em8051 *a, int c) { (void)a; (void)c; }

#define PASS(msg) do { pass_count++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); fail_count++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(msg); else FAIL(msg); } while(0)

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

static void load_and_run(const uint8_t *prog, int len, int clocks) {
    memcpy(cpu.mCodeMem, prog, len);
    for (int i = 0; i < clocks; i++) {
        tick(&cpu);
        stc12_tick(&cpu, &stc);
    }
}

#define ACC (cpu.mSFR[REG_ACC])
#define B_REG (cpu.mSFR[REG_B])
#define PSW (cpu.mSFR[REG_PSW])
#define SP (cpu.mSFR[REG_SP])
#define DPL (cpu.mSFR[REG_DPL])
#define DPH (cpu.mSFR[REG_DPH])
#define P1 (cpu.mSFR[REG_P1])
#define P3 (cpu.mSFR[REG_P3])
#define R(n) (cpu.mLowerData[n])
#define IRAM(a) (cpu.mLowerData[a])

/* ================================================================== *
 * Arithmetic instructions                                             *
 * ================================================================== */

static void test_add_immediate(void) {
    setup();
    /* MOV A,#30h; ADD A,#12h; SJMP $ */
    uint8_t p[] = { 0x74, 0x30, 0x24, 0x12, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x42, "ADD A,#imm: 30h+12h=42h");
    CHECK(!(PSW & PSWMASK_C), "ADD A,#imm: no carry");
    teardown();
}

static void test_add_carry(void) {
    setup();
    uint8_t p[] = { 0x74, 0xFF, 0x24, 0x02, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x01, "ADD A,#imm: FFh+02h=01h (wrap)");
    CHECK(PSW & PSWMASK_C, "ADD A,#imm: carry set on overflow");
    teardown();
}

static void test_addc(void) {
    setup();
    /* MOV A,#FFh; ADD A,#01h (sets C); MOV A,#00; ADDC A,#00 */
    uint8_t p[] = { 0x74, 0xFF, 0x24, 0x01, 0x74, 0x00, 0x34, 0x00, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x01, "ADDC: 00h+00h+C=01h");
    teardown();
}

static void test_subb(void) {
    setup();
    /* CLR C; MOV A,#50h; SUBB A,#30h */
    uint8_t p[] = { 0xC3, 0x74, 0x50, 0x94, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x20, "SUBB: 50h-30h=20h");
    CHECK(!(PSW & PSWMASK_C), "SUBB: no borrow");
    teardown();
}

static void test_subb_borrow(void) {
    setup();
    /* CLR C; MOV A,#10h; SUBB A,#30h */
    uint8_t p[] = { 0xC3, 0x74, 0x10, 0x94, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0xE0, "SUBB: 10h-30h=E0h (underflow)");
    CHECK(PSW & PSWMASK_C, "SUBB: borrow set");
    teardown();
}

static void test_inc_dec(void) {
    setup();
    /* MOV A,#7Fh; INC A */
    uint8_t p[] = { 0x74, 0x7F, 0x04, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x80, "INC A: 7Fh->80h");
    teardown();
}

static void test_inc_wrap(void) {
    setup();
    uint8_t p[] = { 0x74, 0xFF, 0x04, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x00, "INC A: FFh->00h (wrap)");
    teardown();
}

static void test_dec(void) {
    setup();
    uint8_t p[] = { 0x74, 0x00, 0x14, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0xFF, "DEC A: 00h->FFh (wrap)");
    teardown();
}

static void test_mul(void) {
    setup();
    /* MOV A,#12h; MOV B,#0Ah; MUL AB */
    uint8_t p[] = { 0x74, 0x12, 0x75, 0xF0, 0x0A, 0xA4, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    /* 0x12 * 0x0A = 0x00B4 -> A=B4, B=00 */
    CHECK(ACC == 0xB4, "MUL: A=low byte");
    CHECK(B_REG == 0x00, "MUL: B=high byte");
    teardown();
}

static void test_mul_overflow(void) {
    setup();
    uint8_t p[] = { 0x74, 0x80, 0x75, 0xF0, 0x80, 0xA4, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    /* 0x80 * 0x80 = 0x4000 -> A=00, B=40 */
    CHECK(ACC == 0x00, "MUL overflow: A=00");
    CHECK(B_REG == 0x40, "MUL overflow: B=40h");
    CHECK(PSW & PSWMASK_OV, "MUL overflow: OV set");
    teardown();
}

static void test_div(void) {
    setup();
    /* MOV A,#FBh; MOV B,#0Ch; DIV AB */
    uint8_t p[] = { 0x74, 0xFB, 0x75, 0xF0, 0x0C, 0x84, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    /* 251 / 12 = 20 remainder 11 */
    CHECK(ACC == 20, "DIV: quotient=20");
    CHECK(B_REG == 11, "DIV: remainder=11");
    teardown();
}

static void test_div_by_zero(void) {
    setup();
    uint8_t p[] = { 0x74, 0x42, 0x75, 0xF0, 0x00, 0x84, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(PSW & PSWMASK_OV, "DIV/0: OV set");
    teardown();
}

static void test_da(void) {
    setup();
    /* MOV A,#56h; ADD A,#67h; DA A */
    uint8_t p[] = { 0x74, 0x56, 0x24, 0x67, 0xD4, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    /* BCD: 56+67=123 -> A=23h, C=1 */
    CHECK(ACC == 0x23, "DA: BCD 56+67=123, A=23h");
    CHECK(PSW & PSWMASK_C, "DA: carry (hundreds digit)");
    teardown();
}

/* ================================================================== *
 * Logic instructions                                                  *
 * ================================================================== */

static void test_anl(void) {
    setup();
    uint8_t p[] = { 0x74, 0xF0, 0x54, 0x0F, 0x80, 0xFE }; /* ANL A,#0Fh */
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x00, "ANL: F0h & 0Fh = 00h");
    teardown();
}

static void test_orl(void) {
    setup();
    uint8_t p[] = { 0x74, 0xF0, 0x44, 0x0F, 0x80, 0xFE }; /* ORL A,#0Fh */
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0xFF, "ORL: F0h | 0Fh = FFh");
    teardown();
}

static void test_xrl(void) {
    setup();
    uint8_t p[] = { 0x74, 0xAA, 0x64, 0xFF, 0x80, 0xFE }; /* XRL A,#FFh */
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x55, "XRL: AAh ^ FFh = 55h");
    teardown();
}

static void test_cpl(void) {
    setup();
    uint8_t p[] = { 0x74, 0x55, 0xF4, 0x80, 0xFE }; /* CPL A */
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0xAA, "CPL A: 55h -> AAh");
    teardown();
}

static void test_rl_rr(void) {
    setup();
    uint8_t p[] = { 0x74, 0x81, 0x23, 0x80, 0xFE }; /* RL A */
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x03, "RL: 81h -> 03h");
    teardown();
}

static void test_swap(void) {
    setup();
    uint8_t p[] = { 0x74, 0x12, 0xC4, 0x80, 0xFE }; /* SWAP A */
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x21, "SWAP: 12h -> 21h");
    teardown();
}

/* ================================================================== *
 * Data transfer                                                       *
 * ================================================================== */

static void test_mov_rn(void) {
    setup();
    /* MOV R0,#42h; MOV A,R0 */
    uint8_t p[] = { 0x78, 0x42, 0xE8, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x42, "MOV A,R0: R0=42h -> A=42h");
    CHECK(R(0) == 0x42, "MOV R0,#42h: R0=42h");
    teardown();
}

static void test_mov_indirect(void) {
    setup();
    /* MOV R0,#30h; MOV @R0,#99h; MOV A,@R0 */
    uint8_t p[] = { 0x78, 0x30, 0x76, 0x99, 0xE6, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x99, "MOV A,@R0: IRAM[30h]=99h");
    CHECK(IRAM(0x30) == 0x99, "MOV @R0,#99h: IRAM[30h]=99h");
    teardown();
}

static void test_mov_direct(void) {
    setup();
    /* MOV 30h,#ABh; MOV A,30h */
    uint8_t p[] = { 0x75, 0x30, 0xAB, 0xE5, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0xAB, "MOV A,direct: IRAM[30h]=ABh");
    teardown();
}

static void test_xch(void) {
    setup();
    /* MOV A,#12h; MOV R0,#34h; XCH A,R0 */
    uint8_t p[] = { 0x74, 0x12, 0x78, 0x34, 0xC8, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    /* Note: XCH A,R0 is opcode C8 — exchanges A and R0 */
    CHECK(ACC == 0x34, "XCH A,R0: A was 12h, R0 was 34h -> A=34h");
    CHECK(R(0) == 0x12, "XCH A,R0: R0=12h");
    teardown();
}

static void test_movc(void) {
    setup();
    /* MOV DPTR,#0010h; CLR A; MOVC A,@A+DPTR */
    /* At address 0010h put 0x42 */
    uint8_t p[] = { 0x90, 0x00, 0x10, 0xE4, 0x93, 0x80, 0xFE };
    cpu.mCodeMem[0x10] = 0x42;
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x42, "MOVC A,@A+DPTR: code[0010h]=42h");
    teardown();
}

static void test_movx(void) {
    setup();
    /* MOV DPTR,#1234h; MOV A,#ABh; MOVX @DPTR,A; CLR A; MOVX A,@DPTR */
    uint8_t p[] = { 0x90, 0x12, 0x34, 0x74, 0xAB, 0xF0, 0xE4, 0xE0, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 40);
    CHECK(ACC == 0xAB, "MOVX @DPTR round-trip: xdata[1234h]=ABh");
    CHECK(cpu.mExtData[0x1234] == 0xAB, "MOVX: xdata written");
    teardown();
}

static void test_push_pop(void) {
    setup();
    /* MOV 30h,#55h; PUSH 30h; POP ACC */
    uint8_t p[] = { 0x75, 0x30, 0x55, 0xC0, 0x30, 0xD0, 0xE0, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x55, "PUSH/POP: ACC=55h");
    teardown();
}

/* ================================================================== *
 * Branching                                                           *
 * ================================================================== */

static void test_ajmp(void) {
    setup();
    /* AJMP 0005h (01 05); NOP; NOP; NOP; MOV A,#99h; SJMP $ */
    uint8_t p[] = { 0x01, 0x05, 0x00, 0x00, 0x00, 0x74, 0x99, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x99, "AJMP: jumped to 0005h");
    teardown();
}

static void test_ljmp(void) {
    setup();
    /* LJMP 0100h; ... at 0100h: MOV A,#77h; SJMP $ */
    uint8_t p[] = { 0x02, 0x01, 0x00 };
    memcpy(cpu.mCodeMem, p, sizeof(p));
    cpu.mCodeMem[0x100] = 0x74;
    cpu.mCodeMem[0x101] = 0x77;
    cpu.mCodeMem[0x102] = 0x80;
    cpu.mCodeMem[0x103] = 0xFE;
    for (int i = 0; i < 30; i++) { tick(&cpu); stc12_tick(&cpu, &stc); }
    CHECK(ACC == 0x77, "LJMP: jumped to 0100h");
    teardown();
}

static void test_sjmp(void) {
    setup();
    /* SJMP +3; NOP; NOP; NOP; MOV A,#33h; SJMP $ */
    uint8_t p[] = { 0x80, 0x03, 0x00, 0x00, 0x00, 0x74, 0x33, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x33, "SJMP: skipped 3 NOPs");
    teardown();
}

static void test_jz_jnz(void) {
    setup();
    /* MOV A,#00h; JZ +2; MOV A,#FF; MOV A,#42h; SJMP $ */
    uint8_t p[] = { 0x74, 0x00, 0x60, 0x02, 0x74, 0xFF, 0x74, 0x42, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x42, "JZ: A=0 -> jump taken, skip MOV A,#FFh");
    teardown();
}

static void test_jnz(void) {
    setup();
    /* MOV A,#01h; JNZ +2; MOV A,#FFh; MOV A,#42h; SJMP $ */
    uint8_t p[] = { 0x74, 0x01, 0x70, 0x02, 0x74, 0xFF, 0x74, 0x42, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x42, "JNZ: A=1 -> jump taken");
    teardown();
}

static void test_cjne(void) {
    setup();
    /* MOV A,#42h; CJNE A,#42h,+2; MOV A,#01; SJMP $ */
    uint8_t p[] = { 0x74, 0x42, 0xB4, 0x42, 0x02, 0x74, 0x01, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x01, "CJNE: A==42h -> no jump");
    teardown();
}

static void test_djnz(void) {
    setup();
    /* MOV R0,#03h; loop: DJNZ R0,loop; MOV A,#99h; SJMP $ */
    uint8_t p[] = { 0x78, 0x03, 0xD8, 0xFE, 0x74, 0x99, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x99, "DJNZ: counted down 3->0");
    CHECK(R(0) == 0x00, "DJNZ: R0=0 after loop");
    teardown();
}

static void test_acall_ret(void) {
    setup();
    /* ACALL 0008h; SJMP $; ... at 0008h: MOV A,#55h; RET */
    uint8_t p[] = { 0x11, 0x08, 0x80, 0xFE, 0x00, 0x00, 0x00, 0x00,
                    0x74, 0x55, 0x22 };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x55, "ACALL/RET: subroutine returned with A=55h");
    CHECK(cpu.mPC == 0x02, "ACALL/RET: returned to correct address");
    teardown();
}

static void test_lcall_ret(void) {
    setup();
    /* LCALL 0200h; SJMP $; at 0200h: MOV A,#77h; RET */
    uint8_t p[] = { 0x12, 0x02, 0x00, 0x80, 0xFE };
    memcpy(cpu.mCodeMem, p, sizeof(p));
    cpu.mCodeMem[0x200] = 0x74;
    cpu.mCodeMem[0x201] = 0x77;
    cpu.mCodeMem[0x202] = 0x22; /* RET */
    for (int i = 0; i < 40; i++) { tick(&cpu); stc12_tick(&cpu, &stc); }
    CHECK(ACC == 0x77, "LCALL/RET: A=77h");
    CHECK(cpu.mPC == 0x03, "LCALL/RET: returned to 0003h");
    teardown();
}

/* ================================================================== *
 * Bit operations                                                      *
 * ================================================================== */

static void test_setb_clr_bit(void) {
    setup();
    /* SETB P1.0 (D2 90); CLR P1.1 (C2 91) */
    uint8_t p[] = { 0xD2, 0x90, 0xC2, 0x91, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(P1 & 0x01, "SETB P1.0: bit 0 set");
    CHECK(!(P1 & 0x02), "CLR P1.1: bit 1 cleared");
    teardown();
}

static void test_jb_jnb(void) {
    setup();
    /* SETB 20h.0; JB 00h,+2; MOV A,#FFh; MOV A,#42h; SJMP $ */
    /* Bit 00h = RAM byte 20h bit 0 */
    uint8_t p[] = { 0xD2, 0x00, 0x20, 0x00, 0x02, 0x74, 0xFF, 0x74, 0x42, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x42, "JB: bit set -> jump taken");
    teardown();
}

static void test_jbc(void) {
    setup();
    /* SETB 20h.0; JBC 00h,+2; MOV A,#FFh; MOV A,#42h; SJMP $ */
    uint8_t p[] = { 0xD2, 0x00, 0x10, 0x00, 0x02, 0x74, 0xFF, 0x74, 0x42, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x42, "JBC: bit was set -> jump + clear");
    CHECK(!(IRAM(0x20) & 0x01), "JBC: bit cleared after jump");
    teardown();
}

static void test_mov_c_bit(void) {
    setup();
    /* SETB C; MOV 00h,C — set bit 0 of byte 20h from carry */
    uint8_t p[] = { 0xD3, 0x92, 0x00, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x20) & 0x01, "MOV bit,C: bit set from carry");
    teardown();
}

/* ================================================================== *
 * Register bank switching                                             *
 * ================================================================== */

static void test_bank_switch(void) {
    setup();
    /* MOV R0,#11h (bank 0); MOV PSW,#08h (bank 1); MOV R0,#22h; MOV A,R0 */
    uint8_t p[] = { 0x78, 0x11, 0x75, 0xD0, 0x08, 0x78, 0x22, 0xE8, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x22, "Bank switch: A=22h (bank 1 R0)");
    CHECK(IRAM(0x00) == 0x11, "Bank switch: bank 0 R0 still 11h");
    CHECK(IRAM(0x08) == 0x22, "Bank switch: bank 1 R0=22h at addr 08h");
    teardown();
}

/* ================================================================== *
 * DPTR operations                                                     *
 * ================================================================== */

static void test_dptr_inc(void) {
    setup();
    /* MOV DPTR,#00FFh; INC DPTR -> 0100h */
    uint8_t p[] = { 0x90, 0x00, 0xFF, 0xA3, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(DPH == 0x01, "INC DPTR: DPH=01 after FFh->100h");
    CHECK(DPL == 0x00, "INC DPTR: DPL=00");
    teardown();
}

/* ================================================================== *
 * STC12 SFR access                                                    *
 * ================================================================== */

static void test_auxr_write_read(void) {
    setup();
    /* MOV AUXR,#80h; MOV A,AUXR */
    uint8_t p[] = { 0x75, 0x8E, 0x80, 0xE5, 0x8E, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x80, "AUXR: write 80h, read back");
    CHECK(cpu.mSFR[STC_REG_AUXR] == 0x80, "AUXR: SFR set");
    teardown();
}

static void test_p4_write(void) {
    setup();
    /* MOV P4,#55h */
    uint8_t p[] = { 0x75, 0xC0, 0x55, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(cpu.mSFR[STC_REG_P4] == 0x55, "P4: write 55h");
    teardown();
}

static void test_port_mode_regs(void) {
    setup();
    /* MOV P1M0,#03h; MOV P1M1,#0Ch */
    uint8_t p[] = { 0x75, 0x92, 0x03, 0x75, 0x91, 0x0C, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(cpu.mSFR[STC_REG_P1M0] == 0x03, "P1M0: 03h");
    CHECK(cpu.mSFR[STC_REG_P1M1] == 0x0C, "P1M1: 0Ch");
    teardown();
}

static void test_adc_contr_write(void) {
    setup();
    stc12_set_adc_input(&stc, 2, 500);
    /* MOV ADC_CONTR,#E2h (power+speed11+start+ch2) */
    uint8_t p[] = { 0x75, 0xBC, 0xEA, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 200);
    CHECK(cpu.mSFR[STC_REG_ADC_CONTR] & ADC_FLAG, "ADC via opcode: FLAG set");
    uint16_t result = ((uint16_t)cpu.mSFR[STC_REG_ADC_RES] << 2) |
                      (cpu.mSFR[STC_REG_ADC_RESL] & 0x03);
    CHECK(result == 500, "ADC via opcode: result=500");
    teardown();
}

static void test_ccon_write(void) {
    setup();
    /* MOV CCON,#40h (set CR) */
    uint8_t p[] = { 0x75, 0xD8, 0x40, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(cpu.mSFR[STC_REG_CCON] & CCON_CR, "CCON: CR set via opcode");
    /* PCA counter should have started */
    uint16_t pca = cpu.mSFR[STC_REG_CL] | (cpu.mSFR[STC_REG_CH] << 8);
    CHECK(pca > 0, "PCA: counter running after CR set");
    teardown();
}

/* ================================================================== *
 * Interrupts                                                          *
 * ================================================================== */

static void test_timer0_interrupt(void) {
    setup();
    /* Layout:
     * 0000: LJMP 0020h          (main code at 0020h, out of ISR vector space)
     * 000B: INC R7; RETI        (Timer 0 ISR)
     * 0020: main code           */

    /* LJMP 0020h at 0000 */
    cpu.mCodeMem[0x00] = 0x02;
    cpu.mCodeMem[0x01] = 0x00;
    cpu.mCodeMem[0x02] = 0x20;

    /* ISR at 000Bh: INC R7; RETI */
    cpu.mCodeMem[0x0B] = 0x0F; /* INC R7 */
    cpu.mCodeMem[0x0C] = 0x32; /* RETI */

    /* Main at 0020h */
    uint8_t main_code[] = {
        0x75, 0x8E, 0x80,  /* MOV AUXR,#80h (T0x12=1, 1T mode) */
        0x75, 0x89, 0x02,  /* MOV TMOD,#02h (mode 2 auto-reload) */
        0x75, 0x8C, 0xF0,  /* MOV TH0,#F0h (reload, overflow every 16 ticks) */
        0x75, 0x8A, 0xF0,  /* MOV TL0,#F0h */
        0x75, 0xA8, 0x82,  /* MOV IE,#82h (EA+ET0) */
        0xD2, 0x8C,        /* SETB TR0 */
        0x80, 0xFE,        /* SJMP $ */
    };
    memcpy(cpu.mCodeMem + 0x20, main_code, sizeof(main_code));

    /* Run enough for several overflows (16 ticks per overflow in 1T) */
    for (int i = 0; i < 500; i++) { tick(&cpu); stc12_tick(&cpu, &stc); }

    CHECK(R(7) > 0, "Timer 0 ISR: R7 incremented by interrupts");
    CHECK(R(7) > 5, "Timer 0 ISR: multiple interrupts fired");
    teardown();
}

static void test_ext_int0(void) {
    setup();
    /* Layout: LJMP 0020h at 0000; ISR at 0003; main at 0020 */
    cpu.mCodeMem[0x00] = 0x02; cpu.mCodeMem[0x01] = 0x00; cpu.mCodeMem[0x02] = 0x20;
    cpu.mCodeMem[0x03] = 0x74; cpu.mCodeMem[0x04] = 0x99; cpu.mCodeMem[0x05] = 0x32;

    uint8_t main_code[] = {
        0x75, 0xA8, 0x81,  /* MOV IE,#81h (EA+EX0) */
        0x75, 0x88, 0x02,  /* MOV TCON,#02h (IE0=1, force INT0) */
        0x80, 0xFE,        /* SJMP $ */
    };
    memcpy(cpu.mCodeMem + 0x20, main_code, sizeof(main_code));

    for (int i = 0; i < 100; i++) { tick(&cpu); stc12_tick(&cpu, &stc); }
    CHECK(ACC == 0x99, "EXT INT0: ISR executed, A=99h");
    teardown();
}

/* ================================================================== *
 * Parity                                                              *
 * ================================================================== */

static void test_parity(void) {
    setup();
    uint8_t p[] = { 0x74, 0x03, 0x80, 0xFE }; /* MOV A,#03h -> 2 bits set, P=0 */
    load_and_run(p, sizeof(p), 10);
    CHECK(!(PSW & PSWMASK_P), "Parity: A=03h (even) -> P=0");
    teardown();
}

static void test_parity_odd(void) {
    setup();
    uint8_t p[] = { 0x74, 0x07, 0x80, 0xFE }; /* MOV A,#07h -> 3 bits set, P=1 */
    load_and_run(p, sizeof(p), 10);
    CHECK(PSW & PSWMASK_P, "Parity: A=07h (odd) -> P=1");
    teardown();
}

/* ================================================================== *
 * Main                                                                *
 * ================================================================== */

/* ================================================================== *
 * Indirect @Ri addressing modes                                       *
 * ================================================================== */

static void test_inc_indirect(void) {
    setup();
    /* MOV R0,#30h; MOV @R0,#41h; INC @R0 */
    uint8_t p[] = { 0x78, 0x30, 0x76, 0x41, 0x06, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x30) == 0x42, "INC @R0: 41h->42h");
    teardown();
}

static void test_dec_indirect(void) {
    setup();
    /* MOV R1,#31h; MOV @R1,#10h; DEC @R1 */
    uint8_t p[] = { 0x79, 0x31, 0x77, 0x10, 0x17, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x31) == 0x0F, "DEC @R1: 10h->0Fh");
    teardown();
}

static void test_add_a_indirect(void) {
    setup();
    /* MOV R0,#30h; MOV @R0,#05h; MOV A,#03h; ADD A,@R0 */
    uint8_t p[] = { 0x78, 0x30, 0x76, 0x05, 0x74, 0x03, 0x26, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x08, "ADD A,@R0: 03h+05h=08h");
    teardown();
}

static void test_addc_a_indirect(void) {
    setup();
    /* SETB C; MOV R1,#30h; MOV @R1,#02h; MOV A,#03h; ADDC A,@R1 */
    uint8_t p[] = { 0xD3, 0x79, 0x30, 0x77, 0x02, 0x74, 0x03, 0x37, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x06, "ADDC A,@R1: 03h+02h+C=06h");
    teardown();
}

static void test_subb_a_indirect(void) {
    setup();
    /* CLR C; MOV R0,#30h; MOV @R0,#03h; MOV A,#10h; SUBB A,@R0 */
    uint8_t p[] = { 0xC3, 0x78, 0x30, 0x76, 0x03, 0x74, 0x10, 0x96, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x0D, "SUBB A,@R0: 10h-03h=0Dh");
    teardown();
}

static void test_orl_a_indirect(void) {
    setup();
    /* MOV R0,#30h; MOV @R0,#0Fh; MOV A,#F0h; ORL A,@R0 */
    uint8_t p[] = { 0x78, 0x30, 0x76, 0x0F, 0x74, 0xF0, 0x46, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0xFF, "ORL A,@R0: F0h|0Fh=FFh");
    teardown();
}

static void test_anl_a_indirect(void) {
    setup();
    /* MOV R1,#30h; MOV @R1,#AAh; MOV A,#F0h; ANL A,@R1 */
    uint8_t p[] = { 0x79, 0x30, 0x77, 0xAA, 0x74, 0xF0, 0x57, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0xA0, "ANL A,@R1: F0h&AAh=A0h");
    teardown();
}

static void test_xrl_a_indirect(void) {
    setup();
    /* MOV R0,#30h; MOV @R0,#FFh; MOV A,#55h; XRL A,@R0 */
    uint8_t p[] = { 0x78, 0x30, 0x76, 0xFF, 0x74, 0x55, 0x66, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0xAA, "XRL A,@R0: 55h^FFh=AAh");
    teardown();
}

static void test_xch_indirect(void) {
    setup();
    /* MOV R0,#30h; MOV @R0,#ABh; MOV A,#CDh; XCH A,@R0 */
    uint8_t p[] = { 0x78, 0x30, 0x76, 0xAB, 0x74, 0xCD, 0xC6, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0xAB, "XCH A,@R0: A<->@R0");
    CHECK(IRAM(0x30) == 0xCD, "XCH A,@R0: mem got old A");
    teardown();
}

static void test_xchd(void) {
    setup();
    /* MOV R0,#30h; MOV @R0,#12h; MOV A,#34h; XCHD A,@R0 */
    uint8_t p[] = { 0x78, 0x30, 0x76, 0x12, 0x74, 0x34, 0xD6, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x32, "XCHD: A low nibble swapped with @R0 low nibble");
    CHECK(IRAM(0x30) == 0x14, "XCHD: @R0 low nibble = old A low nibble");
    teardown();
}

static void test_mov_indirect_a(void) {
    setup();
    /* MOV A,#77h; MOV R1,#40h; MOV @R1,A */
    uint8_t p[] = { 0x74, 0x77, 0x79, 0x40, 0xF7, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x40) == 0x77, "MOV @R1,A: IRAM[40h]=77h");
    teardown();
}

static void test_mov_mem_indirect(void) {
    setup();
    /* MOV R0,#30h; MOV @R0,#88h; MOV 40h,@R0 */
    uint8_t p[] = { 0x78, 0x30, 0x76, 0x88, 0x86, 0x40, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x40) == 0x88, "MOV mem,@R0: IRAM[40h]=88h");
    teardown();
}

static void test_mov_indirect_mem(void) {
    setup();
    /* MOV 30h,#55h; MOV R0,#40h; MOV @R0,30h */
    uint8_t p[] = { 0x75, 0x30, 0x55, 0x78, 0x40, 0xA6, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(IRAM(0x40) == 0x55, "MOV @R0,mem: IRAM[40h]=55h");
    teardown();
}

static void test_movx_indirect_ri(void) {
    setup();
    /* MOV R0,#34h; MOV A,#BBh; MOVX @R0,A; CLR A; MOVX A,@R0 */
    /* Note: MOVX @Ri uses 8-bit address (P2 as high byte, default 0xFF) */
    uint8_t p[] = { 0x78, 0x34, 0x74, 0xBB, 0xF2, 0xE4, 0xE2, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    /* Address = P2:R0 = FF34h */
    CHECK(ACC == 0xBB, "MOVX @R0: xdata round-trip via Ri");
    teardown();
}

/* ================================================================== *
 * Memory-operand variants of arithmetic/logic                         *
 * ================================================================== */

static void test_add_a_mem(void) {
    setup();
    /* MOV 30h,#05h; MOV A,#03h; ADD A,30h */
    uint8_t p[] = { 0x75, 0x30, 0x05, 0x74, 0x03, 0x25, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x08, "ADD A,mem: 03h+05h=08h");
    teardown();
}

static void test_addc_a_mem(void) {
    setup();
    /* SETB C; MOV 30h,#02h; MOV A,#03h; ADDC A,30h */
    uint8_t p[] = { 0xD3, 0x75, 0x30, 0x02, 0x74, 0x03, 0x35, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x06, "ADDC A,mem: 03h+02h+C=06h");
    teardown();
}

static void test_subb_a_mem(void) {
    setup();
    /* CLR C; MOV 30h,#05h; MOV A,#10h; SUBB A,30h */
    uint8_t p[] = { 0xC3, 0x75, 0x30, 0x05, 0x74, 0x10, 0x95, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x0B, "SUBB A,mem: 10h-05h=0Bh");
    teardown();
}

static void test_orl_a_mem(void) {
    setup();
    /* MOV 30h,#0Fh; MOV A,#F0h; ORL A,30h */
    uint8_t p[] = { 0x75, 0x30, 0x0F, 0x74, 0xF0, 0x45, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0xFF, "ORL A,mem: F0h|0Fh=FFh");
    teardown();
}

static void test_xrl_a_mem(void) {
    setup();
    /* MOV 30h,#AAh; MOV A,#55h; XRL A,30h */
    uint8_t p[] = { 0x75, 0x30, 0xAA, 0x74, 0x55, 0x65, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0xFF, "XRL A,mem: 55h^AAh=FFh");
    teardown();
}

static void test_dec_mem(void) {
    setup();
    /* MOV 30h,#10h; DEC 30h */
    uint8_t p[] = { 0x75, 0x30, 0x10, 0x15, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x30) == 0x0F, "DEC mem: 10h->0Fh");
    teardown();
}

/* ================================================================== *
 * Memory-destination logic ops                                        *
 * ================================================================== */

static void test_orl_mem_imm(void) {
    setup();
    /* MOV 30h,#F0h; ORL 30h,#0Fh */
    uint8_t p[] = { 0x75, 0x30, 0xF0, 0x43, 0x30, 0x0F, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x30) == 0xFF, "ORL mem,#imm: F0h|0Fh=FFh");
    teardown();
}

static void test_anl_mem_a(void) {
    setup();
    /* MOV 30h,#FFh; MOV A,#0Fh; ANL 30h,A */
    uint8_t p[] = { 0x75, 0x30, 0xFF, 0x74, 0x0F, 0x52, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(IRAM(0x30) == 0x0F, "ANL mem,A: FFh&0Fh=0Fh");
    teardown();
}

static void test_anl_mem_imm(void) {
    setup();
    /* MOV 30h,#FFh; ANL 30h,#AAh */
    uint8_t p[] = { 0x75, 0x30, 0xFF, 0x53, 0x30, 0xAA, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x30) == 0xAA, "ANL mem,#imm: FFh&AAh=AAh");
    teardown();
}

static void test_xrl_mem_a(void) {
    setup();
    /* MOV 30h,#AAh; MOV A,#FFh; XRL 30h,A */
    uint8_t p[] = { 0x75, 0x30, 0xAA, 0x74, 0xFF, 0x62, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(IRAM(0x30) == 0x55, "XRL mem,A: AAh^FFh=55h");
    teardown();
}

static void test_xrl_mem_imm(void) {
    setup();
    /* MOV 30h,#FFh; XRL 30h,#0Fh */
    uint8_t p[] = { 0x75, 0x30, 0xFF, 0x63, 0x30, 0x0F, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x30) == 0xF0, "XRL mem,#imm: FFh^0Fh=F0h");
    teardown();
}

static void test_mov_mem_mem(void) {
    setup();
    /* MOV 30h,#42h; MOV 40h,30h */
    uint8_t p[] = { 0x75, 0x30, 0x42, 0x85, 0x30, 0x40, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x40) == 0x42, "MOV mem,mem: IRAM[40h]=42h");
    teardown();
}

static void test_mov_mem_a(void) {
    setup();
    /* MOV A,#33h; MOV 30h,A */
    uint8_t p[] = { 0x74, 0x33, 0xF5, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x30) == 0x33, "MOV mem,A: IRAM[30h]=33h");
    teardown();
}

static void test_xch_a_mem(void) {
    setup();
    /* MOV 30h,#ABh; MOV A,#CDh; XCH A,30h */
    uint8_t p[] = { 0x75, 0x30, 0xAB, 0x74, 0xCD, 0xC5, 0x30, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0xAB, "XCH A,mem: A gets old mem value");
    CHECK(IRAM(0x30) == 0xCD, "XCH A,mem: mem gets old A");
    teardown();
}

/* ================================================================== *
 * Bit operations (remaining)                                          *
 * ================================================================== */

static void test_orl_c_bit(void) {
    setup();
    /* CLR C; SETB 00h; ORL C,00h -> C=1 */
    uint8_t p[] = { 0xC3, 0xD2, 0x00, 0x72, 0x00, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(PSW & PSWMASK_C, "ORL C,bit: C|1=1");
    teardown();
}

static void test_orl_c_compl_bit(void) {
    setup();
    /* CLR C; CLR 00h; ORL C,/00h -> C|(!0)=C|1=1 */
    uint8_t p[] = { 0xC3, 0xC2, 0x00, 0xA0, 0x00, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(PSW & PSWMASK_C, "ORL C,/bit: C|(NOT 0)=1");
    teardown();
}

static void test_anl_c_compl_bit(void) {
    setup();
    /* SETB C; SETB 00h; ANL C,/00h -> 1&(!1)=0 */
    uint8_t p[] = { 0xD3, 0xD2, 0x00, 0xB0, 0x00, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(!(PSW & PSWMASK_C), "ANL C,/bit: 1&(NOT 1)=0");
    teardown();
}

static void test_mov_c_from_bit(void) {
    setup();
    /* SETB 00h; MOV C,00h */
    uint8_t p[] = { 0xD2, 0x00, 0xA2, 0x00, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(PSW & PSWMASK_C, "MOV C,bit: C=1 from bit");
    teardown();
}

static void test_cpl_bit(void) {
    setup();
    /* CLR 00h; CPL 00h -> bit=1 */
    uint8_t p[] = { 0xC2, 0x00, 0xB2, 0x00, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(IRAM(0x20) & 0x01, "CPL bit: 0->1");
    teardown();
}

static void test_cpl_c(void) {
    setup();
    /* CLR C; CPL C -> C=1 */
    uint8_t p[] = { 0xC3, 0xB3, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 10);
    CHECK(PSW & PSWMASK_C, "CPL C: 0->1");
    teardown();
}

/* ================================================================== *
 * Rotate through carry                                                *
 * ================================================================== */

static void test_rrc(void) {
    setup();
    /* SETB C; MOV A,#00h; RRC A -> A=80h, C=0 */
    uint8_t p[] = { 0xD3, 0x74, 0x00, 0x13, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x80, "RRC A: C=1,A=00h -> A=80h");
    CHECK(!(PSW & PSWMASK_C), "RRC A: old bit0=0 -> C=0");
    teardown();
}

/* ================================================================== *
 * Remaining branch/jump instructions                                  *
 * ================================================================== */

static void test_jmp_indirect(void) {
    setup();
    /* MOV DPTR,#0100h; MOV A,#05h; JMP @A+DPTR
     * At 0105h: MOV A,#42h; SJMP $ */
    uint8_t p[] = { 0x90, 0x01, 0x00, 0x74, 0x05, 0x73 };
    memcpy(cpu.mCodeMem, p, sizeof(p));
    cpu.mCodeMem[0x105] = 0x74;
    cpu.mCodeMem[0x106] = 0x42;
    cpu.mCodeMem[0x107] = 0x80;
    cpu.mCodeMem[0x108] = 0xFE;
    for (int i = 0; i < 30; i++) { tick(&cpu); stc12_tick(&cpu, &stc); }
    CHECK(ACC == 0x42, "JMP @A+DPTR: jumped to 0105h");
    teardown();
}

static void test_movc_pc(void) {
    setup();
    /* MOV A,#02h; MOVC A,@A+PC
     * PC will be at +2 after MOVC fetch, so reads code[PC+2+A] */
    /* At addr 0: 74 02 83 xx xx yy — MOVC reads code[0x02+0x02+1]=code[5]
     * Actually: PC points to next instruction after MOVC, which is at addr 3.
     * So reads code[3+2] = code[5] */
    cpu.mCodeMem[0] = 0x74; /* MOV A,#02h */
    cpu.mCodeMem[1] = 0x02;
    cpu.mCodeMem[2] = 0x83; /* MOVC A,@A+PC */
    cpu.mCodeMem[3] = 0x80; /* SJMP $ */
    cpu.mCodeMem[4] = 0xFE;
    cpu.mCodeMem[5] = 0x42; /* target data */
    for (int i = 0; i < 20; i++) { tick(&cpu); stc12_tick(&cpu, &stc); }
    CHECK(ACC == 0x42, "MOVC A,@A+PC: code[PC+A]=42h");
    teardown();
}

static void test_cjne_a_mem(void) {
    setup();
    /* MOV 30h,#10h; MOV A,#10h; CJNE A,30h,+2; MOV A,#01h; SJMP $ */
    uint8_t p[] = { 0x75, 0x30, 0x10, 0x74, 0x10, 0xB5, 0x30, 0x02,
                    0x74, 0x01, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x01, "CJNE A,mem: equal -> no jump");
    teardown();
}

static void test_cjne_indirect(void) {
    setup();
    /* MOV R0,#30h; MOV @R0,#05h; CJNE @R0,#05h,+2; MOV A,#01; SJMP $ */
    uint8_t p[] = { 0x78, 0x30, 0x76, 0x05, 0xB6, 0x05, 0x02,
                    0x74, 0x01, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x01, "CJNE @R0,#imm: equal -> no jump");
    teardown();
}

static void test_djnz_mem(void) {
    setup();
    /* MOV 30h,#03h; loop: DJNZ 30h,loop; MOV A,#99h; SJMP $ */
    uint8_t p[] = { 0x75, 0x30, 0x03, 0xD5, 0x30, 0xFD, 0x74, 0x99, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x99, "DJNZ mem: counted down 3->0");
    CHECK(IRAM(0x30) == 0x00, "DJNZ mem: mem=0 after loop");
    teardown();
}

/* Forward declarations for edge case tests */
static void test_rlc(void);
static void test_anl_c_bit(void);
static void test_jc_taken(void);
static void test_jnc_not_taken(void);
static void test_inc_dptr_wrap(void);
static void test_subb_flags(void);
static void test_add_flags(void);
static void test_cjne_carry(void);
static void test_cjne_no_carry(void);

int main(void) {
    printf("=== Firmware test suite (synthetic machine code) ===\n\n");

    test_add_immediate();
    test_add_carry();
    test_addc();
    test_subb();
    test_subb_borrow();
    test_inc_dec();
    test_inc_wrap();
    test_dec();
    test_mul();
    test_mul_overflow();
    test_div();
    test_div_by_zero();
    test_da();
    test_anl();
    test_orl();
    test_xrl();
    test_cpl();
    test_rl_rr();
    test_swap();
    test_mov_rn();
    test_mov_indirect();
    test_mov_direct();
    test_xch();
    test_movc();
    test_movx();
    test_push_pop();
    test_ajmp();
    test_ljmp();
    test_sjmp();
    test_jz_jnz();
    test_jnz();
    test_cjne();
    test_djnz();
    test_acall_ret();
    test_lcall_ret();
    test_setb_clr_bit();
    test_jb_jnb();
    test_jbc();
    test_mov_c_bit();
    test_bank_switch();
    test_dptr_inc();
    test_auxr_write_read();
    test_p4_write();
    test_port_mode_regs();
    test_adc_contr_write();
    test_ccon_write();
    test_timer0_interrupt();
    test_ext_int0();
    test_parity();
    test_parity_odd();
    test_rlc();
    test_anl_c_bit();
    test_jc_taken();
    test_jnc_not_taken();
    test_inc_dptr_wrap();
    test_subb_flags();
    test_add_flags();
    test_cjne_carry();
    test_cjne_no_carry();

    /* Indirect @Ri addressing */
    test_inc_indirect();
    test_dec_indirect();
    test_add_a_indirect();
    test_addc_a_indirect();
    test_subb_a_indirect();
    test_orl_a_indirect();
    test_anl_a_indirect();
    test_xrl_a_indirect();
    test_xch_indirect();
    test_xchd();
    test_mov_indirect_a();
    test_mov_mem_indirect();
    test_mov_indirect_mem();
    test_movx_indirect_ri();

    /* Memory-operand arithmetic/logic */
    test_add_a_mem();
    test_addc_a_mem();
    test_subb_a_mem();
    test_orl_a_mem();
    test_xrl_a_mem();
    test_dec_mem();

    /* Memory-destination logic */
    test_orl_mem_imm();
    test_anl_mem_a();
    test_anl_mem_imm();
    test_xrl_mem_a();
    test_xrl_mem_imm();
    test_mov_mem_mem();
    test_mov_mem_a();
    test_xch_a_mem();

    /* Bit operations */
    test_orl_c_bit();
    test_orl_c_compl_bit();
    test_anl_c_compl_bit();
    test_mov_c_from_bit();
    test_cpl_bit();
    test_cpl_c();

    /* Rotate */
    test_rrc();

    /* Branches */
    test_jmp_indirect();
    test_movc_pc();
    test_cjne_a_mem();
    test_cjne_indirect();
    test_djnz_mem();

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}

/* Additional edge case tests */

static void test_rlc(void) {
    setup();
    /* SETB C; MOV A,#81h; RLC A -> A=03, C=1 */
    uint8_t p[] = { 0xD3, 0x74, 0x81, 0x33, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x03, "RLC: C=1,A=81h -> A=03h");
    CHECK(PSW & PSWMASK_C, "RLC: old bit7=1 -> C=1");
    teardown();
}



static void test_anl_c_bit(void) {
    setup();
    /* SETB C; SETB 00h; ANL C,00h -> C=1&1=1 */
    uint8_t p[] = { 0xD3, 0xD2, 0x00, 0x82, 0x00, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(PSW & PSWMASK_C, "ANL C,bit: 1&1=1");
    teardown();
}

static void test_jc_taken(void) {
    setup();
    /* SETB C; JC +2; MOV A,#FF; MOV A,#42; SJMP $ */
    uint8_t p[] = { 0xD3, 0x40, 0x02, 0x74, 0xFF, 0x74, 0x42, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x42, "JC taken: C=1 -> jumped past MOV A,#FFh");
    teardown();
}

static void test_jnc_not_taken(void) {
    setup();
    /* SETB C; JNC +2; MOV A,#42; SJMP $ */
    uint8_t p[] = { 0xD3, 0x50, 0x02, 0x74, 0x42, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 30);
    CHECK(ACC == 0x42, "JNC not taken: C=1 -> fell through");
    teardown();
}

static void test_inc_dptr_wrap(void) {
    setup();
    /* MOV DPTR,#FFFFh; INC DPTR -> DPTR=0000h */
    uint8_t p[] = { 0x90, 0xFF, 0xFF, 0xA3, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(DPH == 0x00, "INC DPTR wrap: DPH=00");
    CHECK(DPL == 0x00, "INC DPTR wrap: DPL=00");
    teardown();
}

static void test_subb_flags(void) {
    setup();
    /* CLR C; MOV A,#80h; SUBB A,#01h -> A=7Fh, OV=1, AC=1 */
    uint8_t p[] = { 0xC3, 0x74, 0x80, 0x94, 0x01, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x7F, "SUBB flags: 80h-01h=7Fh");
    CHECK(PSW & PSWMASK_OV, "SUBB flags: OV set (sign change)");
    CHECK(PSW & PSWMASK_AC, "SUBB flags: AC set (half-borrow)");
    teardown();
}

static void test_add_flags(void) {
    setup();
    /* MOV A,#7Fh; ADD A,#01h -> A=80h, OV=1 */
    uint8_t p[] = { 0x74, 0x7F, 0x24, 0x01, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(ACC == 0x80, "ADD flags: 7Fh+01h=80h");
    CHECK(PSW & PSWMASK_OV, "ADD flags: OV set (pos+pos=neg)");
    CHECK(!(PSW & PSWMASK_C), "ADD flags: no carry");
    teardown();
}

static void test_cjne_carry(void) {
    setup();
    /* MOV A,#10h; CJNE A,#20h,+0; -> C=1 (A < operand) */
    uint8_t p[] = { 0x74, 0x10, 0xB4, 0x20, 0x00, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(PSW & PSWMASK_C, "CJNE carry: A(10h) < imm(20h) -> C=1");
    teardown();
}

static void test_cjne_no_carry(void) {
    setup();
    /* MOV A,#30h; CJNE A,#20h,+0; -> C=0 (A > operand) */
    uint8_t p[] = { 0x74, 0x30, 0xB4, 0x20, 0x00, 0x80, 0xFE };
    load_and_run(p, sizeof(p), 20);
    CHECK(!(PSW & PSWMASK_C), "CJNE carry: A(30h) > imm(20h) -> C=0");
    teardown();
}
