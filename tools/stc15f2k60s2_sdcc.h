/*
 * STC15F2K60S2 header for SDCC — master translation of all Keil-style
 * STC15 headers found in the rainbowpeee corpus (stc15fxxxx.h and
 * stc15f2k60s2.h variants, deduplicated).
 *
 * Drop-in replacement for:
 *   #include <STC15F2K60S2.H>    (Keil system header)
 *   #include "stc15fxxxx.h"      (local Keil header)
 *   #include "stc15f2k60s2.h"    (local Keil header)
 *
 * Place in SDCC include path or project directory.
 */

#ifndef __STC15F2K60S2_SDCC_H__
#define __STC15F2K60S2_SDCC_H__

/* ---- Core registers ---------------------------------------------------- */

__sfr __at(0xE0) ACC;
__sfr __at(0xF0) B;
__sfr __at(0xD0) PSW;
__sfr __at(0x81) SP;
__sfr __at(0x82) DPL;
__sfr __at(0x83) DPH;
__sfr __at(0x87) PCON;

/* ---- PSW bits ---------------------------------------------------------- */

__sbit __at(0xD7) CY;
__sbit __at(0xD6) AC;
__sbit __at(0xD5) F0;
__sbit __at(0xD4) RS1;
__sbit __at(0xD3) RS0;
__sbit __at(0xD2) OV;
__sbit __at(0xD1) F1;
__sbit __at(0xD0) P;

/* ---- ACC bits ---------------------------------------------------------- */

__sbit __at(0xE0) ACC0;
__sbit __at(0xE1) ACC1;
__sbit __at(0xE2) ACC2;
__sbit __at(0xE3) ACC3;
__sbit __at(0xE4) ACC4;
__sbit __at(0xE5) ACC5;
__sbit __at(0xE6) ACC6;
__sbit __at(0xE7) ACC7;

/* ---- B bits ------------------------------------------------------------ */

__sbit __at(0xF0) B0;
__sbit __at(0xF1) B1;
__sbit __at(0xF2) B2;
__sbit __at(0xF3) B3;
__sbit __at(0xF4) B4;
__sbit __at(0xF5) B5;
__sbit __at(0xF6) B6;
__sbit __at(0xF7) B7;

/* ---- Port 0 ------------------------------------------------------------ */

__sfr __at(0x80) P0;
__sbit __at(0x80) P00;
__sbit __at(0x81) P01;
__sbit __at(0x82) P02;
__sbit __at(0x83) P03;
__sbit __at(0x84) P04;
__sbit __at(0x85) P05;
__sbit __at(0x86) P06;
__sbit __at(0x87) P07;
__sfr __at(0x93) P0M1;
__sfr __at(0x94) P0M0;

/* ---- Port 1 ------------------------------------------------------------ */

__sfr __at(0x90) P1;
__sbit __at(0x90) P10;
__sbit __at(0x91) P11;
__sbit __at(0x92) P12;
__sbit __at(0x93) P13;
__sbit __at(0x94) P14;
__sbit __at(0x95) P15;
__sbit __at(0x96) P16;
__sbit __at(0x97) P17;
__sfr __at(0x91) P1M1;
__sfr __at(0x92) P1M0;
/* Alternate function aliases */
__sbit __at(0x90) RXD2;
__sbit __at(0x91) TXD2;
__sbit __at(0x90) CCP1;
__sbit __at(0x91) CCP0;
__sbit __at(0x92) SPI_SS;
__sbit __at(0x93) SPI_MOSI;
__sbit __at(0x94) SPI_MISO;
__sbit __at(0x95) SPI_SCLK;

/* ---- Port 2 ------------------------------------------------------------ */

__sfr __at(0xA0) P2;
__sbit __at(0xA0) P20;
__sbit __at(0xA1) P21;
__sbit __at(0xA2) P22;
__sbit __at(0xA3) P23;
__sbit __at(0xA4) P24;
__sbit __at(0xA5) P25;
__sbit __at(0xA6) P26;
__sbit __at(0xA7) P27;
__sfr __at(0x95) P2M1;
__sfr __at(0x96) P2M0;

/* ---- Port 3 ------------------------------------------------------------ */

