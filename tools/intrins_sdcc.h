/*
 * intrins.h stub for SDCC — replaces Keil's <intrins.h>.
 * Provides _nop_(), _crol_(), _cror_(), _push_(), _pop_().
 */
#ifndef __INTRINS_SDCC_H__
#define __INTRINS_SDCC_H__

/* _nop_() — single NOP cycle */
#define _nop_() __asm__("nop")

/* _crol_ / _cror_ — rotate left/right through carry */
static inline unsigned char _crol_(unsigned char val, unsigned char n) {
    while (n--) val = (val << 1) | (val >> 7);
    return val;
}
static inline unsigned char _cror_(unsigned char val, unsigned char n) {
    while (n--) val = (val >> 1) | (val << 7);
    return val;
}

/* _push_ / _pop_ — not meaningfully portable, stub as no-ops */
#define _push_(sfr_addr) /* stub */
#define _pop_(sfr_addr)  /* stub */

#endif /* __INTRINS_SDCC_H__ */
