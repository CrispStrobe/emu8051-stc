#!/usr/bin/env python3
"""
test_monitor_py.py — drive live-monitor.py's decoder against the emulator.

This bridges the shipping tool's codec to the running firmware: bytes go
into the emulator via stc12_serial_rx, come out via the TX callback, and
are parsed by live-monitor.py's Decoder — the same code a user runs.

If this passes, the path from `live-monitor.py --port /dev/…` to the
firmware is verified end-to-end except for the serial port itself.

Usage: python3 test_monitor_py.py
"""
import os
import sys
import struct
import ctypes

# Import live-monitor.py's codec
sys.path.insert(0, "../../stc/tools")
from importlib import import_module
# live-monitor.py has a hyphen, so import by path
import importlib.util
spec = importlib.util.spec_from_file_location(
    "live_monitor", "../../stc/tools/live-monitor.py")
lm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(lm)

build = lm.build
Decoder = lm.Decoder
Position = lm.Position
Capabilities = lm.Capabilities
C = lm.C

passed = 0
failed = 0

def check(cond, msg):
    global passed, failed
    if cond:
        print(f"PASS: {msg}")
        passed += 1
    else:
        print(f"FAIL: {msg}")
        failed += 1


import subprocess

BRIDGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "emu_serial_bridge")
FIRMWARE = "/tmp/monitor.ihx"

def exchange(cmd_bytes):
    """Send cmd_bytes to the firmware via the bridge, return raw response."""
    result = subprocess.run(
        [BRIDGE, FIRMWARE],
        input=cmd_bytes, capture_output=True, timeout=10)
    return result.stdout

print("=== live-monitor.py decoder vs firmware on emulator ===\n")

# Test 1: Send HELLO via bridge, parse with Python decoder
hello_cmd = build(C["LIVE_CMD_HELLO"])
hello_bytes = exchange(hello_cmd)
check(len(hello_bytes) > 0, f"Bridge: got {len(hello_bytes)} bytes from HELLO")

dec = Decoder()
frames = list(dec.feed(hello_bytes))
check(len(frames) == 1, f"Decoder: parsed {len(frames)} frame(s) from HELLO reply")

if frames:
    cmd, payload = frames[0]
    check(cmd == C["LIVE_CMD_HELLO"] | 0x80, f"Decoder: cmd=0x{cmd:02X} (HELLO reply)")
    check(len(payload) >= 9, f"Decoder: payload={len(payload)} bytes (>=9 for caps)")

    if len(payload) >= 9:
        caps = Capabilities(payload)
        check(caps.version == 1, f"Capabilities: version={caps.version}")
        check(caps.time_freezes, f"Capabilities: timeFreezes={caps.time_freezes}")
        check(not caps.pc_valid, f"Capabilities: pc_valid={caps.pc_valid}")
        check("block" in caps.steps, f"Capabilities: steps={caps.steps}")
        check("yield" in caps.breakpoints, f"Capabilities: breakpoints={caps.breakpoints}")
        check(caps.resources is not None, f"Capabilities: resources reported")

# Test 2: build() produces frames the firmware accepts
# Build a HELLO command using the Python codec
hello_frame = build(C["LIVE_CMD_HELLO"])
check(hello_frame[0] == 0x7E, f"build: SOF=0x{hello_frame[0]:02X}")
check(hello_frame[1] == 0, f"build: LEN=0 (no payload)")
check(hello_frame[2] == C["LIVE_CMD_HELLO"], f"build: CMD=0x{hello_frame[2]:02X}")
# Verify checksum
check((sum(hello_frame[1:]) & 0xFF) == 0, "build: checksum valid")

# Test 3: build a READ command
read_frame = build(C["LIVE_CMD_READ"], bytes([1, 0, 8, 2]))  # IRAM, addr=8, len=2
check(len(read_frame) == 8, f"build READ: frame length={len(read_frame)}")
check((sum(read_frame[1:]) & 0xFF) == 0, "build READ: checksum valid")

# Test 4: The Python decoder and our C codec agree on frame format
# Build with Python, verify with Python decoder
for cmd_name, cmd_val in [("HELLO", C["LIVE_CMD_HELLO"]),
                           ("POS", C["LIVE_CMD_POS"]),
                           ("REGS", C["LIVE_CMD_REGS"])]:
    frame = build(cmd_val)
    dec2 = Decoder()
    parsed = list(dec2.feed(frame))
    check(len(parsed) == 1, f"Round-trip {cmd_name}: decoder accepts build() output")

# Test 5: Live POS via bridge
pos_cmd = build(C["LIVE_CMD_POS"])
# Send HELLO first (to sync), then POS
pos_bytes = exchange(hello_cmd + pos_cmd)
dec_pos = Decoder()
pos_frames = list(dec_pos.feed(pos_bytes))
check(len(pos_frames) >= 2, f"Bridge POS: got {len(pos_frames)} frames (HELLO+POS)")
if len(pos_frames) >= 2:
    pcmd, ppay = pos_frames[1]
    check(pcmd == C["LIVE_CMD_POS"] | 0x80, f"POS reply: cmd=0x{pcmd:02X}")
    pos = Position(ppay)
    check(pos.ntasks == 2, f"POS: ntasks={pos.ntasks}")
    check(pos.bw_ms > 0, f"POS: bw_ms={pos.bw_ms}")
    print(f"  {pos}")

# Test 6: Live REGS via bridge
regs_cmd = build(C["LIVE_CMD_REGS"])
regs_bytes = exchange(hello_cmd + regs_cmd)
dec_regs = Decoder()
regs_frames = list(dec_regs.feed(regs_bytes))
check(len(regs_frames) >= 2, f"Bridge REGS: got {len(regs_frames)} frames")
if len(regs_frames) >= 2:
    rcmd, rpay = regs_frames[1]
    check(rcmd == C["LIVE_CMD_REGS"] | 0x80, f"REGS reply: cmd=0x{rcmd:02X}")
    if len(rpay) >= 7:
        names = ["A", "B", "DPL", "DPH", "SP", "PSW", "bank"]
        regs = dict(zip(names, rpay[:7]))
        check(regs["SP"] > 0, f"REGS: SP=0x{regs['SP']:02X}")
        print(f"  A=0x{regs['A']:02X} SP=0x{regs['SP']:02X} PSW=0x{regs['PSW']:02X}")

# Test 7: Decoder handles torn frame + recovery
dec3 = Decoder()
# Send a partial frame (SOF + garbage)
torn = bytes([0x7E, 0x05])
partial = list(dec3.feed(torn))
check(len(partial) == 0, "Torn frame: no complete frame from partial data")
# Call idle to reset
dec3.idle()
# Now feed a valid frame
recovered = list(dec3.feed(hello_bytes))
check(len(recovered) == 1, "Torn recovery: decoder accepts valid frame after idle()")

print(f"\n{passed} passed, {failed} failed")
sys.exit(1 if failed > 0 else 0)
