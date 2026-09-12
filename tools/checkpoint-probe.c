/* Test-only translation unit. Do not link wasm_api.c separately or ship this
 * artifact: including it gives read-only access to the actual live instance.
 * No checkpoint encoding/decoding is used to produce these observations. */
#include "../wasm_api.c"
#include <stdarg.h>
#include <inttypes.h>

static char proof_json[512 * 1024];
static size_t proof_at;
static void out(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(proof_json + proof_at, sizeof(proof_json) - proof_at, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(proof_json) - proof_at) abort();
    proof_at += (size_t)n;
}
static void arr(const uint8_t *p, size_t n) {
    out("[");
    for (size_t i = 0; i < n; i++) out("%s%u", i ? "," : "", p[i]);
    out("]");
}
static uint32_t memory_hash(const uint8_t *p, size_t n) {
    uint32_t h = 5381;
    for (size_t i = 0; i < n; i++) h = h * 33u + p[i];
    return h;
}
EMSCRIPTEN_KEEPALIVE
const char *proof_observe(void) {
    proof_at = 0;
    out("{\"phase\":\"%u\",\"state\":{\"cpu\":{\"pc\":%u,\"delay\":%u,\"scale\":%u,\"sfr\":",
        cpu.mTickDelay, cpu.mPC, cpu.mTickDelay, cpu.mMachineCycleScale);
    arr(cpu.mSFR, 128); out(",\"iram\":"); arr(cpu.mLowerData, 128);
    out(",\"upper\":"); arr(cpu.mUpperData, 128);
    out(",\"memoryHashes\":[%u,%u]},", memory_hash(cpu.mCodeMem, 65536), memory_hash(cpu.mExtData, 65536));
    out("\"timers\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],",
        stc.timer0_prescaler, stc.timer1_prescaler, stc.brt_prescaler,
        stc.adc_countdown, stc.wdt_counter, stc.wdt_prescaler_cnt,
        stc.pca_prescaler, stc.pca_t0_overflow_pending, stc.last_tf1,
        cpu.mSFR[REG_TCON], cpu.mSFR[REG_TMOD], cpu.mSFR[REG_TL0],
        cpu.mSFR[REG_TH0], cpu.mSFR[REG_TL1], cpu.mSFR[REG_TH1]);
    out("\"interrupts\":{\"active\":%u,\"ie\":%u,\"ip\":%u,\"pendingSerial\":%u,\"saved\":[",
        cpu.mInterruptActive, cpu.mSFR[REG_IE], cpu.mSFR[REG_IP], cpu.serial_interrupt_trigger);
    arr(cpu.int_a, 2); out(","); arr(cpu.int_psw, 2); out(","); arr(cpu.int_sp, 2); out("]},");
    out("\"uartInput\":[%u,%u,%u,%u],\"uartOutput\":{\"index\":%u,\"bits\":%u,\"bytes\":",
        cpu.mSFR[REG_SBUF], cpu.mSFR[REG_SCON], cpu.mSFR[STC_REG_S2BUF], cpu.mSFR[STC_REG_S2CON],
        cpu.serial_out_idx, cpu.serial_out_remaining_bits);
    arr((const uint8_t *)cpu.serial_out, 18); out("},");
    out("\"time\":{\"clocks\":\"%" PRIu64 "\",\"ns\":\"%" PRIu64 "\",\"skipped\":\"%" PRIu64 "\",\"quantum\":\"%" PRIu64 "\",\"fosc\":%u},",
        stc.osc_clocks, stc12_get_time_ns(&stc), stc.idle_skipped_clocks, stc.ns_per_clock_x256, stc.fosc);
    out("\"inputs\":{\"digital\":"); arr(stc.port_ext, 6); out(",\"adc\":[");
    for (int i = 0; i < 8; i++) out("%s%u", i ? "," : "", stc.adc_input[i]);
    out("]},\"pins\":{\"m1\":"); arr(stc.pin_m1_shadow, 6);
    out(",\"m0\":"); arr(stc.pin_m0_shadow, 6);
    out(",\"drive\":"); arr(stc.pin_drive_shadow, 6);
    out(",\"cex\":"); arr(stc.pca_cex_last, 3); out("},");
    out("\"history\":{\"enabled\":%u,\"head\":%u,\"count\":%u,\"events\":[",
        stc.pin_history != NULL, stc.pin_history_head, stc.pin_history_count);
    if (stc.pin_history) for (int i = 0; i < PIN_HISTORY_SIZE; i++) {
        const struct stc12_pin_event *e = &stc.pin_history[i];
        out("%s[\"%" PRIu64 "\",%u,%u,%u,%u]", i ? "," : "", e->t_ns, e->port, e->bit, e->mode, e->drive);
    }
    out("]},\"peripheral\":[%u,%u,%u,%u,%u,%u,%u,%u],", stc.adc_countdown,
        cpu.mSFR[STC_REG_ADC_CONTR], cpu.mSFR[STC_REG_ADC_RES], cpu.mSFR[STC_REG_ADC_RESL],
        cpu.mSFR[STC_REG_CL], cpu.mSFR[STC_REG_CH], stc.wdt_counter, stc.wdt_prescaler_cnt);
    out("\"debug\":{\"control\":[%u,%u,%u,%u,%u,%u,%u],\"bps\":[", dbg.state, dbg.step_kind,
        dbg.step_count, dbg.profiling, dbg.profile_total, dbg.last_halt.cause, dbg.syms.n_tasks);
    for (int i = 0; i < DBG_MAX_BP; i++) {
        const struct dbg_breakpoint *b = &dbg.bps[i];
        out("%s[%u,%u,%d,%u,%u]", i ? "," : "", b->kind, b->addr, b->id, b->active, dbg.watch_shadow[i]);
    }
    out("],\"tasks\":[");
    for (int i = 0; i < dbg.syms.n_tasks; i++)
        out("%s[%u,%u]", i ? "," : "", dbg.syms.tasks[i].state_addr, dbg.syms.tasks[i].until_addr);
    out("],\"stepTasks\":"); arr(dbg.step_task_state, 8);
    out(",\"halt\":[%u,%u,%d,%u,%u,%u,%u,%u]}}}", dbg.last_halt.cause,
        dbg.last_halt.pc, dbg.last_halt.bp_id, dbg.last_halt.is_watch,
        dbg.last_halt.watch_space, dbg.last_halt.watch_addr,
        dbg.last_halt.watch_value, dbg.last_halt.watch_prev);
    return proof_json;
}
