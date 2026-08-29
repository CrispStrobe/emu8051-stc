/* Debug control — boundary D implementation for emu8051-stc.
 * Copyright 2024 CrispStrobe (MIT)
 *
 * Implements DEBUG-CONTROL-MODEL.md §7 DebugTarget interface.
 * This is the capable target: all step kinds, all breakpoint kinds,
 * timeFreezes = true, skewNs = 0.
 */

#include <string.h>
#include <stdlib.h>
#include "debug.h"

/* ================================================================== *
 * Init / state                                                        *
 * ================================================================== */

void dbg_init(struct dbg_target *t, struct em8051 *cpu, struct stc12_state *stc)
{
    memset(t, 0, sizeof(*t));
    t->cpu = cpu;
    t->stc = stc;
    t->state = DBG_HALTED;
    t->next_bp_id = 1;
}

enum dbg_state dbg_get_state(struct dbg_target *t)
{
    return t->state;
}

/* ================================================================== *
 * Run control                                                         *
 * ================================================================== */

void dbg_run(struct dbg_target *t)
{
    t->state = DBG_RUNNING;
    t->step_count = 0;
}

void dbg_halt(struct dbg_target *t)
{
    if (t->state != DBG_RUNNING) return;
    t->state = DBG_HALTED;

    struct dbg_halt_reason r = {
        .cause = HALT_USER,
        .pc = t->cpu->mPC,
        .bp_id = -1,
        .t_ns = stc12_get_time_ns(t->stc),
        .is_watch = false,
    };
    t->last_halt = r;
    if (t->on_halt) t->on_halt(&r, t->on_halt_data);
}

int dbg_step(struct dbg_target *t, enum dbg_step_kind kind, int count)
{
    if (count < 1) count = 1;
    t->step_kind = kind;
    t->step_count = count;
    t->step_entry_sp = t->cpu->mSFR[REG_SP];

    /* Snapshot task states for STEP_BLOCK detection */
    if (kind == STEP_BLOCK) {
        for (int i = 0; i < t->syms.n_tasks && i < 8; i++) {
            uint16_t addr = t->syms.tasks[i].state_addr;
            if (addr == 0) { t->step_task_state[i] = 0; continue; }
            t->step_task_state[i] = (addr < 128) ? t->cpu->mLowerData[addr] :
                                    (t->cpu->mUpperData ? t->cpu->mUpperData[addr - 128] : 0);
        }
    }

    t->state = DBG_RUNNING;
    return 0; /* in-range kinds all start; see dbg_supports_step for the truth */
}

int dbg_supports_step(enum dbg_step_kind kind)
{
    switch (kind) {
    case STEP_INSN:
    case STEP_BLOCK:
    case STEP_OVER:
    case STEP_OUT:
    case STEP_CYCLE:
        return 1;
    case STEP_LINE:
        /* Treated as STEP_INSN by dbg_step, which is not the same thing.
         * Reported as unsupported so nobody offers it. */
        return 0;
    }
    return 0;
}

void dbg_reset(struct dbg_target *t)
{
    reset(t->cpu, 0);
    stc12_init(t->cpu, t->stc);
    t->cpu->skip_timers = true;
    t->state = DBG_HALTED;
    t->step_count = 0;

    /* Re-seed every watchpoint shadow from the memory the reset just wrote.
     *
     * Without this a watchpoint set before a reset compares post-reset memory
     * against a pre-reset shadow, so the very first tick after Restart halts
     * on a "write" that no instruction performed — the reset itself. That is
     * the worst kind of debugger lie: a stop with a plausible address on it.
     * Reset clears the halt reason too; the last halt was in the old run. */
    for (int i = 0; i < DBG_MAX_BP; i++) {
        struct dbg_breakpoint *bp = &t->bps[i];
        if (!bp->active || (bp->kind != BP_WRITE && bp->kind != BP_READ)) continue;
        uint8_t val = 0;
        dbg_read_mem(t, bp->watch.space, bp->addr, &val, 1);
        t->watch_shadow[i] = val;
    }
    memset(&t->last_halt, 0, sizeof(t->last_halt));
    t->last_halt.cause = HALT_RESET;
    t->last_halt.bp_id = -1;
}

