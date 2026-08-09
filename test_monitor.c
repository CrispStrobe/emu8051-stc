/* test_monitor.c — run the on-chip debug monitor under the emulator.
 *
 * This is the first time 10-live-firmware has ever been executed.
 * We inject UART commands and check for well-formed responses.
 *
 * Build: gcc -O2 -o test_monitor test_monitor.c core.c opcodes.c disasm.c stc12.c debug.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state stc;
static int pass_count = 0, fail_count = 0;

/* TX capture via callback — unlimited, unlike the 18-byte serial_out ring */
static uint8_t tx_buf[256];
static int tx_idx = 0;

static void tx_callback(uint8_t byte, void *ud) {
    (void)ud;
    if (tx_idx < 256) tx_buf[tx_idx++] = byte;
}

#define CHECK(c, m) do { if (c) { printf("PASS: %s\n", m); pass_count++; } \
                         else   { printf("FAIL: %s\n", m); fail_count++; } } while(0)

static void exc(struct em8051 *a, int c) { (void)a; (void)c; }

/* Run N oscillator clocks */
static void run_clocks(int n) {
    for (int i = 0; i < n; i++) {
        tick(&cpu);
        stc12_tick(&cpu, &stc);
    }
}

/* Run for approximately N milliseconds at 11.0592 MHz */
static void run_ms(int ms) {
    run_clocks((int)((double)ms * 11059200.0 / 1000.0));
}

/* Protocol constants */
#define SOF 0x7E
#define CMD_HELLO 0x01
#define CMD_POS   0x0A
#define CMD_RUN   0x05
#define CMD_HALT  0x06
#define CMD_READ  0x02

/* Build a frame: SOF LEN CMD payload[LEN] SUM */
static int build_frame(uint8_t *buf, uint8_t cmd, const uint8_t *payload, uint8_t len) {
    buf[0] = SOF;
    buf[1] = len;
    buf[2] = cmd;
    uint8_t sum = len + cmd;
    for (int i = 0; i < len; i++) {
        buf[3 + i] = payload[i];
        sum += payload[i];
    }
    buf[3 + len] = (uint8_t)(0 - sum);
    return 4 + len;
}

