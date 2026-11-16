"""Mock of the ESP32 Gadget API, for checking the web panel in a browser.

The device serves its panel from flash over its own access point, which is not
always reachable from a development machine. This stand-in implements the exact
contract instead, so the panel's HTML/CSS/JS can be exercised for real.

    python3 tools/mock_gadget_api.py 8791

Every mutating request is appended to tools/mock_requests.log so a browser test
can prove what the panel actually sent, not just that it looked right.
"""

import json
import os
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "esp32_gadget", "web_ui")
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mock_requests.log")

MIME = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
}

STATE = {
    "firmware": "esp32-gadget",
    "version": "1.0.0",
    "uptimeMs": 734000,
    "heapFree": 248512,
    "psramFree": 8151240,
    "screen": 0,
    "order": [0, 1, 2, 3, 4],
    "names": ["RELOJ", "TRAGAMONEDAS", "METEORITOS", "RED", "MUSICA"],
    "imuOk": True,
    "sta": {"configured": True, "connected": False, "ssid": "VICTORWIFI",
            "ip": "-", "rssi": 0, "state": "failed", "wifiStatusCode": 6,
            "disconnectReason": 15, "mac": "80:45:6B:35:DA:38"},
    "ap": {"ssid": "ESP32-GADGET-35DA38", "password": "12345678",
           "ip": "192.168.4.1", "clients": 1},
    "time": {"synced": False, "tz": -3, "clock24h": True, "local": "--"},
    "display": {"brightness": 90, "orientation": 1, "awake": True,
                "timeoutSec": 120, "timeoutMask": 9},
    "rgb": {"r": 255, "g": 215, "b": 0, "level": 64, "effect": 0,
            "asleep": False},
    "steer": {"sign": 1, "signY": 1, "latAxis": 0, "upAxis": 2,
              "calibrated": True},
    "scores": {"game": 340, "slot": 120},
}

NETWORKS = [    {"ssid": "MiCasa-2.4G", "rssi": -48, "secure": True},
    {"ssid": "Vecino", "rssi": -71, "secure": True},
    {"ssid": "Cafe-Libre", "rssi": -84, "secure": False},
]


# Spotify state, so the panel's card can be walked through its real transitions:
# no client id -> configured but unauthorized -> authorized with a track.
SPOTIFY = {
    "clientId": "",
    "authorized": False,
    "feed": 0,
    "httpStatus": 0,
    "error": "",
}

EMPTY_TRACK = {"title": "", "artist": "", "album": "", "isPlaying": False,
               "progressMs": 0, "durationMs": 0}

PLAYING_TRACK = {"title": "Never Gonna Give You Up",
                 "artist": "Rick Astley, Guest Voice",
                 "album": "Whenever You Need Somebody",
                 "isPlaying": True, "progressMs": 102000,
                 "durationMs": 213573}


def spotify_payload():
    if not SPOTIFY["clientId"]:
        auth = 0
    elif not SPOTIFY["authorized"]:
        auth = 2
    else:
        auth = 3
    return {
        "ok": True,
        "configured": bool(SPOTIFY["clientId"]),
        "clientId": SPOTIFY["clientId"],
        "authorized": SPOTIFY["authorized"],
        "auth": auth,
        "feed": SPOTIFY["feed"],
        "httpStatus": SPOTIFY["httpStatus"],
        "error": SPOTIFY["error"],
        "redirectUri": "http://127.0.0.1:8888/callback",
        "track": PLAYING_TRACK if SPOTIFY["feed"] == 2 else EMPTY_TRACK,
    }