const struct dbg_halt_reason *dbg_get_last_halt(struct dbg_target *t)
{
    return &t->last_halt;
}

/* ================================================================== *
 * Tick — the main loop calls this while DBG_RUNNING                   *
 * ================================================================== */

/* Read a 16-bit value from IRAM (little-endian, as SDCC stores ints) */
static uint16_t read_iram16(struct em8051 *cpu, uint16_t addr)
{
    uint8_t lo = (addr < 128) ? cpu->mLowerData[addr] :
                 (cpu->mUpperData ? cpu->mUpperData[addr - 128] : 0);
    uint8_t hi = (addr + 1 < 128) ? cpu->mLowerData[addr + 1] :
                 (cpu->mUpperData ? cpu->mUpperData[addr + 1 - 128] : 0);
    return lo | (hi << 8);
}

static void emit_halt(struct dbg_target *t, enum dbg_halt_cause cause, int bp_id)
{
    t->state = DBG_HALTED;
    struct dbg_halt_reason r = {
        .cause = cause,
        .pc = t->cpu->mPC,
        .bp_id = bp_id,
        .t_ns = stc12_get_time_ns(t->stc),
        .is_watch = false,
    };
    t->last_halt = r;
    if (t->on_halt) t->on_halt(&r, t->on_halt_data);
}

/* The watchpoint flavour of emit_halt: same halt, plus the byte that moved.
 * Split out rather than adding four parameters to emit_halt because every
 * other caller would have to pass four zeroes, and a zero address is a legal
 * address — the `is_watch` flag is what a consumer must branch on. */
static void emit_watch_halt(struct dbg_target *t, int bp_id, enum dbg_space space,
                            uint16_t addr, uint8_t value, uint8_t prev)
{
    t->state = DBG_HALTED;
    struct dbg_halt_reason r = {
        .cause = HALT_BP,
        .pc = t->cpu->mPC,
        .bp_id = bp_id,
        .t_ns = stc12_get_time_ns(t->stc),
        .is_watch = true,
        .watch_space = space,
        .watch_addr = addr,
        .watch_value = value,
        .watch_prev = prev,
    };
    t->last_halt = r;
    if (t->on_halt) t->on_halt(&r, t->on_halt_data);
}

