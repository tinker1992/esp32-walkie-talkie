#!/usr/bin/env python
"""Background-friendly serial log capture (pio device monitor needs a TTY).

Usage: python scripts/serial_capture.py COM6 90
Note: opening the port toggles DTR/RTS and resets the board, so you also get
the full boot log. Run one process per board for dual-unit timeline tests.
"""
import sys
import time

import serial

port = sys.argv[1]
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0

ser = serial.Serial(port, 115200, timeout=0.25)
print(f"[capture] opened {port} @115200 for {dur}s", flush=True)
t0 = time.time()
while time.time() - t0 < dur:
    data = ser.read(4096)
    if data:
        sys.stdout.write(data.decode("utf-8", "replace"))
        sys.stdout.flush()
ser.close()
print("\n[capture] done", flush=True)
