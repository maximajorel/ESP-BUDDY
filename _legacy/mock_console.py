"""Stand-in for the ESP console, so the web UI can be tested without the board.

Serves web_ui/ over HTTP on 8787 and speaks the same WebSocket protocol on 8788
that the firmware speaks on 81. Commands received are appended to
tools/mock_commands.log so a test can assert what the page actually sent.

    python3 tools/mock_console.py
"""

import asyncio
import json
import math
import threading
import time
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import websockets

ROOT = Path(__file__).resolve().parent.parent / "web_ui"
LOG = Path(__file__).resolve().parent / "mock_commands.log"
UPLOAD_LOG = Path(__file__).resolve().parent / "mock_upload.log"

state = {"r": 255, "g": 136, "b": 0, "level": 64, "effect": 0, "bl": 80}
started = time.time()


def make_state(clients):
    t = time.time() - started
    return {
        "t": "state",
        "ax": round(math.sin(t) * 1.0, 2),
        "ay": round(math.cos(t * 0.7) * 0.6, 2),
        "az": round(0.98 + math.sin(t * 2) * 0.02, 2),
        "gx": round(math.sin(t * 1.3) * 12.0, 1),
        "gy": round(math.cos(t * 1.1) * 8.0, 1),
        "gz": round(math.sin(t * 0.5) * 4.0, 1),
        **state,
        "up": int(t),
        "heap": 256000,
        "clients": clients,
    }


def apply(command):
    t = command.get("t")
    if t == "rgb":
        state.update(r=command.get("r", 0), g=command.get("g", 0), b=command.get("b", 0))
        state["effect"] = 0
    elif t == "level":
        state["level"] = command.get("v", 0)
    elif t == "effect":
        name = command.get("v", "solid")
        state["effect"] = {"solid": 0, "rainbow": 1, "off": 2}.get(name, 0)
        if name == "solid":
            state.update(r=command.get("r", state["r"]), g=command.get("g", state["g"]),
                         b=command.get("b", state["b"]))
    elif t == "bl":
        state["bl"] = command.get("v", 80)
    return


async def handler(conn):
    clients = 1
    await conn.send(json.dumps({"t": "hello", "fw": "mock", "w": 172, "h": 320, "clients": clients}))

    async def pump():
        while True:
            await conn.send(json.dumps(make_state(clients)))
            await asyncio.sleep(0.2)

    pump_task = asyncio.create_task(pump())
    try:
        async for message in conn:
            try:
                command = json.loads(message)
            except json.JSONDecodeError:
                continue
            with LOG.open("a", encoding="utf-8") as handle:
                handle.write(json.dumps(command) + "\n")
            apply(command)
            await conn.send(json.dumps(make_state(clients)))
    except websockets.ConnectionClosed:
        pass
    finally:
        pump_task.cancel()


def extract_multipart_file(body, content_type):
    """Return the bytes of the first file part of a multipart body."""
    marker = "boundary="
    if marker not in content_type:
        return b""
    boundary = content_type.split(marker, 1)[1].strip().strip('"')
    delimiter = b"--" + boundary.encode()

    for part in body.split(delimiter):
        if b"filename=" not in part:
            continue
        _, _, payload = part.partition(b"\r\n\r\n")
        if payload.endswith(b"\r\n"):
            payload = payload[:-2]
        return payload
    return b""


def serve_http():
    class Handler(SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(ROOT), **kwargs)

        def log_message(self, *_args):
            pass

        def do_POST(self):
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length) if length else b""
            content_type = self.headers.get("Content-Type", "")

            # The real ESP32 WebServer only hands the body to its upload
            # callback for multipart parts. Accepting anything here would hide
            # exactly the bug this stand-in exists to catch.
            if not content_type.startswith("multipart/form-data"):
                payload = b'{"ok":false,"error":"expected multipart/form-data"}'
                self.send_response(400)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(payload)))
                self.end_headers()
                self.wfile.write(payload)
                with UPLOAD_LOG.open("w", encoding="utf-8") as handle:
                    handle.write("REJECTED content-type=%s\n" % content_type)
                return

            file_bytes = extract_multipart_file(body, content_type)
            with UPLOAD_LOG.open("w", encoding="utf-8") as handle:
                handle.write(
                    "path=%s length=%d first=%s\n"
                    % (self.path, len(file_bytes), file_bytes[:8].hex())
                )
            payload = b'{"ok":true}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    ThreadingHTTPServer(("127.0.0.1", 8787), Handler).serve_forever()


async def main():
    threading.Thread(target=serve_http, daemon=True).start()
    print("http on http://127.0.0.1:8787   ws on ws://127.0.0.1:8788")
    async with websockets.serve(handler, "127.0.0.1", 8788):
        await asyncio.Future()


if __name__ == "__main__":
    asyncio.run(main())