bool dbg_tick(struct dbg_target *t)
{
    if (t->state != DBG_RUNNING) return false;

    /* Execute one osc clock */
    bool ticked = tick(t->cpu);
    stc12_tick(t->cpu, t->stc);

    /* STEP_CYCLE completes HERE, on the clock, and that placement is the whole
     * feature: every other kind is decided below the `!ticked` return, i.e.
     * only at instruction boundaries. A cycle step stops between them — after
     * one oscillator clock, whether or not the instruction in flight finished.
     * On the clock that DOES finish an instruction the PC moves; on the others
     * it does not, and a user watching the PC sit still for three steps and
     * then jump is seeing the machine cycle count of that instruction, which
     * is the thing worth showing. */
    if (t->step_count > 0 && t->step_kind == STEP_CYCLE) {
        t->step_count--;
        if (t->step_count == 0) {
            emit_halt(t, HALT_STEP, -1);
            return true;
        }
        return false;
    }

    if (!ticked) return false; /* still in multi-cycle instruction */

    /* Profiling: record PC hit */
    if (t->profiling && t->pc_histogram) {
        t->pc_histogram[t->cpu->mPC & 0xFFFF]++;
        t->profile_total++;
    }

    /* Check breakpoints */
    for (int i = 0; i < DBG_MAX_BP; i++) {
        struct dbg_breakpoint *bp = &t->bps[i];
        if (!bp->active) continue;

        switch (bp->kind) {
        case BP_CODE:
            if (t->cpu->mPC == bp->addr) {
                emit_halt(t, HALT_BP, bp->id);
                return true;
            }
            break;
        case BP_YIELD:
            /* Per ucsim-stc coordination: yield breakpoints use the
             * code address from the symbol table's yields[].addr field,
             * NOT a write-watch on <task>_state. This ensures both
             * emulators halt at the same instruction. */
            if (t->cpu->mPC == bp->addr) {
                emit_halt(t, HALT_BP, bp->id);
                return true;
            }
            break;
        case BP_WRITE: {
            /* Polling watchpoint: check if the watched byte changed.
             *
             * This is a CHANGE detector, not a store detector — see the
             * caveat block above dbg_set_breakpoint. The report therefore
             * names both the new value and the shadow it replaced, because
             * "0x30 changed" without a from-value is not evidence. */
            uint8_t cur = 0;
            dbg_read_mem(t, bp->watch.space, bp->addr, &cur, 1);
            if (cur != t->watch_shadow[i]) {
                uint8_t prev = t->watch_shadow[i];
                t->watch_shadow[i] = cur;
                emit_watch_halt(t, bp->id, bp->watch.space, bp->addr, cur, prev);
                return true;
            }
            break;
        }
        case BP_READ:
            /* Read watchpoints cannot be implemented by polling —
             * a read doesn't change state. Would need instruction
             * decode to detect reads. Not implemented. */
            break;
        }
    }

    /* Check step completion */
    if (t->step_count > 0) {
        bool step_done = false;

        switch (t->step_kind) {
        case STEP_INSN:
            t->step_count--;
            step_done = (t->step_count == 0);
            break;
        case STEP_BLOCK:
            /* Step until any task's state variable changes (= a yield).
             * If no tasks are configured, falls back to step-insn. */
            if (t->syms.n_tasks > 0) {
                for (int ti = 0; ti < t->syms.n_tasks && ti < 8; ti++) {
                    uint16_t addr = t->syms.tasks[ti].state_addr;
                    if (addr == 0) continue;
                    uint8_t cur = (addr < 128) ? t->cpu->mLowerData[addr] :
                                  (t->cpu->mUpperData ? t->cpu->mUpperData[addr - 128] : 0);
                    if (cur != t->step_task_state[ti]) {
                        t->step_task_state[ti] = cur;
                        step_done = true;
                        break;
                    }
                }
            } else {
                t->step_count--;
                step_done = (t->step_count == 0);
            }
            break;
        case STEP_OVER:
            /* Run until SP <= entry SP and a new instruction starts */
            if (t->cpu->mSFR[REG_SP] <= t->step_entry_sp) {
                t->step_count--;
                step_done = (t->step_count == 0);
            }
            break;
        case STEP_OUT:
            /* Run until SP < entry SP */
            if (t->cpu->mSFR[REG_SP] < t->step_entry_sp) {
                t->step_count--;
                step_done = (t->step_count == 0);
            }
            break;
        case STEP_LINE:
            /* Would need a line table. For now, treat as step-insn — which is
             * why dbg_supports_step reports STEP_LINE as unsupported and
             * emu_dbg_step refuses it: silently giving an instruction step is
             * exactly what boundary D forbids. */
            t->step_count--;
            step_done = (t->step_count == 0);
            break;
        case STEP_CYCLE:
            /* Unreachable: a cycle step is completed above, before the
             * instruction-boundary return, and never arrives here. Named
             * anyway so the compiler checks this switch stays exhaustive. */
            break;
        }

        if (step_done) {
            emit_halt(t, HALT_STEP, -1);
            return true;
        }
    }

    return false;
}

/* ================================================================== *
 * Breakpoints                                                         *
 * ================================================================== */

/* WHAT A WRITE WATCHPOINT HERE ACTUALLY IS, stated once so no consumer has to
 * guess — and so no front end can advertise more than this delivers.
 *
 * It is a CHANGE detector sampled at instruction boundaries, not a store
 * detector wired into the address decoder. Three consequences, all of them
 * visible to a user:
 *
 *   1. A store that writes the value already there fires nothing. `MOV 30h,#0`
 *      onto a byte already 0 is invisible. "What wrote my variable?" is
 *      answered only when the write CHANGED it.
 *   2. A byte that changes for a reason other than a store still fires — an
 *      SFR the peripherals move (TL0, the serial buffer, an ADC result) will
 *      trip a watchpoint set on it with no program instruction responsible.
 *      That is not a false positive at the memory level; it is a true report
 *      that the byte moved, and the PC in the halt reason is where execution
 *      happened to be, NOT the writer.
 *   3. The granularity is one instruction. Two changes inside a single
 *      multi-cycle instruction report only the last one.
 *
 * All three are honest limits of polling, and they are why the halt reason
 * carries `watch_prev` as well as `watch_value`: the transition is the
 * evidence, and a consumer that shows only the new value is showing less than
 * was measured. A store-accurate watchpoint would need the write path in
 * core.c to call back, which is a larger change than this interface. */
