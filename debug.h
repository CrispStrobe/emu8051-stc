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

/* Halt reason (§7)
 *
 * `watch_*` describe the byte a WRITE watchpoint caught, and are meaningful
 * only when `is_watch` is true. They exist because "halted at PC 0x0142" is
 * not an answer to "what wrote my variable?": the user set the watchpoint on
 * an ADDRESS and the only useful report names that address and what it now
 * holds. `watch_prev` is what the byte held at the previous instruction
 * boundary, so a front end can render the transition rather than a value with
 * no before.
 *
 * On every other cause `is_watch` is false and the four fields are zero. A
 * consumer must branch on `is_watch`, not on a sentinel address — 0x00 is a
 * legal IRAM address.
 */
struct dbg_halt_reason {
    enum dbg_halt_cause cause;
    uint16_t pc;
    int bp_id;                   /* -1 if no breakpoint */
    uint64_t t_ns;               /* program-time nanoseconds since reset */
    bool is_watch;               /* true when a BP_WRITE caused this halt */
    enum dbg_space watch_space;  /* which space the watched byte lives in */
    uint16_t watch_addr;         /* the watched address */
    uint8_t watch_value;         /* what it holds now */
    uint8_t watch_prev;          /* what it held one instruction ago */
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

    /* The last reason emitted, kept so a host that cannot receive a struct
     * through a callback (the WASM build: a JS callback gets a pointer into a
     * heap whose layout it must not assume) can read it back field by field.
     * Written on every halt, including HALT_USER. */
    struct dbg_halt_reason last_halt;

    /* Write watchpoint shadow (polling, checked after each instruction) */
    uint8_t watch_shadow[DBG_MAX_BP];

    /* Task state snapshot for STEP_BLOCK detection */
    uint8_t step_task_state[8];  /* max 8 tasks */

    /* Profiling */
    bool profiling;
    uint32_t *pc_histogram;  /* 64K entries, allocated on start */
    uint32_t profile_total;
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

/* The last halt reason emitted, readable without a callback. Never NULL:
 * before the first halt it reads cause = HALT_HALTED-at-init (zeroed) with
 * bp_id 0; after dbg_reset it reads HALT_RESET. Use it when the transport
 * cannot carry a struct — the WASM build reads it field by field. */
const struct dbg_halt_reason *dbg_get_last_halt(struct dbg_target *t);

/* Time */
uint64_t dbg_get_time_ns(struct dbg_target *t);

/* Profiling: PC histogram. Call dbg_profile_start to begin, then
 * dbg_profile_get to read counts. Each code address that executes
 * during profiling increments its counter. */
void dbg_profile_start(struct dbg_target *t);
void dbg_profile_stop(struct dbg_target *t);
uint32_t dbg_profile_get(struct dbg_target *t, uint16_t addr);
uint32_t dbg_profile_total(struct dbg_target *t);

/* Capabilities: consumes field (§7 decision 5).
 * Emulators return an empty list (take nothing).
 * The on-chip monitor would return e.g. ["timer1", "uart1"]. */
#define DBG_CONSUMES_NONE 0

#endif /* DEBUG_H */