int main(void) {
    printf("=== Monitor firmware test (10-live-firmware) ===\n\n");

    /* Setup */
    memset(&cpu, 0, sizeof(cpu));
    memset(&stc, 0, sizeof(stc));
    cpu.mCodeMemMaxIdx = 65535;
    cpu.mCodeMem = calloc(65536, 1);
    cpu.mExtDataMaxIdx = 65535;
    cpu.mExtData = calloc(65536, 1);
    cpu.mUpperData = calloc(128, 1);
    cpu.except = exc;
    reset(&cpu, 1);
    stc12_init(&cpu, &stc);
    cpu.skip_timers = true;

    /* Set up TX callback for reliable capture */
    stc12_set_serial_callback(&stc, tx_callback, NULL);

    /* Load the monitor firmware */
    int rc = load_obj(&cpu, "/tmp/monitor.ihx");
    CHECK(rc == 0, "Load monitor firmware");
    if (rc != 0) {
        printf("Cannot load /tmp/monitor.ihx — build it first\n");
        return 1;
    }

    /* Run init phase (~5ms should be enough for timers + UART init) */
    run_ms(5);

    /* Check that AUXR was configured (BRTR + BRTx12 + S1BRS = 0x15) */
    CHECK((cpu.mSFR[STC_REG_AUXR] & 0x15) == 0x15,
          "Init: AUXR = 0x15 (BRT running, 1T, UART1 source)");

    /* Check Timer 0 and Timer 1 are running */
    CHECK(cpu.mSFR[REG_TCON] & 0x10, "Init: TR0 set (program time)");
    CHECK(cpu.mSFR[REG_TCON] & 0x40, "Init: TR1 set (wall time)");

    /* Check interrupts enabled */
    CHECK(cpu.mSFR[REG_IE] & 0x80, "Init: EA enabled");
    CHECK(cpu.mSFR[REG_IE] & 0x02, "Init: ET0 enabled");
    CHECK(cpu.mSFR[REG_IE] & 0x08, "Init: ET1 enabled");

    /* Check SCON = 0x50 (mode 1, REN) */
    CHECK(cpu.mSFR[REG_SCON] == 0x50 || (cpu.mSFR[REG_SCON] & 0x50) == 0x50,
          "Init: SCON mode 1 + REN");

    /* Check LEDs went on (P1.0 and P1.1 should be active) */
    printf("  P1 = 0x%02X (expect bits 0,1 active)\n", cpu.mSFR[REG_P1]);

    /* Run longer to let bw_ms advance */
    run_ms(20);

    /* === Send HELLO command === */
    printf("\n--- Sending HELLO command ---\n");

    uint8_t frame[8];
    int flen = build_frame(frame, CMD_HELLO, NULL, 0);

    /* Inject each byte into the UART RX, with some clocks between */
    for (int i = 0; i < flen; i++) {
        stc12_serial_rx(&cpu, &stc, frame[i]);
        run_clocks(1000); /* let the firmware process the byte */
    }

    /* Run a bit more to let the firmware process and respond */
    run_ms(5);

    /* Check TX output — the monitor should have sent a HELLO reply */
    printf("  TX output: %d bytes\n", tx_idx);
    if (tx_idx > 0) {
        printf("  First bytes:");
        for (int i = 0; i < tx_idx && i < 20; i++)
            printf(" %02X", tx_buf[i]);
        printf("\n");

        CHECK(tx_buf[0] == SOF, "HELLO reply: starts with SOF (0x7E)");

        if (tx_idx >= 4 && tx_buf[0] == SOF) {
            uint8_t rlen = tx_buf[1];
            uint8_t rcmd = tx_buf[2];
            printf("  Reply: LEN=%d CMD=0x%02X\n", rlen, rcmd);

            CHECK(rcmd == (CMD_HELLO | 0x80),
                  "HELLO reply: CMD = 0x81 (HELLO | reply bit)");

            uint8_t sum = 0;
            for (int i = 1; i < 3 + rlen + 1 && i < tx_idx; i++)
                sum += tx_buf[i];
            CHECK(sum == 0, "HELLO reply: checksum valid");

            if (rlen >= 9) {
                uint8_t *cap = &tx_buf[3];
                printf("  Protocol version: %d\n", cap[0]);
                printf("  Max payload: %d\n", cap[1]);
                printf("  Step kinds: 0x%02X\n", cap[2]);
                printf("  BP kinds: 0x%02X\n", cap[3]);
                printf("  Readable spaces: 0x%02X\n", cap[4]);
                printf("  Writable spaces: 0x%02X\n", cap[5]);
                printf("  Flags: 0x%02X\n", cap[6]);
                printf("  Max tasks: %d\n", cap[7]);
                printf("  Resources: 0x%02X\n", cap[8]);

                CHECK(cap[0] == 1, "Capabilities: protocol v1");
                CHECK(cap[6] & 0x01, "Capabilities: timeFreezes = true");
                CHECK(!(cap[6] & 0x02), "Capabilities: PC not valid (yield target)");
                CHECK(cap[8] & 0x0B, "Capabilities: consumes T0+T1+BRT");
            }
        }
    } else {
        CHECK(0, "HELLO reply: no TX output received");
    }

    /* === Send POS command (Level 1 position) === */
    printf("\n--- Sending POS command ---\n");
    tx_idx = 0; /* clear TX buffer */

    flen = build_frame(frame, CMD_POS, NULL, 0);
    for (int i = 0; i < flen; i++) {
        stc12_serial_rx(&cpu, &stc, frame[i]);
        run_clocks(1000);
    }
    run_ms(10);

    printf("  TX output: %d bytes\n", tx_idx);
    if (tx_idx >= 4 && tx_buf[0] == SOF) {
        uint8_t rcmd = tx_buf[2];
        uint8_t rlen = tx_buf[1];
        CHECK(rcmd == (CMD_POS | 0x80), "POS reply: CMD = 0x8A");

        printf("  Position blob (%d bytes):", rlen);
        for (int i = 0; i < rlen && i < 20; i++)
            printf(" %02X", tx_buf[3 + i]);
        printf("\n");

        /* Verify checksum */
        uint8_t sum = 0;
        for (int i = 1; i < 3 + rlen + 1 && i < tx_idx; i++)
            sum += tx_buf[i];
        CHECK(sum == 0, "POS reply: checksum valid");
    } else if (tx_idx > 0) {
        printf("  Raw bytes:");
        for (int i = 0; i < tx_idx && i < 20; i++)
            printf(" %02X", tx_buf[i]);
        printf("\n");
        CHECK(0, "POS reply: response not well-formed");
    } else {
        CHECK(0, "POS reply: no TX output");
    }

    printf("\n=== Results: %d passed, %d failed ===\n", pass_count, fail_count);

    free(cpu.mCodeMem);
    free(cpu.mExtData);
    free(cpu.mUpperData);
    return fail_count > 0 ? 1 : 0;
}