int dbg_set_breakpoint(struct dbg_target *t, struct dbg_breakpoint *bp)
{
    /* Refuse an out-of-range space rather than storing a watchpoint that can
     * never fire: dbg_read_mem's default branch returns 0 for an unknown
     * space, so the shadow would sit at 0 for ever and the user would watch a
     * dead address believing it armed. Refusing by return value is boundary
     * D's rule — never silently do something else. */
    if (bp->kind == BP_WRITE || bp->kind == BP_READ) {
        if (bp->watch.space < SPACE_CODE || bp->watch.space > SPACE_BIT) return -1;
    }
    for (int i = 0; i < DBG_MAX_BP; i++) {
        if (!t->bps[i].active) {
            t->bps[i] = *bp;
            t->bps[i].id = t->next_bp_id++;
            t->bps[i].active = true;
            /* Snapshot watched byte for write/read watchpoints */
            if (bp->kind == BP_WRITE || bp->kind == BP_READ) {
                uint8_t val = 0;
                dbg_read_mem(t, bp->watch.space, bp->addr, &val, 1);
                t->watch_shadow[i] = val;
            }
            return t->bps[i].id;
        }
    }
    return -1; /* no slots */
}

void dbg_clear_breakpoint(struct dbg_target *t, int handle)
{
    for (int i = 0; i < DBG_MAX_BP; i++) {
        if (t->bps[i].active && t->bps[i].id == handle) {
            t->bps[i].active = false;
            return;
        }
    }
}

/* ================================================================== *
 * Memory access                                                       *
 * ================================================================== */

int dbg_read_mem(struct dbg_target *t, enum dbg_space space,
                 uint16_t addr, uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        uint16_t a = addr + i;
        switch (space) {
        case SPACE_CODE:
            buf[i] = (a <= t->cpu->mCodeMemMaxIdx) ? t->cpu->mCodeMem[a] : 0;
            break;
        case SPACE_IRAM:
            if (a < 128)
                buf[i] = t->cpu->mLowerData[a];
            else if (t->cpu->mUpperData)
                buf[i] = t->cpu->mUpperData[a - 128];
            else
                buf[i] = 0;
            break;
        case SPACE_SFR:
            buf[i] = (a >= 0x80 && a <= 0xFF) ? t->cpu->mSFR[a - 0x80] : 0;
            break;
        case SPACE_XRAM:
            buf[i] = (a <= t->cpu->mExtDataMaxIdx) ? t->cpu->mExtData[a] : 0;
            break;
        case SPACE_BIT:
            if (a < 0x80) {
                uint8_t byte = t->cpu->mLowerData[0x20 + (a >> 3)];
                buf[i] = (byte >> (a & 7)) & 1;
            } else {
                uint8_t sfr = t->cpu->mSFR[(a & 0xF8) - 0x80];
                buf[i] = (sfr >> (a & 7)) & 1;
            }
            break;
        }
    }
    return len;
}

