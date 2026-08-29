/* test_debug.c — tests for boundary D (debug control interface).
 *
 * Build: gcc -O2 -o test_debug test_debug.c core.c opcodes.c disasm.c stc12.c debug.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "emu8051.h"
#include "stc12.h"
#include "debug.h"

static struct em8051 cpu;
static struct stc12_state stc;
static struct dbg_target dbg;
static int pass_count = 0, fail_count = 0;

static void exc(struct em8051 *a, int c) { (void)a; (void)c; }
#define CHECK(cond, msg) do { if (cond) pass_count++; else { printf("FAIL: %s\n", msg); fail_count++; } } while(0)

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
    dbg_init(&dbg, &cpu, &stc);
}

static void teardown(void) {
    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
}

/* ================================================================== */
/* Test: initial state                                                 */
/* ================================================================== */
static void test_initial_state(void) {
    printf("--- test_initial_state ---\n");
    setup();
    CHECK(dbg_get_state(&dbg) == DBG_HALTED, "Initial state = halted");
    CHECK(dbg_get_pc(&dbg) == 0, "Initial PC = 0");
    CHECK(dbg_get_sp(&dbg) == 7, "Initial SP = 7");
    CHECK(dbg_get_acc(&dbg) == 0, "Initial ACC = 0");
    teardown();
}

/* ================================================================== */
/* Test: step insn                                                     */
/* ================================================================== */
static int halt_count = 0;
static struct dbg_halt_reason last_halt;

static void test_on_halt(struct dbg_halt_reason *r, void *ud) {
    (void)ud;
    halt_count++;
    last_halt = *r;
}

static void test_step_insn(void) {
    printf("--- test_step_insn ---\n");
    setup();
    /* MOV A,#42h; MOV R0,#10h; NOP; SJMP $ */
    cpu.mCodeMem[0] = 0x74; cpu.mCodeMem[1] = 0x42;
    cpu.mCodeMem[2] = 0x78; cpu.mCodeMem[3] = 0x10;
    cpu.mCodeMem[4] = 0x00;
    cpu.mCodeMem[5] = 0x80; cpu.mCodeMem[6] = 0xFE;

    halt_count = 0;
    dbg_set_on_halt(&dbg, test_on_halt, NULL);

    /* Step 1 instruction */
    dbg_step(&dbg, STEP_INSN, 1);
    while (dbg_get_state(&dbg) == DBG_RUNNING) dbg_tick(&dbg);

    CHECK(halt_count == 1, "step(insn,1): halt callback fired");
    CHECK(last_halt.cause == HALT_STEP, "step(insn,1): cause = step");
    CHECK(dbg_get_acc(&dbg) == 0x42, "step(insn,1): ACC = 42h after MOV A,#42h");
    CHECK(dbg_get_pc(&dbg) == 2, "step(insn,1): PC = 2");

    /* Step 2 more */
    halt_count = 0;
    dbg_step(&dbg, STEP_INSN, 2);
    while (dbg_get_state(&dbg) == DBG_RUNNING) dbg_tick(&dbg);

    CHECK(halt_count == 1, "step(insn,2): halt fired once");
    CHECK(dbg_get_pc(&dbg) == 5, "step(insn,2): PC = 5 (at SJMP)");
    CHECK(cpu.mLowerData[0] == 0x10, "step(insn,2): R0 = 10h");

    teardown();
}

