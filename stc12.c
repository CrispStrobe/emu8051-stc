/* STC12C5A60S2 peripheral model for emu8051
 * Copyright 2024 CrispStrobe
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * (i.e. the MIT License)
 *
 * stc12.c
 * STC12C5A60S2 peripheral logic: timers with 1T/12T, port modes,
 * ADC, PCA/PWM. All register addresses from the datasheet (2011-07-15)
 * cross-checked against SDCC's mcs51/stc12.h.
 */

#include <string.h>
#include "emu8051.h"
#include "stc12.h"

/* We stash the stc12_state pointer in a file-scope variable so that
 * the SFR callbacks (which only receive em8051*) can reach it.
 * This limits us to one emulator instance, which is fine for our use. */
static struct stc12_state *g_stc = NULL;

/* ================================================================== *
 * Boundary A — pin change detection and board callbacks                *
 * ================================================================== */

/* Emit setPin callbacks for any pins whose mode or drive changed */
static void emit_pin_changes(struct em8051 *aCPU, struct stc12_state *st, int port)
{
    if (!st->on_pin_change && !st->pin_history) return;

    static const uint8_t port_regs[] = {
        REG_P0, REG_P1, REG_P2, REG_P3, STC_REG_P4, STC_REG_P5
    };
    static const uint8_t m1_regs[] = {
        STC_REG_P0M1, STC_REG_P1M1, STC_REG_P2M1,
        STC_REG_P3M1, STC_REG_P4M1, STC_REG_P5M1
    };
    static const uint8_t m0_regs[] = {
        STC_REG_P0M0, STC_REG_P1M0, STC_REG_P2M0,
        STC_REG_P3M0, STC_REG_P4M0, STC_REG_P5M0
    };
    uint8_t latch = aCPU->mSFR[port_regs[port]];
    uint8_t m1 = aCPU->mSFR[m1_regs[port]];
    uint8_t m0 = aCPU->mSFR[m0_regs[port]];

    /* Check each pin for mode or drive changes */
    uint8_t drive_changed = latch ^ st->pin_drive_shadow[port];
    uint8_t m1_changed = m1 ^ st->pin_m1_shadow[port];
    uint8_t m0_changed = m0 ^ st->pin_m0_shadow[port];
    uint8_t any_changed = drive_changed | m1_changed | m0_changed;

    if (!any_changed) return;

    for (int bit = 0; bit < 8; bit++) {
        if (any_changed & (1 << bit)) {
            enum stc12_pin_mode mode = (enum stc12_pin_mode)
                (((m1 >> bit) & 1) << 1 | ((m0 >> bit) & 1));
            bool drive = (latch >> bit) & 1;
            if (st->on_pin_change)
                st->on_pin_change(port, bit, mode, drive, st->board_user_data);

            /* Record in pin history ring buffer */
            if (st->pin_history) {
                uint32_t idx = st->pin_history_head % PIN_HISTORY_SIZE;
                st->pin_history[idx].t_ns = stc12_get_time_ns(st);
                st->pin_history[idx].port = port;
                st->pin_history[idx].bit = bit;
                st->pin_history[idx].mode = mode;
                st->pin_history[idx].drive = drive;
                st->pin_history_head++;
                st->pin_history_count++;
            }
        }
    }
    st->pin_drive_shadow[port] = latch;
    st->pin_m1_shadow[port] = m1;
    st->pin_m0_shadow[port] = m0;
}

/* SFR write callbacks for port data and mode registers.
 * aRegister is the full SFR address (0x80..0xFF), not the index. */
static void sfr_write_port(struct em8051 *aCPU, uint8_t aRegister)
{
    if (!g_stc) return;
    int port = -1;
    switch (aRegister) {
    case 0x80:  port = 0; break;  /* P0 */
    case 0x90:  port = 1; break;  /* P1 */
    case 0xA0:  port = 2; break;  /* P2 */
    case 0xB0:  port = 3; break;  /* P3 */
    case 0xC0:  port = 4; break;  /* P4 */
    case 0xC8:  port = 5; break;  /* P5 */
    }
    if (port >= 0) emit_pin_changes(aCPU, g_stc, port);
}

static void sfr_write_port_mode(struct em8051 *aCPU, uint8_t aRegister)
{
    if (!g_stc) return;
    int port = -1;
    switch (aRegister) {
    case 0x94: case 0x93: port = 0; break;  /* P0M0, P0M1 */
    case 0x92: case 0x91: port = 1; break;  /* P1M0, P1M1 */
    case 0x96: case 0x95: port = 2; break;  /* P2M0, P2M1 */
    case 0xB2: case 0xB1: port = 3; break;  /* P3M0, P3M1 */
    case 0xB4: case 0xB3: port = 4; break;  /* P4M0, P4M1 */
    case 0xCA: case 0xC9: port = 5; break;  /* P5M0, P5M1 */
    }
    if (port >= 0) emit_pin_changes(aCPU, g_stc, port);
}

/* ================================================================== *
 * Port mode logic                                                     *
 * ================================================================== */

/* Get the mode register pair for a port (0-5).
 * Returns mode via PxM1 and PxM0 SFR values. */
static void get_port_mode(struct em8051 *aCPU, int port,
                          uint8_t *m1, uint8_t *m0)
{
    static const uint8_t m1_regs[] = {
        STC_REG_P0M1, STC_REG_P1M1, STC_REG_P2M1,
        STC_REG_P3M1, STC_REG_P4M1, STC_REG_P5M1
    };
    static const uint8_t m0_regs[] = {
        STC_REG_P0M0, STC_REG_P1M0, STC_REG_P2M0,
        STC_REG_P3M0, STC_REG_P4M0, STC_REG_P5M0
    };
    *m1 = aCPU->mSFR[m1_regs[port]];
    *m0 = aCPU->mSFR[m0_regs[port]];
}

/* Apply port mode to compute what a port read returns.
 * latch = the SFR latch value, ext = external pin state,
 * m1/m0 = mode register values (one bit per pin). */
static uint8_t apply_port_mode(uint8_t latch, uint8_t ext,
                               uint8_t m1, uint8_t m0)
{
    uint8_t result = 0;
    for (int bit = 0; bit < 8; bit++) {
        uint8_t mask = 1 << bit;
        int mode = ((m1 & mask) ? 2 : 0) | ((m0 & mask) ? 1 : 0);
        switch (mode) {
        case 0: /* quasi-bidirectional: read = latch AND external */
            result |= (latch & ext) & mask;
            break;
        case 1: /* push-pull: read = latch (strong drive both ways) */
            result |= latch & mask;
            break;
        case 2: /* input-only: read = external */
            result |= ext & mask;
            break;
        case 3: /* open-drain: read = latch AND external */
            result |= (latch & ext) & mask;
            break;
        }
    }
    return result;
}

