/* STC15-family regressions driven by the manufacturer's mode/pin tables.
 * This does not use a HAL implementation as the expected-value oracle.
 * See docs/bw8051-1-qualification.md for sources and remaining limits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu8051.h"
#include "stc12.h"

static struct em8051 cpu;
static struct stc12_state st;
static unsigned char code[65536], xram[65536], upper[128];
static int passed, failed;
#define CHECK(c, message) do { \
    if (c) passed++; else { fprintf(stderr, "FAIL: %s\n", message); failed++; } \
} while (0)

static void setup(uint8_t part)
{
    memset(&cpu, 0, sizeof cpu);
    memset(&st, 0, sizeof st);
    cpu.mCodeMem = code;
    cpu.mExtData = xram;
    cpu.mUpperData = upper;
    cpu.mCodeMemMaxIdx = cpu.mExtDataMaxIdx = 65535;
    reset(&cpu, true);
    st.part_id = part;
    stc12_init(&cpu, &st);
    cpu.skip_timers = true;
    cpu.mMachineCycleScale = part == PART_STC89 ? 12 : 1;
}

static void write_sfr(uint8_t addr, uint8_t value)
{
    cpu.mSFR[addr - 0x80] = value;
    if (cpu.sfrwrite[addr - 0x80])
        cpu.sfrwrite[addr - 0x80](&cpu, addr);
}

static void clocks(unsigned count)
{
    while (count--) stc12_tick(&cpu, &st);
}

static uint16_t timer0(void)
{
    return ((uint16_t)cpu.mSFR[REG_TH0] << 8) | cpu.mSFR[REG_TL0];
}

static void start_mode0(uint8_t part, uint16_t preload, int one_t)
{
    setup(part);
    write_sfr(0x89, 0); /* TMOD: mode0, timer, ungated */
    write_sfr(0x8e, one_t ? 0x80 : 0);
    write_sfr(0x8a, preload);
    write_sfr(0x8c, preload >> 8);
    write_sfr(0x88, 0x10); /* TR0 */
}

static void test_timer0(uint8_t part)
{
    start_mode0(part, 0, 1);
    clocks(32);
    CHECK(timer0() == 32, "STC15 mode0 carries TL0 at 256, not 32");

    start_mode0(part, 0xfffc, 1);
    clocks(3);
    CHECK(timer0() == 0xffff, "mode0 approaches first overflow");
    CHECK(!(cpu.mSFR[REG_TCON] & TCONMASK_TF0), "no early overflow");
    clocks(1);
    CHECK(timer0() == 0xfffc, "mode0 reloads its hidden 16-bit latch");
    CHECK(cpu.mSFR[REG_TCON] & TCONMASK_TF0, "overflow sets TF0");
    write_sfr(0x88, 0x10);
    clocks(3);
    CHECK(!(cpu.mSFR[REG_TCON] & TCONMASK_TF0), "second period is not shortened");
    clocks(1);
    CHECK(timer0() == 0xfffc && (cpu.mSFR[REG_TCON] & TCONMASK_TF0),
          "the reload survives repeated periods");

    clocks(1); /* counter FFFD */
    write_sfr(0x8a, 0xf8);
    write_sfr(0x8c, 0xfe);
    CHECK(timer0() == 0xfffd, "running writes leave the readable counter alone");
    clocks(3);
    CHECK(timer0() == 0xfef8, "new reload takes effect at the next overflow");
    write_sfr(0x88, 0x10);
    clocks(263);
    CHECK(timer0() == 0xffff && !(cpu.mSFR[REG_TCON] & TCONMASK_TF0),
          "changed reload produces the independently calculated 264-clock period");
    clocks(1);
    CHECK(timer0() == 0xfef8, "changed reload repeats");

    write_sfr(0x88, 0);
    write_sfr(0x8a, 0x55);
    write_sfr(0x8c, 0xaa);
    clocks(32);
    CHECK(timer0() == 0xaa55 && st.timer0_reload == 0xaa55,
          "stopped writes update both counter and reload");

    start_mode0(part, 0xfffc, 0);
    clocks(47);
    CHECK(timer0() == 0xffff && !(cpu.mSFR[REG_TCON] & TCONMASK_TF0),
          "12T mode has no overflow before 48 oscillator clocks");
    clocks(1);
    CHECK(timer0() == 0xfffc && (cpu.mSFR[REG_TCON] & TCONMASK_TF0),
          "12T mode reloads at 48 clocks");

    start_mode0(part, 0, 1);
    write_sfr(0x89, 0x08); /* gated mode0 */
    stc12_set_port_input(&st, 3, 0xfb); /* INT0 low */
    clocks(10);
    CHECK(timer0() == 0, "mode0 gate pauses while INT0 is low");
    stc12_set_port_input(&st, 3, 0xff);
    clocks(10);
    CHECK(timer0() == 10, "mode0 gate admits clocks while INT0 is high");

    start_mode0(part, 0xfffc, 1);
    write_sfr(0x89, 1); /* mode1 remains a plain 16-bit counter */
    clocks(4);
    CHECK(timer0() == 0, "STC15 mode1 still wraps without auto-reload");
    write_sfr(0x8a, 0x20);
    CHECK(timer0() == 0x20, "mode1 running writes still change the counter");
}