/* ================================================================== */
/* Test: code breakpoint                                               */
/* ================================================================== */
static void test_code_breakpoint(void) {
    printf("--- test_code_breakpoint ---\n");
    setup();
    /* MOV A,#01; MOV A,#02; MOV A,#03; MOV A,#04; SJMP $ */
    cpu.mCodeMem[0] = 0x74; cpu.mCodeMem[1] = 0x01;
    cpu.mCodeMem[2] = 0x74; cpu.mCodeMem[3] = 0x02;
    cpu.mCodeMem[4] = 0x74; cpu.mCodeMem[5] = 0x03;
    cpu.mCodeMem[6] = 0x74; cpu.mCodeMem[7] = 0x04;
    cpu.mCodeMem[8] = 0x80; cpu.mCodeMem[9] = 0xFE;

    halt_count = 0;
    dbg_set_on_halt(&dbg, test_on_halt, NULL);

    /* Set breakpoint at address 4 (MOV A,#03) */
    struct dbg_breakpoint bp = { .kind = BP_CODE, .addr = 4 };
    int h = dbg_set_breakpoint(&dbg, &bp);
    CHECK(h > 0, "set_breakpoint: returns positive handle");

    /* Run */
    dbg_run(&dbg);
    for (int i = 0; i < 100 && dbg_get_state(&dbg) == DBG_RUNNING; i++)
        dbg_tick(&dbg);

    CHECK(dbg_get_state(&dbg) == DBG_HALTED, "code bp: halted");
    CHECK(halt_count == 1, "code bp: halt fired");
    CHECK(last_halt.cause == HALT_BP, "code bp: cause = breakpoint");
    CHECK(last_halt.bp_id == h, "code bp: bp_id matches handle");
    CHECK(dbg_get_pc(&dbg) == 4, "code bp: PC = 4");
    CHECK(dbg_get_acc(&dbg) == 0x02, "code bp: ACC = 02h (MOV A,#02 executed)");

    /* Clear and run past */
    dbg_clear_breakpoint(&dbg, h);
    halt_count = 0;

    /* Step past to verify it doesn't hit again */
    dbg_step(&dbg, STEP_INSN, 3);
    while (dbg_get_state(&dbg) == DBG_RUNNING) dbg_tick(&dbg);

    CHECK(dbg_get_acc(&dbg) == 0x04, "clear bp: ran past, ACC = 04h");

    teardown();
}

/* ================================================================== */
/* Test: memory read/write                                             */
/* ================================================================== */
static void test_memory_access(void) {
    printf("--- test_memory_access ---\n");
    setup();

    /* Write to IRAM */
    uint8_t data[] = { 0xAA, 0xBB, 0xCC };
    dbg_write_mem(&dbg, SPACE_IRAM, 0x30, data, 3);
    uint8_t buf[3];
    dbg_read_mem(&dbg, SPACE_IRAM, 0x30, buf, 3);
    CHECK(buf[0] == 0xAA && buf[1] == 0xBB && buf[2] == 0xCC,
          "IRAM write/read round-trip");

    /* Write to SFR */
    uint8_t sfr_val = 0x55;
    dbg_write_mem(&dbg, SPACE_SFR, 0x90, &sfr_val, 1); /* P1 */
    uint8_t sfr_read;
    dbg_read_mem(&dbg, SPACE_SFR, 0x90, &sfr_read, 1);
    CHECK(sfr_read == 0x55, "SFR write/read P1 = 55h");

    /* Write to XRAM */
    uint8_t xdata = 0x77;
    dbg_write_mem(&dbg, SPACE_XRAM, 0x1234, &xdata, 1);
    uint8_t xread;
    dbg_read_mem(&dbg, SPACE_XRAM, 0x1234, &xread, 1);
    CHECK(xread == 0x77, "XRAM write/read = 77h");

    /* Read code */
    cpu.mCodeMem[0x100] = 0x42;
    uint8_t cread;
    dbg_read_mem(&dbg, SPACE_CODE, 0x100, &cread, 1);
    CHECK(cread == 0x42, "CODE read = 42h");

    /* Bit space */
    cpu.mLowerData[0x20] = 0x00;
    uint8_t bit_val = 1;
    dbg_write_mem(&dbg, SPACE_BIT, 0x03, &bit_val, 1); /* bit 3 of byte 20h */
    CHECK(cpu.mLowerData[0x20] == 0x08, "BIT write: bit 3 set in byte 20h");

    uint8_t bit_read;
    dbg_read_mem(&dbg, SPACE_BIT, 0x03, &bit_read, 1);
    CHECK(bit_read == 1, "BIT read: bit 3 = 1");

    teardown();
}