def log_request(entry):
    with open(LOG, "a", encoding="utf-8") as handle:
        handle.write(json.dumps(entry, ensure_ascii=False) + "\n")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args):
        pass  # keep the test output clean

    def _send(self, code, body, content_type):
        raw = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(raw)

    def _json(self, code, payload):
        self._send(code, json.dumps(payload, ensure_ascii=False), "application/json")

    def _fail(self, code, message):
        self._json(code, {"ok": False, "error": message})

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/api/status":
            self._json(200, STATE)
            return
        if path == "/api/scan":
            self._json(200, {"ok": True, "networks": NETWORKS})
            return
        if path == "/api/spotify":
            self._json(200, spotify_payload())
            return
        if path.startswith("/api/"):
            self._fail(405, "Metodo no permitido")
            return

        if path == "/":
            path = "/index.html"
        full = os.path.normpath(os.path.join(ROOT, path.lstrip("/")))
        if not full.startswith(os.path.normpath(ROOT)) or not os.path.isfile(full):
            self._send(404, "not found", "text/plain")
            return
        ext = os.path.splitext(full)[1].lower()
        with open(full, "rb") as handle:
            self._send(200, handle.read(), MIME.get(ext, "application/octet-stream"))

    def do_POST(self):
        path = self.path.split("?")[0]
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length).decode("utf-8") if length else ""
        try:
            body = json.loads(raw) if raw else {}
        except ValueError:
            self._fail(400, "Los datos enviados no son validos")
            return

        log_request({"path": path, "body": body})

        if path == "/api/wifi":
            ssid = body.get("ssid", "")
            if not ssid:
                self._fail(400, "El nombre de la red no puede estar vacio")
                return
            STATE["sta"] = {"configured": True, "connected": True, "ssid": ssid,
                            "ip": "192.168.1.42", "rssi": -48}
            STATE["time"]["synced"] = True
            STATE["time"]["local"] = "2026-11-15 21:30:04"
            self._json(200, {"ok": True})
            return

        if path == "/api/ap":
            ssid = body.get("ssid", "")
            password = body.get("pass", "")
            if not ssid:
                self._fail(400, "El nombre del punto de acceso no puede estar vacio")
                return
            if len(password) > 0 and len(password) < 8:
                self._fail(400, "La contrasena debe tener al menos 8 caracteres")
                return
            STATE["ap"]["ssid"] = ssid
            STATE["ap"]["password"] = password
            self._json(200, {"ok": True})
            return

        if path == "/api/settings":
            if "brightness" in body:
                STATE["display"]["brightness"] = body["brightness"]
            if "orientation" in body:
                STATE["display"]["orientation"] = body["orientation"]
            if "clock24h" in body:
                STATE["time"]["clock24h"] = body["clock24h"]
            if "steerSign" in body:
                STATE["steer"]["sign"] = body["steerSign"]
            if "steerSignY" in body:
                STATE["steer"]["signY"] = body["steerSignY"]
            if "screenTimeoutSec" in body:
                STATE["display"]["timeoutSec"] = body["screenTimeoutSec"]
            if "screenTimeoutMask" in body:
                STATE["display"]["timeoutMask"] = body["screenTimeoutMask"]
            if "rgb" in body:
                STATE["rgb"].update(body["rgb"])
            self._json(200, {"ok": True})
            return

        if path == "/api/screens/order":
            order = body.get("order")
            count = len(STATE["names"])
            if not isinstance(order, list) or sorted(order) != list(range(count)):
                self._fail(400, "El orden repite o pierde pantallas")
                return
            STATE["order"] = order
            self._json(200, {"ok": True})
            return

        if path == "/api/screens/names":
            names = body.get("names")
            if not isinstance(names, list) or len(names) != len(STATE["names"]):
                self._fail(400, "Los nombres tienen que listar todas las pantallas")
                return
            cleaned = [str(name).strip()[:12] for name in names]
            if any(not name for name in cleaned):
                self._fail(400, "Cada nombre necesita entre 1 y 12 caracteres")
                return
            STATE["names"] = cleaned
            self._json(200, {"ok": True})
            return

        if path == "/api/panel/colors":
            self._json(200, {"ok": True})
            return

        if path == "/api/screen":
            index = body.get("index")
            if not isinstance(index, int) or not 0 <= index < len(STATE["names"]):
                self._fail(400, "Indice de pantalla fuera de rango")
                return
            STATE["screen"] = index
            self._json(200, {"ok": True})
            return

        if path in ("/api/press", "/api/steer/reset", "/api/steer/swap",
                    "/api/restart", "/api/factory"):
            if path == "/api/steer/swap":
                lat = STATE["steer"]["latAxis"]
                STATE["steer"]["latAxis"] = 3 - lat - STATE["steer"]["upAxis"]
            if path == "/api/factory":
                # The firmware clears the name overrides under their own NVS
                # keys during a factory reset; the mock follows the same rule.
                STATE["order"] = list(range(len(STATE["names"])))
                STATE["names"] = ["RELOJ", "TRAGAMONEDAS", "METEORITOS", "RED",
                                  "MUSICA"]
            self._json(200, {"ok": True})
            return

        if path == "/api/scores/reset":
            STATE["scores"] = {"game": 0, "slot": 0}
            self._json(200, {"ok": True})
            return

        if path == "/api/spotify/config":
            client = (body.get("clientId") or "").strip()
            if len(client) < 8:
                self._fail(400, "Client id invalido (8 a 63 caracteres)")
                return
            SPOTIFY["clientId"] = client
            SPOTIFY["authorized"] = False
            SPOTIFY["feed"] = 0
            self._json(200, {"ok": True})
            return

        if path == "/api/spotify/url":
            if not SPOTIFY["clientId"]:
                self._fail(400, "Guarda el client id primero")
                return
            url = ("https://accounts.spotify.com/authorize?response_type=code"
                   "&client_id=" + SPOTIFY["clientId"] +
                   "&scope=user-read-currently-playing"
                   "&redirect_uri=http%3A%2F%2F127.0.0.1%3A8888%2Fcallback"
                   "&code_challenge_method=S256"
                   "&code_challenge=mock-challenge-not-a-real-one")
            self._json(200, {"ok": True, "url": url,
                             "redirectUri": "http://127.0.0.1:8888/callback"})
            return

        if path == "/api/spotify/code":
            code = (body.get("code") or "").strip()
            if not code:
                self._fail(400, "Falta el code")
                return
            SPOTIFY["authorized"] = True
            SPOTIFY["feed"] = 2
            SPOTIFY["error"] = ""
            self._json(200, {"ok": True})
            return

        if path == "/api/spotify/forget":
            SPOTIFY["authorized"] = False
            SPOTIFY["feed"] = 0
            self._json(200, {"ok": True})
            return

        self._fail(404, "No existe ese recurso")


def main(argv):
    port = int(argv[1]) if len(argv) > 1 else 8791
    open(LOG, "w", encoding="utf-8").close()
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print("mock gadget API on http://127.0.0.1:%d  (serving %s)" % (port, ROOT))
    sys.stdout.flush()
    server.serve_forever()


if __name__ == "__main__":
    main(sys.argv)
