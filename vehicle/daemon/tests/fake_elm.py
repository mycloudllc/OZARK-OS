#!/usr/bin/env python3
"""A fake ELM327 on a pseudo-terminal, answering like a CAN car. Prints the
pty path, then serves until killed. For trying ozark-vehicled on a PC."""
import os, pty, select, sys, time

master, slave = pty.openpty()
print(os.ttyname(slave), flush=True)
cleared = False
start = time.time()

def answer(cmd):
    global cleared
    rpm = int((800 + 400 * ((time.time() - start) % 4)) * 4)
    if cmd in ("ATI", "ATWS"): return "ELM327 v1.5"
    if cmd == "ATDPN": return "A6"
    if cmd.startswith("AT"): return "OK"
    return {
        "0100": "SEARCHING...\r7E8 06 41 00 98 1A 80 00",
        "0101": "7E8 06 41 01 00 07 E5 00" if cleared else "7E8 06 41 01 82 07 E5 00",
        "010C": "7E8 04 41 0C %02X %02X" % (rpm >> 8, rpm & 0xFF),
        "010D": "7E8 03 41 0D 40",
        "0105": "7E8 03 41 05 80",
        "0104": "7E8 03 41 04 50",
        "010F": "7E8 03 41 0F 3C",
        "0111": "7E8 03 41 11 30",
        "03": "7E8 02 43 00" if cleared else "7E8 06 43 02 03 01 04 20",
        "07": "7E8 02 47 00",
        "0902": "7E8 10 14 49 02 01 31 47 31\r7E8 21 4A 43 35 34 34 34 52\r7E8 22 37 32 35 32 33 36 37",
    }.get(cmd, "NO DATA")

buf = b""
while True:
    select.select([master], [], [])
    buf += os.read(master, 256)
    while b"\r" in buf:
        line, buf = buf.split(b"\r", 1)
        cmd = line.decode(errors="replace").strip().upper()
        if not cmd:
            continue
        if cmd == "04":
            cleared = True
            out = "7E8 01 44"
        else:
            out = answer(cmd)
        os.write(master, (out + "\r\r>").encode())
