/* A hardware/debug reset restarts the chip, not its board configuration. */
#include <stdio.h>
#include <string.h>
#include "debug.h"

static struct em8051 cpu;
static struct stc12_state st;
static struct dbg_target dbg;
static unsigned char code[65536], xram[65536], upper[128];
static int passed, failed, pin_calls, analog_calls, serial_calls, serial2_calls;
static int user_token, wrong_user;
#define CHECK(c, message) do { \
    if (c) passed++; else { fprintf(stderr, "FAIL: %s\n", message); failed++; } \
} while (0)

static void pin(int p, int b, enum stc12_pin_mode m, bool high, void *user)
{ (void)p; (void)b; (void)m; (void)high; pin_calls++; wrong_user += user != &user_token; }
static int read_pin(int p, int b, void *user)
{ (void)p; (void)b; wrong_user += user != &user_token; return 1; }
static double analog(int p, int b, void *user)
{ (void)p; (void)b; analog_calls++; wrong_user += user != &user_token; return 1.65; }
static void advance(uint32_t lo, uint32_t hi, void *user)
{ (void)lo; (void)hi; wrong_user += user != &user_token; }
static void serial(uint8_t byte, void *user)
{ serial_calls += byte == 0x41; wrong_user += user != &user_token; }
static void serial2(uint8_t byte, void *user)
{ serial2_calls += byte == 0x42; wrong_user += user != &user_token; }

static void write_sfr(uint8_t addr, uint8_t value)
{
    cpu.mSFR[addr - 0x80] = value;
    if (cpu.sfrwrite[addr - 0x80]) cpu.sfrwrite[addr - 0x80](&cpu, addr);
}

static void test_part(uint8_t part)
{
    memset(&cpu, 0, sizeof cpu);
    memset(&st, 0, sizeof st);
    memset(&dbg, 0, sizeof dbg);
    cpu.mCodeMem = code;
    cpu.mExtData = xram;
    cpu.mUpperData = upper;
    cpu.mCodeMemMaxIdx = cpu.mExtDataMaxIdx = 65535;
    reset(&cpu, true);
    stc12_init(&cpu, &st);
    stc12_configure_part(&cpu, &st, part);
    dbg_init(&dbg, &cpu, &st);
    stc12_set_fosc(&st, 12000000);
    stc12_set_vcc(&st, 3.3);
    stc12_set_board_callbacks(&st, pin, read_pin, analog, advance, &user_token);
    st.on_serial_tx = serial;
    st.on_serial2_tx = serial2;

    for (int cycle = 0; cycle < 4; ++cycle) {
        st.osc_clocks = 999;
        st.timer0_reload = 0xfffc;
        st.rx_len = 2;
        cpu.mSFR[REG_TCON] = TCONMASK_TR0;
        pin_calls = analog_calls = serial_calls = serial2_calls = wrong_user = 0;
        if (cycle & 1) dbg_reset(&dbg);
        else { reset(&cpu, cycle == 2); stc12_reset(&cpu, &st); }

        CHECK(st.fosc == 12000000 && st.vcc == 3.3, "reset preserves clock and supply");
        CHECK(st.part_id == part && cpu.mMachineCycleScale == (part == PART_STC89 ? 12 : 1),
              "reset preserves part and instruction scale");
        CHECK(st.osc_clocks == 0 && st.timer0_reload == 0 && st.rx_len == 0 &&
              cpu.mSFR[REG_TCON] == 0, "reset clears volatile timer/time/UART state");
        CHECK(st.on_pin_change == pin && st.on_read_pin == read_pin &&
              st.on_read_analog == analog && st.on_advance == advance &&
              st.board_user_data == &user_token, "board connections survive reset");
        CHECK(st.on_serial_tx == serial && st.on_serial2_tx == serial2,
              "serial connections survive reset");
        CHECK(!pin_calls && !analog_calls && !serial_calls && !serial2_calls,
              "reset emits no artificial board or serial event");
        for (int tick = 0; tick < 12; ++tick) stc12_tick(&cpu, &st);
        uint64_t ns = stc12_get_time_ns(&st);
        CHECK(ns >= 999 && ns <= 1000, "twelve oscillator clocks at 12MHz take one microsecond");

        write_sfr(0x90, 0xfe);
        write_sfr(0x99, 0x41);
        write_sfr(0x9b, 0x42);
        CHECK(pin_calls > 0 && serial_calls == 1, "pin and UART1 callbacks still deliver after reset");
        CHECK(serial2_calls == (part == PART_STC89 || part == PART_STC15W ? 0 : 1),
              "UART2 callback follows part availability after reset");
        if (part != PART_STC89) {
            write_sfr(0x9d, 1);
            write_sfr(0xbc, 0xe8);
            for (int tick = 0; tick < 600; ++tick) stc12_tick(&cpu, &st);
            CHECK(analog_calls == 1 && cpu.mSFR[STC_REG_ADC_RES] == 0x80 &&
                  cpu.mSFR[STC_REG_ADC_RESL] == 0,
                  "ADC still uses the 3.3V supply and live analog callback");
        }
        CHECK(wrong_user == 0, "callback user data survives reset");
    }

    stc12_init(&cpu, &st);
    CHECK(st.fosc == 11059200 && st.vcc == 5.0, "raw init restores default board configuration");
    CHECK(st.on_serial_tx == NULL && st.on_serial2_tx == NULL,
          "raw init clears serial bindings for a newly created target");
}

int main(void)
{
    test_part(PART_STC12);
    test_part(PART_STC15);
    test_part(PART_STC15W);
    test_part(PART_STC89);
    printf("%d passed, %d failed (reset configuration)\n", passed, failed);
    return failed ? 1 : 0;
}
