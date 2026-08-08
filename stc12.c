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
    if (!st->on_pin_change) return;

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
            st->on_pin_change(port, bit, mode, drive, st->board_user_data);
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
    /* AUXR.BRTR enables the BRT */
    if (!(aCPU->mSFR[STC_REG_AUXR] & AUXR_BRTR))
        return;

    /* AUXR.BRTx12: 1 = 1T, 0 = 12T */
    bool is_1t = aCPU->mSFR[STC_REG_AUXR] & AUXR_BRTx12;
    if (!is_1t) {
        st->brt_prescaler++;
        if (st->brt_prescaler < 12)
            return;
        st->brt_prescaler = 0;
    }

    /* BRT is an 8-bit auto-reload timer (reload from BRT register) */
    uint16_t v = aCPU->mSFR[STC_REG_BRT];
    /* Wait — BRT has its own counter that's not directly visible as an SFR.
     * The SFR at 0x9C is the reload value. We need internal state.
     * For simplicity, we'll just track overflow timing. The BRT overflow
     * feeds the UART baud rate generator, which we're not fully implementing
     * yet. For now, stub it. */
    (void)v;
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

static void stc12_pca_tick(struct em8051 *aCPU, struct stc12_state *st)
{
    /* PCA only runs if CR bit in CCON is set */
    if (!(aCPU->mSFR[STC_REG_CCON] & CCON_CR))
        return;

    /* CIDL: if set, PCA stops in idle mode */
    /* (not checking idle mode for now) */

    /* Determine clock source */
    uint8_t cps = (aCPU->mSFR[STC_REG_CMOD] & CMOD_CPS_MASK) >> 1;
    bool do_tick = false;

    switch (cps) {
    case 0: /* FOSC/12 */
        st->pca_prescaler++;
        if (st->pca_prescaler >= 12) {
            st->pca_prescaler = 0;
            do_tick = true;
        }
        break;
    case 1: /* FOSC/2 */
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
    case 3: /* ECI pin — not implemented, would need external clock */
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

    /* Process each PCA module */
    for (int mod = 0; mod < 2; mod++) {
        uint8_t ccapm = aCPU->mSFR[mod == 0 ? STC_REG_CCAPM0 : STC_REG_CCAPM1];
        uint8_t ccapl_reg = mod == 0 ? STC_REG_CCAP0L : STC_REG_CCAP1L;
        uint8_t ccaph_reg = mod == 0 ? STC_REG_CCAP0H : STC_REG_CCAP1H;
        uint8_t pwm_reg   = mod == 0 ? STC_REG_PCA_PWM0 : STC_REG_PCA_PWM1;
        uint8_t ccf_mask  = mod == 0 ? CCON_CCF0 : CCON_CCF1;

        if (ccapm & CCAPM_PWM) {
            /* 8-bit PWM mode */
            /* Compare CL against CCAP0L. When CL < CCAP0L, output high;
             * when CL >= CCAP0L, output low. (Or the inverse — datasheet
             * §11.5 says output is low when CL < CCAPnL, high when >=)
             * Actually: the PWM output goes low when CL rolls over from
             * 0xFF to 0x00 (and CCAPnL is reloaded from CCAPnH), and goes
             * high when CL matches CCAPnL.
             *
             * For now we just track the comparison; the actual pin output
             * will be visible through port reads. */

            /* Reload CCAPnL from CCAPnH on CL overflow */
            if (aCPU->mSFR[STC_REG_CL] == 0x00) {
                aCPU->mSFR[ccapl_reg] = aCPU->mSFR[ccaph_reg];
            }

            /* The PWM output is: CL < CCAPnL ? 0 : 1
             * (active low — matches STC12 convention) */
            (void)pwm_reg; /* EPCnL/EPCnH bits for 9-bit — future */
        }

        if ((ccapm & CCAPM_ECOM) && (ccapm & CCAPM_MAT)) {
            /* Software timer / high-speed output: compare */
            uint16_t compare = aCPU->mSFR[ccapl_reg] |
                               (aCPU->mSFR[ccaph_reg] << 8);
            if (counter == compare) {
                aCPU->mSFR[STC_REG_CCON] |= ccf_mask;
                if (ccapm & CCAPM_TOG) {
                    /* Toggle output — we'd toggle the CCPn pin here */
                }
            }
        }

        if ((ccapm & CCAPM_CAPP) || (ccapm & CCAPM_CAPN)) {
            /* Capture mode — triggered by external pin edge.
             * Not fully implemented (needs pin edge detection). */
        }
    }
}

/* ================================================================== *
 * Main tick — called once per oscillator clock in STC12 mode          *
 * ================================================================== */

void stc12_tick(struct em8051 *aCPU, struct stc12_state *aState)
{
    if (!aState->stc12_mode)
        return;

    aState->osc_clocks++;

    bool t0_overflowed = stc12_timer0_tick(aCPU, aState);
    stc12_timer1_tick(aCPU, aState);
    stc12_brt_tick(aCPU, aState);

    /* T0 overflow drives the PCA when CPS=10 */
    if (t0_overflowed)
        aState->pca_t0_overflow_pending = true;

    stc12_adc_tick(aCPU, aState);
    stc12_pca_tick(aCPU, aState);
}

/* ================================================================== *
 * Initialization                                                      *
 * ================================================================== */

void stc12_init(struct em8051 *aCPU, struct stc12_state *aState)
{
    /* Preserve board callbacks across init (they're set before first reset) */
    stc12_pin_callback         saved_pin    = aState->on_pin_change;
    stc12_read_pin_callback    saved_read   = aState->on_read_pin;
    stc12_read_analog_callback saved_analog = aState->on_read_analog;
    stc12_advance_callback     saved_adv    = aState->on_advance;
    void                      *saved_ud     = aState->board_user_data;

    g_stc = aState;

    memset(aState, 0, sizeof(*aState));
    aState->stc12_mode = true;
    aState->fosc = 11059200; /* default: 11.0592 MHz crystal */
    aState->vcc = 5.0;

    /* Restore board callbacks */
    aState->on_pin_change  = saved_pin;
    aState->on_read_pin    = saved_read;
    aState->on_read_analog = saved_analog;
    aState->on_advance     = saved_adv;
    aState->board_user_data = saved_ud;

    /* Pre-compute ns per clock (fixed-point * 16 for precision) */
    if (aState->fosc > 0)
        aState->ns_per_clock_x16 = (uint64_t)(16.0e9 / aState->fosc + 0.5);

    /* All external port pins default high (quasi-bidirectional pull-up) */
    for (int i = 0; i < 6; i++)
        aState->port_ext[i] = 0xFF;

    /* Pin drive shadows: all high at reset (ports default to 0xFF) */
    for (int i = 0; i < 6; i++)
        aState->pin_drive_shadow[i] = 0xFF;

    /* Set STC12 SFR reset values */
    aCPU->mSFR[STC_REG_P4] = 0xFF;
    aCPU->mSFR[STC_REG_P5] = 0xFF;
    aCPU->mSFR[STC_REG_AUXR] = 0x00;
    aCPU->mSFR[STC_REG_AUXR1] = 0x00;

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

    /* ADC read/write callbacks */
    aCPU->sfrread[STC_REG_ADC_CONTR] = sfr_read_adc_contr;
    aCPU->sfrwrite[STC_REG_ADC_CONTR] = sfr_write_adc_contr;
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
    if (aState->on_advance)
        aState->on_advance(stc12_get_time_ns(aState), aState->board_user_data);
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

bool stc12_is_valid_sfr(uint8_t addr)
{
    for (unsigned i = 0; i < sizeof(stc12_valid_sfr_set); i++) {
        if (stc12_valid_sfr_set[i] == addr) return true;
    }
    return false;
}

void stc12_set_part(struct stc12_state *aState, uint8_t part_id)
{
    aState->part_id = part_id;
}
