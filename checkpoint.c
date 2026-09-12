#include "checkpoint.h"
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(double) == 8, "checkpoint v1 requires IEEE-width double");

/*
 * Every rewind-visible value is named below. Pointers, C padding, function
 * tables, callbacks, and user-data addresses never enter the wire format.
 * Decode starts from copies of the live structs, preserving those bindings;
 * only newly owned buffers are swapped at the final no-fail commit point.
 *
 * V1 always reserves full code/XRAM/IRAM/SFR, all pin-history slots, and the
 * full profiling histogram. Presence bytes distinguish disabled optional
 * storage from allocated zeroes. JS-owned future-input queues are external;
 * already accepted UART/input latches live in SFR/serial/port_ext state here.
 */
#define MAGIC 0x31435045u /* "EPC1" in little endian */
#define HEADER_SIZE 20u

struct writer {
  uint8_t *p;
  uint32_t n, at;
  int bad;
};
struct reader {
  const uint8_t *p;
  uint32_t n, at;
  int bad;
};
static void wu8(struct writer *w, uint8_t v) {
  if (w->at == UINT32_MAX) {
    w->bad = 1;
    return;
  }
  if (w->p && w->at < w->n)
    w->p[w->at] = v;
  else if (w->p)
    w->bad = 1;
  w->at++;
}
static void wu16(struct writer *w, uint16_t v) {
  wu8(w, v);
  wu8(w, v >> 8);
}
static void wu32(struct writer *w, uint32_t v) {
  wu16(w, v);
  wu16(w, v >> 16);
}
static void wu64(struct writer *w, uint64_t v) {
  wu32(w, (uint32_t)v);
  wu32(w, (uint32_t)(v >> 32));
}
static void wbytes(struct writer *w, const void *p, uint32_t n) {
  if (!w->p) {
    if (UINT32_MAX - w->at < n) {
      w->bad = 1;
      return;
    }
    w->at += n;
    return;
  }
  const uint8_t *b = p;
  for (uint32_t i = 0; i < n; i++)
    wu8(w, b[i]);
}
static uint8_t ru8(struct reader *r) {
  if (r->at >= r->n) {
    r->bad = 1;
    return 0;
  }
  return r->p[r->at++];
}
static uint16_t ru16(struct reader *r) {
  uint16_t v = ru8(r);
  return v | ((uint16_t)ru8(r) << 8);
}
static uint32_t ru32(struct reader *r) {
  uint32_t v = ru16(r);
  return v | ((uint32_t)ru16(r) << 16);
}
static uint64_t ru64(struct reader *r) {
  uint64_t v = ru32(r);
  return v | ((uint64_t)ru32(r) << 32);
}
static void rbytes(struct reader *r, void *p, uint32_t n) {
  uint8_t *b = p;
  for (uint32_t i = 0; i < n; i++)
    b[i] = ru8(r);
}
static int rbool(struct reader *r, bool *v) {
  uint8_t x = ru8(r);
  if (x > 1)
    return 0;
  *v = x != 0;
  return 1;
}