/* ================================================================== */
/* Test: step over / step out                                          */
/* ================================================================== */
static void test_step_over_out(void) {
    printf("--- test_step_over_out ---\n");
    setup();
    /* main: LCALL sub; MOV A,#99; SJMP $
     * sub:  MOV A,#01; MOV A,#02; RET */
    cpu.mCodeMem[0] = 0x12; cpu.mCodeMem[1] = 0x00; cpu.mCodeMem[2] = 0x08;
    cpu.mCodeMem[3] = 0x74; cpu.mCodeMem[4] = 0x99;
    cpu.mCodeMem[5] = 0x80; cpu.mCodeMem[6] = 0xFE;
    cpu.mCodeMem[7] = 0x00; /* padding */
    cpu.mCodeMem[8] = 0x74; cpu.mCodeMem[9] = 0x01;   /* sub: MOV A,#01 */
    cpu.mCodeMem[10] = 0x74; cpu.mCodeMem[11] = 0x02;  /* MOV A,#02 */
    cpu.mCodeMem[12] = 0x22;                            /* RET */

    halt_count = 0;
    dbg_set_on_halt(&dbg, test_on_halt, NULL);

    /* Step over the LCALL — should execute sub and return */
    dbg_step(&dbg, STEP_OVER, 1);
    for (int i = 0; i < 200 && dbg_get_state(&dbg) == DBG_RUNNING; i++)
        dbg_tick(&dbg);

    CHECK(dbg_get_state(&dbg) == DBG_HALTED, "step over: halted");
    CHECK(dbg_get_pc(&dbg) == 3, "step over: PC = 3 (after LCALL returned)");
    CHECK(dbg_get_acc(&dbg) == 0x02, "step over: ACC = 02 (sub executed)");

    /* Reset and test step out from inside the sub */
    dbg_reset(&dbg);
    cpu.mCodeMem[0] = 0x12; cpu.mCodeMem[1] = 0x00; cpu.mCodeMem[2] = 0x08;
    cpu.mCodeMem[3] = 0x74; cpu.mCodeMem[4] = 0x99;
    cpu.mCodeMem[5] = 0x80; cpu.mCodeMem[6] = 0xFE;
    cpu.mCodeMem[8] = 0x74; cpu.mCodeMem[9] = 0x01;
    cpu.mCodeMem[10] = 0x74; cpu.mCodeMem[11] = 0x02;
    cpu.mCodeMem[12] = 0x22;

    /* Step into the LCALL first */
    dbg_step(&dbg, STEP_INSN, 1);
    while (dbg_get_state(&dbg) == DBG_RUNNING) dbg_tick(&dbg);
    CHECK(dbg_get_pc(&dbg) == 8, "step into: PC = 8 (inside sub)");

    /* Now step out */
    dbg_step(&dbg, STEP_OUT, 1);
    for (int i = 0; i < 200 && dbg_get_state(&dbg) == DBG_RUNNING; i++)
        dbg_tick(&dbg);

    CHECK(dbg_get_state(&dbg) == DBG_HALTED, "step out: halted");
    CHECK(dbg_get_pc(&dbg) == 3, "step out: PC = 3 (returned from sub)");

    teardown();
}

/* ================================================================== */
/* Test: Level 1 position (§2)                                         */
/* ================================================================== */
static void test_level1_position(void) {
    printf("--- test_level1_position ---\n");
    setup();

    /* Simulate task state in IRAM */
    struct dbg_task_pos tasks[2] = {
        { .name = "bw_task0", .state_addr = 0x30, .until_addr = 0x32 },
        { .name = "bw_task1", .state_addr = 0x34, .until_addr = 0x36 },
    };
    struct dbg_symbols syms = {
        .bw_ms_addr = 0x28,
        .tasks = tasks,
        .n_tasks = 2,
    };
    dbg_set_symbols(&dbg, &syms);

    /* Write values to IRAM */
    cpu.mLowerData[0x28] = 0x64; cpu.mLowerData[0x29] = 0x00; /* bw_ms = 100 */
    cpu.mLowerData[0x30] = 0x03; cpu.mLowerData[0x31] = 0x00; /* task0_state = 3 */
    cpu.mLowerData[0x32] = 0xC8; cpu.mLowerData[0x33] = 0x00; /* task0_until = 200 */
    cpu.mLowerData[0x34] = 0xFF; cpu.mLowerData[0x35] = 0xFF; /* task1_state = 0xFFFF (done) */

    CHECK(dbg_get_bw_ms(&dbg) == 100, "Level 1: bw_ms = 100");
    CHECK(dbg_get_task_state(&dbg, 0) == 3, "Level 1: task0_state = 3");
    CHECK(dbg_get_task_until(&dbg, 0) == 200, "Level 1: task0_until = 200");
    CHECK(dbg_get_task_state(&dbg, 1) == 0xFFFF, "Level 1: task1_state = 0xFFFF (done)");

    teardown();
}

