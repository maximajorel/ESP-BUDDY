"""Log the board's serial output to a file, optionally stopping at a pattern.

Useful while waiting for an event that only a human can trigger (joining the
access point, pressing a button): the log is flushed line by line, so another
process can read it live instead of waiting for this one to finish.

    python tools/serial_log.py COM5 tools/serial.log --until "[ws] client"
"""

import argparse
import sys
import time

import serial


def open_port(port):
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    return ser


def main(argv):
    parser = argparse.ArgumentParser()
    parser.add_argument("port")
    parser.add_argument("path")
    parser.add_argument("--until", default=None, help="stop at the first line containing this text")
    parser.add_argument("--seconds", type=float, default=900.0)
    args = parser.parse_args(argv[1:])

    ser = None
    while ser is None:
        try:
            ser = open_port(args.port)
        except Exception as exc:  # port busy or re-enumerating
            time.sleep(1.0)

    deadline = time.time() + args.seconds
    buffer = ""

    with open(args.path, "a", encoding="utf-8") as log:
        log.write("\n--- watch started %s ---\n" % time.strftime("%H:%M:%S"))
        log.flush()

        while time.time() < deadline:
            try:
                chunk = ser.read(4096)
            except Exception:
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                while ser is None:
                    try:
                        ser = open_port(args.port)
                    except Exception:
                        time.sleep(1.0)
                continue

            if not chunk:
                continue

            buffer += chunk.decode("utf-8", "replace")
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.rstrip("\r")
                if not line.strip():
                    continue
                log.write(line + "\n")
                log.flush()
                print(line, flush=True)
                if args.until and args.until in line:
                    log.write("--- matched %r ---\n" % args.until)
                    log.flush()
                    ser.close()
                    return 0

    try:
        ser.close()
    except Exception:
        pass
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