__sfr __at(0xB0) P3;
__sbit __at(0xB0) P30;
__sbit __at(0xB1) P31;
__sbit __at(0xB2) P32;
__sbit __at(0xB3) P33;
__sbit __at(0xB4) P34;
__sbit __at(0xB5) P35;
__sbit __at(0xB6) P36;
__sbit __at(0xB7) P37;
__sfr __at(0xB1) P3M1;
__sfr __at(0xB2) P3M0;
/* Alternate function aliases */
__sbit __at(0xB0) RXD;
__sbit __at(0xB1) TXD;
__sbit __at(0xB2) INT0;
__sbit __at(0xB3) INT1;
__sbit __at(0xB4) T0;
__sbit __at(0xB5) T1;
__sbit __at(0xB6) WR;
__sbit __at(0xB7) RD;
__sbit __at(0xB7) CCP2;
__sbit __at(0xB5) CLKOUT0;
__sbit __at(0xB4) CLKOUT1;

/* ---- Port 4 ------------------------------------------------------------ */

__sfr __at(0xC0) P4;
__sbit __at(0xC0) P40;
__sbit __at(0xC1) P41;
__sbit __at(0xC2) P42;
__sbit __at(0xC3) P43;
__sbit __at(0xC4) P44;
__sbit __at(0xC5) P45;
__sbit __at(0xC6) P46;
__sbit __at(0xC7) P47;
__sfr __at(0xB3) P4M1;
__sfr __at(0xB4) P4M0;

/* ---- Port 5 ------------------------------------------------------------ */

__sfr __at(0xC8) P5;
__sbit __at(0xC8) P50;
__sbit __at(0xC9) P51;
__sbit __at(0xCA) P52;
__sbit __at(0xCB) P53;
__sbit __at(0xCC) P54;
__sbit __at(0xCD) P55;
__sbit __at(0xCE) P56;
__sbit __at(0xCF) P57;
__sfr __at(0xC9) P5M1;
__sfr __at(0xCA) P5M0;

/* ---- Port 6 ------------------------------------------------------------ */

__sfr __at(0xE8) P6;
__sbit __at(0xE8) P60;
__sbit __at(0xE9) P61;
__sbit __at(0xEA) P62;
__sbit __at(0xEB) P63;
__sbit __at(0xEC) P64;
__sbit __at(0xED) P65;
__sbit __at(0xEE) P66;
__sbit __at(0xEF) P67;
__sfr __at(0xCB) P6M1;
__sfr __at(0xCC) P6M0;

/* ---- Port 7 ------------------------------------------------------------ */

__sfr __at(0xF8) P7;
__sbit __at(0xF8) P70;
__sbit __at(0xF9) P71;
__sbit __at(0xFA) P72;
__sbit __at(0xFB) P73;
__sbit __at(0xFC) P74;
__sbit __at(0xFD) P75;
__sbit __at(0xFE) P76;
__sbit __at(0xFF) P77;
__sfr __at(0xE1) P7M1;
__sfr __at(0xE2) P7M0;

/* ---- Timers 0/1 -------------------------------------------------------- */

__sfr __at(0x88) TCON;
__sbit __at(0x8F) TF1;
__sbit __at(0x8E) TR1;
__sbit __at(0x8D) TF0;
__sbit __at(0x8C) TR0;
__sbit __at(0x8B) IE1;
__sbit __at(0x8A) IT1;
__sbit __at(0x89) IE0;
__sbit __at(0x88) IT0;
__sfr __at(0x89) TMOD;
__sfr __at(0x8A) TL0;
__sfr __at(0x8B) TL1;
__sfr __at(0x8C) TH0;
__sfr __at(0x8D) TH1;

/* ---- Timers 2/3/4 ------------------------------------------------------ */

__sfr __at(0xD1) T4T3M;
#define T3T4M T4T3M
__sfr __at(0xD2) T4H;
__sfr __at(0xD3) T4L;
__sfr __at(0xD4) T3H;
__sfr __at(0xD5) T3L;
__sfr __at(0xD6) T2H;
__sfr __at(0xD7) T2L;
/* Keil aliases */
#define TH4     T4H
#define TL4     T4L
#define TH3     T3H
#define TL3     T3L
#define TH2     T2H
#define TL2     T2L
#define RL_TH0  TH0
#define RL_TL0  TL0
#define RL_TH1  TH1
#define RL_TL1  TL1
#define RL_T4H  T4H
#define RL_T4L  T4L
#define RL_T3H  T3H
#define RL_T3L  T3L
#define RL_T2H  T2H
#define RL_T2L  T2L

/* ---- Auxiliary --------------------------------------------------------- */

