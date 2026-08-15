# Bench session: first silicon flash of stc-flash.js

Step-by-step guide for flashing a real STC12C5A60S2 chip from the
browser using `stc-flash/demo.html`. This is the milestone that
converts the mock-peer test result (internal consistency) into a
silicon-verified claim.

## What you need

| Item | Notes |
|------|-------|
| STC12C5A60S2 board | HC6800-ES V2.0, or any board with the chip |
| USB-TTL adapter | CH340, CP2102, FT232, or PL2303. Must be 5V-tolerant or have a 5V/3.3V jumper |
| Wiring | TXD→RxD (P3.0), RXD→TxD (P3.1), GND→GND |
| Power switch | The board must be power-cyclable without unplugging USB |
| Chrome or Edge | WebSerial requires a Chromium browser |
| A `.hex` file | Start with `test_images/01-blink.hex` (172 bytes) |

## Wiring

```
USB-TTL adapter          STC12 board
  TXD  ──────────────→  P3.0 (RxD, pin 10 on PDIP-40)
  RXD  ←──────────────  P3.1 (TxD, pin 11)
  GND  ──────────────→  GND  (pin 20)
  5V   ──────────────→  VCC  (pin 40)  ← OR use the board's own supply
```

**Do NOT connect the adapter's 5V if the board has its own power
supply.** The adapter's 5V is only for boards without one.

P1.0 and P1.1 do NOT need to be grounded unless the chip was
previously programmed with "Next program code, P1.0/P1.1 need = 0/0".
The factory default does not require them.

## Step by step

### 1. Serve the demo page

The demo page must be served over HTTPS or localhost (WebSerial
requires a secure context). Options:

```bash
# From the repo root:
python3 -m http.server 8080 -d stc-flash
# Then open http://localhost:8080/demo.html
```

Or deploy to GitHub Pages (the repo's `stc-flash/` directory).

### 2. Load the hex file

Click "Choose File" and select `test_images/01-blink.hex`, or paste
its contents into the text area. The page should show "172 bytes".

### 3. Click Flash

The browser will prompt for a serial port. Select the USB-TTL adapter.

The log will show:
```
waiting for bootloader — pull the power and reapply it
```

### 4. Power-cycle the board

**This is the critical step.** The ISP bootloader runs ONLY after a
cold power-on. A reset button will NOT work.

- If the board has a power switch: turn it off, wait 1 second, turn
  it back on.
- If powering from the adapter's 5V: unplug the VCC wire, wait 1
  second, reconnect it.
- Do NOT unplug the USB adapter itself — only interrupt power to the
  MCU.

### 5. Watch the log

A successful session looks like:
```
bootloader: magic 0xd17e, 11.052 MHz, BSL 7.2
negotiating baud…
switched to 115200 baud
part: STC12C5A60S2, 61440 bytes flash
erasing 2 blocks…
  wrote 128 bytes at 0x0000
  wrote 128 bytes at 0x0080
  wrote 128 bytes at 0x0100
  wrote 128 bytes at 0x0180
done: 172 bytes (padded to 512)
```

### 6. Verify

The LED on P1.0 should blink. If it does, the flash worked.

**Then change something and flash again.** A board still running
yesterday's program looks exactly like one that was just programmed.
Change the blink interval (use a different hex) and confirm the
behaviour changes.

## If it fails

| Symptom | Likely cause |
|---------|-------------|
| "no bootloader greeting" | Power cycle was not cold enough, or wiring is wrong. Try: unplug VCC for 3 seconds. Check TXD/RXD are crossed (not straight). |
| Port selection shows nothing | The USB-TTL adapter's driver is not installed. Check Device Manager / `ls /dev/tty*`. |
| "handshake refused" | The chip answered but rejected the handshake. May be an STC15 or STC89 (different protocol). Check the magic number in the log. |
| "baud switch refused" | The chip could not switch to 115200. Try a lower transfer baud (not yet configurable in the demo — edit `transferBaud` in the source). |
| Handshake OK, then timeout after "negotiating baud" | The baud switch (close/reopen port) lost bytes. This is the most fragile step. Try again; if it fails consistently, it is a WebSerial timing issue specific to this adapter. |
| "write refused at 0xNNN" | Flash write failed. Power supply may be insufficient (flash erase/write draws current spikes). Try a shorter USB cable or a powered hub. |
| LED does not blink after "done" | The hex may be for a different board (check pin assignments). Or the chip did not reset — power-cycle manually. |

## What this proves and what it does not

**A successful flash proves:** the STC12 ISP protocol implementation
speaks correctly to a real bootloader, the packet framing and checksums
are right, the baud negotiation works across a real USB-TTL adapter,
and the image is written and runs.

**It does NOT prove:** that every hex file works (test with the largest
program available), that the baud switch is reliable across all
adapters (try at least two), or that option bytes are preserved
(they should be, since we do not write them).

## Recording the result

Record in the session notes:
- Adapter model (CH340 / CP2102 / etc.)
- Board (HC6800-ES V2.0 / bare chip / etc.)
- Browser and version
- The log output verbatim
- Whether the LED blinked (and changed on re-flash)

This is the evidence that converts the mock-peer test result
(category 2, internal consistency) into a silicon-verified claim.