/* Port SFR read callbacks */
static uint8_t port_read(struct em8051 *aCPU, int port_idx)
{
    static const uint8_t port_regs[] = {
        REG_P0, REG_P1, REG_P2, REG_P3, STC_REG_P4, STC_REG_P5
    };
    if (!g_stc || !g_stc->stc12_mode)
        return aCPU->mSFR[port_regs[port_idx]];

    /* Get external pin state: from board callback or legacy array */
    uint8_t ext;
    if (g_stc->on_read_pin) {
        ext = 0;
        for (int bit = 0; bit < 8; bit++) {
            if (g_stc->on_read_pin(port_idx, bit, g_stc->board_user_data))
                ext |= (1 << bit);
        }
    } else {
        ext = g_stc->port_ext[port_idx];
    }

    uint8_t m1, m0;
    get_port_mode(aCPU, port_idx, &m1, &m0);
    return apply_port_mode(aCPU->mSFR[port_regs[port_idx]], ext, m1, m0);
}

static uint8_t sfr_read_p0(struct em8051 *aCPU, uint8_t r) { (void)r; return port_read(aCPU, 0); }
static uint8_t sfr_read_p1(struct em8051 *aCPU, uint8_t r) { (void)r; return port_read(aCPU, 1); }
static uint8_t sfr_read_p2(struct em8051 *aCPU, uint8_t r) { (void)r; return port_read(aCPU, 2); }
static uint8_t sfr_read_p3(struct em8051 *aCPU, uint8_t r) { (void)r; return port_read(aCPU, 3); }
static uint8_t sfr_read_p4(struct em8051 *aCPU, uint8_t r) { (void)r; return port_read(aCPU, 4); }
static uint8_t sfr_read_p5(struct em8051 *aCPU, uint8_t r) { (void)r; return port_read(aCPU, 5); }

/* ADC_CONTR read: ADC_FLAG must be cleared by software writing 0 to it
 * (datasheet §10.4). Reading returns the register unchanged. */
static uint8_t sfr_read_adc_contr(struct em8051 *aCPU, uint8_t r)
{
    (void)r;
    return aCPU->mSFR[STC_REG_ADC_CONTR];
}

/* ================================================================== *
 * ADC write callback                                                  *
 * ================================================================== */

static void sfr_write_adc_contr(struct em8051 *aCPU, uint8_t r)
{
    (void)r;
    if (!g_stc) return;

    uint8_t val = aCPU->mSFR[STC_REG_ADC_CONTR];

    /* If ADC_START is set and ADC_POWER is on, begin conversion */
    if ((val & ADC_START) && (val & ADC_POWER)) {
        int speed = (val & ADC_SPEED_MASK) >> 5;
        static const uint16_t clocks[] = {
            ADC_CLOCKS_SPEED0, ADC_CLOCKS_SPEED1,
            ADC_CLOCKS_SPEED2, ADC_CLOCKS_SPEED3
        };
        g_stc->adc_countdown = clocks[speed];
    }
}

/* ================================================================== *
 * Timer logic with 1T/12T support                                     *
 *                                                                     *
 * In classic 8051, timer_tick() is called once per machine cycle       *
 * (= 12 osc clocks). The STC12 in 1T mode (AUXR.7 for T0) makes      *
 * the timer tick every single osc clock. We handle this by calling     *
 * stc12_tick() once per osc clock and using prescalers for 12T mode.   *
 * ================================================================== */

/* Returns true if Timer 0 overflowed this tick (for PCA clock source) */
static bool stc12_timer0_tick(struct em8051 *aCPU, struct stc12_state *st)
{
    /* Check AUXR.T0x12: 1 = 1T mode, 0 = 12T mode */
    bool is_1t = aCPU->mSFR[STC_REG_AUXR] & AUXR_T0x12;

    if (!is_1t) {
        st->timer0_prescaler++;
        if (st->timer0_prescaler < 12)
            return false;
        st->timer0_prescaler = 0;
    }

    bool overflowed = false;

    /* Now we should increment Timer 0 if it's running.
     * We replicate the upstream logic but only for the increment part.
     * The actual timer logic (modes, overflow, interrupt flags) stays
     * in core.c's timer_tick(). We just need to call it at the right rate.
     *
     * PROBLEM: upstream timer_tick() is called from tick() and handles
     * both T0 and T1 together. We can't easily split them.
     *
     * SOLUTION: We'll override the upstream timer_tick by replacing it.
     * For STC12 mode, we implement our own complete timer logic here,
     * and the upstream timer_tick() is skipped (by patching tick()).
     *
     * For now, we implement the timer increment inline. */

    /* Check if Timer 0 is run-enabled */
    if (!(aCPU->mSFR[REG_TMOD] & TMODMASK_GATE_0) &&
        (aCPU->mSFR[REG_TCON] & TCONMASK_TR0))
    {
        /* Timer mode (not counter mode) */
        if (!(aCPU->mSFR[REG_TMOD] & TMODMASK_CT_0))
        {
            uint16_t v;
            uint8_t tmod_mode = aCPU->mSFR[REG_TMOD] & (TMODMASK_M0_0 | TMODMASK_M1_0);

            switch (tmod_mode) {
            case 0: /* 13-bit timer */
                v = aCPU->mSFR[REG_TL0] & 0x1f;
                v++;
                aCPU->mSFR[REG_TL0] = (aCPU->mSFR[REG_TL0] & ~0x1f) | (v & 0x1f);
                if (v > 0x1f) {
                    v = aCPU->mSFR[REG_TH0];
                    v++;
                    aCPU->mSFR[REG_TH0] = v & 0xff;
                    if (v > 0xff)
                        { aCPU->mSFR[REG_TCON] |= TCONMASK_TF0; overflowed = true; }
                }
                break;
            case TMODMASK_M0_0: /* 16-bit timer */
                v = aCPU->mSFR[REG_TL0];
                v++;
                aCPU->mSFR[REG_TL0] = v & 0xff;
                if (v > 0xff) {
                    v = aCPU->mSFR[REG_TH0];
                    v++;
                    aCPU->mSFR[REG_TH0] = v & 0xff;
                    if (v > 0xff)
                        { aCPU->mSFR[REG_TCON] |= TCONMASK_TF0; overflowed = true; }
                }
                break;
            case TMODMASK_M1_0: /* 8-bit auto-reload */
                v = aCPU->mSFR[REG_TL0];
                v++;
                aCPU->mSFR[REG_TL0] = v & 0xff;
                if (v > 0xff) {
                    aCPU->mSFR[REG_TL0] = aCPU->mSFR[REG_TH0];
                    aCPU->mSFR[REG_TCON] |= TCONMASK_TF0;
                    overflowed = true;
                }
                break;
            case (TMODMASK_M0_0 | TMODMASK_M1_0): /* Mode 3: two 8-bit timers */
                /* TL0 as 8-bit timer with TF0 */
                v = aCPU->mSFR[REG_TL0];
                v++;
                aCPU->mSFR[REG_TL0] = v & 0xff;
                if (v > 0xff) {
                    aCPU->mSFR[REG_TCON] |= TCONMASK_TF0;
                    overflowed = true;
                }
                break;
            }

            /* In mode 3, TH0 runs as a separate timer using TR1/TF1 */
            if (tmod_mode == (TMODMASK_M0_0 | TMODMASK_M1_0)) {
                if (aCPU->mSFR[REG_TCON] & TCONMASK_TR1) {
                    v = aCPU->mSFR[REG_TH0];
                    v++;
                    aCPU->mSFR[REG_TH0] = v & 0xff;
                    if (v > 0xff)
                        aCPU->mSFR[REG_TCON] |= TCONMASK_TF1;
                }
            }
        }
    }
    return overflowed;
}