__sfr __at(0x8E) AUXR;
__sfr __at(0xA2) AUXR1;
#define P_SW1 AUXR1
__sfr __at(0x8F) INT_CLKO;
#define WAKE_CLKO INT_CLKO
#define AUXR2     INT_CLKO
__sfr __at(0x97) CLK_DIV;
#define PCON2 CLK_DIV
__sfr __at(0xA1) BUS_SPEED;
__sfr __at(0x9D) P1ASF;
__sfr __at(0xBA) P_SW2;

/* ---- Interrupts -------------------------------------------------------- */

__sfr __at(0xA8) IE;
__sbit __at(0xAF) EA;
__sbit __at(0xAE) ELVD;
__sbit __at(0xAD) EADC;
__sbit __at(0xAC) ES;
__sbit __at(0xAB) ET1;
__sbit __at(0xAA) EX1;
__sbit __at(0xA9) ET0;
__sbit __at(0xA8) EX0;
__sfr __at(0xAF) IE2;
__sfr __at(0xB8) IP;
__sbit __at(0xBF) PPCA;
__sbit __at(0xBE) PLVD;
__sbit __at(0xBD) PADC;
__sbit __at(0xBC) PS;
__sbit __at(0xBB) PT1;
__sbit __at(0xBA) PX1;
__sbit __at(0xB9) PT0;
__sbit __at(0xB8) PX0;
__sfr __at(0xB5) IP2;
__sfr __at(0xB6) IPH2;
__sfr __at(0xB7) IPH;

/* ---- Serial ------------------------------------------------------------ */

__sfr __at(0x98) SCON;
__sbit __at(0x9F) SM0;
__sbit __at(0x9E) SM1;
__sbit __at(0x9D) SM2;
__sbit __at(0x9C) REN;
__sbit __at(0x9B) TB8;
__sbit __at(0x9A) RB8;
__sbit __at(0x99) TI;
__sbit __at(0x98) RI;
__sfr __at(0x99) SBUF;
__sfr __at(0x9A) S2CON;
__sfr __at(0x9B) S2BUF;
__sfr __at(0xAC) S3CON;
__sfr __at(0xAD) S3BUF;
__sfr __at(0x84) S4CON;
__sfr __at(0x85) S4BUF;
__sfr __at(0xA9) SADDR;
__sfr __at(0xB9) SADEN;

/* ---- ADC --------------------------------------------------------------- */

__sfr __at(0xBC) ADC_CONTR;
__sfr __at(0xBD) ADC_RES;
__sfr __at(0xBE) ADC_RESL;

/* ---- SPI --------------------------------------------------------------- */

__sfr __at(0xCD) SPSTAT;
__sfr __at(0xCE) SPCTL;
__sfr __at(0xCF) SPDAT;

/* ---- IAP/ISP ----------------------------------------------------------- */

__sfr __at(0xC2) IAP_DATA;
__sfr __at(0xC3) IAP_ADDRH;
__sfr __at(0xC4) IAP_ADDRL;
__sfr __at(0xC5) IAP_CMD;
__sfr __at(0xC6) IAP_TRIG;
__sfr __at(0xC7) IAP_CONTR;
#define ISP_DATA   IAP_DATA
#define ISP_ADDRH  IAP_ADDRH
#define ISP_ADDRL  IAP_ADDRL
#define ISP_CMD    IAP_CMD
#define ISP_TRIG   IAP_TRIG
#define ISP_CONTR  IAP_CONTR

/* ---- PCA --------------------------------------------------------------- */

__sfr __at(0xD8) CCON;
__sbit __at(0xDF) CF;
__sbit __at(0xDE) CR;
__sbit __at(0xDA) CCF2;
__sbit __at(0xD9) CCF1;
__sbit __at(0xD8) CCF0;
__sfr __at(0xD9) CMOD;
__sfr __at(0xDA) CCAPM0;
__sfr __at(0xDB) CCAPM1;
__sfr __at(0xDC) CCAPM2;
__sfr __at(0xE9) CL;
__sfr __at(0xF9) CH;
__sfr __at(0xEA) CCAP0L;
__sfr __at(0xEB) CCAP1L;
__sfr __at(0xEC) CCAP2L;
__sfr __at(0xFA) CCAP0H;
__sfr __at(0xFB) CCAP1H;
__sfr __at(0xFC) CCAP2H;
__sfr __at(0xF2) PCA_PWM0;
__sfr __at(0xF3) PCA_PWM1;
__sfr __at(0xF4) PCA_PWM2;

/* ---- Comparator -------------------------------------------------------- */

__sfr __at(0xE6) CMPCR1;
__sfr __at(0xE7) CMPCR2;