/* ================================================================== */
/* Test: time tracking                                                 */
/* ================================================================== */
static void test_time(void) {
    printf("--- test_time ---\n");
    setup();
    stc.fosc = 11059200;
    stc.ns_per_clock_x256 = (uint64_t)(256.0e9 / stc.fosc + 0.5);
    cpu.mCodeMem[0] = 0x00; /* NOP loop */

    CHECK(dbg_get_time_ns(&dbg) == 0, "Time: 0 at start");

    /* Run some ticks */
    dbg_run(&dbg);
    for (int i = 0; i < 100; i++) dbg_tick(&dbg);
    dbg_halt(&dbg);

    CHECK(dbg_get_time_ns(&dbg) > 0, "Time: > 0 after running");

    teardown();
}

/* ================================================================== */
/* Test: write watchpoint                                              */
/* ================================================================== */
static void test_write_watchpoint(void) {
    printf("--- test_write_watchpoint ---\n");
    setup();

    /* Program: MOV 30h,#00; MOV 30h,#42; SJMP $ */
    cpu.mCodeMem[0] = 0x75; cpu.mCodeMem[1] = 0x30; cpu.mCodeMem[2] = 0x00;
    cpu.mCodeMem[3] = 0x75; cpu.mCodeMem[4] = 0x30; cpu.mCodeMem[5] = 0x42;
    cpu.mCodeMem[6] = 0x80; cpu.mCodeMem[7] = 0xFE;

    /* Set write watchpoint on IRAM 0x30 */
    cpu.mLowerData[0x30] = 0x00; /* pre-set so first write (0x00) is no-change */
    struct dbg_breakpoint wbp = {
        .kind = BP_WRITE, .addr = 0x30,
        .watch = { .space = SPACE_IRAM, .len = 1 }
    };
    int id = dbg_set_breakpoint(&dbg, &wbp);
    CHECK(id >= 0, "write watchpoint: set OK");

    /* Run — should halt when 0x30 changes from 0x00 to 0x42 */
    dbg_run(&dbg);
    for (int i = 0; i < 200 && dbg_get_state(&dbg) == DBG_RUNNING; i++)
        dbg_tick(&dbg);

    CHECK(dbg_get_state(&dbg) == DBG_HALTED, "write watchpoint: halted");
    CHECK(cpu.mLowerData[0x30] == 0x42, "write watchpoint: value changed to 0x42");

    /* The halt must NAME the byte. Halting is half an answer: the user set the
     * watchpoint on an address, so the report has to carry that address and
     * the transition, not just a PC. Before this, `struct dbg_halt_reason`
     * carried neither and a front end could only say "something stopped". */
    const struct dbg_halt_reason *r = dbg_get_last_halt(&dbg);
    CHECK(r->cause == HALT_BP, "write watchpoint: cause = breakpoint");
    CHECK(r->bp_id == id, "write watchpoint: bp_id matches the handle");
    CHECK(r->is_watch, "write watchpoint: reason is flagged as a watch");
    CHECK(r->watch_space == SPACE_IRAM, "write watchpoint: space = iram");
    CHECK(r->watch_addr == 0x30, "write watchpoint: reports address 30h");
    CHECK(r->watch_value == 0x42, "write watchpoint: reports new value 42h");
    CHECK(r->watch_prev == 0x00, "write watchpoint: reports previous value 00h");

    teardown();
}

