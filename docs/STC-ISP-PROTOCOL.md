# STC12 ISP Protocol Specification

Protocol facts for implementing a browser-based STC12 flash programmer.
Each fact cites its source: datasheet (DS), wire observation (WO), or
STC application note (AN). Facts from GPL sources are NOT cited; this
is a clean-room specification.

## 1. Entry condition (DS Ch. 13, p. 382)

The ISP bootloader runs ONLY after a **cold power-on reset**. A warm
reset (watchdog, software reset, reset pin) runs user code directly.
The PC application must be sending before the MCU is powered on.

Source: datasheet p. 382: "Must be cold-reset (power-on reset), MCU
will run from ISP monitor code, for any warm-reset (include reset-pin,
watchdog), MCU will run user code directly."

## 2. Physical connection (DS Ch. 13, p. 383-384)

- MCU RXD (P3.0) ↔ PC TXD (via RS-232 level shifter or USB-serial)
- MCU TXD (P3.1) ↔ PC RXD
- GND connected
- P1.0/P1.1 may need to be tied to GND depending on option bits

The ISP pins are the same as the UART1 pins (P3.0/P3.1). No special
programming pins exist; ISP uses the standard serial port.

Source: datasheet p. 383-384, circuit diagram and notes.

## 3. Handshake: the 0x7F pulse train (DS Ch. 13, p. 382)

The host sends 0x7F bytes continuously at a low baud rate (typically
2400 baud). On power-on, the bootloader detects this as a legitimate
ISP command and enters programming mode. If no valid command is
detected within tens to hundreds of milliseconds, the bootloader
jumps to user code.

The 0x7F byte is chosen because at any standard baud rate it produces
a recognisable pulse pattern on the RxD line (alternating high/low
bits) that the bootloader's baud-rate detection can measure.

Source: datasheet p. 382 flowchart ("Detect whether there is a
legitimate ISP command"), p. 385 ("Press the Download button in the
PC side application... then power on MCU").

## 4. Packet framing (WO)

Each packet has this structure:

    [START_HI] [START_LO] [DIR] [LEN_HI] [LEN_LO] [PAYLOAD...] [SUM_HI] [SUM_LO] [END]

| Field | Value | Notes |
|-------|-------|-------|
| START | 0x46, 0xB9 | Two-byte frame start marker |
| DIR | 0x6A (host→MCU) or 0x68 (MCU→host) | Direction byte |
| LEN | 16-bit big-endian | Total length including DIR, LEN, PAYLOAD, SUM, END |
| PAYLOAD | variable | Command-specific data |
| SUM | 16-bit big-endian | Sum of DIR + LEN bytes + PAYLOAD bytes, masked to 16 bits |
| END | 0x16 | Frame end marker |

The checksum is the arithmetic sum of all bytes from DIR through the
last PAYLOAD byte (inclusive), truncated to 16 bits.

Source: wire observation. The frame markers 0x46B9 and 0x16 and the
direction bytes 0x6A/0x68 are observable in any serial capture of an
STC ISP session.

## 5. Session flow (DS Ch. 13 + WO)

### Phase 1: Detection (at handshake baud, typically 2400)

1. Host opens serial port at handshake baud rate
2. Host sends 0x7F continuously
3. User applies power to the MCU
4. Bootloader sends a STATUS packet containing:
   - Eight 16-bit counter values (for baud-rate measurement)
   - BSL (bootloader) version
   - Part magic number (identifies the chip model)

### Phase 2: Baud negotiation (at handshake baud)

5. Host sends HANDSHAKE command (cmd 0x50) with the part's magic
6. Bootloader acknowledges (response 0x8F)
7. Host sends BAUD_SET command (cmd 0x8F) with BRT reload value,
   checksum, IAP delay, and delay parameters
8. Bootloader acknowledges (response 0x8F)
9. Host sends BAUD_SWITCH command (cmd 0x8E) confirming the new rate
10. Bootloader acknowledges (response 0x84) and switches baud rate

### Phase 3: Baud switch

11. Both sides switch to the transfer baud rate (typically 115200)
    Note: WebSerial cannot change baud on an open port — must close
    and reopen. Bytes can be lost across this gap.

### Phase 4: Erase (at transfer baud)

12. Host sends ERASE command (cmd 0x84) with block count and flash
    size information
13. Bootloader erases and acknowledges (response 0x00)

### Phase 5: Program (at transfer baud)

14. For each 128-byte block:
    - Host sends WRITE command with address and data
    - Bootloader writes and acknowledges (response 0x00)

### Phase 6: Finish

15. Host sends FINISH command (cmd 0x69) with part magic
16. Bootloader acknowledges (response 0x8D)
17. Host sends RESET command (cmd 0x82)
18. Bootloader resets to user code

## 6. Baud rate calculation (DS Ch. 8.1.6, p. 236)

The BRT (Baud Rate Timer) reload value for a target baud rate:

    BRT = 256 - round(FOSC / (baud × 16))

At FOSC = 11.0592 MHz:
- 115200 baud: BRT = 256 - round(11059200 / (115200 × 16)) = 256 - 6 = 250
- 2400 baud: BRT = 256 - round(11059200 / (2400 × 16)) = 256 - 288 (does not fit)

The host derives the MCU's clock frequency from the eight counter
values in the status packet:

    clockHz = (handshakeBaud × counter_avg × 12) / 7

Source: datasheet p. 236 (baud rate formula), counter derivation from
wire observation.

## 7. IAP wait states (DS Ch. 12, p. 365)

The IAP delay parameter in the baud-set command encodes the flash
write wait states, which depend on clock frequency:

| Clock range | Delay byte |
|-------------|-----------|
| < 1 MHz | 0x87 |
| 1-2 MHz | 0x86 |
| 2-3 MHz | 0x85 |
| 3-6 MHz | 0x84 |
| 6-12 MHz | 0x83 |
| 12-20 MHz | 0x82 |
| 20-24 MHz | 0x81 |
| > 24 MHz | 0x80 |

Source: datasheet Ch. 12 IAP timing tables.

## 8. Part identification

The bootloader's status packet contains a 16-bit magic number at
bytes 20-21 that identifies the chip model:

| Magic | Part |
|-------|------|
| 0xD17E | STC12C5A60S2 |
| 0xD168 | STC12C5A16S2 |

Source: wire observation (the magic appears in the status packet and
is echoed back in several commands).

## 9. What is NOT covered

- **STC15 protocol.** Different packet format, different commands,
  different baud negotiation. A separate specification.
- **STC89 protocol.** Yet another variant.
- **Option byte programming.** The erase/program sequence above does
  NOT write option bytes (clock source, watchdog, reset pin config).
  This is deliberate: an incorrect option byte can disable ISP and
  lock the user out of the part.
- **Encryption.** Some STC parts support encrypted downloads. Not
  covered.
- **Silicon verification.** This protocol spec is derived from
  datasheet descriptions and wire observations. It has NOT been
  verified against a real STC12 chip by this project. The existing
  implementation in the compiler repository has the same status
  (BENCH-FLASHING.md: "None of the three has ever programmed a
  real board").

## 10. Licence note

This specification documents PROTOCOL FACTS: byte values, packet
formats, and timing observable on a serial line. It was written
clean-room from the STC datasheet (public) and wire-level
observations. No GPL source code was copied or adapted.
