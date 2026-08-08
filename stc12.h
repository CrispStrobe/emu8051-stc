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
 * stc12.h
 * STC12C5A60S2 SFR addresses, bit masks, and peripheral state.
 *
 * Register addresses cross-checked against SDCC's mcs51/stc12.h and
 * the official STC12C5A60S2 datasheet (2011-07-15).
 */

#ifndef STC12_H
#define STC12_H

#include "emu8051.h"

/* ------------------------------------------------------------------ *
 * SFR addresses (as offsets into mSFR[128], i.e. addr - 0x80)        *
 * Only registers NOT already defined in emu8051.h.                    *
 * ------------------------------------------------------------------ */

/* Auxiliary registers */
#define STC_REG_AUXR     (0x8E - 0x80)
#define STC_REG_AUXR1    (0xA2 - 0x80)
#define STC_REG_CLK_DIV  (0x97 - 0x80)

/* Port mode registers */
#define STC_REG_P1M1     (0x91 - 0x80)
#define STC_REG_P1M0     (0x92 - 0x80)
#define STC_REG_P0M1     (0x93 - 0x80)
#define STC_REG_P0M0     (0x94 - 0x80)
#define STC_REG_P2M1     (0x95 - 0x80)
#define STC_REG_P2M0     (0x96 - 0x80)
#define STC_REG_P3M1     (0xB1 - 0x80)
#define STC_REG_P3M0     (0xB2 - 0x80)
#define STC_REG_P4M1     (0xB3 - 0x80)
#define STC_REG_P4M0     (0xB4 - 0x80)
#define STC_REG_P5M1     (0xC9 - 0x80)
#define STC_REG_P5M0     (0xCA - 0x80)

/* Port 4/5 data */
#define STC_REG_P4       (0xC0 - 0x80)
#define STC_REG_P5       (0xC8 - 0x80)
#define STC_REG_P4SW     (0xBB - 0x80)

/* ADC */
#define STC_REG_ADC_CONTR (0xBC - 0x80)
#define STC_REG_ADC_RES   (0xBD - 0x80)
#define STC_REG_ADC_RESL  (0xBE - 0x80)
#define STC_REG_P1ASF     (0x9D - 0x80)

/* PCA */
#define STC_REG_CCON     (0xD8 - 0x80)
#define STC_REG_CMOD     (0xD9 - 0x80)
#define STC_REG_CCAPM0   (0xDA - 0x80)
#define STC_REG_CCAPM1   (0xDB - 0x80)
#define STC_REG_CL       (0xE9 - 0x80)
#define STC_REG_CH       (0xF9 - 0x80)
#define STC_REG_CCAP0L   (0xEA - 0x80)
#define STC_REG_CCAP0H   (0xFA - 0x80)
#define STC_REG_CCAP1L   (0xEB - 0x80)
#define STC_REG_CCAP1H   (0xFB - 0x80)
#define STC_REG_PCA_PWM0 (0xF2 - 0x80)
#define STC_REG_PCA_PWM1 (0xF3 - 0x80)

/* SPI (stub) */
#define STC_REG_SPCTL    (0x85 - 0x80)
#define STC_REG_SPDAT    (0x86 - 0x80)
#define STC_REG_SPSTAT   (0xCE - 0x80)

/* UART2 (stub) */
#define STC_REG_S2CON    (0x9A - 0x80)
#define STC_REG_S2BUF    (0x9B - 0x80)

/* BRT — baud rate timer */
#define STC_REG_BRT      (0x9C - 0x80)

/* Watchdog (stub) */
#define STC_REG_WDT_CONTR (0xC1 - 0x80)

/* Interrupt priority */
#define STC_REG_IPH      (0xB7 - 0x80)
#define STC_REG_IP2H     (0xB6 - 0x80)
#define STC_REG_SADDR    (0xA9 - 0x80)
#define STC_REG_SADEN    (0xB9 - 0x80)

/* ------------------------------------------------------------------ *
 * AUXR bit masks (0x8E)                                               *
 * Bit layout: T0x12 T1x12 UART_M0x6 BRTR S2SMOD BRTx12 EXTRAM S1BRS *
 * ------------------------------------------------------------------ */
