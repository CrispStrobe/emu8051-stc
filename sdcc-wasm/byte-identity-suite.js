/**
 * byte-identity-suite.js — compile multiple programs with both native
 * and WASM SDCC and compare the output.
 *
 * Each program exercises a different code path: interrupts, PCA, inline
 * assembly, larger code size, different data types.
 *
 * Usage: node byte-identity-suite.js <native-sdcc> <dist-dir> <installed-dir>
 */

const PROGRAMS = [
  {
    name: '01-minimal',
    desc: 'minimal (172 bytes, the original test)',
    source: `#include <8051.h>
void main(void) { P1 = 0xAA; while(1); }
`
  },
  {
    name: '02-blink-timer',
    desc: 'Timer 0 interrupt, bit-addressed SFR writes',
    source: `#include <8051.h>
volatile unsigned int ms;
void timer0_isr(void) __interrupt(1) {
    TL0 = 0x67; TH0 = 0xFC;
    ms++;
}
void main(void) {
    TMOD = 0x01; TL0 = 0x67; TH0 = 0xFC;
    ET0 = 1; EA = 1; TR0 = 1;
    while(1) {
        if (ms >= 500) { ms = 0; P1 ^= 0x01; }
    }
}
`
  },
  {
    name: '03-adc-poll',
    desc: 'ADC polling loop with bit manipulation',
    source: `#include <8051.h>
#include <stc12.h>
unsigned int adc_read(unsigned char ch) {
    ADC_CONTR = 0xE8 | ch;
    { unsigned char i; for(i=0;i<8;i++); }
    while(!(ADC_CONTR & 0x10));
    ADC_CONTR &= ~0x10;
    return ((unsigned int)ADC_RES << 2) | (ADC_RESL & 0x03);
}
void main(void) {
    P1ASF = 0x08; P1M1 |= 0x08; P1M0 &= ~0x08;
    while(1) {
        unsigned int v = adc_read(3);
        P0 = (unsigned char)(v >> 2);
    }
}
`
  },
  {
    name: '04-serial-echo',
    desc: 'UART init and echo loop',
    source: `#include <8051.h>
void uart_init(void) {
    SCON = 0x50; TMOD |= 0x20;
    TH1 = 0xFD; TR1 = 1;
}
void main(void) {
    uart_init(); EA = 1; ES = 1;
    while(1) {
        if (RI) { unsigned char c = SBUF; RI = 0; SBUF = c; while(!TI); TI = 0; }
    }
}
`
  },
  {
    name: '05-switch-case',
    desc: 'switch/case (jump table codegen)',
    source: `#include <8051.h>
void handle(unsigned char cmd) {
    switch(cmd) {
        case 0x01: P1 = 0x01; break;
        case 0x02: P1 = 0x02; break;
        case 0x03: P1 = 0x04; break;
        case 0x04: P1 = 0x08; break;
        case 0x10: P1 = 0x10; break;
        case 0x20: P1 = 0x20; break;
        case 0xFF: P1 = 0x80; break;
        default: P1 = 0x00; break;
    }
}
void main(void) {
    unsigned char i;
    for(i = 0; ; i++) { handle(i); }
}
`
  },
  {
    name: '06-long-arithmetic',
    desc: 'unsigned long multiply and divide',
    source: `#include <8051.h>
volatile unsigned long result;
void main(void) {
    unsigned long a = 11059200UL;
    unsigned long b = 12UL;
    unsigned long c = 1000UL;
    result = a / b / c;
    P1 = (unsigned char)(result & 0xFF);
    while(1);
}
`
  },
  {
    name: '07-array-init',
    desc: 'initialized array in code memory',
    source: `#include <8051.h>
__code unsigned char pattern[] = {
    0xFE, 0xFD, 0xFB, 0xF7, 0xEF, 0xDF, 0xBF, 0x7F,
    0x7F, 0xBF, 0xDF, 0xEF, 0xF7, 0xFB, 0xFD, 0xFE
};
void delay(void) { unsigned int i; for(i=0;i<10000;i++); }
void main(void) {
    unsigned char i = 0;
    while(1) { P1 = pattern[i]; delay(); i = (i+1) & 0x0F; }
}
`
  },
  {
    name: '08-function-ptr',
    desc: 'function pointer call (indirect jump)',
    source: `#include <8051.h>
void led_on(void) { P1 = 0x00; }
void led_off(void) { P1 = 0xFF; }
typedef void (*func_t)(void);
__code func_t table[] = { led_on, led_off };
void main(void) {
    unsigned char i = 0;
    while(1) { table[i](); i ^= 1; }
}
`
  },
  {
    name: '09-multi-interrupt',
    desc: 'two ISRs (Timer 0 + external INT0)',
    source: `#include <8051.h>
volatile unsigned char t0_count;
volatile unsigned char int0_count;
void timer0_isr(void) __interrupt(1) {
    TL0 = 0x00; TH0 = 0xFC;
    t0_count++;
}
void ext0_isr(void) __interrupt(0) {
    int0_count++;
}
void main(void) {
    TMOD = 0x01; TL0 = 0x00; TH0 = 0xFC;
    ET0 = 1; EX0 = 1; IT0 = 1; EA = 1; TR0 = 1;
    while(1) {
        P1 = t0_count;
        P2 = int0_count;
    }
}
`
  },
  {
    name: '10-struct-xdata',
    desc: 'struct in xdata, pointer arithmetic',
    source: `#include <8051.h>
typedef struct { unsigned int x; unsigned int y; unsigned char flags; } point_t;
__xdata point_t points[4];
void main(void) {
    unsigned char i;
    for(i = 0; i < 4; i++) {
        points[i].x = i * 100;
        points[i].y = i * 50;
        points[i].flags = 0x01 << i;
    }
    P1 = points[2].flags;
    while(1);
}
`
  },
];

module.exports = PROGRAMS;