static void test_classic_modes(void)
{
    const uint8_t parts[] = { PART_STC12, PART_STC12_16, PART_STC89 };
    for (unsigned p = 0; p < sizeof parts; ++p) {
        start_mode0(parts[p], 0, parts[p] != PART_STC89);
        clocks(parts[p] == PART_STC89 ? 32 * 12 : 32);
        CHECK(cpu.mSFR[REG_TH0] == 1 && cpu.mSFR[REG_TL0] == 0,
              "STC12/STC89 retain classic 13-bit mode0");
        CHECK(cpu.sfrwrite[REG_TL0] == NULL,
              "STC15 reload callbacks do not leak into classic parts");
    }
}

static void test_w408as(void)
{
    setup(PART_STC15W);
    CHECK(stc12_flash_size(st.part_id) == 8192 && stc12_xram_size(st.part_id) == 256,
          "W408AS retains 8K flash and 256-byte auxiliary RAM");
    CHECK(!stc12_is_valid_sfr(&st, 0x8b) && !stc12_is_valid_sfr(&st, 0x8d),
          "W408AS has no Timer1 register pair");
    CHECK(!stc12_is_valid_sfr(&st, 0x9a) && !stc12_is_valid_sfr(&st, 0x9b),
          "W408AS has only one UART");
    CHECK(stc12_is_valid_sfr(&st, 0xdc) && stc12_is_valid_sfr(&st, 0xfc),
          "W408AS has a third PCA module");
    write_sfr(0x89, 0x10);
    write_sfr(0x8e, 0x40);
    write_sfr(0x88, 0x40);
    clocks(32);
    CHECK(cpu.mSFR[REG_TL1] == 0 && cpu.mSFR[REG_TH1] == 0,
          "W408AS does not run the absent Timer1");
    CHECK(cpu.sfrwrite[STC_REG_S2BUF] == NULL, "no absent UART2 TX callback");
    stc12_serial2_rx(&cpu, &st, 0x55);
    CHECK(cpu.mSFR[STC_REG_S2BUF] == 0, "UART2 injection has no effect on W408AS");
    write_sfr(0x82, 0x34);
    write_sfr(0x83, 0x12);
    write_sfr(0xa2, 1);
    CHECK(cpu.mSFR[REG_DPL] == 0x34 && cpu.mSFR[REG_DPH] == 0x12,
          "W408AS single DPTR does not swap on reserved DPS");

    /* ADC register packing comes from the STC table, not a HAL macro. */
    write_sfr(0x9d, 1);
    stc12_set_adc_input(&st, 0, 300);
    write_sfr(0x97, 0x20); /* CLK_DIV.ADRJ */
    write_sfr(0xbc, 0xe8);
    clocks(600);
    CHECK(cpu.mSFR[STC_REG_ADC_RES] == 1 && cpu.mSFR[STC_REG_ADC_RESL] == 44,
          "W408AS ADC uses CLK_DIV.ADRJ and right-justified packing");
    write_sfr(0x97, 0);
    write_sfr(0xa2, 4); /* STC12 ADRJ location must not change it */
    write_sfr(0xbc, 0xe8);
    clocks(600);
    CHECK(cpu.mSFR[STC_REG_ADC_RES] == 75 && cpu.mSFR[STC_REG_ADC_RESL] == 0,
          "W408AS ignores STC12's ADC justification bit");

    write_sfr(0xce, 0x40); /* STC15 SPI enable */
    write_sfr(0xcf, 0xa5);
    CHECK(cpu.mSFR[STC15_REG_SPSTAT] & 0x80, "W408AS SPI flag uses STC15 addresses");
    write_sfr(0xcd, 0x80);
    CHECK(!(cpu.mSFR[STC15_REG_SPSTAT] & 0x80), "W408AS SPI flag can be cleared");
    CHECK(cpu.sfrwrite[STC15_REG_SPCTL] == NULL,
          "STC12 SPSTAT callback does not intercept STC15 SPCTL");

    setup(PART_STC15W);
    write_sfr(0xd6, 0x12);
    write_sfr(0xd7, 0x34);
    write_sfr(0x8e, 0x14);
    clocks(1);
    CHECK(cpu.mSFR[STC_REG_T2H] == 0x12 && cpu.mSFR[STC_REG_T2L] == 0x35,
          "W408AS selects the STC15 Timer2 register pair");
}