static void stc12_timer1_tick(struct em8051 *aCPU, struct stc12_state *st)
{
    bool is_1t = aCPU->mSFR[STC_REG_AUXR] & AUXR_T1x12;

    if (!is_1t) {
        st->timer1_prescaler++;
        if (st->timer1_prescaler < 12)
            return;
        st->timer1_prescaler = 0;
    }

    /* Skip T1 if T0 is in mode 3 (T1 is borrowed) — wait, actually in
     * mode 3, T1 still runs but its overflow doesn't set TF1 (TH0 uses
     * TF1 instead). T1 continues counting for baud rate generation. */

    if (!(aCPU->mSFR[REG_TMOD] & TMODMASK_GATE_1) &&
        (aCPU->mSFR[REG_TCON] & TCONMASK_TR1))
    {
        if (!(aCPU->mSFR[REG_TMOD] & TMODMASK_CT_1))
        {
            uint16_t v;
            uint8_t t0_mode3 = (aCPU->mSFR[REG_TMOD] & (TMODMASK_M0_0 | TMODMASK_M1_0))
                               == (TMODMASK_M0_0 | TMODMASK_M1_0);

            switch (aCPU->mSFR[REG_TMOD] & (TMODMASK_M0_1 | TMODMASK_M1_1)) {
            case 0: /* 13-bit */
                v = aCPU->mSFR[REG_TL1] & 0x1f;
                v++;
                aCPU->mSFR[REG_TL1] = (aCPU->mSFR[REG_TL1] & ~0x1f) | (v & 0x1f);
                if (v > 0x1f) {
                    v = aCPU->mSFR[REG_TH1];
                    v++;
                    aCPU->mSFR[REG_TH1] = v & 0xff;
                    if (v > 0xff && !t0_mode3)
                        aCPU->mSFR[REG_TCON] |= TCONMASK_TF1;
                }
                break;
            case TMODMASK_M0_1: /* 16-bit */
                v = aCPU->mSFR[REG_TL1];
                v++;
                aCPU->mSFR[REG_TL1] = v & 0xff;
                if (v > 0xff) {
                    v = aCPU->mSFR[REG_TH1];
                    v++;
                    aCPU->mSFR[REG_TH1] = v & 0xff;
                    if (v > 0xff && !t0_mode3)
                        aCPU->mSFR[REG_TCON] |= TCONMASK_TF1;
                }
                break;
            case TMODMASK_M1_1: /* 8-bit auto-reload */
                v = aCPU->mSFR[REG_TL1];
                v++;
                aCPU->mSFR[REG_TL1] = v & 0xff;
                if (v > 0xff) {
                    aCPU->mSFR[REG_TL1] = aCPU->mSFR[REG_TH1];
                    if (!t0_mode3)
                        aCPU->mSFR[REG_TCON] |= TCONMASK_TF1;
                }
                break;
            default: /* disabled */
                break;
            }
        }
    }
}

/* ================================================================== *
 * BRT — independent baud rate timer                                   *
 * ================================================================== */

static void stc12_brt_tick(struct em8051 *aCPU, struct stc12_state *st)
{
    if (st->part_id == PART_STC15) {
        /* STC15: Timer 2 replaces BRT.
         * AUXR.4 = T2R (run), AUXR.2 = T2x12 (1T/12T)
         * T2H/T2L (0xD6/0xD7) = 16-bit auto-reload timer */
        if (!(aCPU->mSFR[STC_REG_AUXR] & 0x10)) /* T2R */
            return;

        bool is_1t = aCPU->mSFR[STC_REG_AUXR] & 0x04; /* T2x12 */
        if (!is_1t) {
            st->brt_prescaler++;
            if (st->brt_prescaler < 12)
                return;
            st->brt_prescaler = 0;
        }

        /* 16-bit auto-reload: count in T2L/T2H, reload from same */
        uint16_t v = aCPU->mSFR[STC_REG_T2L] | (aCPU->mSFR[STC_REG_T2H] << 8);
        v++;
        aCPU->mSFR[STC_REG_T2L] = v & 0xFF;
        aCPU->mSFR[STC_REG_T2H] = (v >> 8) & 0xFF;
        /* Overflow: auto-reload (T2H/T2L serve as both counter and reload) */
        /* On real hardware T2 has separate reload registers — simplified here */
    } else {
        /* STC12: BRT (8-bit baud rate timer) */
        if (!(aCPU->mSFR[STC_REG_AUXR] & AUXR_BRTR))
            return;

        bool is_1t = aCPU->mSFR[STC_REG_AUXR] & AUXR_BRTx12;
        if (!is_1t) {
            st->brt_prescaler++;
            if (st->brt_prescaler < 12)
                return;
            st->brt_prescaler = 0;
        }

        /* BRT stub — counter runs but doesn't feed UART */
        (void)aCPU;
    }
}

/* ================================================================== *
 * ADC tick                                                            *
 * ================================================================== */

