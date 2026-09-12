#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "checkpoint.h"

static void pin_a(int p, int b, enum stc12_pin_mode m, bool h, void *u)
{ (void)p; (void)b; (void)m; (void)h; (void)u; }
static void pin_b(int p, int b, enum stc12_pin_mode m, bool h, void *u)
{ (void)p; (void)b; (void)m; (void)h; (void)u; }

int main(void)
{
    struct em8051 cpu = {0};
    struct stc12_state stc = {0};
    struct dbg_target dbg = {0};
    struct dbg_task_pos tasks[8] = {{0}};
    int initialized = 1;

    cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    assert(cpu.mCodeMem && cpu.mExtData && cpu.mUpperData);
    cpu.mCodeMemMaxIdx = cpu.mExtDataMaxIdx = 65535;
    reset(&cpu, true);
    cpu.mMachineCycleScale = 1;
    cpu.skip_timers = true;
    stc.part_id = PART_STC15;
    stc.on_pin_change = pin_a;
    stc12_init(&cpu, &stc);
    dbg_init(&dbg, &cpu, &stc);
    dbg.syms.tasks = tasks;

    cpu.mCodeMem[0x1234] = 0x42;
    cpu.mExtData[0x2345] = 0x99;
    cpu.mLowerData[7] = 0x11;
    stc.osc_clocks = 987654321;
    stc.pin_history = calloc(PIN_HISTORY_SIZE, sizeof(*stc.pin_history));
    assert(stc.pin_history);
    stc.pin_history_head = stc.pin_history_count = 1;
    stc.pin_history[0] = (struct stc12_pin_event){123, 5, 7, PIN_OPENDRAIN, 1};

    uint32_t size = emu_checkpoint_codec_size();
    uint8_t *blob = malloc(size);
    assert(blob && size > 393216);
    assert(emu_checkpoint_encode(&cpu, &stc, &dbg, initialized, blob, size) == 0);

    stc.on_pin_change = pin_b; /* external binding must not rewind */
    stc.part_id = PART_STC89;
    stc12_rebind_callbacks(&cpu, &stc);
    assert(cpu.sfrread[STC_REG_P5] == NULL);
    cpu.mCodeMem[0x1234] = cpu.mExtData[0x2345] = cpu.mLowerData[7] = 0;
    stc.osc_clocks = 0;
    assert(emu_checkpoint_decode(&cpu, &stc, &dbg, tasks, &initialized,
                                 blob, size) == 0);
    assert(stc.part_id == PART_STC15 && stc.on_pin_change == pin_b);
    assert(cpu.sfrread[STC_REG_P5] != NULL); /* topology follows decoded part */
    assert(cpu.mCodeMem[0x1234] == 0x42 && cpu.mExtData[0x2345] == 0x99);
    assert(cpu.mLowerData[7] == 0x11 && stc.osc_clocks == 987654321);
    assert(stc.pin_history_count == 1 && stc.pin_history[0].port == 5);

    uint8_t *again = malloc(size);
    assert(again);
    assert(emu_checkpoint_encode(&cpu, &stc, &dbg, initialized, again, size) == 0);
    assert(memcmp(blob, again, size) == 0);
    assert(emu_checkpoint_decode(&cpu, &stc, &dbg, tasks, &initialized,
                                 again, size) == 0); /* restore twice */

    uint8_t before = cpu.mCodeMem[0x1234];
    blob[size - 1] ^= 1;
    assert(emu_checkpoint_decode(&cpu, &stc, &dbg, tasks, &initialized,
                                 blob, size) == EMU_CHECKPOINT_MALFORMED);
    assert(cpu.mCodeMem[0x1234] == before);
    assert(emu_checkpoint_decode(&cpu, &stc, &dbg, tasks, &initialized,
                                 blob, size - 1) == EMU_CHECKPOINT_WRONG_LENGTH);
    memcpy(blob, again, size);
    blob[4] = 2;
    assert(emu_checkpoint_decode(&cpu, &stc, &dbg, tasks, &initialized,
                                 blob, size) == EMU_CHECKPOINT_UNSUPPORTED_VERSION);
    memcpy(blob, again, size);
    blob[8] ^= 1;
    assert(emu_checkpoint_decode(&cpu, &stc, &dbg, tasks, &initialized,
                                 blob, size) == EMU_CHECKPOINT_INCOMPATIBLE_BUILD);

    free(again); free(blob);
    free(cpu.mCodeMem); free(cpu.mExtData); free(cpu.mUpperData);
    free(stc.pin_history); free(dbg.pc_histogram);
    return 0;
}
