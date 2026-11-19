"""Drive the ESP32 Gadget serial console from the Windows side.

The panel has no readable bus (MISO is not wired), so the only honest way to
check what is on the glass is the firmware's own `dump` command, which renders
the framebuffer as ASCII. This script opens the CDC port, sends a list of
console commands and writes everything it hears to a file.

    python tools\\gadget_probe.py boot 4
    python tools\\gadget_probe.py status "dump 4" time
    python tools\\gadget_probe.py press 400 "dump 4"

Opening the port resets the board on this board revision (DTR/RTS are wired to
the reset circuit), so `boot` waits for the banner before sending anything.
"""

import sys
import time

import serial

PORT = "COM5"
BAUD = 115200
OUT = "tools/gadget_probe.log"


def open_port(retries=10):
    last = None
    for _ in range(retries):
        try:
            return serial.Serial(PORT, BAUD, timeout=0.2)
        except Exception as exc:  # the CDC re-enumerates while the board resets
            last = exc
            time.sleep(0.6)
    raise SystemExit("cannot open %s: %s" % (PORT, last))


def drain(port, silence_s, max_s, log=None):
    """Read until the line goes quiet for silence_s, or max_s elapses."""
    start = time.time()
    last_data = time.time()
    while True:
        chunk = port.read(4096)
        if chunk:
            text = chunk.decode("utf-8", "replace")
            if log is not None:
                log.write(text)
                log.flush()
            sys.stdout.write(text)
            sys.stdout.flush()
            last_data = time.time()
        now = time.time()
        if now - last_data > silence_s and now - start > 0.2:
            return
        if now - start > max_s:
            return


def main(argv):
    if len(argv) < 2:
        raise SystemExit(__doc__)

    first = argv[1]
    commands = argv[2:]

    # `@file` keeps JSON bodies away from any shell quoting: one command per
    # line, read verbatim. Lines starting with `#` are comments for the reader,
    # not firmware input: sending them made every annotated probe open with a
    # burst of `unknown: #` that looked like a broken console.
    if first.startswith("@"):
        with open(first[1:], "r", encoding="utf-8") as handle:
            commands = [
                line.rstrip("\n")
                for line in handle
                if line.strip() and not line.lstrip().startswith("#")
            ]
        first = "boot"

    log = open(OUT, "w", encoding="utf-8", newline="\n")
    port = open_port()
    port.reset_input_buffer()

    if first == "boot":
        # Opening the port asserts the modem lines, which resets this board, and
        # the firmware boots while the host waits for the banner. Do not touch
        # DTR/RTS afterwards: on this revision an RTS pulse with DTR deasserted
        # holds GPIO0 low and parks the chip in the USB download loader, where
        # the console is silent. That failure looked like a hung device.
        drain(port, silence_s=1.2, max_s=6.0, log=log)
    else:
        commands = [first] + commands
        drain(port, silence_s=0.6, max_s=1.5, log=log)

    for command in commands:
        # `sleep <ms>` is handled here, not by the firmware: the console has no
        # notion of waiting and some checks need time to pass on the device.
        if command.startswith("sleep "):
            time.sleep(int(command.split()[1]) / 1000.0)
            drain(port, silence_s=0.4, max_s=float(command.split()[1]) / 1000.0 + 2,
                  log=log)
            continue
        port.write((command + "\r\n").encode("ascii", "replace"))
        port.flush()
        time.sleep(0.15)
        drain(port, silence_s=0.8, max_s=8.0, log=log)

    port.close()
    log.close()
    print("\n[probe] log -> %s" % OUT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