static void stc12_adc_tick(struct em8051 *aCPU, struct stc12_state *st)
{
    if (st->adc_countdown == 0)
        return;

    st->adc_countdown--;
    if (st->adc_countdown == 0) {
        /* Conversion complete */
        int ch = aCPU->mSFR[STC_REG_ADC_CONTR] & ADC_CHS_MASK;
        uint16_t result;

        if (st->on_read_analog) {
            /* Boundary A: board returns volts, MCU converts to counts */
            double volts = st->on_read_analog(1, ch, st->board_user_data);
            double vcc = st->vcc > 0.0 ? st->vcc : 5.0;
            if (volts < 0.0) volts = 0.0;
            if (volts > vcc) volts = vcc;
            result = (uint16_t)(volts / vcc * 1023.0 + 0.5);
        } else {
            /* Legacy: direct counts */
            result = st->adc_input[ch];
        }
        if (result > 1023) result = 1023;

        /* ADRJ controls justification. Its location depends on the part:
         * STC12: AUXR1 (0xA2) bit 2
         * STC15: CLK_DIV (0x97) bit 5  (STC15-PERIPHERAL-MODEL.md §2.1) */
        bool adrj;
        if (st->part_id == PART_STC15)
            adrj = aCPU->mSFR[STC_REG_CLK_DIV] & STC15_CLK_DIV_ADRJ;
        else
            adrj = aCPU->mSFR[STC_REG_AUXR1] & AUXR1_ADRJ;

        if (adrj) {
            /* Left-justified (ADRJ=1): result right-aligned in 10 bits */
            aCPU->mSFR[STC_REG_ADC_RESL] = result & 0xFF;
            aCPU->mSFR[STC_REG_ADC_RES]  = (result >> 8) & 0x03;
        } else {
            /* Right-justified (ADRJ=0, default): high 8 in RES, low 2 in RESL */
            aCPU->mSFR[STC_REG_ADC_RES]  = (result >> 2) & 0xFF;
            aCPU->mSFR[STC_REG_ADC_RESL] = (result & 0x03) << 0;
        }

        /* Set ADC_FLAG, clear ADC_START */
        aCPU->mSFR[STC_REG_ADC_CONTR] |= ADC_FLAG;
        aCPU->mSFR[STC_REG_ADC_CONTR] &= ~ADC_START;
    }
}

/* ================================================================== *
 * PCA tick                                                            *
 * ================================================================== */

/* Resolve PCA module pin: port and bit for CCP0/CCP1/CCP2.
 * STC12: CCP0=P1.3, CCP1=P1.4 (or P4.2, P4.3 if PCA_P4).
 * STC15: CCP2=P3.7 default. CCP_S pin switching not yet implemented. */
static void pca_pin(struct em8051 *aCPU, struct stc12_state *st,
                    int mod, int *port, int *bit)
{
    static const uint8_t default_bits[] = { 3, 4, 7 }; /* P1.3, P1.4, P3.7 */
    static const uint8_t p4_bits[] = { 2, 3, 1 };      /* P4.2, P4.3, P4.1 */
    if (mod == 2 && st->part_id == PART_STC15) {
        /* CCP2 default is P3.7 on STC15 */
        *port = 3;
        *bit = 7;
    } else {
        *port = 1;
        *bit = default_bits[mod];
        if (aCPU->mSFR[STC_REG_AUXR1] & AUXR1_PCA_P4) {
            *port = 4;
            *bit = p4_bits[mod];
        }
    }
}

static void stc12_pca_tick(struct em8051 *aCPU, struct stc12_state *st)
{
    /* PCA only runs if CR bit in CCON is set */
    if (!(aCPU->mSFR[STC_REG_CCON] & CCON_CR))
        return;

    /* CIDL: if set, PCA stops in idle mode */
    /* (not checking idle mode for now) */

    /* Determine clock source (CPS2:1:0, §5.2) */
    uint8_t cps = (aCPU->mSFR[STC_REG_CMOD] & CMOD_CPS_MASK) >> 1;
    bool do_tick = false;

    switch (cps) {
    case 0: /* SYSclk/12 */
        st->pca_prescaler++;
        if (st->pca_prescaler >= 12) {
            st->pca_prescaler = 0;
            do_tick = true;
        }
        break;
    case 1: /* SYSclk/2 */
        st->pca_prescaler++;
        if (st->pca_prescaler >= 2) {
            st->pca_prescaler = 0;
            do_tick = true;
        }
        break;
    case 2: /* Timer 0 overflow */
        if (st->pca_t0_overflow_pending) {
            do_tick = true;
            st->pca_t0_overflow_pending = false;
        }
        break;
    case 3: /* ECI pin — not implemented */
        break;
    case 4: /* SYSclk (every osc clock) */
        do_tick = true;
        break;
    case 5: /* SYSclk/4 */
        st->pca_prescaler++;
        if (st->pca_prescaler >= 4) {
            st->pca_prescaler = 0;
            do_tick = true;
        }
        break;
    case 6: /* SYSclk/6 */
        st->pca_prescaler++;
        if (st->pca_prescaler >= 6) {
            st->pca_prescaler = 0;
            do_tick = true;
        }
        break;
    case 7: /* SYSclk/8 */
        st->pca_prescaler++;
        if (st->pca_prescaler >= 8) {
            st->pca_prescaler = 0;
            do_tick = true;
        }
        break;
    }

    if (!do_tick)
        return;

    /* Increment PCA counter (CL:CH) */
    uint16_t counter = aCPU->mSFR[STC_REG_CL] | (aCPU->mSFR[STC_REG_CH] << 8);
    counter++;
    aCPU->mSFR[STC_REG_CL] = counter & 0xFF;
    aCPU->mSFR[STC_REG_CH] = (counter >> 8) & 0xFF;

    /* Counter overflow -> set CF */
    if (counter == 0) {
        aCPU->mSFR[STC_REG_CCON] |= CCON_CF;
    }

    /* Process each PCA module (2 on STC12, 3 on STC15) */
    static const uint8_t ccapm_regs[] = { STC_REG_CCAPM0, STC_REG_CCAPM1, STC_REG_CCAPM2 };
    static const uint8_t ccapl_regs[] = { STC_REG_CCAP0L, STC_REG_CCAP1L, STC_REG_CCAP2L };
    static const uint8_t ccaph_regs[] = { STC_REG_CCAP0H, STC_REG_CCAP1H, STC_REG_CCAP2H };
    static const uint8_t pwm_regs[]   = { STC_REG_PCA_PWM0, STC_REG_PCA_PWM1, STC_REG_PCA_PWM2 };
    static const uint8_t ccf_masks[]  = { CCON_CCF0, CCON_CCF1, CCON_CCF2 };
    int n_modules;
    switch (st->part_id) {
    case PART_STC15:  n_modules = 3; break;
    case PART_STC12:  n_modules = 2; break;
    case PART_STC15W: n_modules = 0; break; /* no PCA on W408AS */
    default:          n_modules = 2; break;
    }

    for (int mod = 0; mod < n_modules; mod++) {
        uint8_t ccapm = aCPU->mSFR[ccapm_regs[mod]];
        uint8_t ccapl_reg = ccapl_regs[mod];
        uint8_t ccaph_reg = ccaph_regs[mod];
        uint8_t pwm_reg   = pwm_regs[mod];
        uint8_t ccf_mask  = ccf_masks[mod];

        if (ccapm & CCAPM_PWM) {
            /* 8-bit PWM mode (§5.3):
             * Comparator: {EPCnL, CCAPnL} vs (0, CL)
             *   (0,CL) <  {EPCnL,CCAPnL}  → output LOW
             *   (0,CL) >= {EPCnL,CCAPnL}  → output HIGH
             *
             * ⚠ LARGER compare value = LONGER low time.
             * Duty-as-fraction-HIGH = (256 - {EPCnL,CCAPnL}) / 256
             *
             * Double buffering: on CL overflow (0xFF→0x00),
             * {EPCnH, CCAPnH} reloads into {EPCnL, CCAPnL}.
             * Software writes the NEXT duty to CCAPnH/EPCnH. */

            /* Reload on CL overflow */
            if (aCPU->mSFR[STC_REG_CL] == 0x00) {
                aCPU->mSFR[ccapl_reg] = aCPU->mSFR[ccaph_reg];
                /* Also reload EPCnL from EPCnH (9-bit extension) */
                uint8_t pwm = aCPU->mSFR[pwm_reg];
                pwm = (pwm & ~0x01) | ((pwm >> 1) & 0x01); /* EPCnH → EPCnL */
                aCPU->mSFR[pwm_reg] = pwm;
            }

            /* Drive the PWM output pin. */
            {
                static const uint8_t port_regs[] = {
                    REG_P0, REG_P1, REG_P2, REG_P3,
                    STC_REG_P4, STC_REG_P5
                };
                int pin_port, pin_bit;
                pca_pin(aCPU, st, mod, &pin_port, &pin_bit);
                uint8_t compare = aCPU->mSFR[ccapl_reg];
                uint8_t cl = aCPU->mSFR[STC_REG_CL];
                bool output_high = (cl >= compare);
                uint8_t mask = 1 << pin_bit;
                uint8_t old = aCPU->mSFR[port_regs[pin_port]];
                if (output_high)
                    aCPU->mSFR[port_regs[pin_port]] |= mask;
                else
                    aCPU->mSFR[port_regs[pin_port]] &= ~mask;
                /* Emit pin change if the value actually changed */
                if ((old ^ aCPU->mSFR[port_regs[pin_port]]) & mask)
                    emit_pin_changes(aCPU, st, pin_port);
            }
        }

        if ((ccapm & CCAPM_ECOM) && (ccapm & CCAPM_MAT)) {
            /* Software timer / high-speed output: compare */
            uint16_t compare = aCPU->mSFR[ccapl_reg] |
                               (aCPU->mSFR[ccaph_reg] << 8);
            if (counter == compare) {
                aCPU->mSFR[STC_REG_CCON] |= ccf_mask;
                if (ccapm & CCAPM_TOG) {
                    static const uint8_t port_regs[] = {
                        REG_P0, REG_P1, REG_P2, REG_P3,
                        STC_REG_P4, STC_REG_P5
                    };
                    int pin_port, pin_bit;
                    pca_pin(aCPU, st, mod, &pin_port, &pin_bit);
                    aCPU->mSFR[port_regs[pin_port]] ^= (1 << pin_bit);
                    emit_pin_changes(aCPU, st, pin_port);
                }
            }
        }

        if ((ccapm & CCAPM_CAPP) || (ccapm & CCAPM_CAPN)) {
            /* Capture mode — triggered by external CEXn pin edge. */
            int pin_port, pin_bit;
            pca_pin(aCPU, st, mod, &pin_port, &pin_bit);
            uint8_t cur_level = (st->port_ext[pin_port] >> pin_bit) & 1;
            uint8_t prev_level = st->pca_cex_last[mod];
            st->pca_cex_last[mod] = cur_level;

            bool capture = false;
            if ((ccapm & CCAPM_CAPP) && !prev_level && cur_level)  /* rising */
                capture = true;
            if ((ccapm & CCAPM_CAPN) && prev_level && !cur_level)  /* falling */
                capture = true;

            if (capture) {
                aCPU->mSFR[ccapl_reg] = aCPU->mSFR[STC_REG_CL];
                aCPU->mSFR[ccaph_reg] = aCPU->mSFR[STC_REG_CH];
                aCPU->mSFR[STC_REG_CCON] |= (mod == 0) ? CCON_CCF0 : CCON_CCF1;
            }
        }
    }
}