#define AUXR_T0x12      0x80
#define AUXR_T1x12      0x40
#define AUXR_UART_M0x6  0x20
#define AUXR_BRTR       0x10
#define AUXR_S2SMOD     0x08
#define AUXR_BRTx12     0x04
#define AUXR_EXTRAM     0x02
#define AUXR_S1BRS      0x01

/* ------------------------------------------------------------------ *
 * AUXR1 bit masks (0xA2)                                              *
 * Bit layout: - PCA_P4 SPI_P4 S2_P4 GF2 ADRJ - DPS                  *
 * ------------------------------------------------------------------ */
#define AUXR1_PCA_P4    0x40
#define AUXR1_SPI_P4    0x20
#define AUXR1_S2_P4     0x10
#define AUXR1_GF2       0x08
#define AUXR1_ADRJ      0x04
#define AUXR1_DPS       0x01

/* STC15 delta: ADRJ moves from AUXR1 bit 2 to CLK_DIV (0x97) bit 5.
 * STC15-PERIPHERAL-MODEL.md §2.1 */
#define STC15_CLK_DIV_ADRJ  0x20  /* bit 5 of CLK_DIV (0x97) */

/* STC15 Timer 2 registers */
#define STC_REG_T2H      (0xD6 - 0x80)
#define STC_REG_T2L      (0xD7 - 0x80)

/* STC15 INT_CLKO (was WAKE_CLKO on STC12) */
#define STC_REG_INT_CLKO (0x8F - 0x80)

/* ------------------------------------------------------------------ *
 * ADC_CONTR bit masks (0xBC)                                          *
 * Bit layout: ADC_POWER SPEED1 SPEED0 ADC_FLAG ADC_START CHS2 CHS1 CHS0 *
 * ------------------------------------------------------------------ */
#define ADC_POWER       0x80
#define ADC_SPEED1      0x40
#define ADC_SPEED0      0x20
#define ADC_FLAG         0x10
#define ADC_START        0x08
#define ADC_CHS2         0x04
#define ADC_CHS1         0x02
#define ADC_CHS0         0x01
#define ADC_CHS_MASK    (ADC_CHS2 | ADC_CHS1 | ADC_CHS0)
#define ADC_SPEED_MASK  (ADC_SPEED1 | ADC_SPEED0)

/* ADC conversion times in oscillator clocks (datasheet §10.5):
 * SPEED1:0 = 00 -> 420 clocks
 *            01 -> 280
 *            10 -> 140
 *            11 -> 70                                               */
#define ADC_CLOCKS_SPEED0  420
#define ADC_CLOCKS_SPEED1  280
#define ADC_CLOCKS_SPEED2  140
#define ADC_CLOCKS_SPEED3   70

/* ------------------------------------------------------------------ *
 * CCON bit masks (0xD8) — PCA control                                 *
 * Bit layout: CF CR - - - - CCF1 CCF0                                 *
 * ------------------------------------------------------------------ */
#define CCON_CF          0x80
#define CCON_CR          0x40
#define CCON_CCF1        0x02
#define CCON_CCF0        0x01

/* ------------------------------------------------------------------ *
 * CMOD bit masks (0xD9) — PCA mode                                    *
 * Bit layout (STC12-PERIPHERAL-MODEL.md §5.1, confirmed against        *
 * datasheet §10):                                                      *
 *   Bit 7: CIDL, Bits 6-4: unused, Bit 3: CPS2, Bit 2: CPS1,         *
 *   Bit 1: CPS0, Bit 0: ECF                                           *
 * Three CPS bits → 8 clock sources (§5.2).                             *
 * ------------------------------------------------------------------ */
#define CMOD_CIDL        0x80
#define CMOD_CPS2_BIT    0x08  /* bit 3 */
#define CMOD_CPS1_BIT    0x04  /* bit 2 */
#define CMOD_CPS0_BIT    0x02  /* bit 1 */
#define CMOD_CPS_MASK   (CMOD_CPS2_BIT | CMOD_CPS1_BIT | CMOD_CPS0_BIT)
#define CMOD_ECF         0x01  /* bit 0 — PCA counter overflow interrupt enable */

/* PCA counter clock sources (CPS2:1:0 in bits 3:1, extract with >> 1):
 * 0 = SYSclk/12
 * 1 = SYSclk/2
 * 2 = Timer 0 overflow
 * 3 = ECI pin (P1.2 / P3.4 depending on PCA_P4)
 * 4 = SYSclk
 * 5 = SYSclk/4
 * 6 = SYSclk/6
 * 7 = SYSclk/8 */