/* ================================================================== */
/* Test: the three honest limits of a polling watchpoint               */
/* ================================================================== */
static void test_write_watchpoint_limits(void) {
    printf("--- test_write_watchpoint_limits ---\n");

    /* (a) A store of the value already there is INVISIBLE. This is the
     * limitation that most surprises a user, so it is pinned as behaviour
     * rather than left to be discovered: if this ever starts firing, the
     * watchpoint became a store detector and every doc saying otherwise —
     * including the front end's own wording — needs re-reading. */
    setup();
    /* MOV 30h,#00 ; MOV 30h,#00 ; SJMP $   onto a byte already 0 */
    cpu.mCodeMem[0] = 0x75; cpu.mCodeMem[1] = 0x30; cpu.mCodeMem[2] = 0x00;
    cpu.mCodeMem[3] = 0x75; cpu.mCodeMem[4] = 0x30; cpu.mCodeMem[5] = 0x00;
    cpu.mCodeMem[6] = 0x80; cpu.mCodeMem[7] = 0xFE;
    cpu.mLowerData[0x30] = 0x00;
    struct dbg_breakpoint same = {
        .kind = BP_WRITE, .addr = 0x30, .watch = { .space = SPACE_IRAM, .len = 1 }
    };
    CHECK(dbg_set_breakpoint(&dbg, &same) > 0, "same-value: watchpoint set");
    dbg_run(&dbg);
    for (int i = 0; i < 300 && dbg_get_state(&dbg) == DBG_RUNNING; i++) dbg_tick(&dbg);
    CHECK(dbg_get_state(&dbg) == DBG_RUNNING,
          "same-value: two stores of 00h onto a 00h byte do NOT halt (change detector)");
    teardown();

    /* (b) An unknown space is REFUSED, not stored. dbg_read_mem answers 0 for
     * a space it does not know, so an accepted watchpoint there would sit at
     * shadow 0 for ever: armed to the user, dead in fact. */
    setup();
    struct dbg_breakpoint bogus = {
        .kind = BP_WRITE, .addr = 0x30, .watch = { .space = (enum dbg_space)9, .len = 1 }
    };
    CHECK(dbg_set_breakpoint(&dbg, &bogus) == -1,
          "bad space: watchpoint refused with -1 rather than armed and dead");
    teardown();

    /* (c) Reset re-seeds the shadow. A watchpoint that survives a Restart must
     * not fire on the reset's own rewriting of memory — a stop with a real
     * address on it that no instruction caused is the worst failure this
     * interface can have.
     *
     * The watched byte is an SFR and that is deliberate, not incidental:
     * dbg_reset calls reset(cpu, 0), a WARM reset, which leaves IRAM and XRAM
     * alone but does `memset(mSFR, 0, 128)` and then restores P0..P3 to 0xFF.
     * So an SFR watchpoint is the one that actually goes stale, and an IRAM
     * watchpoint would pass this test whether or not the re-seed exists —
     * which is what the first version of this test did, and it proved
     * nothing: removing the re-seed left it green. */
    setup();
    cpu.mCodeMem[0] = 0x00;                          /* NOP */
    cpu.mCodeMem[1] = 0x80; cpu.mCodeMem[2] = 0xFE;  /* SJMP $ */
    cpu.mSFR[REG_P1] = 0x00;                         /* not its post-reset 0xFF */
    struct dbg_breakpoint keep = {
        .kind = BP_WRITE, .addr = 0x90,              /* P1, absolute SFR address */
        .watch = { .space = SPACE_SFR, .len = 1 }
    };
    CHECK(dbg_set_breakpoint(&dbg, &keep) > 0, "reset re-seed: watchpoint armed on P1 at 00h");
    dbg_reset(&dbg);                                  /* restores P1 to 0xFF */
    CHECK(dbg_get_last_halt(&dbg)->cause == HALT_RESET, "reset re-seed: reason says reset");
    CHECK(cpu.mSFR[REG_P1] == 0xFF, "reset re-seed: the reset really did move P1 00h -> FFh");
    dbg_run(&dbg);
    for (int i = 0; i < 100 && dbg_get_state(&dbg) == DBG_RUNNING; i++) dbg_tick(&dbg);
    CHECK(dbg_get_state(&dbg) == DBG_RUNNING,
          "reset re-seed: the reset's own rewrite of P1 does not fire the watchpoint");
    teardown();
}

