                                      1 ;--------------------------------------------------------
                                      2 ; File Created by SDCC : free open source ANSI-C Compiler
                                      3 ; Version 4.2.0 #13081 (Linux)
                                      4 ;--------------------------------------------------------
                                      5 	.module main
                                      6 	.optsdcc -mmcs51 --model-small
                                      7 	
                                      8 ;--------------------------------------------------------
                                      9 ; Public variables in this module
                                     10 ;--------------------------------------------------------
                                     11 	.globl _main
                                     12 	.globl _CCF0
                                     13 	.globl _CCF1
                                     14 	.globl _CR
                                     15 	.globl _CF
                                     16 	.globl _P5_3
                                     17 	.globl _P5_2
                                     18 	.globl _P5_1
                                     19 	.globl _P5_0
                                     20 	.globl _P4_7
                                     21 	.globl _P4_6
                                     22 	.globl _P4_5
                                     23 	.globl _P4_4
                                     24 	.globl _P4_3
                                     25 	.globl _P4_2
                                     26 	.globl _P4_1
                                     27 	.globl _P4_0
                                     28 	.globl _PADC
                                     29 	.globl _PLVD
                                     30 	.globl _PPCA
                                     31 	.globl _EADC
                                     32 	.globl _ELVD
                                     33 	.globl _CY
                                     34 	.globl _AC
                                     35 	.globl _F0
                                     36 	.globl _RS1
                                     37 	.globl _RS0
                                     38 	.globl _OV
                                     39 	.globl _F1
                                     40 	.globl _P
                                     41 	.globl _PS
                                     42 	.globl _PT1
                                     43 	.globl _PX1
                                     44 	.globl _PT0
                                     45 	.globl _PX0
                                     46 	.globl _RD
                                     47 	.globl _WR
                                     48 	.globl _T1
                                     49 	.globl _T0
                                     50 	.globl _INT1
                                     51 	.globl _INT0
                                     52 	.globl _TXD
                                     53 	.globl _RXD
                                     54 	.globl _P3_7
                                     55 	.globl _P3_6
                                     56 	.globl _P3_5
                                     57 	.globl _P3_4
                                     58 	.globl _P3_3
                                     59 	.globl _P3_2
                                     60 	.globl _P3_1
                                     61 	.globl _P3_0
                                     62 	.globl _EA
                                     63 	.globl _ES
                                     64 	.globl _ET1
                                     65 	.globl _EX1
                                     66 	.globl _ET0
                                     67 	.globl _EX0
                                     68 	.globl _P2_7
                                     69 	.globl _P2_6
                                     70 	.globl _P2_5
                                     71 	.globl _P2_4
                                     72 	.globl _P2_3
                                     73 	.globl _P2_2
                                     74 	.globl _P2_1
                                     75 	.globl _P2_0
                                     76 	.globl _SM0
                                     77 	.globl _SM1
                                     78 	.globl _SM2
                                     79 	.globl _REN
                                     80 	.globl _TB8
                                     81 	.globl _RB8
                                     82 	.globl _TI
                                     83 	.globl _RI
                                     84 	.globl _P1_7
                                     85 	.globl _P1_6
                                     86 	.globl _P1_5
                                     87 	.globl _P1_4
                                     88 	.globl _P1_3
                                     89 	.globl _P1_2
                                     90 	.globl _P1_1
                                     91 	.globl _P1_0
                                     92 	.globl _TF1
                                     93 	.globl _TR1
                                     94 	.globl _TF0
                                     95 	.globl _TR0
                                     96 	.globl _IE1
                                     97 	.globl _IT1
                                     98 	.globl _IE0
                                     99 	.globl _IT0
                                    100 	.globl _P0_7
                                    101 	.globl _P0_6
                                    102 	.globl _P0_5
                                    103 	.globl _P0_4
                                    104 	.globl _P0_3
                                    105 	.globl _P0_2
                                    106 	.globl _P0_1
                                    107 	.globl _P0_0
                                    108 	.globl _IAP_CONTR
                                    109 	.globl _IAP_TRIG
                                    110 	.globl _IAP_CMD
                                    111 	.globl _IAP_ADDRL
                                    112 	.globl _IAP_ADDRH
                                    113 	.globl _IAP_DATA
                                    114 	.globl _SPDAT
                                    115 	.globl _SPSTAT
                                    116 	.globl _SPCTL
                                    117 	.globl _ADC_RESL
                                    118 	.globl _ADC_RES
                                    119 	.globl _ADC_CONTR
                                    120 	.globl _P1ASF
                                    121 	.globl _PCA_PWM1
                                    122 	.globl _PCA_PWM0
                                    123 	.globl _CCAP1H
                                    124 	.globl _CCAP1L
                                    125 	.globl _CCAP0H
                                    126 	.globl _CCAP0L
                                    127 	.globl _CCAPM1
                                    128 	.globl _CCAPM0
                                    129 	.globl _CH
                                    130 	.globl _CL
                                    131 	.globl _CMOD
                                    132 	.globl _CCON
                                    133 	.globl _WDT_CONTR
                                    134 	.globl _BRT
                                    135 	.globl _S2BUF
                                    136 	.globl _S2CON
                                    137 	.globl _SADDR
                                    138 	.globl _SADEN
                                    139 	.globl _P5M1
                                    140 	.globl _P5M0
                                    141 	.globl _P4SW
                                    142 	.globl _P4M1
                                    143 	.globl _P4M0
                                    144 	.globl _P3M1
                                    145 	.globl _P3M0
                                    146 	.globl _P2M1
                                    147 	.globl _P2M0
                                    148 	.globl _P1M1
                                    149 	.globl _P1M0
                                    150 	.globl _P0M1
                                    151 	.globl _P0M0
                                    152 	.globl _P5
                                    153 	.globl _P4
                                    154 	.globl _IP2H
                                    155 	.globl _IP2
                                    156 	.globl _IPH
                                    157 	.globl _IE2
                                    158 	.globl _BUS_SPEED
                                    159 	.globl _CLK_DIV
                                    160 	.globl _WAKE_CLKO
                                    161 	.globl _AUXR1
                                    162 	.globl _AUXR
                                    163 	.globl _B
                                    164 	.globl _ACC
                                    165 	.globl _PSW
                                    166 	.globl _IP
                                    167 	.globl _P3
                                    168 	.globl _IE
                                    169 	.globl _P2
                                    170 	.globl _SBUF
                                    171 	.globl _SCON
                                    172 	.globl _P1
                                    173 	.globl _TH1
                                    174 	.globl _TH0
                                    175 	.globl _TL1
                                    176 	.globl _TL0
                                    177 	.globl _TMOD
                                    178 	.globl _TCON
                                    179 	.globl _PCON
                                    180 	.globl _DPH
                                    181 	.globl _DPL
                                    182 	.globl _SP
                                    183 	.globl _P0
                                    184 ;--------------------------------------------------------
                                    185 ; special function registers
                                    186 ;--------------------------------------------------------
                                    187 	.area RSEG    (ABS,DATA)
      000000                        188 	.org 0x0000
                           000080   189 _P0	=	0x0080
                           000081   190 _SP	=	0x0081
                           000082   191 _DPL	=	0x0082
                           000083   192 _DPH	=	0x0083
                           000087   193 _PCON	=	0x0087
                           000088   194 _TCON	=	0x0088
                           000089   195 _TMOD	=	0x0089
                           00008A   196 _TL0	=	0x008a
                           00008B   197 _TL1	=	0x008b
                           00008C   198 _TH0	=	0x008c
                           00008D   199 _TH1	=	0x008d
                           000090   200 _P1	=	0x0090
                           000098   201 _SCON	=	0x0098
                           000099   202 _SBUF	=	0x0099
                           0000A0   203 _P2	=	0x00a0
                           0000A8   204 _IE	=	0x00a8
                           0000B0   205 _P3	=	0x00b0
                           0000B8   206 _IP	=	0x00b8
                           0000D0   207 _PSW	=	0x00d0
                           0000E0   208 _ACC	=	0x00e0
                           0000F0   209 _B	=	0x00f0
                           00008E   210 _AUXR	=	0x008e
                           0000A2   211 _AUXR1	=	0x00a2
                           00008F   212 _WAKE_CLKO	=	0x008f
                           000097   213 _CLK_DIV	=	0x0097
                           0000A1   214 _BUS_SPEED	=	0x00a1
                           0000AF   215 _IE2	=	0x00af
                           0000B7   216 _IPH	=	0x00b7
                           0000B5   217 _IP2	=	0x00b5
                           0000B6   218 _IP2H	=	0x00b6
                           0000C0   219 _P4	=	0x00c0
                           0000C8   220 _P5	=	0x00c8
                           000094   221 _P0M0	=	0x0094
                           000093   222 _P0M1	=	0x0093
                           000092   223 _P1M0	=	0x0092
                           000091   224 _P1M1	=	0x0091
                           000096   225 _P2M0	=	0x0096
                           000095   226 _P2M1	=	0x0095
                           0000B2   227 _P3M0	=	0x00b2
                           0000B1   228 _P3M1	=	0x00b1
                           0000B4   229 _P4M0	=	0x00b4
                           0000B3   230 _P4M1	=	0x00b3
                           0000BB   231 _P4SW	=	0x00bb
                           0000CA   232 _P5M0	=	0x00ca
                           0000C9   233 _P5M1	=	0x00c9
                           0000B9   234 _SADEN	=	0x00b9
                           0000A9   235 _SADDR	=	0x00a9
                           00009A   236 _S2CON	=	0x009a
                           00009B   237 _S2BUF	=	0x009b
                           00009C   238 _BRT	=	0x009c
                           0000C1   239 _WDT_CONTR	=	0x00c1
                           0000D8   240 _CCON	=	0x00d8
                           0000D9   241 _CMOD	=	0x00d9
                           0000E9   242 _CL	=	0x00e9
                           0000F9   243 _CH	=	0x00f9
                           0000DA   244 _CCAPM0	=	0x00da
                           0000DB   245 _CCAPM1	=	0x00db
                           0000EA   246 _CCAP0L	=	0x00ea
                           0000FA   247 _CCAP0H	=	0x00fa
                           0000EB   248 _CCAP1L	=	0x00eb
                           0000FB   249 _CCAP1H	=	0x00fb
                           0000F2   250 _PCA_PWM0	=	0x00f2
                           0000F3   251 _PCA_PWM1	=	0x00f3
                           00009D   252 _P1ASF	=	0x009d
                           0000BC   253 _ADC_CONTR	=	0x00bc
                           0000BD   254 _ADC_RES	=	0x00bd
                           0000BE   255 _ADC_RESL	=	0x00be
                           0000CE   256 _SPCTL	=	0x00ce
                           0000CD   257 _SPSTAT	=	0x00cd
                           0000CF   258 _SPDAT	=	0x00cf
                           0000C2   259 _IAP_DATA	=	0x00c2
                           0000C3   260 _IAP_ADDRH	=	0x00c3
                           0000C4   261 _IAP_ADDRL	=	0x00c4
                           0000C5   262 _IAP_CMD	=	0x00c5
                           0000C6   263 _IAP_TRIG	=	0x00c6
                           0000C7   264 _IAP_CONTR	=	0x00c7
                                    265 ;--------------------------------------------------------
                                    266 ; special function bits
                                    267 ;--------------------------------------------------------
                                    268 	.area RSEG    (ABS,DATA)
      000000                        269 	.org 0x0000
                           000080   270 _P0_0	=	0x0080
                           000081   271 _P0_1	=	0x0081
                           000082   272 _P0_2	=	0x0082
                           000083   273 _P0_3	=	0x0083
                           000084   274 _P0_4	=	0x0084
                           000085   275 _P0_5	=	0x0085
                           000086   276 _P0_6	=	0x0086
                           000087   277 _P0_7	=	0x0087
                           000088   278 _IT0	=	0x0088
                           000089   279 _IE0	=	0x0089
                           00008A   280 _IT1	=	0x008a
                           00008B   281 _IE1	=	0x008b
                           00008C   282 _TR0	=	0x008c
                           00008D   283 _TF0	=	0x008d
                           00008E   284 _TR1	=	0x008e
                           00008F   285 _TF1	=	0x008f
                           000090   286 _P1_0	=	0x0090
                           000091   287 _P1_1	=	0x0091
                           000092   288 _P1_2	=	0x0092
                           000093   289 _P1_3	=	0x0093
                           000094   290 _P1_4	=	0x0094
                           000095   291 _P1_5	=	0x0095
                           000096   292 _P1_6	=	0x0096
                           000097   293 _P1_7	=	0x0097
                           000098   294 _RI	=	0x0098
                           000099   295 _TI	=	0x0099
                           00009A   296 _RB8	=	0x009a
                           00009B   297 _TB8	=	0x009b
                           00009C   298 _REN	=	0x009c
                           00009D   299 _SM2	=	0x009d
                           00009E   300 _SM1	=	0x009e
                           00009F   301 _SM0	=	0x009f
                           0000A0   302 _P2_0	=	0x00a0
                           0000A1   303 _P2_1	=	0x00a1
                           0000A2   304 _P2_2	=	0x00a2
                           0000A3   305 _P2_3	=	0x00a3
                           0000A4   306 _P2_4	=	0x00a4
                           0000A5   307 _P2_5	=	0x00a5
                           0000A6   308 _P2_6	=	0x00a6
                           0000A7   309 _P2_7	=	0x00a7
                           0000A8   310 _EX0	=	0x00a8
                           0000A9   311 _ET0	=	0x00a9
                           0000AA   312 _EX1	=	0x00aa
                           0000AB   313 _ET1	=	0x00ab
                           0000AC   314 _ES	=	0x00ac
                           0000AF   315 _EA	=	0x00af
                           0000B0   316 _P3_0	=	0x00b0
                           0000B1   317 _P3_1	=	0x00b1
                           0000B2   318 _P3_2	=	0x00b2
                           0000B3   319 _P3_3	=	0x00b3
                           0000B4   320 _P3_4	=	0x00b4
                           0000B5   321 _P3_5	=	0x00b5
                           0000B6   322 _P3_6	=	0x00b6
                           0000B7   323 _P3_7	=	0x00b7
                           0000B0   324 _RXD	=	0x00b0
                           0000B1   325 _TXD	=	0x00b1
                           0000B2   326 _INT0	=	0x00b2
                           0000B3   327 _INT1	=	0x00b3
                           0000B4   328 _T0	=	0x00b4
                           0000B5   329 _T1	=	0x00b5
                           0000B6   330 _WR	=	0x00b6
                           0000B7   331 _RD	=	0x00b7
                           0000B8   332 _PX0	=	0x00b8
                           0000B9   333 _PT0	=	0x00b9
                           0000BA   334 _PX1	=	0x00ba
                           0000BB   335 _PT1	=	0x00bb
                           0000BC   336 _PS	=	0x00bc
                           0000D0   337 _P	=	0x00d0
                           0000D1   338 _F1	=	0x00d1
                           0000D2   339 _OV	=	0x00d2
                           0000D3   340 _RS0	=	0x00d3
                           0000D4   341 _RS1	=	0x00d4
                           0000D5   342 _F0	=	0x00d5
                           0000D6   343 _AC	=	0x00d6
                           0000D7   344 _CY	=	0x00d7
                           0000AE   345 _ELVD	=	0x00ae
                           0000AD   346 _EADC	=	0x00ad
                           0000BF   347 _PPCA	=	0x00bf
                           0000BE   348 _PLVD	=	0x00be
                           0000BD   349 _PADC	=	0x00bd
                           0000C0   350 _P4_0	=	0x00c0
                           0000C1   351 _P4_1	=	0x00c1
                           0000C2   352 _P4_2	=	0x00c2
                           0000C3   353 _P4_3	=	0x00c3
                           0000C4   354 _P4_4	=	0x00c4
                           0000C5   355 _P4_5	=	0x00c5
                           0000C6   356 _P4_6	=	0x00c6
                           0000C7   357 _P4_7	=	0x00c7
                           0000C8   358 _P5_0	=	0x00c8
                           0000C9   359 _P5_1	=	0x00c9
                           0000CA   360 _P5_2	=	0x00ca
                           0000CB   361 _P5_3	=	0x00cb
                           0000DF   362 _CF	=	0x00df
                           0000DE   363 _CR	=	0x00de
                           0000D9   364 _CCF1	=	0x00d9
                           0000D8   365 _CCF0	=	0x00d8
                                    366 ;--------------------------------------------------------
                                    367 ; overlayable register banks
                                    368 ;--------------------------------------------------------
                                    369 	.area REG_BANK_0	(REL,OVR,DATA)
      000000                        370 	.ds 8
                                    371 ;--------------------------------------------------------
                                    372 ; internal ram data
                                    373 ;--------------------------------------------------------
                                    374 	.area DSEG    (DATA)
                                    375 ;--------------------------------------------------------
                                    376 ; overlayable items in internal ram
                                    377 ;--------------------------------------------------------
                                    378 	.area	OSEG    (OVR,DATA)
                                    379 ;--------------------------------------------------------
                                    380 ; Stack segment in internal ram
                                    381 ;--------------------------------------------------------
                                    382 	.area	SSEG
      000008                        383 __start__stack:
      000008                        384 	.ds	1
                                    385 
                                    386 ;--------------------------------------------------------
                                    387 ; indirectly addressable internal ram data
                                    388 ;--------------------------------------------------------
                                    389 	.area ISEG    (DATA)
                                    390 ;--------------------------------------------------------
                                    391 ; absolute internal ram data
                                    392 ;--------------------------------------------------------
                                    393 	.area IABS    (ABS,DATA)
                                    394 	.area IABS    (ABS,DATA)
                                    395 ;--------------------------------------------------------
                                    396 ; bit data
                                    397 ;--------------------------------------------------------
                                    398 	.area BSEG    (BIT)
                                    399 ;--------------------------------------------------------
                                    400 ; paged external ram data
                                    401 ;--------------------------------------------------------
                                    402 	.area PSEG    (PAG,XDATA)
                                    403 ;--------------------------------------------------------
                                    404 ; external ram data
                                    405 ;--------------------------------------------------------
                                    406 	.area XSEG    (XDATA)
                                    407 ;--------------------------------------------------------
                                    408 ; absolute external ram data
                                    409 ;--------------------------------------------------------
                                    410 	.area XABS    (ABS,XDATA)
                                    411 ;--------------------------------------------------------
                                    412 ; external initialized ram data
                                    413 ;--------------------------------------------------------
                                    414 	.area XISEG   (XDATA)
                                    415 	.area HOME    (CODE)
                                    416 	.area GSINIT0 (CODE)
                                    417 	.area GSINIT1 (CODE)
                                    418 	.area GSINIT2 (CODE)
                                    419 	.area GSINIT3 (CODE)
                                    420 	.area GSINIT4 (CODE)
                                    421 	.area GSINIT5 (CODE)
                                    422 	.area GSINIT  (CODE)
                                    423 	.area GSFINAL (CODE)
                                    424 	.area CSEG    (CODE)
                                    425 ;--------------------------------------------------------
                                    426 ; interrupt vector
                                    427 ;--------------------------------------------------------
                                    428 	.area HOME    (CODE)
      000000                        429 __interrupt_vect:
      000000 02 00 06         [24]  430 	ljmp	__sdcc_gsinit_startup
                                    431 ;--------------------------------------------------------
                                    432 ; global & static initialisations
                                    433 ;--------------------------------------------------------
                                    434 	.area HOME    (CODE)
                                    435 	.area GSINIT  (CODE)
                                    436 	.area GSFINAL (CODE)
                                    437 	.area GSINIT  (CODE)
                                    438 	.globl __sdcc_gsinit_startup
                                    439 	.globl __sdcc_program_startup
                                    440 	.globl __start__stack
                                    441 	.globl __mcs51_genXINIT
                                    442 	.globl __mcs51_genXRAMCLEAR
                                    443 	.globl __mcs51_genRAMCLEAR
                                    444 	.area GSFINAL (CODE)
      00005F 02 00 03         [24]  445 	ljmp	__sdcc_program_startup
                                    446 ;--------------------------------------------------------
                                    447 ; Home
                                    448 ;--------------------------------------------------------
                                    449 	.area HOME    (CODE)
                                    450 	.area HOME    (CODE)
      000003                        451 __sdcc_program_startup:
      000003 02 00 A2         [24]  452 	ljmp	_main
                                    453 ;	return from main will return to caller
                                    454 ;--------------------------------------------------------
                                    455 ; code
                                    456 ;--------------------------------------------------------
                                    457 	.area CSEG    (CODE)
                                    458 ;------------------------------------------------------------
                                    459 ;Allocation info for local variables in function 'board_init'
                                    460 ;------------------------------------------------------------
                                    461 ;	include/board.h:70: static void board_init(void)
                                    462 ;	-----------------------------------------
                                    463 ;	 function board_init
                                    464 ;	-----------------------------------------
      000062                        465 _board_init:
                           000007   466 	ar7 = 0x07
                           000006   467 	ar6 = 0x06
                           000005   468 	ar5 = 0x05
                           000004   469 	ar4 = 0x04
                           000003   470 	ar3 = 0x03
                           000002   471 	ar2 = 0x02
                           000001   472 	ar1 = 0x01
                           000000   473 	ar0 = 0x00
                                    474 ;	include/board.h:72: P1M1 &= ~LED_MASK;   /* M1 = 0 */
      000062 53 91 FC         [24]  475 	anl	_P1M1,#0xfc
                                    476 ;	include/board.h:73: P1M0 |=  LED_MASK;   /* M0 = 1  -> push-pull */
      000065 43 92 03         [24]  477 	orl	_P1M0,#0x03
                                    478 ;	include/board.h:75: LED1 = LED_OFF;
                                    479 ;	assignBit
      000068 D2 90            [12]  480 	setb	_P1_0
                                    481 ;	include/board.h:76: LED2 = LED_OFF;
                                    482 ;	assignBit
      00006A D2 91            [12]  483 	setb	_P1_1
                                    484 ;	include/board.h:77: }
      00006C 22               [24]  485 	ret
                                    486 ;------------------------------------------------------------
                                    487 ;Allocation info for local variables in function 'delay_init'
                                    488 ;------------------------------------------------------------
                                    489 ;	include/delay.h:23: static void delay_init(void)
                                    490 ;	-----------------------------------------
                                    491 ;	 function delay_init
                                    492 ;	-----------------------------------------
      00006D                        493 _delay_init:
                                    494 ;	include/delay.h:25: AUXR &= ~0x80;              /* T0x12 = 0 -> Timer 0 runs at FOSC/12 */
      00006D 53 8E 7F         [24]  495 	anl	_AUXR,#0x7f
                                    496 ;	include/delay.h:26: TMOD  = (TMOD & 0xF0) | 0x01;  /* Timer 0, mode 1: 16-bit */
      000070 E5 89            [12]  497 	mov	a,_TMOD
      000072 54 F0            [12]  498 	anl	a,#0xf0
      000074 44 01            [12]  499 	orl	a,#0x01
      000076 F5 89            [12]  500 	mov	_TMOD,a
                                    501 ;	include/delay.h:27: TR0   = 0;
                                    502 ;	assignBit
      000078 C2 8C            [12]  503 	clr	_TR0
                                    504 ;	include/delay.h:28: TF0   = 0;
                                    505 ;	assignBit
      00007A C2 8D            [12]  506 	clr	_TF0
                                    507 ;	include/delay.h:29: }
      00007C 22               [24]  508 	ret
                                    509 ;------------------------------------------------------------
                                    510 ;Allocation info for local variables in function 'delay_ms'
                                    511 ;------------------------------------------------------------
                                    512 ;ms                        Allocated to registers 
                                    513 ;------------------------------------------------------------
                                    514 ;	include/delay.h:31: static void delay_ms(unsigned int ms)
                                    515 ;	-----------------------------------------
                                    516 ;	 function delay_ms
                                    517 ;	-----------------------------------------
      00007D                        518 _delay_ms:
      00007D AE 82            [24]  519 	mov	r6,dpl
      00007F AF 83            [24]  520 	mov	r7,dph
                                    521 ;	include/delay.h:33: while (ms--) {
      000081                        522 00104$:
      000081 8E 04            [24]  523 	mov	ar4,r6
      000083 8F 05            [24]  524 	mov	ar5,r7
      000085 1E               [12]  525 	dec	r6
      000086 BE FF 01         [24]  526 	cjne	r6,#0xff,00126$
      000089 1F               [12]  527 	dec	r7
      00008A                        528 00126$:
      00008A EC               [12]  529 	mov	a,r4
      00008B 4D               [12]  530 	orl	a,r5
      00008C 60 13            [24]  531 	jz	00107$
                                    532 ;	include/delay.h:34: TL0 = (unsigned char)(T0_RELOAD & 0xFF);
      00008E 75 8A 67         [24]  533 	mov	_TL0,#0x67
                                    534 ;	include/delay.h:35: TH0 = (unsigned char)((T0_RELOAD >> 8) & 0xFF);
      000091 75 8C FC         [24]  535 	mov	_TH0,#0xfc
                                    536 ;	include/delay.h:36: TF0 = 0;
                                    537 ;	assignBit
      000094 C2 8D            [12]  538 	clr	_TF0
                                    539 ;	include/delay.h:37: TR0 = 1;
                                    540 ;	assignBit
      000096 D2 8C            [12]  541 	setb	_TR0
                                    542 ;	include/delay.h:38: while (!TF0)
      000098                        543 00101$:
      000098 30 8D FD         [24]  544 	jnb	_TF0,00101$
                                    545 ;	include/delay.h:40: TR0 = 0;
                                    546 ;	assignBit
      00009B C2 8C            [12]  547 	clr	_TR0
                                    548 ;	include/delay.h:41: TF0 = 0;
                                    549 ;	assignBit
      00009D C2 8D            [12]  550 	clr	_TF0
      00009F 80 E0            [24]  551 	sjmp	00104$
      0000A1                        552 00107$:
                                    553 ;	include/delay.h:43: }
      0000A1 22               [24]  554 	ret
                                    555 ;------------------------------------------------------------
                                    556 ;Allocation info for local variables in function 'main'
                                    557 ;------------------------------------------------------------
                                    558 ;i                         Allocated to registers r7 
                                    559 ;------------------------------------------------------------
                                    560 ;	src/01-blink/main.c:17: void main(void)
                                    561 ;	-----------------------------------------
                                    562 ;	 function main
                                    563 ;	-----------------------------------------
      0000A2                        564 _main:
                                    565 ;	src/01-blink/main.c:21: board_init();
      0000A2 12 00 62         [24]  566 	lcall	_board_init
                                    567 ;	src/01-blink/main.c:22: delay_init();
      0000A5 12 00 6D         [24]  568 	lcall	_delay_init
                                    569 ;	src/01-blink/main.c:26: for (i = 0; i < 6; i++) {
      0000A8                        570 00112$:
      0000A8 7F 00            [12]  571 	mov	r7,#0x00
      0000AA                        572 00104$:
                                    573 ;	src/01-blink/main.c:27: LED1 = LED_ON;
                                    574 ;	assignBit
      0000AA C2 90            [12]  575 	clr	_P1_0
                                    576 ;	src/01-blink/main.c:28: LED2 = LED_OFF;
                                    577 ;	assignBit
      0000AC D2 91            [12]  578 	setb	_P1_1
                                    579 ;	src/01-blink/main.c:29: delay_ms(150);
      0000AE 90 00 96         [24]  580 	mov	dptr,#0x0096
      0000B1 C0 07            [24]  581 	push	ar7
      0000B3 12 00 7D         [24]  582 	lcall	_delay_ms
                                    583 ;	src/01-blink/main.c:31: LED1 = LED_OFF;
                                    584 ;	assignBit
      0000B6 D2 90            [12]  585 	setb	_P1_0
                                    586 ;	src/01-blink/main.c:32: LED2 = LED_ON;
                                    587 ;	assignBit
      0000B8 C2 91            [12]  588 	clr	_P1_1
                                    589 ;	src/01-blink/main.c:33: delay_ms(150);
      0000BA 90 00 96         [24]  590 	mov	dptr,#0x0096
      0000BD 12 00 7D         [24]  591 	lcall	_delay_ms
      0000C0 D0 07            [24]  592 	pop	ar7
                                    593 ;	src/01-blink/main.c:26: for (i = 0; i < 6; i++) {
      0000C2 0F               [12]  594 	inc	r7
      0000C3 BF 06 00         [24]  595 	cjne	r7,#0x06,00134$
      0000C6                        596 00134$:
      0000C6 40 E2            [24]  597 	jc	00104$
                                    598 ;	src/01-blink/main.c:37: for (i = 0; i < 2; i++) {
      0000C8 7F 00            [12]  599 	mov	r7,#0x00
      0000CA                        600 00106$:
                                    601 ;	src/01-blink/main.c:38: LED1 = LED_ON;
                                    602 ;	assignBit
      0000CA C2 90            [12]  603 	clr	_P1_0
                                    604 ;	src/01-blink/main.c:39: LED2 = LED_ON;
                                    605 ;	assignBit
      0000CC C2 91            [12]  606 	clr	_P1_1
                                    607 ;	src/01-blink/main.c:40: delay_ms(1000);
      0000CE 90 03 E8         [24]  608 	mov	dptr,#0x03e8
      0000D1 C0 07            [24]  609 	push	ar7
      0000D3 12 00 7D         [24]  610 	lcall	_delay_ms
                                    611 ;	src/01-blink/main.c:42: LED1 = LED_OFF;
                                    612 ;	assignBit
      0000D6 D2 90            [12]  613 	setb	_P1_0
                                    614 ;	src/01-blink/main.c:43: LED2 = LED_OFF;
                                    615 ;	assignBit
      0000D8 D2 91            [12]  616 	setb	_P1_1
                                    617 ;	src/01-blink/main.c:44: delay_ms(1000);
      0000DA 90 03 E8         [24]  618 	mov	dptr,#0x03e8
      0000DD 12 00 7D         [24]  619 	lcall	_delay_ms
      0000E0 D0 07            [24]  620 	pop	ar7
                                    621 ;	src/01-blink/main.c:37: for (i = 0; i < 2; i++) {
      0000E2 0F               [12]  622 	inc	r7
      0000E3 BF 02 00         [24]  623 	cjne	r7,#0x02,00136$
      0000E6                        624 00136$:
      0000E6 40 E2            [24]  625 	jc	00106$
                                    626 ;	src/01-blink/main.c:47: }
      0000E8 80 BE            [24]  627 	sjmp	00112$
                                    628 	.area CSEG    (CODE)
                                    629 	.area CONST   (CODE)
                                    630 	.area XINIT   (CODE)
                                    631 	.area CABS    (ABS,CODE)