/* ------------------------------------------------------------------ *
 * CCAPMn bit masks — PCA module mode (0xDA, 0xDB)                     *
 * Bit layout: - ECOM0 CAPP0 CAPN0 MAT0 TOG0 PWM0 ECCF0              *
 * ------------------------------------------------------------------ */
#define CCAPM_ECOM       0x40
#define CCAPM_CAPP       0x20
#define CCAPM_CAPN       0x10
#define CCAPM_MAT        0x08
#define CCAPM_TOG        0x04
#define CCAPM_PWM        0x02
#define CCAPM_ECCF       0x01

/* ------------------------------------------------------------------ *
 * ISR vectors for STC12 extras                                        *
 * ------------------------------------------------------------------ */
#define ISR_ADC          0x2B   /* ADC interrupt vector */
#define ISR_PCA          0x33   /* PCA interrupt vector */
/* Note: on STC12, the standard vectors are the same as 8052:
 * 0x03 INT0, 0x0B T0, 0x13 INT1, 0x1B T1, 0x23 UART1,
 * 0x2B ADC (replaces T2 on generic 8052), 0x33 PCA */

/* ------------------------------------------------------------------ *
 * Boundary A — the pin bus (MCU ⇄ board)                              *
 *                                                                     *
 * Conforms to simulation-contract.md boundary A. The MCU owns time    *
 * and pushes pin changes; the board is passive and only answers.      *
 * ------------------------------------------------------------------ */

/* Pin mode, matching the contract's PinMode type */
enum stc12_pin_mode {
    PIN_QUASI    = 0,   /* quasi-bidirectional (reset default) */
    PIN_PUSHPULL = 1,   /* push-pull */
    PIN_INPUT    = 2,   /* input-only (high impedance) */
    PIN_OPENDRAIN = 3   /* open-drain */
};

/* Callback: the MCU changed what it does to a pin.
 * port = 0..5 (P0..P5), bit = 0..7,
 * mode = pin mode, drive_high = latch value. */
typedef void (*stc12_pin_callback)(int port, int bit,
                                   enum stc12_pin_mode mode,
                                   bool drive_high,
                                   void *user_data);

/* Callback: the MCU wants to read a digital pin level from the board.
 * Returns 0 or 1. */
typedef int (*stc12_read_pin_callback)(int port, int bit,
                                       void *user_data);

/* Callback: the MCU wants to read an analog voltage from the board.
 * Returns volts (0.0 .. VCC). The MCU converts to counts itself. */
typedef double (*stc12_read_analog_callback)(int port, int bit,
                                             void *user_data);

/* Callback: time has advanced. t_ns = nanoseconds since reset. */
typedef void (*stc12_advance_callback)(uint64_t t_ns, void *user_data);

/* Callback: a byte was transmitted on the serial port. */
typedef void (*stc12_serial_tx_callback)(uint8_t byte, void *user_data);

/* ------------------------------------------------------------------ *
 * STC12 peripheral state — extends em8051                             *
 * ------------------------------------------------------------------ */
struct stc12_state
{
    /* Timer prescaler: in 12T mode, timers tick once per 12 oscillator
     * clocks. In 1T mode (AUXR.7 for T0, AUXR.6 for T1), they tick
     * every oscillator clock. The upstream tick() is called once per
     * machine cycle. In STC12 1T mode, 1 machine cycle = 1 osc clock.
     * We track a sub-tick counter for the 12T case. */
    uint8_t timer0_prescaler;   /* counts 0..11, ticks timer at 0 */
    uint8_t timer1_prescaler;
    uint8_t brt_prescaler;      /* for the independent baud rate timer */

    /* ADC state */
    uint16_t adc_countdown;     /* osc clocks remaining until conversion done */
    uint16_t adc_input[8];      /* external analog values, 0-1023 (10-bit) */

    /* PCA state */
    uint8_t pca_prescaler;      /* for FOSC/12 and FOSC/2 sources */
    bool    pca_t0_overflow_pending; /* latched T0 overflow for PCA clock */

    /* Port external input state (active-low: 0xFF = all high) */
    uint8_t port_ext[6];        /* P0..P5 external pin state */

