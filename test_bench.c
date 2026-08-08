/* test_bench.c — performance benchmark.
 * Measures how many simulated MHz the emulator sustains.
 *
 * Build: gcc -O2 -o test_bench test_bench.c core.c opcodes.c disasm.c stc12.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;

static void exc(struct em8051 *a, int c) { (void)a; (void)c; }

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    char *hexfile = argc > 1 ? argv[1] : NULL;
    int millions = argc > 2 ? atoi(argv[2]) : 100;

    memset(&cpu, 0, sizeof(cpu));
    cpu.mCodeMemMaxIdx = 65535;
    cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535;
    cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    cpu.except = exc;
    reset(&cpu, 1);
    stc12_init(&cpu, &stc);
    cpu.skip_timers = true;

    if (hexfile) {
        if (load_obj(&cpu, hexfile) != 0) {
            fprintf(stderr, "Failed to load %s\n", hexfile);
            return 1;
        }
        printf("Loaded %s\n", hexfile);
    } else {
        /* NOP loop */
        cpu.mCodeMem[0] = 0x00;
        printf("Running NOP loop (no firmware)\n");
    }

    long long total = (long long)millions * 1000000LL;
    printf("Running %lld osc clocks (%d M)...\n", total, millions);

    /* Warm up */
    for (int i = 0; i < 100000; i++) {
        tick(&cpu);
        stc12_tick(&cpu, &stc);
    }

    /* Benchmark */
    double t0 = now_sec();
    for (long long i = 0; i < total; i++) {
        tick(&cpu);
        stc12_tick(&cpu, &stc);
    }
    double elapsed = now_sec() - t0;

    double mhz = (total / 1e6) / elapsed;
    printf("\n%.1f M osc clocks in %.3f s = %.1f simulated MHz\n",
           total / 1e6, elapsed, mhz);
    printf("At FOSC=11.0592 MHz that's %.1fx real-time\n", mhz / 11.0592);

    /* Also benchmark advanceTo */
    stc.osc_clocks = 0;
    stc.ns_per_clock_x16 = (uint64_t)(16.0e9 / 11059200.0 + 0.5);
    uint64_t target_ns = 1000000000ULL; /* 1 second */

    t0 = now_sec();
    stc12_advance_to(&cpu, &stc, target_ns);
    elapsed = now_sec() - t0;

    printf("\nadvanceTo(1s) took %.3f s = %.1fx real-time\n",
           elapsed, 1.0 / elapsed);

    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
    return 0;
}
