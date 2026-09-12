/* Fault injection confined to checkpoint.c's allocations. This translation
 * unit replaces checkpoint.c in the test link; production source is unchanged. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int fail_at, allocations;
static void *fault_malloc(size_t n) {
    if (++allocations == fail_at) return NULL;
    return malloc(n);
}
static void *fault_calloc(size_t n, size_t s) {
    if (++allocations == fail_at) return NULL;
    return calloc(n, s);
}
#define malloc fault_malloc
#define calloc fault_calloc
#include "../checkpoint.c"
#undef malloc
#undef calloc

int main(void) {
    struct em8051 c = {0};
    struct stc12_state s = {0};
    struct dbg_target d = {0};
    struct dbg_task_pos tasks[8] = {{0}};
    int initialized = 1;
    c.mCodeMem = calloc(65536, 1); c.mExtData = calloc(65536, 1);
    c.mUpperData = calloc(128, 1); c.mCodeMemMaxIdx = c.mExtDataMaxIdx = 65535;
    assert(c.mCodeMem && c.mExtData && c.mUpperData);
    reset(&c, true); stc12_init(&c, &s); c.skip_timers = true; c.mMachineCycleScale = 1;
    dbg_init(&d, &c, &s); d.syms.tasks = tasks;
    s.pin_history = calloc(PIN_HISTORY_SIZE, sizeof(*s.pin_history));
    d.pc_histogram = calloc(65536, sizeof(*d.pc_histogram));
    assert(s.pin_history && d.pc_histogram);
    d.profiling = true; d.pc_histogram[5] = 9; d.profile_total = 9;
    c.mLowerData[0x30] = 0xa5; c.mTickDelay = 3; s.osc_clocks = 123;
    const uint32_t size = emu_checkpoint_codec_size();
    uint8_t *blob = malloc(size), *again = malloc(size);
    assert(blob && again);
    assert(emu_checkpoint_encode(&c, &s, &d, initialized, blob, size) == 0);
    // Three CPU buffers and both present optional buffers => five failure sites.
    for (int site = 1; site <= 5; site++) {
        struct em8051 before_c = c;
        struct stc12_state before_s = s;
        struct dbg_target before_d = d;
        struct dbg_task_pos before_tasks[8]; memcpy(before_tasks, tasks, sizeof(tasks));
        fail_at = site; allocations = 0;
        assert(emu_checkpoint_decode(&c, &s, &d, tasks, &initialized, blob, size) == -8);
        assert(allocations >= site && initialized == 1);
        assert(memcmp(&c, &before_c, sizeof(c)) == 0);
        assert(memcmp(&s, &before_s, sizeof(s)) == 0);
        assert(memcmp(&d, &before_d, sizeof(d)) == 0);
        assert(memcmp(tasks, before_tasks, sizeof(tasks)) == 0);
        assert(emu_checkpoint_encode(&c, &s, &d, initialized, again, size) == 0);
        assert(memcmp(blob, again, size) == 0);
        fail_at = 0; allocations = 0;
        assert(emu_checkpoint_decode(&c, &s, &d, tasks, &initialized, blob, size) == 0);
        assert(allocations == 5);
        assert(emu_checkpoint_encode(&c, &s, &d, initialized, again, size) == 0);
        assert(memcmp(blob, again, size) == 0);
        printf("ALLOCATION_REFUSAL site=%d status=-8 unchanged=1 retry=OK\n", site);
    }
    free(blob); free(again); free(c.mCodeMem); free(c.mExtData); free(c.mUpperData);
    free(s.pin_history); free(d.pc_histogram);
    puts("ALLOCATION_PROOF: 5/5");
    return 0;
}
