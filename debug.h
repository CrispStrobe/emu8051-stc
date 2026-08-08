/* Debug control interface — boundary D (DEBUG-CONTROL-MODEL.md §7)
 * Copyright 2024 CrispStrobe (MIT)
 *
 * Implements the DebugTarget interface for the emu8051-stc emulator.
 * This is the capable target: all step kinds, all breakpoint kinds,
 * all address spaces, timeFreezes = true, skewNs = 0.
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdint.h>
#include <stdbool.h>
#include "emu8051.h"
#include "stc12.h"

/* ------------------------------------------------------------------ *
 * Types matching the spec                                             *
 * ------------------------------------------------------------------ */

enum dbg_state { DBG_HALTED, DBG_RUNNING };
enum dbg_step_kind { STEP_INSN, STEP_LINE, STEP_BLOCK, STEP_OVER, STEP_OUT };
enum dbg_space { SPACE_CODE, SPACE_IRAM, SPACE_SFR, SPACE_XRAM, SPACE_BIT };
enum dbg_halt_cause { HALT_BP, HALT_STEP, HALT_USER, HALT_RESET, HALT_FAULT };

enum dbg_bp_kind { BP_CODE, BP_YIELD, BP_WRITE, BP_READ };

struct dbg_breakpoint {
    enum dbg_bp_kind kind;
    uint16_t addr;               /* code address (used for code AND yield BPs) */
    union {
        struct { uint8_t task; uint16_t state; } yield; /* yield metadata */
        struct { enum dbg_space space; uint16_t len; } watch;
    };
    int id;                      /* handle, assigned on set */
    bool active;
};

#define DBG_MAX_BP 32

/* Level 1 task position (§2) */
struct dbg_task_pos {
    const char *name;            /* task name from symbol table */
    uint16_t state_addr;         /* IRAM address of <task>_state */
    uint16_t until_addr;         /* IRAM address of <task>_until (0 if none) */
};

/* Symbol table input (§2) — provided by stc-compiler / sb3-creator */
struct dbg_symbols {
    uint16_t bw_ms_addr;         /* IRAM address of bw_ms */
    struct dbg_task_pos *tasks;
    int n_tasks;
};

/* Halt reason (§7) */
struct dbg_halt_reason {
    enum dbg_halt_cause cause;
    uint16_t pc;
    int bp_id;                   /* -1 if no breakpoint */
    uint64_t t_ns;               /* program-time nanoseconds since reset */
};

/* Halt callback */
typedef void (*dbg_on_halt_fn)(struct dbg_halt_reason *reason, void *user_data);

/* ------------------------------------------------------------------ *
 * Debug target state                                                  *
 * ------------------------------------------------------------------ */

struct dbg_target {
    struct em8051 *cpu;
    struct stc12_state *stc;

    enum dbg_state state;
    struct dbg_breakpoint bps[DBG_MAX_BP];
    int next_bp_id;

    /* Symbol table */
    struct dbg_symbols syms;

    /* Step state */
    enum dbg_step_kind step_kind;
    int step_count;
    uint8_t step_entry_sp;       /* for step-over / step-out */

    /* Halt callback */
    dbg_on_halt_fn on_halt;
    void *on_halt_data;
};

/* ------------------------------------------------------------------ *
 * Public API                                                          *
 * ------------------------------------------------------------------ */

void dbg_init(struct dbg_target *t, struct em8051 *cpu, struct stc12_state *stc);

/* State */
enum dbg_state dbg_get_state(struct dbg_target *t);

/* Run control */
void dbg_run(struct dbg_target *t);
void dbg_halt(struct dbg_target *t);
int  dbg_step(struct dbg_target *t, enum dbg_step_kind kind, int count);
void dbg_reset(struct dbg_target *t);

/* Tick — call from the main loop when state == DBG_RUNNING.
 * Returns true if the target halted (breakpoint, step complete). */
bool dbg_tick(struct dbg_target *t);

/* Breakpoints */
int  dbg_set_breakpoint(struct dbg_target *t, struct dbg_breakpoint *bp);
void dbg_clear_breakpoint(struct dbg_target *t, int handle);

/* Memory access */
int  dbg_read_mem(struct dbg_target *t, enum dbg_space space,
                  uint16_t addr, uint8_t *buf, int len);
int  dbg_write_mem(struct dbg_target *t, enum dbg_space space,
                   uint16_t addr, const uint8_t *buf, int len);

/* Registers */
uint16_t dbg_get_pc(struct dbg_target *t);
uint8_t  dbg_get_acc(struct dbg_target *t);
uint8_t  dbg_get_b(struct dbg_target *t);
uint16_t dbg_get_dptr(struct dbg_target *t);
uint8_t  dbg_get_sp(struct dbg_target *t);
uint8_t  dbg_get_psw(struct dbg_target *t);
uint8_t  dbg_get_rn(struct dbg_target *t, int n);

/* Level 1 position (§2) — read task state from IRAM */
uint16_t dbg_get_bw_ms(struct dbg_target *t);
uint16_t dbg_get_task_state(struct dbg_target *t, int task_idx);
uint16_t dbg_get_task_until(struct dbg_target *t, int task_idx);

/* Symbol table */
void dbg_set_symbols(struct dbg_target *t, struct dbg_symbols *syms);

/* Halt callback */
void dbg_set_on_halt(struct dbg_target *t, dbg_on_halt_fn fn, void *data);

/* Time */
uint64_t dbg_get_time_ns(struct dbg_target *t);

#endif /* DEBUG_H */
