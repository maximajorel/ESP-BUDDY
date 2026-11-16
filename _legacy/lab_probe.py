"""Talk to the particle_lab firmware over its USB CDC serial port.

Opening the port resets the chip, so the script waits for the banner before
sending anything. Each argument is either a command to send or a `wait:<s>`
pause; everything the board prints is echoed to stdout.

    python lab_probe.py COM5 s d wait:2 "g -1 0" wait:2 d
"""

import sys
import time

import serial


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2

    port = argv[1]
    actions = argv[2:]

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()

    def drain(seconds):
        end = time.time() + seconds
        chunks = []
        while time.time() < end:
            data = ser.read(4096)
            if data:
                chunks.append(data)
        return b"".join(chunks).decode("utf-8", "replace")

    # Opening the port resets the chip, and a USB CDC port silently drops
    # anything written before the firmware starts reading. Wait for the boot
    # banner instead of guessing a delay.
    banner = ""
    deadline = time.time() + 15
    while time.time() < deadline:
        banner += drain(0.3)
        if "commands:" in banner:
            break
    sys.stdout.write(banner)
    if "commands:" not in banner:
        sys.stdout.write("\n[warn] boot banner not seen; commands may be dropped]\n")

    for action in actions:
        if action.startswith("wait:"):
            sys.stdout.write(drain(float(action[5:])))
            continue
        ser.write((action + "\n").encode())
        ser.flush()
        sys.stdout.write(drain(0.6))

    ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