/* ================================================================== */
/* Test: a halt that is NOT a watchpoint says so                       */
/* ================================================================== */
static void test_halt_reason_not_a_watch(void) {
    printf("--- test_halt_reason_not_a_watch ---\n");
    setup();

    /* MOV A,#42h ; MOV A,#43h ; SJMP $ — step once, then read the reason. */
    cpu.mCodeMem[0] = 0x74; cpu.mCodeMem[1] = 0x42;
    cpu.mCodeMem[2] = 0x74; cpu.mCodeMem[3] = 0x43;
    cpu.mCodeMem[4] = 0x80; cpu.mCodeMem[5] = 0xFE;

    dbg_step(&dbg, STEP_INSN, 1);
    for (int i = 0; i < 100 && dbg_get_state(&dbg) == DBG_RUNNING; i++) dbg_tick(&dbg);

    const struct dbg_halt_reason *r = dbg_get_last_halt(&dbg);
    CHECK(r->cause == HALT_STEP, "step halt: cause = step");
    CHECK(!r->is_watch, "step halt: is_watch is false");
    CHECK(r->bp_id == -1, "step halt: no breakpoint handle");
    /* And the watch fields are zero rather than stale from an earlier halt. */
    CHECK(r->watch_addr == 0 && r->watch_value == 0 && r->watch_prev == 0,
          "step halt: watch fields are zero, not stale");

    teardown();
}

/* ================================================================== */
/* Test: step over with nested call (LCALL calls LCALL calls RET)      */
/* ================================================================== */
static void test_step_over_nested(void) {
    printf("--- test_step_over_nested ---\n");
    setup();

    /* main:  LCALL outer; MOV A,#99; SJMP $
     * outer: LCALL inner; MOV A,#55; RET
     * inner: MOV A,#33; RET */
    cpu.mCodeMem[0] = 0x12; cpu.mCodeMem[1] = 0x00; cpu.mCodeMem[2] = 0x06; /* LCALL 0006 */
    cpu.mCodeMem[3] = 0x74; cpu.mCodeMem[4] = 0x99;                          /* MOV A,#99 */
    cpu.mCodeMem[5] = 0x80; cpu.mCodeMem[6] = 0xFE;                          /* SJMP $ */
    /* outer at 0x06: */
    cpu.mCodeMem[6] = 0x12; cpu.mCodeMem[7] = 0x00; cpu.mCodeMem[8] = 0x0C; /* LCALL 000C */
    cpu.mCodeMem[9] = 0x74; cpu.mCodeMem[10] = 0x55;                         /* MOV A,#55 */
    cpu.mCodeMem[11] = 0x22;                                                  /* RET */
    /* inner at 0x0C: */
    cpu.mCodeMem[12] = 0x74; cpu.mCodeMem[13] = 0x33;                        /* MOV A,#33 */
    cpu.mCodeMem[14] = 0x22;                                                  /* RET */

    /* Step over the LCALL outer — should execute both nested calls and return */
    dbg_step(&dbg, STEP_OVER, 1);
    for (int i = 0; i < 500 && dbg_get_state(&dbg) == DBG_RUNNING; i++)
        dbg_tick(&dbg);

    CHECK(dbg_get_state(&dbg) == DBG_HALTED, "step over nested: halted");
    CHECK(dbg_get_pc(&dbg) == 3, "step over nested: PC = 3 (after outer returned)");
    /* ACC should be 0x55 (last MOV in outer, after inner's 0x33) */
    CHECK(dbg_get_acc(&dbg) == 0x55, "step over nested: ACC = 55 (outer executed)");

    teardown();
}

int main(void) {
    printf("=== Debug control tests (boundary D) ===\n\n");

    test_initial_state();
    test_step_insn();
    test_code_breakpoint();
    test_memory_access();
    test_step_over_out();
    test_write_watchpoint();
    test_write_watchpoint_limits();
    test_halt_reason_not_a_watch();
    test_step_over_nested();
    test_level1_position();
    test_time();

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    return fail_count > 0 ? 1 : 0;
}
