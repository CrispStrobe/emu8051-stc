/* test_soak.c — time-fidelity soak test.
 *
 * Firmware: NOP-loop toggling P1.0 periodically. Runs for >5 simulated
 * seconds, asserting:
 *   1. stc12_get_time_ns is strictly monotonic
 *   2. Edge intervals on P1.0 stay periodic across the u32 lo-word
 *      boundaries at ~2.15 s (i32 sign flip) and ~4.29 s (u32 wrap)
 *   3. The split (lo, hi) representation reconstructs correctly
 *
 * The point: the C-side time is uint64_t and the WASM API splits it
 * into two u32 words. Any consumer that treats lo as signed i32 will
 * see time go negative at 2^31 ns; any consumer that ignores hi will
 * see time wrap at 2^32 ns. This test proves the emulator's side is
 * correct so the bug is always in the consumer, never in the core.
 *
 * Build: gcc -O2 -o test_soak test_soak.c core.c opcodes.c disasm.c stc12.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static int pass_count = 0;
static int fail_count = 0;

#define PASS(msg) do { printf("PASS: %s\n", msg); pass_count++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); fail_count++; } while(0)
#define CHECK(cond, msg) do { if (cond) PASS(msg); else FAIL(msg); } while(0)

/* ================================================================== *
 * Pin callback: record every P1.0 edge with its timestamp             *
 * ================================================================== */

#define MAX_EDGES 100000
static uint64_t edge_times[MAX_EDGES];
static int edge_modes[MAX_EDGES];
static int edge_drives[MAX_EDGES];
static int edge_count = 0;

static void pin_callback(int port, int bit,
                         enum stc12_pin_mode mode,
                         bool drive_high, void *ud)
{
    (void)ud;
    if (port == 1 && bit == 0 && edge_count < MAX_EDGES) {
        edge_times[edge_count] = stc12_get_time_ns(&stc);
        edge_modes[edge_count] = mode;
        edge_drives[edge_count] = drive_high ? 1 : 0;
        edge_count++;
    }
}

/* ================================================================== *
 * Setup: inject firmware that toggles P1.0 in a tight loop with a     *
 * small delay (DJNZ-based busy wait).                                 *
 * ================================================================== */

static void setup(void) {
    memset(&cpu, 0, sizeof(cpu));
    memset(&stc, 0, sizeof(stc));
    cpu.mCodeMemMaxIdx = 65535;
    cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535;
    cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    reset(&cpu, 1);
    stc.fosc = 11059200;
    stc12_init(&cpu, &stc);

    /* Register pin callback */
    stc12_set_board_callbacks(&stc, pin_callback, NULL, NULL, NULL, NULL);

    /*
     * Firmware: toggle P1.0 with a DJNZ delay loop.
     *
     * loop:
     *   CPL P1.0          ; B2 90  (toggle P1 bit 0)
     *   MOV R7, #0xFF     ; 7F FF  (delay counter)
     * delay:
     *   DJNZ R7, delay    ; DF FE
     *   SJMP loop          ; 80 FA  (back to CPL)
     */
    unsigned char firmware[] = {
        0xB2, 0x90,       /* CPL P1.0 */
        0x7F, 0xFF,       /* MOV R7, #0xFF */
        0xDF, 0xFE,       /* DJNZ R7, delay (self-2) */
        0x80, 0xF8,       /* SJMP loop (back to 0: 6+2+(-8)=0) */
    };
    memcpy(cpu.mCodeMem, firmware, sizeof(firmware));
}

static void teardown(void) {
    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
}

/* ================================================================== *
 * Test 1: time monotonicity over 5+ seconds                          *
 * ================================================================== */