/* ---- Enhanced PWM ------------------------------------------------------ */

__sfr __at(0xF1) PWMCFG;
__sfr __at(0xF5) PWMCR;
__sfr __at(0xF6) PWMIF;
__sfr __at(0xF7) PWMFDCR;

/* xdata-mapped PWM channel registers */
#define PWMC        (*(unsigned int  volatile __xdata *)0xFFF0)
#define PWMCH       (*(unsigned char volatile __xdata *)0xFFF0)
#define PWMCL       (*(unsigned char volatile __xdata *)0xFFF1)
#define PWMCKS      (*(unsigned char volatile __xdata *)0xFFF2)
#define PWM2T1      (*(unsigned int  volatile __xdata *)0xFF00)
#define PWM2T1H     (*(unsigned char volatile __xdata *)0xFF00)
#define PWM2T1L     (*(unsigned char volatile __xdata *)0xFF01)
#define PWM2T2      (*(unsigned int  volatile __xdata *)0xFF02)
#define PWM2T2H     (*(unsigned char volatile __xdata *)0xFF02)
#define PWM2T2L     (*(unsigned char volatile __xdata *)0xFF03)
#define PWM2CR      (*(unsigned char volatile __xdata *)0xFF04)
#define PWM3T1      (*(unsigned int  volatile __xdata *)0xFF10)
#define PWM3T1H     (*(unsigned char volatile __xdata *)0xFF10)
#define PWM3T1L     (*(unsigned char volatile __xdata *)0xFF11)
#define PWM3T2      (*(unsigned int  volatile __xdata *)0xFF12)
#define PWM3T2H     (*(unsigned char volatile __xdata *)0xFF12)
#define PWM3T2L     (*(unsigned char volatile __xdata *)0xFF13)
#define PWM3CR      (*(unsigned char volatile __xdata *)0xFF14)
#define PWM4T1      (*(unsigned int  volatile __xdata *)0xFF20)
#define PWM4T1H     (*(unsigned char volatile __xdata *)0xFF20)
#define PWM4T1L     (*(unsigned char volatile __xdata *)0xFF21)
#define PWM4T2      (*(unsigned int  volatile __xdata *)0xFF22)
#define PWM4T2H     (*(unsigned char volatile __xdata *)0xFF22)
#define PWM4T2L     (*(unsigned char volatile __xdata *)0xFF23)
#define PWM4CR      (*(unsigned char volatile __xdata *)0xFF24)
#define PWM5T1      (*(unsigned int  volatile __xdata *)0xFF30)
#define PWM5T1H     (*(unsigned char volatile __xdata *)0xFF30)
#define PWM5T1L     (*(unsigned char volatile __xdata *)0xFF31)
#define PWM5T2      (*(unsigned int  volatile __xdata *)0xFF32)
#define PWM5T2H     (*(unsigned char volatile __xdata *)0xFF32)
#define PWM5T2L     (*(unsigned char volatile __xdata *)0xFF33)
#define PWM5CR      (*(unsigned char volatile __xdata *)0xFF34)
#define PWM6T1      (*(unsigned int  volatile __xdata *)0xFF40)
#define PWM6T1H     (*(unsigned char volatile __xdata *)0xFF40)
#define PWM6T1L     (*(unsigned char volatile __xdata *)0xFF41)
#define PWM6T2      (*(unsigned int  volatile __xdata *)0xFF42)
#define PWM6T2H     (*(unsigned char volatile __xdata *)0xFF42)
#define PWM6T2L     (*(unsigned char volatile __xdata *)0xFF43)
#define PWM6CR      (*(unsigned char volatile __xdata *)0xFF44)
#define PWM7T1      (*(unsigned int  volatile __xdata *)0xFF50)
#define PWM7T1H     (*(unsigned char volatile __xdata *)0xFF50)
#define PWM7T1L     (*(unsigned char volatile __xdata *)0xFF51)
#define PWM7T2      (*(unsigned int  volatile __xdata *)0xFF52)
#define PWM7T2H     (*(unsigned char volatile __xdata *)0xFF52)
#define PWM7T2L     (*(unsigned char volatile __xdata *)0xFF53)
#define PWM7CR      (*(unsigned char volatile __xdata *)0xFF54)

/* ---- Watchdog / Wake --------------------------------------------------- */

__sfr __at(0xC1) WDT_CONTR;
__sfr __at(0xAA) WKTCL;
__sfr __at(0xAB) WKTCH;

#endif /* __STC15F2K60S2_SDCC_H__ */