/* ================================================================== *
 * Main tick — called once per oscillator clock in STC12 mode          *
 * ================================================================== */

static void sfr_write_wdt(struct em8051 *aCPU, uint8_t aRegister);
static void stc12_wdt_tick(struct em8051 *aCPU, struct stc12_state *st);

void stc12_tick(struct em8051 *aCPU, struct stc12_state *aState)
{
    if (!aState->stc12_mode)
        return;

    aState->osc_clocks++;

    /* STC89: 12T timing is handled by mMachineCycleScale in core.c tick().
     * No STC-specific peripheral ticking needed. */
    if (aState->part_id == PART_STC89)
        return;

    bool t0_overflowed = stc12_timer0_tick(aCPU, aState);
    stc12_timer1_tick(aCPU, aState);
    stc12_brt_tick(aCPU, aState);

    /* T0 overflow drives the PCA when CPS=10 */
    if (t0_overflowed) {
        aState->pca_t0_overflow_pending = true;

        /* STC15 INT_CLKO.T0CLKO: toggle P3.5 on Timer 0 overflow */
        if (aState->part_id == PART_STC15 &&
            (aCPU->mSFR[STC_REG_INT_CLKO] & INT_CLKO_T0CLKO)) {
            aCPU->mSFR[REG_P3] ^= (1 << 5); /* toggle P3.5 */
            emit_pin_changes(aCPU, aState, 3);
        }
    }

    /* STC15 INT_CLKO.T1CLKO: detect TF1 rising edge, toggle P3.4 */
    if (aState->part_id == PART_STC15 &&
        (aCPU->mSFR[STC_REG_INT_CLKO] & INT_CLKO_T1CLKO)) {
        uint8_t tf1 = aCPU->mSFR[REG_TCON] & TCONMASK_TF1;
        if (tf1 && !aState->last_tf1) {
            aCPU->mSFR[REG_P3] ^= (1 << 4); /* toggle P3.4 */
            emit_pin_changes(aCPU, aState, 3);
        }
        aState->last_tf1 = tf1;
    }

    stc12_adc_tick(aCPU, aState);
    stc12_pca_tick(aCPU, aState);
    stc12_wdt_tick(aCPU, aState);
}

/* ================================================================== *
 * Initialization                                                      *
 * ================================================================== */


/* ================================================================== *
 * Watchdog timer                                                      *
 * ================================================================== */

/* WDT_CONTR bits */
#define WDT_FLAG    0x80
#define EN_WDT      0x20
#define CLR_WDT     0x10
#define IDLE_WDT    0x08
#define WDT_PS_MASK 0x07

static void sfr_write_wdt(struct em8051 *aCPU, uint8_t aRegister)
{
    (void)aRegister;
    if (!g_stc) return;
    uint8_t val = aCPU->mSFR[STC_REG_WDT_CONTR];

    /* CLR_WDT: writing 1 clears the watchdog counter */
    if (val & CLR_WDT) {
        g_stc->wdt_counter = 0;
        g_stc->wdt_prescaler_cnt = 0;
        /* CLR_WDT is auto-cleared by hardware */
        aCPU->mSFR[STC_REG_WDT_CONTR] &= ~CLR_WDT;
    }
}