static void test_pca(uint8_t part)
{
    const uint8_t ports[3][3] = {{1, 1, 3}, {3, 3, 3}, {2, 2, 2}};
    const uint8_t bits[3][3] = {{1, 0, 7}, {5, 6, 7}, {5, 6, 7}};
    const uint8_t port_sfr[] = {0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xc8};
    for (unsigned group = 0; group < 3; ++group) {
        setup(part);
        write_sfr(0xa2, group << 4);
        write_sfr(0xd9, 8); /* PCA SYSclk */
        for (unsigned mod = 0; mod < 3; ++mod) {
            write_sfr(0xda + mod, CCAPM_ECOM | CCAPM_PWM);
            write_sfr(0xea + mod, 128);
            write_sfr(0xfa + mod, 128);
        }
        write_sfr(0xd8, CCON_CR);
        clocks(1);
        for (unsigned mod = 0; mod < 3; ++mod)
            CHECK(!(cpu.mSFR[port_sfr[ports[group][mod]] - 0x80] & (1u << bits[group][mod])),
                  "all three STC15 PCA PWM pins follow the selected pin group");
        clocks(127);
        for (unsigned mod = 0; mod < 3; ++mod)
            CHECK(cpu.mSFR[port_sfr[ports[group][mod]] - 0x80] & (1u << bits[group][mod]),
                  "PCA outputs transition at the configured compare value");
    }
    setup(part);
    write_sfr(0xd9, 8);
    write_sfr(0xdc, CCAPM_CAPP);
    write_sfr(0xd8, CCON_CR);
    stc12_set_port_input(&st, 3, 0x7f);
    clocks(1);
    stc12_set_port_input(&st, 3, 0xff);
    clocks(1);
    CHECK((cpu.mSFR[STC_REG_CCON] & 7) == CCON_CCF2,
          "third-module capture sets CCF2, not CCF1");
}

int main(void)
{
    test_timer0(PART_STC15);
    test_timer0(PART_STC15W);
    test_classic_modes();
    test_w408as();
    test_pca(PART_STC15);
    test_pca(PART_STC15W);
    printf("%d passed, %d failed (STC15 family fidelity)\n", passed, failed);
    return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
