/* emu_serial_bridge.c — stdin/stdout bridge for the monitor firmware.
 *
 * Reads bytes from stdin, injects them via stc12_serial_rx.
 * Writes TX bytes (from callback) to stdout.
 * Runs the emulator between bytes, one ms at a time.
 *
 * Usage: echo -ne '\x7e\x00\x01\xff' | ./emu_serial_bridge /tmp/monitor.ihx
 *
 * Build: gcc -O2 -o emu_serial_bridge emu_serial_bridge.c \
 *        core.c opcodes.c disasm.c stc12.c debug.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "emu8051.h"
#include "stc12.h"

#define SOF 0x7E

static struct em8051 cpu;
static struct stc12_state stc;
static void exc(struct em8051 *a, int c) { (void)a; (void)c; }

static void tx_cb(uint8_t byte, void *ud) {
    (void)ud;
    uint8_t b = byte;
    write(STDOUT_FILENO, &b, 1);
}

static void run_clocks(int n) {
    for (int i = 0; i < n; i++) { tick(&cpu); stc12_tick(&cpu, &stc); }
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s firmware.ihx\n", argv[0]); return 1; }

    memset(&cpu, 0, sizeof(cpu)); memset(&stc, 0, sizeof(stc));
    cpu.mCodeMemMaxIdx = 65535; cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535; cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1); cpu.except = exc;
    reset(&cpu, 1); stc12_init(&cpu, &stc); cpu.skip_timers = 1;
    stc12_set_serial_callback(&stc, tx_cb, NULL);

    if (load_obj(&cpu, argv[1]) != 0) {
        fprintf(stderr, "cannot load %s\n", argv[1]); return 1;
    }

    /* Init phase */
    run_clocks(11059200 / 1000 * 5); /* 5ms */

    /* Set stdin non-blocking */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    /* Read all stdin */
    uint8_t inbuf[1024];
    fcntl(STDIN_FILENO, F_SETFL, flags & ~O_NONBLOCK);
    int total_in = read(STDIN_FILENO, inbuf, sizeof(inbuf));
    if (total_in < 0) total_in = 0;

    /* Parse frames from input and inject one frame at a time with
     * processing time between them */
    int pos = 0;
    while (pos < total_in) {
        /* Find SOF */
        if (inbuf[pos] != SOF) { pos++; continue; }
        if (pos + 1 >= total_in) break;
        int flen = inbuf[pos + 1]; /* payload length */
        int frame_size = 4 + flen; /* SOF + LEN + CMD + payload + SUM */
        if (pos + frame_size > total_in) break;

        /* Inject this frame's bytes */
        for (int i = 0; i < frame_size; i++) {
            stc12_serial_rx(&cpu, &stc, inbuf[pos + i]);
            run_clocks(500);
        }
        pos += frame_size;

        /* Run 10ms for firmware to process and respond */
        run_clocks(11059200 / 1000 * 10);
    }

    /* Final 10ms flush */
    run_clocks(11059200 / 1000 * 10);

    free(cpu.mCodeMem); free(cpu.mExtData); free(cpu.mUpperData);
    return 0;
}