    /* Configuration */
    bool     stc12_mode;        /* true = STC12 model, false = classic 8051 */
    uint32_t fosc;              /* oscillator frequency in Hz */
    double   vcc;               /* supply voltage, default 5.0 */

    /* Pin history ring buffer for waveform rendering */
    #define PIN_HISTORY_SIZE 4096
    struct stc12_pin_event {
        uint64_t t_ns;
        uint8_t port;
        uint8_t bit;
        uint8_t mode;
        uint8_t drive;
    } *pin_history;              /* allocated on first use */
    uint32_t pin_history_head;   /* next write position */
    uint32_t pin_history_count;  /* total events written */

    /* Stage 0: part identity.
     * 0 = STC12C5A60S2, 1 = STC15F2K60S2. */
    #define PART_STC12  0
    #define PART_STC15  1
    uint8_t  part_id;
    /* Count of accesses to SFRs not modelled for this part */
    uint32_t unmodelled_sfr_accesses;

    /* Boundary A callbacks (all optional — NULL = legacy mode) */
    stc12_pin_callback         on_pin_change;
    stc12_read_pin_callback    on_read_pin;
    stc12_read_analog_callback on_read_analog;
    stc12_advance_callback     on_advance;
    stc12_serial_tx_callback   on_serial_tx;
    void                      *board_user_data;

    /* Time tracking */
    uint64_t osc_clocks;        /* total oscillator clocks since reset */
    uint64_t ns_per_clock_x16;  /* (1e9 / fosc) * 16, fixed-point */

    /* Shadow of last-emitted pin state, for change detection */
    uint8_t pin_m1_shadow[6];    /* last PxM1 value emitted per port */
    uint8_t pin_m0_shadow[6];    /* last PxM0 value emitted per port */
    uint8_t pin_drive_shadow[6]; /* last latch value emitted per port */
};

/* ------------------------------------------------------------------ *
 * Public API                                                          *
 * ------------------------------------------------------------------ */

/* Initialize STC12 peripheral state and install SFR callbacks.
 * Call after reset(). */
void stc12_init(struct em8051 *aCPU, struct stc12_state *aState);

/* Tick STC12 peripherals. Call once per oscillator clock (i.e. once
 * per call to the upstream tick() when in 1T mode). */
void stc12_tick(struct em8051 *aCPU, struct stc12_state *aState);

/* Set an external analog input value for ADC channel 0-7 (0-1023). */
void stc12_set_adc_input(struct stc12_state *aState, int channel, uint16_t value);

/* Set external pin input for a port (0-5 = P0-P5).
 * Legacy API — use boundary A callbacks for new integrations. */
void stc12_set_port_input(struct stc12_state *aState, int port, uint8_t value);

/* Stage 0: check if an SFR address is valid for the declared part. */
bool stc12_is_valid_sfr(uint8_t addr);

/* Set the part identity. Call before stc12_init or after reset. */
void stc12_set_part(struct stc12_state *aState, uint8_t part_id);

/* Serial port: write a byte into the receive buffer (simulates RX).
 * Sets RI in SCON and copies the byte to SBUF for the firmware to read. */
void stc12_serial_rx(struct em8051 *aCPU, struct stc12_state *aState, uint8_t byte);

/* Set the serial TX callback. */
void stc12_set_serial_callback(struct stc12_state *aState,
                                stc12_serial_tx_callback cb, void *user_data);

/* ------------------------------------------------------------------ *
 * Boundary A API                                                      *
 * ------------------------------------------------------------------ */

/* Register boundary A callbacks. All are optional (NULL = disabled).
 * user_data is passed through to every callback. */
void stc12_set_board_callbacks(struct stc12_state *aState,
                               stc12_pin_callback on_pin,
                               stc12_read_pin_callback on_read,
                               stc12_read_analog_callback on_analog,
                               stc12_advance_callback on_advance,
                               void *user_data);

/* Run until the MCU's clock reaches target_ns (nanoseconds since reset).
 * Calls on_advance at the end. Returns number of instructions executed. */
int stc12_advance_to(struct em8051 *aCPU, struct stc12_state *aState,
                     uint64_t target_ns);

/* Get current MCU time in nanoseconds since reset. */
uint64_t stc12_get_time_ns(struct stc12_state *aState);

#endif /* STC12_H */