static void stc12_wdt_tick(struct em8051 *aCPU, struct stc12_state *st)
{
    uint8_t wdt = aCPU->mSFR[STC_REG_WDT_CONTR];
    if (!(wdt & EN_WDT)) return;

    /* Prescaler: divide by 2^(PS+1) where PS = bits 2:0 */
    uint8_t ps = wdt & WDT_PS_MASK;
    uint16_t divisor = 2 << ps; /* 2^(ps+1) */

    st->wdt_prescaler_cnt++;
    if (st->wdt_prescaler_cnt < divisor) return;
    st->wdt_prescaler_cnt = 0;

    /* Increment watchdog counter. Overflow at 32768 → reset */
    st->wdt_counter++;
    if (st->wdt_counter >= 32768) {
        /* Watchdog overflow → set flag and reset */
        aCPU->mSFR[STC_REG_WDT_CONTR] |= WDT_FLAG;
        st->wdt_counter = 0;
        /* In a real chip this resets the CPU.
         * For the emulator, we just set the flag. */
    }
}

static void sfr_write_sbuf(struct em8051 *aCPU, uint8_t aRegister);
static void sfr_write_s2buf(struct em8051 *aCPU, uint8_t aRegister);
static void sfr_write_spdat(struct em8051 *aCPU, uint8_t aRegister);
static void sfr_write_spstat(struct em8051 *aCPU, uint8_t aRegister);
static void sfr_write_auxr1(struct em8051 *aCPU, uint8_t aRegister);

void stc12_init(struct em8051 *aCPU, struct stc12_state *aState)
{
    /* Preserve configuration across init (not callbacks — in WASM they
     * may point to invalidated addFunction entries after removeFunction). */
    stc12_pin_callback         saved_pin    = aState->on_pin_change;
    stc12_read_pin_callback    saved_read   = aState->on_read_pin;
    stc12_read_analog_callback saved_analog = aState->on_read_analog;
    stc12_advance_callback     saved_adv    = aState->on_advance;
    void                      *saved_ud     = aState->board_user_data;
    uint8_t                    saved_part   = aState->part_id;
    struct stc12_pin_event    *saved_hist   = aState->pin_history;

    g_stc = aState;

    memset(aState, 0, sizeof(*aState));
    aState->stc12_mode = true;
    aState->fosc = 11059200; /* default: 11.0592 MHz crystal */
    aState->vcc = 5.0;

    /* Restore board callbacks and configuration */
    aState->on_pin_change  = saved_pin;
    aState->on_read_pin    = saved_read;
    aState->on_read_analog = saved_analog;
    aState->on_advance     = saved_adv;
    aState->board_user_data = saved_ud;
    aState->part_id        = saved_part;
    aState->pin_history    = saved_hist;
    /* Serial callbacks are NOT preserved — they must be re-registered
     * after reset since they may be WASM addFunction pointers. */

    /* Pre-compute ns per clock (fixed-point * 16 for precision) */
    if (aState->fosc > 0)
        aState->ns_per_clock_x16 = (uint64_t)(16.0e9 / aState->fosc + 0.5);

    /* All external port pins default high (quasi-bidirectional pull-up) */
    for (int i = 0; i < 6; i++)
        aState->port_ext[i] = 0xFF;

    /* Pin drive shadows: all high at reset (ports default to 0xFF) */
    for (int i = 0; i < 6; i++)
        aState->pin_drive_shadow[i] = 0xFF;

    /* Install port read callbacks */
    aCPU->sfrread[REG_P0] = sfr_read_p0;
    aCPU->sfrread[REG_P1] = sfr_read_p1;
    aCPU->sfrread[REG_P2] = sfr_read_p2;
    aCPU->sfrread[REG_P3] = sfr_read_p3;
    aCPU->sfrread[STC_REG_P4] = sfr_read_p4;
    aCPU->sfrread[STC_REG_P5] = sfr_read_p5;

    /* Port write callbacks — emit pin changes to the board */
    aCPU->sfrwrite[REG_P0] = sfr_write_port;
    aCPU->sfrwrite[REG_P1] = sfr_write_port;
    aCPU->sfrwrite[REG_P2] = sfr_write_port;
    aCPU->sfrwrite[REG_P3] = sfr_write_port;
    aCPU->sfrwrite[STC_REG_P4] = sfr_write_port;
    aCPU->sfrwrite[STC_REG_P5] = sfr_write_port;

    /* STC-specific peripherals — not present on the classic 8052 (STC89) */
    if (aState->part_id != PART_STC89) {
        /* Port mode write callbacks — emit pin mode changes */
        aCPU->sfrwrite[STC_REG_P0M0] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P0M1] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P1M0] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P1M1] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P2M0] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P2M1] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P3M0] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P3M1] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P4M0] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P4M1] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P5M0] = sfr_write_port_mode;
        aCPU->sfrwrite[STC_REG_P5M1] = sfr_write_port_mode;

        /* ADC */
        aCPU->sfrread[STC_REG_ADC_CONTR] = sfr_read_adc_contr;
        aCPU->sfrwrite[STC_REG_ADC_CONTR] = sfr_write_adc_contr;

        /* Watchdog */
        aCPU->sfrwrite[STC_REG_WDT_CONTR] = sfr_write_wdt;

        /* SPI */
        aCPU->sfrwrite[STC_REG_SPDAT] = sfr_write_spdat;
        aCPU->sfrwrite[STC_REG_SPSTAT] = sfr_write_spstat;
        aCPU->sfrwrite[STC15_REG_SPDAT] = sfr_write_spdat;
        aCPU->sfrwrite[STC15_REG_SPSTAT] = sfr_write_spstat;

        /* UART2 */
        aCPU->sfrwrite[STC_REG_S2BUF] = sfr_write_s2buf;

        /* Dual DPTR / AUXR1 */
        aCPU->sfrwrite[STC_REG_AUXR1] = sfr_write_auxr1;

        /* STC12/15 SFR reset values */
        aCPU->mSFR[STC_REG_P4] = 0xFF;
        aCPU->mSFR[STC_REG_P5] = 0xFF;
        aCPU->mSFR[STC_REG_AUXR] = 0x00;
        aCPU->mSFR[STC_REG_AUXR1] = 0x00;
    }

    /* Serial port TX: all parts have UART1 */
    aCPU->sfrwrite[REG_SBUF] = sfr_write_sbuf;
}

/* ================================================================== *
 * Utility functions                                                   *
 * ================================================================== */

void stc12_set_adc_input(struct stc12_state *aState, int channel, uint16_t value)
{
    if (channel >= 0 && channel < 8)
        aState->adc_input[channel] = value > 1023 ? 1023 : value;
}

void stc12_set_port_input(struct stc12_state *aState, int port, uint8_t value)
{
    if (port >= 0 && port < 6)
        aState->port_ext[port] = value;
}