static uint32_t checksum(const uint8_t *bytes, uint32_t length) {
  uint32_t hash = 2166136261u;
  for (uint32_t i = HEADER_SIZE; i < length; i++) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

static void write_cpu(struct writer *w, const struct em8051 *c) {
  wu16(w, c->mCodeMemMaxIdx);
  wu16(w, c->mExtDataMaxIdx);
  wbytes(w, c->mCodeMem, 65536);
  wbytes(w, c->mExtData, 65536);
  wbytes(w, c->mLowerData, 128);
  wbytes(w, c->mUpperData, 128);
  wbytes(w, c->mSFR, 128);
  wu16(w, c->mPC);
  wu8(w, c->mTickDelay);
  wu8(w, c->mMachineCycleScale);
  wu8(w, c->mInterruptActive);
  wbytes(w, c->int_a, 2);
  wbytes(w, c->int_psw, 2);
  wbytes(w, c->int_sp, 2);
  wbytes(w, c->serial_out, 18);
  wu8(w, c->serial_out_idx);
  wu8(w, c->serial_out_remaining_bits);
  wu8(w, c->serial_interrupt_trigger);
  wu8(w, c->skip_timers);
}
static int read_cpu(struct reader *r, struct em8051 *c) {
  c->mCodeMemMaxIdx = ru16(r);
  c->mExtDataMaxIdx = ru16(r);
  rbytes(r, c->mCodeMem, 65536);
  rbytes(r, c->mExtData, 65536);
  rbytes(r, c->mLowerData, 128);
  rbytes(r, c->mUpperData, 128);
  rbytes(r, c->mSFR, 128);
  c->mPC = ru16(r);
  c->mTickDelay = ru8(r);
  c->mMachineCycleScale = ru8(r);
  c->mInterruptActive = ru8(r);
  rbytes(r, c->int_a, 2);
  rbytes(r, c->int_psw, 2);
  rbytes(r, c->int_sp, 2);
  rbytes(r, c->serial_out, 18);
  c->serial_out_idx = ru8(r);
  c->serial_out_remaining_bits = ru8(r);
  return rbool(r, &c->serial_interrupt_trigger) && rbool(r, &c->skip_timers);
}
static void write_stc(struct writer *w, const struct stc12_state *s) {
  wu8(w, s->timer0_prescaler);
  wu8(w, s->timer1_prescaler);
  wu8(w, s->brt_prescaler);
  wu16(w, s->adc_countdown);
  for (int i = 0; i < 8; i++)
    wu16(w, s->adc_input[i]);
  wu8(w, s->dptr1_l);
  wu8(w, s->dptr1_h);
  wu8(w, s->last_dps);
  wu32(w, s->wdt_counter);
  wu8(w, s->wdt_prescaler_cnt);
  wu8(w, s->pca_prescaler);
  wu8(w, s->pca_t0_overflow_pending);
  wbytes(w, s->pca_cex_last, 3);
  wu8(w, s->last_tf1);
  wu8(w, s->stc89_scaled);
  wbytes(w, s->port_ext, 6);
  wu8(w, s->stc12_mode);
  wu32(w, s->fosc);
  uint64_t vcc;
  memcpy(&vcc, &s->vcc, 8);
  wu64(w, vcc);
  wu8(w, s->pin_history != NULL);
  wu32(w, s->pin_history_head);
  wu32(w, s->pin_history_count);
  for (uint32_t i = 0; i < PIN_HISTORY_SIZE; i++) {
    const struct stc12_pin_event z = {0};
    const struct stc12_pin_event *e = s->pin_history ? &s->pin_history[i] : &z;
    wu64(w, e->t_ns);
    wu8(w, e->port);
    wu8(w, e->bit);
    wu8(w, e->mode);
    wu8(w, e->drive);
  }
  wu8(w, s->part_id);
  wu32(w, s->unmodelled_sfr_accesses);
  wu64(w, s->osc_clocks);
  wu64(w, s->idle_skipped_clocks);
  wu64(w, s->ns_per_clock_x256);
  wbytes(w, s->pin_m1_shadow, 6);
  wbytes(w, s->pin_m0_shadow, 6);
  wbytes(w, s->pin_drive_shadow, 6);
}
static int read_stc(struct reader *r, struct stc12_state *s) {
  s->timer0_prescaler = ru8(r);
  s->timer1_prescaler = ru8(r);
  s->brt_prescaler = ru8(r);
  s->adc_countdown = ru16(r);
  for (int i = 0; i < 8; i++)
    s->adc_input[i] = ru16(r);
  s->dptr1_l = ru8(r);
  s->dptr1_h = ru8(r);
  s->last_dps = ru8(r);
  s->wdt_counter = ru32(r);
  s->wdt_prescaler_cnt = ru8(r);
  s->pca_prescaler = ru8(r);
  if (!rbool(r, &s->pca_t0_overflow_pending))
    return 0;
  rbytes(r, s->pca_cex_last, 3);
  s->last_tf1 = ru8(r);
  if (!rbool(r, &s->stc89_scaled))
    return 0;
  rbytes(r, s->port_ext, 6);
  if (!rbool(r, &s->stc12_mode))
    return 0;
  s->fosc = ru32(r);
  uint64_t vcc = ru64(r);
  memcpy(&s->vcc, &vcc, 8);
  bool has;
  if (!rbool(r, &has))
    return 0;
  if (has) {
    s->pin_history = calloc(PIN_HISTORY_SIZE, sizeof(*s->pin_history));
    if (!s->pin_history)
      return -1;
  } else
    s->pin_history = NULL;
  s->pin_history_head = ru32(r);
  s->pin_history_count = ru32(r);
  for (uint32_t i = 0; i < PIN_HISTORY_SIZE; i++) {
    struct stc12_pin_event e;
    e.t_ns = ru64(r);
    e.port = ru8(r);
    e.bit = ru8(r);
    e.mode = ru8(r);
    e.drive = ru8(r);
    if (has)
      s->pin_history[i] = e;
  }
  s->part_id = ru8(r);
  s->unmodelled_sfr_accesses = ru32(r);
  s->osc_clocks = ru64(r);
  s->idle_skipped_clocks = ru64(r);
  s->ns_per_clock_x256 = ru64(r);
  rbytes(r, s->pin_m1_shadow, 6);
  rbytes(r, s->pin_m0_shadow, 6);
  rbytes(r, s->pin_drive_shadow, 6);
  return 1;
}
static void write_dbg(struct writer *w, const struct dbg_target *d) {
  wu8(w, d->state);
  wu32(w, d->next_bp_id);
  for (int i = 0; i < DBG_MAX_BP; i++) {
    const struct dbg_breakpoint *b = &d->bps[i];
    wu8(w, b->kind);
    wu16(w, b->addr);
    if (b->kind == BP_YIELD) {
      wu32(w, b->yield.task);
      wu16(w, b->yield.state);
    } else {
      wu32(w, b->watch.space);
      wu16(w, b->watch.len);
    }
    wu32(w, b->id);
    wu8(w, b->active);
  }
  wu16(w, d->syms.bw_ms_addr);
  wu8(w, d->syms.n_tasks);
  for (int i = 0; i < 8; i++) {
    const struct dbg_task_pos *t =
        i < d->syms.n_tasks ? &d->syms.tasks[i] : NULL;
    wu16(w, t ? t->state_addr : 0);
    wu16(w, t ? t->until_addr : 0);
  }
  wu8(w, d->step_kind);
  wu32(w, d->step_count);
  wu8(w, d->step_entry_sp);
  wu8(w, d->last_halt.cause);
  wu16(w, d->last_halt.pc);
  wu32(w, d->last_halt.bp_id);
  wu64(w, d->last_halt.t_ns);
  wu8(w, d->last_halt.is_watch);
  wu8(w, d->last_halt.watch_space);
  wu16(w, d->last_halt.watch_addr);
  wu8(w, d->last_halt.watch_value);
  wu8(w, d->last_halt.watch_prev);
  wbytes(w, d->watch_shadow, DBG_MAX_BP);
  wbytes(w, d->step_task_state, 8);
  wu8(w, d->profiling);
  wu8(w, d->pc_histogram != NULL);
  wu32(w, d->profile_total);
  for (int i = 0; i < 65536; i++)
    wu32(w, d->pc_histogram ? d->pc_histogram[i] : 0);
}
static int read_dbg(struct reader *r, struct dbg_target *d,
                    struct dbg_task_pos *tasks) {
  d->state = ru8(r);
  uint32_t raw = ru32(r);
  if (raw > INT_MAX)
    return -2;
  d->next_bp_id = (int)raw;
  for (int i = 0; i < DBG_MAX_BP; i++) {
    struct dbg_breakpoint *b = &d->bps[i];
    b->kind = ru8(r);
    b->addr = ru16(r);
    uint32_t a = ru32(r);
    uint16_t z = ru16(r);
    if (b->kind == BP_YIELD) {
      if (a > UINT8_MAX)
        return -2;
      b->yield.task = (uint8_t)a;
      b->yield.state = z;
    } else {
      b->watch.space = (enum dbg_space)a;
      b->watch.len = z;
    }
    raw = ru32(r);
    if (raw > INT_MAX)
      return -2;
    b->id = (int)raw;
    if (!rbool(r, &b->active))
      return 0;
  }
  d->syms.bw_ms_addr = ru16(r);
  d->syms.n_tasks = ru8(r);
  d->syms.tasks = tasks;
  for (int i = 0; i < 8; i++) {
    tasks[i].name = NULL;
    tasks[i].state_addr = ru16(r);
    tasks[i].until_addr = ru16(r);
  }
  d->step_kind = ru8(r);
  raw = ru32(r);
  if (raw > INT_MAX)
    return -2;
  d->step_count = (int)raw;
  d->step_entry_sp = ru8(r);
  d->last_halt.cause = ru8(r);
  d->last_halt.pc = ru16(r);
  raw = ru32(r);
  if (raw != UINT32_MAX && raw > INT_MAX)
    return -2;
  d->last_halt.bp_id = raw == UINT32_MAX ? -1 : (int)raw;
  d->last_halt.t_ns = ru64(r);
  if (!rbool(r, &d->last_halt.is_watch))
    return 0;
  d->last_halt.watch_space = ru8(r);
  d->last_halt.watch_addr = ru16(r);
  d->last_halt.watch_value = ru8(r);
  d->last_halt.watch_prev = ru8(r);
  rbytes(r, d->watch_shadow, DBG_MAX_BP);
  rbytes(r, d->step_task_state, 8);
  if (!rbool(r, &d->profiling))
    return 0;
  bool has;
  if (!rbool(r, &has))
    return 0;
  d->profile_total = ru32(r);
  if (has) {
    d->pc_histogram = calloc(65536, sizeof(uint32_t));
    if (!d->pc_histogram)
      return -1;
  } else
    d->pc_histogram = NULL;
  for (int i = 0; i < 65536; i++) {
    uint32_t x = ru32(r);
    if (has)
      d->pc_histogram[i] = x;
  }
  return 1;
}
static void write_all(struct writer *w, const struct em8051 *c,
                      const struct stc12_state *s, const struct dbg_target *d,
                      int initialized, uint32_t total) {
  wu32(w, MAGIC);
  wu32(w, EMU_CHECKPOINT_VERSION);
  wu32(w, EMU_CHECKPOINT_BUILD_ID);
  wu32(w, total);
  wu32(w, 0); /* payload checksum, filled after encoding */
  wu8(w, initialized ? 1 : 0);
  wu8(w, stc12_get_idle_fastforward() ? 1 : 0);
  write_cpu(w, c);
  write_stc(w, s);
  write_dbg(w, d);
}
uint32_t emu_checkpoint_codec_size(void) {
  static uint32_t size;
  if (!size) {
    struct writer w = {0};
    struct em8051 c = {0};
    struct stc12_state s = {0};
    struct dbg_target d = {0};
    write_all(&w, &c, &s, &d, 0, 0);
    if (w.bad)
      return 0;
    size = w.at;
  }
  return size;
}
int emu_checkpoint_encode(const struct em8051 *c, const struct stc12_state *s,
                          const struct dbg_target *d, int initialized,
                          uint8_t *dst, uint32_t len) {
  if (!initialized)
    return -1;
  if (!dst)
    return -2;
  if (len != emu_checkpoint_codec_size())
    return -3;
  struct writer w = {dst, len, 0, 0};
  write_all(&w, c, s, d, initialized, len);
  uint32_t hash = checksum(dst, len);
  dst[16] = (uint8_t)hash;
  dst[17] = (uint8_t)(hash >> 8);
  dst[18] = (uint8_t)(hash >> 16);
  dst[19] = (uint8_t)(hash >> 24);
  return w.bad ? -4 : 0;
}
int emu_checkpoint_decode(struct em8051 *c, struct stc12_state *s,
                          struct dbg_target *d, struct dbg_task_pos *wasm_tasks,
                          int *initialized, const uint8_t *src, uint32_t len) {
  if (!*initialized)
    return -1;
  if (!src)
    return -2;
  if (len != emu_checkpoint_codec_size())
    return -3;
  struct reader r = {src, len, 0, 0};
  if (ru32(&r) != MAGIC)
    return -4;
  if (ru32(&r) != EMU_CHECKPOINT_VERSION)
    return -5;
  if (ru32(&r) != EMU_CHECKPOINT_BUILD_ID)
    return -6;
  if (ru32(&r) != len)
    return -4;
  if (ru32(&r) != checksum(src, len))
    return -4;
  bool init, idle;
  if (!rbool(&r, &init) || !rbool(&r, &idle) || !init)
    return -4;
  struct em8051 tc = *c;
  struct stc12_state ts = *s;
  struct dbg_target td = *d;
  struct dbg_task_pos tasks[8];
  tc.mCodeMem = malloc(65536);
  tc.mExtData = malloc(65536);
  tc.mUpperData = malloc(128);
  ts.pin_history = NULL;
  td.pc_histogram = NULL;
  if (!tc.mCodeMem || !tc.mExtData || !tc.mUpperData) {
    free(tc.mCodeMem);
    free(tc.mExtData);
    free(tc.mUpperData);
    return -8;
  }
  int ss = 1, dd = 1;
  if (!read_cpu(&r, &tc) || (ss = read_stc(&r, &ts)) <= 0 ||
      (dd = read_dbg(&r, &td, tasks)) <= 0 || r.bad || r.at != len) {
    free(tc.mCodeMem);
    free(tc.mExtData);
    free(tc.mUpperData);
    free(ts.pin_history);
    free(td.pc_histogram);
    if (ss == -1 || dd == -1)
      return -8;
    return dd == -2 ? -7 : -4;
  }
  if (tc.mCodeMemMaxIdx != 65535 || tc.mExtDataMaxIdx != 65535 ||
      (tc.mMachineCycleScale != 1 && tc.mMachineCycleScale != 12) ||
      tc.mInterruptActive > 3 || tc.serial_out_idx >= 18 ||
      tc.serial_out_remaining_bits > 10 || ts.timer0_prescaler >= 12 ||
      ts.timer1_prescaler >= 12 || ts.brt_prescaler >= 12 ||
      ts.pca_prescaler >= 12 || ts.part_id > PART_STC12_16 ||
      !isfinite(ts.vcc) || ts.vcc < 0.0 || ts.vcc > 20.0 ||
      (ts.stc12_mode && (!ts.fosc || !ts.ns_per_clock_x256)) ||
      (ts.stc12_mode && ts.part_id == PART_STC89 &&
       (tc.mMachineCycleScale != 12 || tc.skip_timers)) ||
      (ts.stc12_mode && ts.part_id != PART_STC89 &&
       (tc.mMachineCycleScale != 1 || !tc.skip_timers)) ||
      td.state > DBG_RUNNING ||
      (!ts.pin_history && (ts.pin_history_head || ts.pin_history_count)) ||
      (ts.pin_history && ts.pin_history_head != ts.pin_history_count) ||
      td.syms.n_tasks < 0 || td.syms.n_tasks > 8 || td.step_kind > STEP_CYCLE ||
      td.last_halt.cause > HALT_FAULT ||
      (td.last_halt.is_watch && td.last_halt.watch_space > SPACE_BIT) ||
      (td.profiling && !td.pc_histogram)) {
    free(tc.mCodeMem);
    free(tc.mExtData);
    free(tc.mUpperData);
    free(ts.pin_history);
    free(td.pc_histogram);
    return -7;
  }
  for (int i = 0; i < 8; i++)
    if (ts.adc_input[i] > 1023) {
      free(tc.mCodeMem);
      free(tc.mExtData);
      free(tc.mUpperData);
      free(ts.pin_history);
      free(td.pc_histogram);
      return -7;
    }
  if (ts.pin_history)
    for (uint32_t i = 0; i < PIN_HISTORY_SIZE; i++) {
      const struct stc12_pin_event *event = &ts.pin_history[i];
      if (event->port >= 6 || event->bit >= 8 || event->mode > PIN_OPENDRAIN ||
          event->drive > 1) {
        free(tc.mCodeMem);
        free(tc.mExtData);
        free(tc.mUpperData);
        free(ts.pin_history);
        free(td.pc_histogram);
        return -7;
      }
    }
  for (int i = 0; i < DBG_MAX_BP; i++) {
    const struct dbg_breakpoint *bp = &td.bps[i];
    if (bp->kind > BP_READ ||
        ((bp->kind == BP_READ || bp->kind == BP_WRITE) &&
         bp->watch.space > SPACE_BIT)) {
      free(tc.mCodeMem);
      free(tc.mExtData);
      free(tc.mUpperData);
      free(ts.pin_history);
      free(td.pc_histogram);
      return -7;
    }
  }
  unsigned char *oc = c->mCodeMem, *ox = c->mExtData, *ou = c->mUpperData;
  struct stc12_pin_event *oph = s->pin_history;
  uint32_t *oh = d->pc_histogram;
  dbg_on_halt_fn halt = d->on_halt;
  void *haltdata = d->on_halt_data;
  *c = tc;
  *s = ts;
  *d = td;
  em8051_rebind_tables(c);
  stc12_rebind_callbacks(c, s);
  d->cpu = c;
  d->stc = s;
  d->on_halt = halt;
  d->on_halt_data = haltdata;
  memcpy(wasm_tasks, tasks, sizeof(tasks));
  d->syms.tasks = wasm_tasks;
  stc12_set_idle_fastforward(idle);
  free(oc);
  free(ox);
  free(ou);
  free(oph);
  free(oh);
  return 0;
}