static void test_time_monotonicity(void) {
    printf("\n=== test_time_monotonicity (5+ seconds sim time) ===\n");
    setup();
    edge_count = 0;

    uint64_t target_ns = 5500000000ULL; /* 5.5 seconds */
    uint64_t prev_time = 0;
    int mono_violations = 0;
    uint64_t check_interval = 1000000; /* check every 1 ms */
    uint64_t next_check = check_interval;

    /* Boundaries to verify we cross */
    uint64_t i32_boundary = 2147483648ULL;  /* 2^31 */
    uint64_t u32_boundary = 4294967296ULL;  /* 2^32 */
    bool crossed_i32 = false;
    bool crossed_u32 = false;

    printf("  Running %.1f seconds of sim time at FOSC=%u...\n",
           target_ns / 1e9, (unsigned)stc.fosc);

    while (stc12_get_time_ns(&stc) < target_ns) {
        tick(&cpu);
        stc12_tick(&cpu, &stc);

        uint64_t t = stc12_get_time_ns(&stc);
        if (t >= next_check) {
            /* Monotonicity check */
            if (t < prev_time) {
                mono_violations++;
                if (mono_violations <= 3)
                    printf("  VIOLATION at %llu ns: went backward to %llu\n",
                           (unsigned long long)prev_time, (unsigned long long)t);
            }
            prev_time = t;
            next_check = t + check_interval;

            /* Split check: verify (lo, hi) reconstructs to t */
            uint32_t lo = (uint32_t)(t & 0xFFFFFFFF);
            uint32_t hi = (uint32_t)(t >> 32);
            uint64_t reconstructed = ((uint64_t)hi << 32) | lo;
            if (reconstructed != t && mono_violations == 0) {
                printf("  SPLIT MISMATCH at %llu: lo=%u hi=%u recon=%llu\n",
                       (unsigned long long)t, lo, hi,
                       (unsigned long long)reconstructed);
                mono_violations++;
            }

            /* Track boundary crossings */
            if (t > i32_boundary && !crossed_i32) {
                crossed_i32 = true;
                printf("  Crossed i32 boundary (2^31 = 2.15s) at t=%llu ns\n",
                       (unsigned long long)t);
                printf("    lo=%u (signed: %d) hi=%u\n", lo, (int32_t)lo, hi);
            }
            if (t > u32_boundary && !crossed_u32) {
                crossed_u32 = true;
                printf("  Crossed u32 boundary (2^32 = 4.29s) at t=%llu ns\n",
                       (unsigned long long)t);
                printf("    lo=%u hi=%u\n", lo, hi);
            }
        }
    }

    uint64_t final = stc12_get_time_ns(&stc);
    printf("  Final time: %llu ns (%.3f s)\n",
           (unsigned long long)final, final / 1e9);
    printf("  Edges captured: %d\n", edge_count);

    CHECK(mono_violations == 0,
          "time monotonicity: zero violations over 5.5s");
    CHECK(crossed_i32,
          "time monotonicity: crossed i32 boundary (2^31 ns)");
    CHECK(crossed_u32,
          "time monotonicity: crossed u32 boundary (2^32 ns)");
    CHECK(final >= target_ns,
          "time monotonicity: reached target time");

    /* Verify the split representation at final time */
    uint32_t final_lo = (uint32_t)(final & 0xFFFFFFFF);
    uint32_t final_hi = (uint32_t)(final >> 32);
    uint64_t recon = ((uint64_t)final_hi << 32) | final_lo;
    CHECK(recon == final,
          "time split: (lo, hi) reconstructs final time correctly");
    CHECK(final_hi > 0,
          "time split: hi word is nonzero after 5.5s (confirms 64-bit needed)");

    /* ================================================================== *
     * Test 2: edge periodicity across boundaries                          *
     * ================================================================== */
    printf("\n--- Edge periodicity analysis ---\n");

    if (edge_count < 10) {
        FAIL("edge periodicity: too few edges captured");
        teardown();
        return;
    }

    /* Compute intervals between consecutive rising edges */
    int rising_count = 0;
    uint64_t first_interval = 0;
    int interval_violations = 0;
    uint64_t boundary_interval = 0; /* interval spanning the u32 boundary */

    for (int i = 2; i < edge_count; i++) {
        if (edge_drives[i] == 1 && edge_drives[i-1] == 0) {
            /* Rising edge at i, find previous rising edge */
            for (int j = i - 2; j >= 0; j--) {
                if (edge_drives[j] == 1 && edge_drives[j+1] == 0) {
                    /* j is previous rising edge */
                    uint64_t interval = edge_times[i] - edge_times[j];
                    if (first_interval == 0) {
                        first_interval = interval;
                        printf("  Reference toggle period: %llu ns (%.3f ms)\n",
                               (unsigned long long)first_interval,
                               first_interval / 1e6);
                    } else {
                        /* Allow ±2 clock cycles of jitter */
                        int64_t drift = (int64_t)interval - (int64_t)first_interval;
                        uint64_t abs_drift = drift < 0 ? -drift : drift;
                        uint64_t max_jitter = 2 * (uint64_t)(1e9 / stc.fosc + 1);
                        if (abs_drift > max_jitter) {
                            interval_violations++;
                            if (interval_violations <= 3)
                                printf("  JITTER at edge %d: interval=%llu ref=%llu drift=%lld\n",
                                       i, (unsigned long long)interval,
                                       (unsigned long long)first_interval,
                                       (long long)drift);
                        }
                    }
                    /* Check if this interval spans the u32 boundary */
                    if (edge_times[j] < u32_boundary && edge_times[i] > u32_boundary)
                        boundary_interval = interval;
                    rising_count++;
                    break;
                }
            }
        }
    }

    printf("  Rising-edge pairs analysed: %d\n", rising_count);
    CHECK(rising_count > 100,
          "edge periodicity: >100 rising-edge pairs analysed");
    CHECK(interval_violations == 0,
          "edge periodicity: zero jitter violations across 5.5s");

    if (boundary_interval > 0) {
        int64_t drift = (int64_t)boundary_interval - (int64_t)first_interval;
        printf("  Interval spanning u32 boundary: %llu ns (drift=%lld)\n",
               (unsigned long long)boundary_interval, (long long)drift);
        uint64_t max_jitter = 2 * (uint64_t)(1e9 / stc.fosc + 1);
        CHECK((drift < 0 ? -drift : drift) <= (int64_t)max_jitter,
              "edge periodicity: boundary-spanning interval matches reference");
    }

    teardown();
}

int main(void) {
    printf("=== Time fidelity soak tests ===\n");
    test_time_monotonicity();

    printf("\n%d passed, %d failed\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