/* ================================================================== *
 * Boundary A implementation                                           *
 * ================================================================== */

void stc12_set_board_callbacks(struct stc12_state *aState,
                               stc12_pin_callback on_pin,
                               stc12_read_pin_callback on_read,
                               stc12_read_analog_callback on_analog,
                               stc12_advance_callback on_advance,
                               void *user_data)
{
    aState->on_pin_change  = on_pin;
    aState->on_read_pin    = on_read;
    aState->on_read_analog = on_analog;
    aState->on_advance     = on_advance;
    aState->board_user_data = user_data;
}

uint64_t stc12_get_time_ns(struct stc12_state *aState)
{
    return (aState->osc_clocks * aState->ns_per_clock_x16) >> 4;
}

int stc12_advance_to(struct em8051 *aCPU, struct stc12_state *aState,
                     uint64_t target_ns)
{
    int count = 0;
    while (stc12_get_time_ns(aState) < target_ns) {
        bool ticked = tick(aCPU);
        stc12_tick(aCPU, aState);
        if (ticked) count++;
    }
    if (aState->on_advance) {
        uint64_t t = stc12_get_time_ns(aState);
        aState->on_advance((uint32_t)t, (uint32_t)(t >> 32), aState->board_user_data);
    }
    return count;
}

/* ================================================================== *
 * Stage 0: part identity and SFR validation                           *
 * ================================================================== */

/* Valid SFR addresses for STC12C5A60S2.
 * Source: stc_disasm.py SFR table, cross-checked against SDCC's stc12.h.
 * Also includes standard 8051 SFRs (SP, DPL, DPH, PCON, SCON, SBUF,
 * IE, IP, PSW, ACC, B) and STC12-specific registers not in the
 * disassembler map (SPCTL 0x85, SPDAT 0x86, WDT_CONTR 0xC1,
 * SPSTAT 0xCE, CCAP0L 0xEA, CCAP1L 0xEB). */
static const uint8_t stc12_valid_sfr_set[] = {
    0x80, 0x81, 0x82, 0x83, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A,
    0x8B, 0x8C, 0x8D, 0x8E, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95,
    0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0xA0, 0xA2,
    0xA8, 0xA9, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB6, 0xB7, 0xB8,
    0xB9, 0xBB, 0xBC, 0xBD, 0xBE, 0xC0, 0xC1, 0xC8, 0xC9, 0xCA,
    0xCE, 0xD0, 0xD8, 0xD9, 0xDA, 0xDB, 0xE0, 0xE9, 0xEA, 0xEB,
    0xF0, 0xF2, 0xF3, 0xF9, 0xFA, 0xFB,
};

/* Classic 8052 SFRs (for STC89C52RC) — no STC extensions */
static const uint8_t stc89_valid_sfr_set[] = {
    0x80, /* P0 */  0x81, /* SP */  0x82, /* DPL */ 0x83, /* DPH */
    0x87, /* PCON */ 0x88, /* TCON */ 0x89, /* TMOD */
    0x8A, /* TL0 */ 0x8B, /* TL1 */ 0x8C, /* TH0 */ 0x8D, /* TH1 */
    0x90, /* P1 */  0x98, /* SCON */ 0x99, /* SBUF */
    0xA0, /* P2 */  0xA8, /* IE */  0xB0, /* P3 */  0xB8, /* IP */
    0xC8, /* T2CON */ 0xC9, /* T2MOD */
    0xCA, /* RCAP2L */ 0xCB, /* RCAP2H */
    0xCC, /* TL2 */ 0xCD, /* TH2 */
    0xD0, /* PSW */ 0xE0, /* ACC */ 0xF0, /* B */
};

/* STC15 adds these SFRs (STC15-PERIPHERAL-MODEL.md §3) */
static const uint8_t stc15_extra_sfr_set[] = {
    0x8F, /* INT_CLKO/AUXR2 */
    0xA1, /* BUS_SPEED */
    0xAA, /* WKTCL */
    0xAB, /* WKTCH */
    0xBA, /* P_SW2 */
    0xC2, /* IAP_DATA */
    0xC3, /* IAP_ADDRH */
    0xC4, /* IAP_ADDRL */
    0xC5, /* IAP_CMD */
    0xC6, /* IAP_TRIG */
    0xC7, /* IAP_CONTR */
    0xCD, /* SPSTAT (STC15 address) */
    0xCF, /* SPDAT (STC15 address) */
    0xD6, /* T2H */
    0xD7, /* T2L */
    0xDC, /* CCAPM2 */
    0xEC, /* CCAP2L */
    0xF4, /* PCA_PWM2 */
    0xFC, /* CCAP2H */
};

/* STC12 has SPI at 0x85/0x86/0xCE; STC15 moves to 0xCD/0xCE/0xCF.
 * P4SW (0xBB) is STC12-only. */
static const uint8_t stc12_only_sfr_set[] = {
    0x85, /* SPCTL (STC12 address) */
    0x86, /* SPDAT (STC12 address) */
    0xBB, /* P4SW */
};

bool stc12_is_valid_sfr(struct stc12_state *aState, uint8_t addr)
{
    /* STC89: classic 8052, completely different SFR set */
    if (aState && aState->part_id == PART_STC89) {
        for (unsigned i = 0; i < sizeof(stc89_valid_sfr_set); i++)
            if (stc89_valid_sfr_set[i] == addr) return true;
        return false;
    }

    /* STC12/STC15/STC15W: check common STC12 set */
    for (unsigned i = 0; i < sizeof(stc12_valid_sfr_set); i++) {
        if (stc12_valid_sfr_set[i] == addr) {
            /* Some addresses are STC12-only — check exclusions for STC15 variants */
            if (aState && (aState->part_id == PART_STC15 || aState->part_id == PART_STC15W)) {
                for (unsigned j = 0; j < sizeof(stc12_only_sfr_set); j++) {
                    if (stc12_only_sfr_set[j] == addr) return false;
                }
            }
            return true;
        }
    }
    /* Check STC15 extras */
    if (aState && (aState->part_id == PART_STC15 || aState->part_id == PART_STC15W)) {
        for (unsigned i = 0; i < sizeof(stc15_extra_sfr_set); i++) {
            if (stc15_extra_sfr_set[i] == addr) return true;
        }
    }
    return false;
}

void stc12_set_part(struct stc12_state *aState, uint8_t part_id)
{
    aState->part_id = part_id;
}

uint32_t stc12_flash_size(uint8_t part_id)
{
    switch (part_id) {
    case PART_STC12:    return 61440;  /* 60 KB */
    case PART_STC15:    return 61440;  /* 60 KB */
    case PART_STC89:    return 8192;   /* 8 KB */
    case PART_STC15W:   return 8192;   /* 8 KB */
    case PART_STC12_16: return 16384;  /* 16 KB */
    default:            return 65535;
    }
}

