/* test_uart_rx_fifo.c — UART1 receive: bytes that arrive faster than the
 * firmware reads them queue behind SBUF instead of overwriting it.
 *
 * Before 2026-10-05 stc12_serial_rx wrote SBUF and set RI unconditionally,
 * so a host sending "42\r" in one go left the firmware the last byte only.
 * Each byte now waits until the firmware has cleared RI AND one character
 * time (10 bits at 9600 baud) has passed since the previous one -- otherwise
 * the usual `RI = 0; c = SBUF;` reads the byte after the one it was told of.
 *
 * Build: gcc -O2 -o test_uart_rx_fifo test_uart_rx_fifo.c core.c opcodes.c disasm.c stc12.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static int fails;
#define CHECK(c, ...) do { if (c) { printf("PASS: "); } else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

static void noexc(struct em8051 *a, int c) { (void)a; (void)c; }

static void setup(void) {
    memset(&cpu, 0, sizeof(cpu));
    cpu.mCodeMemMaxIdx = 65535; cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535; cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    cpu.except = noexc;
    cpu.mCodeMem[0] = 0x80; cpu.mCodeMem[1] = 0xFE;   /* SJMP $ */
    reset(&cpu, 1);
    stc12_init(&cpu, &stc);
    stc12_set_fosc(&stc, 11059200);
}

static void run_clocks(long n) {
    for (long i = 0; i < n; i++) { tick(&cpu); stc12_tick(&cpu, &stc); }
}

/* What the firmware does on RI: take SBUF, clear RI. */
static int take(void) {
    if (!(cpu.mSFR[REG_SCON] & SCONMASK_RI)) return -1;
    int c = cpu.mSFR[REG_SBUF];
    cpu.mSFR[REG_SCON] &= ~SCONMASK_RI;
    return c;
}

int main(void) {
    setup();
    const char *msg = "42\r";
    for (const char *p = msg; *p; p++) stc12_serial_rx(&cpu, &stc, (uint8_t)*p);
    CHECK(stc12_serial_rx_pending(&stc) == 2, "two bytes queue behind the first (pending %d)", stc12_serial_rx_pending(&stc));

    char got[8] = {0}; int n = 0;
    got[n++] = (char)take();
    run_clocks(10);                       /* RI clear, but not a character time yet */
    CHECK(take() == -1, "the next byte does not land the instant RI is cleared");
    for (int guard = 0; guard < 100 && n < 3; guard++) {
        run_clocks(2000);
        int c = take();
        if (c >= 0) got[n++] = (char)c;
    }
    CHECK(strcmp(got, msg) == 0, "the firmware reads \"42\\r\" in order (read \"%s\")", got);
    CHECK(stc12_serial_rx_pending(&stc) == 0, "nothing left queued");

    /* A byte to an idle receiver still lands at once (the old behaviour). */
    setup();
    stc12_serial_rx(&cpu, &stc, 'A');
    CHECK(take() == 'A', "an idle receiver gets its byte immediately");

    /* An unread SBUF holds the queue: nothing overwrites it. */
    setup();
    stc12_serial_rx(&cpu, &stc, 'x');
    stc12_serial_rx(&cpu, &stc, 'y');
    run_clocks(50000);
    CHECK(cpu.mSFR[REG_SBUF] == 'x', "while RI stays set, SBUF keeps its byte");

    printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "ALL PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
