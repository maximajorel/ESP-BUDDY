"""Watch the serial port and log any view change as soon as it happens.

Writes straight to a file with an explicit flush, because a redirected Python
stdout and the shell's own buffering both hold lines back for seconds.

Windows likes to hand back ClearCommError from a CDC port that just
re-enumerated, so the read loop reopens the port instead of dying. Exits as soon
as a press is seen, or when the deadline passes.

    python boot_watch.py COM5 out.log 120
"""

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
    port = argv[1]
    log_path = argv[2]
    seconds = float(argv[3]) if len(argv) > 3 else 120.0

    with open(log_path, "w", encoding="utf-8") as log:

        def emit(text):
            log.write(text)
            log.flush()

        ser = None
        while ser is None:
            try:
                ser = open_port(port)
            except Exception as exc:
                emit("open failed: %r, retrying\n" % (exc,))
                time.sleep(1.0)

        emit("watching %s for %.0fs\n" % (port, seconds))
        deadline = time.time() + seconds
        buffer = ""
        reads = 0

        while time.time() < deadline:
            try:
                chunk = ser.read(4096)
                reads += 1
            except Exception as exc:
                emit("read error: %r, reopening\n" % (exc,))
                try:
                    ser.close()
                except Exception:
                    pass
                ser = None
                while ser is None:
                    try:
                        ser = open_port(port)
                    except Exception as exc2:
                        time.sleep(1.0)
                continue

            if not chunk:
                continue
            buffer += chunk.decode("utf-8", "replace")
            while "\n" in buffer:
                line, buffer = buffer.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                emit(line + "\n")
                if line.startswith("mode ->"):
                    emit("PRESS_SEEN\n")
                    ser.close()
                    return 0

        emit("TIMEOUT after %d reads: no view change observed\n" % reads)
        try:
            ser.close()
        except Exception:
            pass
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
