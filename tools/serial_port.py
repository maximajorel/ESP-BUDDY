"""Open the board's CDC serial port, shared by the host-side tools.

Every script that talks to the gadget needs the same three details: 115200 baud,
a short read timeout, and DTR/RTS left deasserted so opening the port does not
reset the board. Getting one of them wrong looks like a firmware bug (silence,
or a device parked in the USB download loader), so the defaults live here
instead of being copied into each script.

`gadget_probe.py` keeps its own opener on purpose: it *wants* the reset that
asserting the modem lines causes, and retries while the CDC re-enumerates.
"""

import serial

BAUD = 115200


def open_port(port, baud=BAUD, timeout=0.2, dtr=False, rts=False):
    """Open `port` without resetting the board.

    Raises the underlying serial exception when the port is missing or busy; the
    caller decides whether to retry.
    """
    ser = serial.Serial()
    ser.port = port
    ser.baudrate = baud
    ser.timeout = timeout
    ser.dtr = dtr
    ser.rts = rts
    ser.open()
    return ser
