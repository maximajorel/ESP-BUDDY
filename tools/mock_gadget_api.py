"""Mock of the ESP32 Gadget API, for checking the web panel in a browser.

The device serves its panel from flash over its own access point, which is not
always reachable from a development machine. This stand-in implements the exact
contract instead, so the panel's HTML/CSS/JS can be exercised for real.

    python3 tools/mock_gadget_api.py 8791
    python3 tools/mock_gadget_api.py 8791 0.0.0.0   # visible from Windows/LAN

Every mutating request is appended to tools/mock_requests.log so a browser test
can prove what the panel actually sent, not just that it looked right.
"""

import json
import os
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "esp32_gadget", "web_ui")
LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mock_requests.log")

MIME = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
}

# What the panel is logged in with. The firmware seeds this from `.env` and keeps
# it in NVS; the mock keeps the same two pieces of state so the browser exercises
# the real flow: log in, change the pair, log out.
ADMIN = {"user": "admin", "pass": "gadget", "mustChange": True}
SESSIONS = set()
SESSION_COOKIE = "sid"

STATE = {
    "firmware": "esp32-gadget",
    "version": "1.0.0",
    "uptimeMs": 734000,
    "heapFree": 248512,
    "psramFree": 8151240,
    "screen": 0,
    "order": [0, 1, 2, 3, 4, 5],
    "names": ["RELOJ", "TRAGAMONEDAS", "METEORITOS", "RED", "MUSICA", "SOFIA", "CONFIG"],
    "imuOk": True,
    "sta": {"configured": True, "connected": False, "ssid": "VICTORWIFI",
            "ip": "-", "rssi": 0, "state": "failed", "wifiStatusCode": 6,
            "disconnectReason": 15, "mac": "80:45:6B:35:DA:38"},
    "ap": {"ssid": "ESP32-GADGET-35DA38", "password": "12345678",
           "ip": "192.168.4.1", "clients": 1},
    "time": {"synced": False, "tz": -3, "clock24h": True, "local": "--"},
    "display": {"brightness": 90, "orientation": 1, "awake": True,
                "rotationMode": "auto", "rotationLocked": False,
                "timeoutSec": 120, "timeoutMask": 9},
    "rgb": {"r": 255, "g": 215, "b": 0, "level": 64, "effect": 0,
            "asleep": False},
    "steer": {"sign": 1, "signY": 1, "latAxis": 0, "upAxis": 2,
              "calibrated": True},
    "buddy": {"configured": True, "online": True, "state": "idle",
              "label": "tranquila", "active": 0, "ageMs": 800,
              "url": "http://192.168.100.64:8787",
              "eyeColor": "#00B4FF"},
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


def focus_payload():
    """The study countdown as the firmware reports it: state, not a local clock."""
    f = STATE.get("focus") or {}
    minutes = f.get("minutes") or 0
    started = f.get("startedMs")
    if not minutes or started is None:
        return {"active": False, "finished": False, "minutes": minutes, "remainingMs": 0}
    elapsed = int(time.time() * 1000) - started
    total = minutes * 60000
    left = max(0, total - elapsed)
    return {
        "active": left > 0,
        "finished": left == 0 and elapsed < total + 6000,
        "minutes": minutes,
        "remainingMs": left,
    }


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_args):
        pass  # keep the test output clean

    def _send(self, code, body, content_type, extra_headers=None):
        raw = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(raw)))
        self.send_header("Cache-Control", "no-store")
        for key, value in (extra_headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        self.wfile.write(raw)

    def _cookie_token(self):
        header = self.headers.get("Cookie") or ""
        for part in header.split(";"):
            name, _, value = part.strip().partition("=")
            if name == SESSION_COOKIE:
                return value
        return ""

    def _authed(self):
        return self._cookie_token() in SESSIONS

    def _unauthorized(self):
        self._json(401, {"ok": False, "error": "Sesion requerida"})

    def _json(self, code, payload):
        self._send(code, json.dumps(payload, ensure_ascii=False), "application/json")

    def _fail(self, code, message):
        self._json(code, {"ok": False, "error": message})

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/api/status":
            payload = dict(STATE)
            payload["focus"] = focus_payload()
            self._json(200, payload)
            return
        if path == "/api/scan":
            self._json(200, {"ok": True, "networks": NETWORKS})
            return
        if path == "/api/spotify":
            self._json(200, spotify_payload())
            return
        if path == "/api/session":
            payload = {"ok": True, "authenticated": self._authed()}
            if payload["authenticated"]:
                payload["user"] = ADMIN["user"]
                payload["mustChange"] = ADMIN["mustChange"]
            self._json(200, payload)
            return
        if path.startswith("/api/"):
            self._fail(405, "Metodo no permitido")
            return

        if path == "/api/session":
            return
        if path in ("/admin", "/admin/") and not self._authed():
            self._send(302, "", "text/plain", {"Location": "/login?next=/admin"})
            return

        if path in ("/", "/index"):
            path = "/index.html"
        elif path in ("/admin", "/admin/"):
            path = "/admin.html"
        elif path in ("/login", "/login/"):
            path = "/login.html"
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

        if path == "/api/session":
            if body.get("user") != ADMIN["user"] or body.get("pass") != ADMIN["pass"]:
                self._fail(401, "Usuario o contrasena incorrectos")
                return
            token = os.urandom(16).hex()
            SESSIONS.add(token)
            self._send(
                200,
                json.dumps({"ok": True, "user": ADMIN["user"],
                            "mustChange": ADMIN["mustChange"]}),
                "application/json",
                {"Set-Cookie": "%s=%s; Path=/; HttpOnly; SameSite=Strict; Max-Age=43200"
                               % (SESSION_COOKIE, token)},
            )
            return

        if path == "/api/session/logout":
            SESSIONS.discard(self._cookie_token())
            self._send(
                200,
                json.dumps({"ok": True}),
                "application/json",
                {"Set-Cookie": "%s=; Path=/; HttpOnly; SameSite=Strict; Max-Age=0"
                               % SESSION_COOKIE},
            )
            return

        # The public writes, exactly like the firmware's transport: they only
        # change what the glass shows, so the picker and the web game work
        # without a session.
        if path == "/api/screen":
            index = body.get("index")
            if not isinstance(index, int) or not 0 <= index < len(STATE["names"]):
                self._fail(400, "Indice de pantalla fuera de rango")
                return
            STATE["screen"] = index
            self._json(200, {"ok": True})
            return

        if path == "/api/focus":
            minutes = body.get("minutes")
            if not isinstance(minutes, int) or minutes < 0:
                minutes = 0
            minutes = min(minutes, 180)
            STATE["focus"] = {
                "minutes": minutes,
                "startedMs": int(time.time() * 1000) if minutes else None,
            }
            self._json(200, {"ok": True})
            return
        if path == "/api/face":
            mood = str(body.get("mood") or "")
            hold = body.get("ms") or 0
            if not isinstance(hold, int):
                hold = 0
            STATE["face"] = {"mood": mood, "ms": hold}
            self._json(200, {"ok": True})
            return

        # Every other write is protected, exactly like the firmware's transport.
        if not self._authed():
            self._unauthorized()
            return

        if path == "/api/admin/credentials":
            if body.get("current") != ADMIN["pass"]:
                self._fail(403, "La contrasena actual no es correcta")
                return
            user = str(body.get("user") or "")
            password = str(body.get("pass") or "")
            if not user or len(user) > 32:
                self._fail(400, "El usuario necesita entre 1 y 32 caracteres sin espacios")
                return
            if len(password) < 8 or len(password) > 64:
                self._fail(400, "La contrasena nueva necesita entre 8 y 64 caracteres")
                return
            ADMIN.update({"user": user, "pass": password, "mustChange": False})
            self._json(200, {"ok": True})
            return

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
                STATE["display"]["rotationMode"] = "raw"
            if "rotationMode" in body:
                mode = body["rotationMode"]
                STATE["display"]["rotationMode"] = mode
                # The image is what the mode asks for; "auto" keeps whatever the
                # mock was already showing.
                if mode == "horizontal":
                    STATE["display"]["orientation"] = 1
                elif mode == "inverted":
                    STATE["display"]["orientation"] = 2
            if "rotationLocked" in body:
                STATE["display"]["rotationLocked"] = bool(body["rotationLocked"])
            if "clock24h" in body:
                STATE["time"]["clock24h"] = body["clock24h"]
            if "steerSign" in body:
                STATE["steer"]["sign"] = body["steerSign"]
            if "steerSignY" in body:
                STATE["steer"]["signY"] = body["steerSignY"]
            if "buddyUrl" in body:
                STATE["buddy"]["url"] = body["buddyUrl"]
            if "buddyToken" in body:
                STATE["buddy"]["configured"] = bool(body["buddyToken"])
            if "buddyEyeColor" in body:
                # Empty string is the firmware's "back to the shipped cyan".
                STATE["buddy"]["eyeColor"] = body["buddyEyeColor"] or "#00B4FF"
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

        if path == "/api/buddy/ask":
            prompt = (body.get("prompt") or "").strip()
            if not prompt:
                self._fail(400, "Escribi un pedido")
                return
            STATE["buddy"]["state"] = "thinking"
            STATE["buddy"]["label"] = prompt[:40]
            self._json(200, {"ok": True})
            return

        if path == "/api/panel/colors":
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
                                  "MUSICA", "SOFIA"]
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
    # 127.0.0.1 keeps the mock private by default; pass 0.0.0.0 to open the
    # panel from a Windows browser when WSL localhost forwarding is not enough.
    host = argv[2] if len(argv) > 2 else os.environ.get("MOCK_HOST", "127.0.0.1")
    open(LOG, "w", encoding="utf-8").close()
    server = ThreadingHTTPServer((host, port), Handler)
    print("mock gadget API on http://%s:%d  (serving %s)" % (host, port, ROOT))
    sys.stdout.flush()
    server.serve_forever()


if __name__ == "__main__":
    main(sys.argv)