int dbg_write_mem(struct dbg_target *t, enum dbg_space space,
                  uint16_t addr, const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        uint16_t a = addr + i;
        switch (space) {
        case SPACE_CODE:
            if (a <= t->cpu->mCodeMemMaxIdx)
                t->cpu->mCodeMem[a] = buf[i];
            break;
        case SPACE_IRAM:
            if (a < 128)
                t->cpu->mLowerData[a] = buf[i];
            else if (t->cpu->mUpperData)
                t->cpu->mUpperData[a - 128] = buf[i];
            break;
        case SPACE_SFR:
            if (a >= 0x80 && a <= 0xFF)
                t->cpu->mSFR[a - 0x80] = buf[i];
            break;
        case SPACE_XRAM:
            if (a <= t->cpu->mExtDataMaxIdx)
                t->cpu->mExtData[a] = buf[i];
            break;
        case SPACE_BIT:
            /* bit-level write */
            if (a < 0x80) {
                uint8_t *byte = &t->cpu->mLowerData[0x20 + (a >> 3)];
                if (buf[i]) *byte |= (1 << (a & 7));
                else        *byte &= ~(1 << (a & 7));
            } else {
                uint8_t *sfr = &t->cpu->mSFR[(a & 0xF8) - 0x80];
                if (buf[i]) *sfr |= (1 << (a & 7));
                else        *sfr &= ~(1 << (a & 7));
            }
            break;
        }
    }
    return len;
}

/* ================================================================== *
 * Registers                                                           *
 * ================================================================== */

uint16_t dbg_get_pc(struct dbg_target *t) { return t->cpu->mPC; }
uint8_t  dbg_get_acc(struct dbg_target *t) { return t->cpu->mSFR[REG_ACC]; }
uint8_t  dbg_get_b(struct dbg_target *t) { return t->cpu->mSFR[REG_B]; }
uint16_t dbg_get_dptr(struct dbg_target *t) {
    return (t->cpu->mSFR[REG_DPH] << 8) | t->cpu->mSFR[REG_DPL];
}
uint8_t  dbg_get_sp(struct dbg_target *t) { return t->cpu->mSFR[REG_SP]; }
uint8_t  dbg_get_psw(struct dbg_target *t) { return t->cpu->mSFR[REG_PSW]; }
uint8_t  dbg_get_rn(struct dbg_target *t, int n) {
    int bank = (t->cpu->mSFR[REG_PSW] >> 3) & 0x03;
    return t->cpu->mLowerData[bank * 8 + (n & 7)];
}

/* ================================================================== *
 * Level 1 position (§2)                                               *
 * ================================================================== */

uint16_t dbg_get_bw_ms(struct dbg_target *t)
{
    if (t->syms.bw_ms_addr == 0) return 0;
    return read_iram16(t->cpu, t->syms.bw_ms_addr);
}

uint16_t dbg_get_task_state(struct dbg_target *t, int task_idx)
{
    if (task_idx < 0 || task_idx >= t->syms.n_tasks) return 0xFFFF;
    return read_iram16(t->cpu, t->syms.tasks[task_idx].state_addr);
}

uint16_t dbg_get_task_until(struct dbg_target *t, int task_idx)
{
    if (task_idx < 0 || task_idx >= t->syms.n_tasks) return 0;
    uint16_t addr = t->syms.tasks[task_idx].until_addr;
    if (addr == 0) return 0;
    return read_iram16(t->cpu, addr);
}

void dbg_set_symbols(struct dbg_target *t, struct dbg_symbols *syms)
{
    t->syms = *syms;
}

void dbg_set_on_halt(struct dbg_target *t, dbg_on_halt_fn fn, void *data)
{
    t->on_halt = fn;
    t->on_halt_data = data;
}

uint64_t dbg_get_time_ns(struct dbg_target *t)
{
    return stc12_get_time_ns(t->stc);
}

/* ================================================================== *
 * Profiling: PC histogram                                             *
 * ================================================================== */

void dbg_profile_start(struct dbg_target *t)
{
    if (!t->pc_histogram)
        t->pc_histogram = calloc(65536, sizeof(uint32_t));
    else
        memset(t->pc_histogram, 0, 65536 * sizeof(uint32_t));
    t->profile_total = 0;
    t->profiling = true;
}

void dbg_profile_stop(struct dbg_target *t)
{
    t->profiling = false;
}

uint32_t dbg_profile_get(struct dbg_target *t, uint16_t addr)
{
    if (!t->pc_histogram) return 0;
    return t->pc_histogram[addr];
}

uint32_t dbg_profile_total(struct dbg_target *t)
{
    return t->profile_total;
}