uint16_t stc12_xram_size(uint8_t part_id)
{
    switch (part_id) {
    case PART_STC12:    return 1024;   /* 1280 total - 256 scratch = 1024 aux */
    case PART_STC15:    return 1792;   /* 2048 total - 256 scratch */
    case PART_STC89:    return 256;    /* 512 total - 256 scratch */
    case PART_STC15W:   return 256;    /* 512 total - 256 scratch */
    case PART_STC12_16: return 1024;
    default:            return 65535;
    }
}

/* ================================================================== *
 * Serial port (UART1)                                                 *
 * ================================================================== */

/* SFR write callback for SBUF — firmware is transmitting a byte */

static void sfr_write_sbuf(struct em8051 *aCPU, uint8_t aRegister)
{
    (void)aRegister;
    if (!g_stc) return;

    uint8_t byte = aCPU->mSFR[REG_SBUF];

    /* In mode 1 (8-bit UART, the common case), writing SBUF starts TX.
     * For simplicity, we complete the transmission immediately rather
     * than bit-banging through Timer 1 baud rate. This is functionally
     * correct: the byte appears in the output, TI is set, and the
     * serial interrupt fires if enabled.
     *
     * The upstream serial_tx does bit-by-bit clocking through Timer 1,
     * which we bypassed with skip_timers. This instant-complete model
     * is simpler and sufficient for printf-style output. */

    /* Store in the visual buffer (upstream feature) */
    aCPU->serial_out[aCPU->serial_out_idx] = byte;
    aCPU->serial_out_idx = (aCPU->serial_out_idx + 1) % sizeof(aCPU->serial_out);

    /* Set TI (transmit interrupt flag) */
    aCPU->mSFR[REG_SCON] |= SCONMASK_TI;

    /* Trigger serial interrupt if enabled */
    if (aCPU->mSFR[REG_IE] & IEMASK_ES)
        aCPU->serial_interrupt_trigger = true;

    /* Call the TX callback */
    if (g_stc->on_serial_tx)
        g_stc->on_serial_tx(byte, g_stc->board_user_data);
}

void stc12_serial_rx(struct em8051 *aCPU, struct stc12_state *aState, uint8_t byte)
{
    (void)aState;
    /* Place byte in SBUF for firmware to read */
    aCPU->mSFR[REG_SBUF] = byte;

    /* Set RI (receive interrupt flag) */
    aCPU->mSFR[REG_SCON] |= SCONMASK_RI;

    /* Trigger serial interrupt if enabled */
    if (aCPU->mSFR[REG_IE] & IEMASK_ES)
        aCPU->serial_interrupt_trigger = true;
}

void stc12_set_serial_callback(struct stc12_state *aState,
                                stc12_serial_tx_callback cb, void *user_data)
{
    aState->on_serial_tx = cb;
    /* user_data is shared with board callbacks — fine for single-instance use */
    (void)user_data;
}

/* ================================================================== *
 * Serial port 2 (UART2) — S2CON (0x9A), S2BUF (0x9B)                 *
 * ================================================================== */

/* S2CON bit masks */
#define S2CON_S2RI  0x01
#define S2CON_S2TI  0x02

static void sfr_write_s2buf(struct em8051 *aCPU, uint8_t aRegister)
{
    (void)aRegister;
    if (!g_stc) return;

    uint8_t byte = aCPU->mSFR[STC_REG_S2BUF];

    /* Set S2TI (transmit complete) */
    aCPU->mSFR[STC_REG_S2CON] |= S2CON_S2TI;

    /* Call the UART2 TX callback */
    if (g_stc->on_serial2_tx)
        g_stc->on_serial2_tx(byte, g_stc->board_user_data);
}

void stc12_serial2_rx(struct em8051 *aCPU, struct stc12_state *aState, uint8_t byte)
{
    (void)aState;
    aCPU->mSFR[STC_REG_S2BUF] = byte;
    aCPU->mSFR[STC_REG_S2CON] |= S2CON_S2RI;
}

/* ================================================================== *
 * SPI (minimal — flag dance only, no shift register)                  *
 * ================================================================== */

#define SPSTAT_SPIF  0x80
#define SPSTAT_WCOL  0x40
#define SPCTL_SPEN   0x40

/* Resolve SPI register indices based on part.
 * STC12: SPCTL=0x85, SPDAT=0x86, SPSTAT=0xCE
 * STC15: SPSTAT=0xCD, SPCTL=0xCE, SPDAT=0xCF */
static uint8_t spi_reg_spctl(void) {
    return (g_stc && g_stc->part_id == PART_STC15) ? STC15_REG_SPCTL : STC_REG_SPCTL;
}
static uint8_t spi_reg_spstat(void) {
    return (g_stc && g_stc->part_id == PART_STC15) ? STC15_REG_SPSTAT : STC_REG_SPSTAT;
}

static void sfr_write_spdat(struct em8051 *aCPU, uint8_t aRegister)
{
    (void)aRegister;
    if (!g_stc) return;

    /* If SPI is enabled, complete the transfer immediately and set SPIF */
    if (aCPU->mSFR[spi_reg_spctl()] & SPCTL_SPEN) {
        aCPU->mSFR[spi_reg_spstat()] |= SPSTAT_SPIF;
    }
}

/* SPSTAT write: writing 1 to SPIF or WCOL clears them */
static void sfr_write_spstat(struct em8051 *aCPU, uint8_t aRegister)
{
    (void)aRegister;
    uint8_t reg = spi_reg_spstat();
    uint8_t val = aCPU->mSFR[reg];
    if (val & SPSTAT_SPIF) aCPU->mSFR[reg] &= ~SPSTAT_SPIF;
    if (val & SPSTAT_WCOL) aCPU->mSFR[reg] &= ~SPSTAT_WCOL;
}

/* ================================================================== *
 * Dual DPTR — AUXR1.DPS (bit 0) selects DPTR0 or DPTR1               *
 * ================================================================== */

static void sfr_write_auxr1(struct em8051 *aCPU, uint8_t aRegister)
{
    (void)aRegister;
    if (!g_stc) return;

    uint8_t dps = aCPU->mSFR[STC_REG_AUXR1] & AUXR1_DPS;
    uint8_t old_dps = g_stc->last_dps;

    if (dps != old_dps) {
        /* DPS changed: swap active DPTR with stored one */
        uint8_t tmp_l = aCPU->mSFR[REG_DPL];
        uint8_t tmp_h = aCPU->mSFR[REG_DPH];
        aCPU->mSFR[REG_DPL] = g_stc->dptr1_l;
        aCPU->mSFR[REG_DPH] = g_stc->dptr1_h;
        g_stc->dptr1_l = tmp_l;
        g_stc->dptr1_h = tmp_h;
        g_stc->last_dps = dps;
    }
}
