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

# El clima: Open-Meteo no pide clave, asi que lo unico que se guarda es el lugar
# y cada cuanto se refresca. El mock arranca sin lugar, como el firmware recien
# flasheado, y devuelve un pronostico fijo para que el panel y las pruebas vean
# siempre el mismo dato en vez de uno inventado por request.
WEATHER_NO_DATA = {"valid": False, "tempC": 0.0, "feelsC": 0.0, "humidity": 0,
                   "windKph": 0.0, "tmaxC": 0.0, "tminC": 0.0, "code": -1,
                   "label": "Sin datos", "icon": 0, "isDay": True}

WEATHER = {
    "place": {"valid": False, "name": "", "lat": 0.0, "lon": 0.0},
    "now": dict(WEATHER_NO_DATA),
    "refreshMin": 15,
    "hasData": False,
    "syncing": False,
    "lastAttemptMs": 0,
    "lastSuccessMs": 0,
    "lastError": "",
}

WEATHER_NOW = {"valid": True, "tempC": 22.4, "feelsC": 21.8, "humidity": 60,
               "windKph": 12.0, "tmaxC": 27.0, "tminC": 14.0, "code": 2,
               "label": "Parcialmente nublado", "icon": 2, "isDay": True}

# Lo que responde ipwho.is y lo que devuelve el geocoder para "tuc": fijo, para
# que detectar y buscar no dependan de la red desde la compu.
WEATHER_DETECTED = {"name": "San Miguel de Tucuman", "lat": -26.8241,
                    "lon": -65.2226}
WEATHER_CITIES = [
    {"name": "San Miguel de Tucuman", "region": "Tucuman", "country": "AR",
     "lat": -26.8241, "lon": -65.2226},
]

WEATHER_REFRESH_MIN_MIN = 5
WEATHER_REFRESH_MIN_MAX = 180
WEATHER_NAME_MAX = 48

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
    "time": {"synced": False, "tz": -3, "local": "--"},
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
    "weather": WEATHER,
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


CALENDAR = {
    "clientId": "", "secret": False, "authorized": False, "pending": False,
    "pendingSince": 0.0, "userCode": "", "selectedId": "", "selectedName": "",
    "fallbackLeadMin": 10, "graceMin": 5, "lastError": "",
    "calendars": [
        {"id": "primary", "name": "maxi@example.com", "primary": True},
        {"id": "trabajo@group.calendar.google.com", "name": "Trabajo",
         "primary": False},
        {"id": "familia@group.calendar.google.com", "name": "Familia",
         "primary": False},
    ],
}


def calendar_tick():
    # El mock aprueba solo despues de unos segundos, para poder mirar el flujo
    # completo en el navegador sin una cuenta de Google real. Lo llaman los dos
    # endpoints del calendario: si lo hiciera uno solo, la agenda diria "sin
    # eventos" mientras el estado ya dice "listo".
    if CALENDAR["pending"] and time.time() - CALENDAR["pendingSince"] > 4:
        CALENDAR["pending"] = False
        CALENDAR["authorized"] = True
        CALENDAR["userCode"] = ""


def calendar_payload():
    calendar_tick()
    if not CALENDAR["clientId"]:
        kind = "unconfigured"
    elif CALENDAR["pending"]:
        kind = "auth_pending"
    elif not CALENDAR["authorized"]:
        kind = "needs_auth"
    else:
        kind = "ready"
    selected = CALENDAR["selectedId"] or "primary"
    name = CALENDAR["selectedName"] or (
        "maxi@example.com" if selected == "primary" else selected)
    next_summary = "Dentista"
    in_min = 20
    lead = 30
    started = False
    phase = "early"
    intensity = 33
    return {
        "ok": True, "kind": kind,
        "configured": bool(CALENDAR["clientId"]),
        "authorized": CALENDAR["authorized"],
        "pending": CALENDAR["pending"],
        "syncing": False,
        "userCode": CALENDAR["userCode"],
        "verificationUrl": "https://www.google.com/device",
        "pendingSecondsLeft": 1790 if CALENDAR["pending"] else 0,
        "selectedId": selected, "selectedName": name,
        "fallbackLeadMin": CALENDAR["fallbackLeadMin"],
        "graceMin": CALENDAR["graceMin"],
        "lastError": CALENDAR["lastError"],
        "lastSyncMs": 12000 if CALENDAR["authorized"] else 0,
        "calendarCount": len(CALENDAR["calendars"]) if CALENDAR["authorized"] else 0,
        # Coherente con `GET /api/calendar/events`: el contador de la
        # cabecera es el del mismo dia que sirve la agenda.
        "eventCount": calendar_events_payload()["count"],
        "hasNext": bool(CALENDAR["authorized"]),
        "nextSummary": next_summary if CALENDAR["authorized"] else "",
        "nextInMin": in_min if CALENDAR["authorized"] else 0,
        "nextStarted": started if CALENDAR["authorized"] else False,
        "nextLeadMin": lead if CALENDAR["authorized"] else -1,
        "reaction": {
            "active": bool(CALENDAR["authorized"]),
            "phase": phase if CALENDAR["authorized"] else "none",
            "intensity": intensity if CALENDAR["authorized"] else 0,
            "msToStart": in_min * 60000 if CALENDAR["authorized"] else 0,
        },
    }


def calendar_events_payload():
    """La agenda que publica `GET /api/calendar/events`.

    Se arma relativa a la medianoche local del equipo (tz -3) y al momento de
    la consulta, asi que el popover ve siempre el mismo dia: un evento de todo el
    dia, uno ya terminado, el proximo (dentro de media hora) y uno de manana. El
    dia viaja como `dayStart`/`dayEnd`, igual que en el firmware.
    """
    calendar_tick()
    tz = STATE["time"]["tz"]
    offset = tz * 3600
    now = int(time.time())
    local = now + offset
    day_start = local - (local % 86400) - offset
    day_end = day_start + 86400

    def event(summary, location, start, end, lead=10, all_day=False):
        return {"summary": summary, "location": location,
                "startEpoch": start, "endEpoch": end,
                "leadMin": lead, "allDay": all_day}

    events = [
        event("Feriado municipal", "", day_start, day_end, -1, True),
        event("Daily con el equipo", "Meet", day_start + 8 * 3600,
              day_start + 8 * 3600 + 2700, 5),
        event("Reunion con el disenador", "Zoom", now + 30 * 60,
              now + 90 * 60),
        event("Turno con la Dra. Paz", "Consultorio", day_start + 15 * 3600,
              day_start + 16 * 3600, 20),
        event("Cumpleanos de Ana", "", day_end + 10 * 3600,
              day_end + 11 * 3600, 30),
    ]
    events.sort(key=lambda item: item["startEpoch"])
    return {"ok": True, "tz": tz, "dayStart": day_start, "dayEnd": day_end,
            "count": len(events) if CALENDAR["authorized"] else 0,
            "events": events if CALENDAR["authorized"] else []}


def weather_kind():
    # Misma precedencia que `weatherStatusInfo()`: sin lugar, lo que importa es
    # que falta configurar, no el error viejo de un lugar que ya no existe.
    if not WEATHER["place"]["valid"]:
        return "unconfigured"
    if WEATHER["hasData"]:
        return "ready"
    if WEATHER["lastError"]:
        return "error"
    return "no_data"


def weather_payload():
    return {
        "ok": True,
        "kind": weather_kind(),
        "configured": WEATHER["place"]["valid"],
        "hasData": WEATHER["hasData"],
        "syncing": WEATHER["syncing"],
        "refreshMin": WEATHER["refreshMin"],
        "lastAttemptMs": WEATHER["lastAttemptMs"],
        "lastSuccessMs": WEATHER["lastSuccessMs"],
        "lastError": WEATHER["lastError"],
        "place": dict(WEATHER["place"]),
        "now": dict(WEATHER["now"]),
    }


def weather_summary():
    """El resumen compacto que viaja dentro de `/api/status`."""
    payload = weather_payload()
    return {
        "configured": payload["configured"],
        "hasData": payload["hasData"],
        "syncing": payload["syncing"],
        "kind": payload["kind"],
        "place": payload["place"]["name"],
        "lastError": payload["lastError"],
        "refreshMin": payload["refreshMin"],
        "tempC": payload["now"]["tempC"],
        "label": payload["now"]["label"],
    }


def weather_save_place(lat, lon, name):
    # Un lugar nuevo invalida la lectura anterior: mostrarla seria mentir sobre
    # otra ciudad hasta que el proximo refresco llegue.
    WEATHER["place"] = {"valid": True, "name": name, "lat": lat, "lon": lon}
    WEATHER["hasData"] = False
    WEATHER["now"] = dict(WEATHER_NO_DATA)
    WEATHER["lastError"] = ""


def weather_forget():
    WEATHER["place"] = {"valid": False, "name": "", "lat": 0.0, "lon": 0.0}
    WEATHER["now"] = dict(WEATHER_NO_DATA)
    WEATHER["hasData"] = False
    WEATHER["syncing"] = False
    WEATHER["lastAttemptMs"] = 0
    WEATHER["lastSuccessMs"] = 0
    WEATHER["lastError"] = ""


def weather_apply_refresh():
    """El firmware pide el pronostico a Open-Meteo; el mock lo sirve fijo."""
    if not WEATHER["place"]["valid"]:
        return False
    now_ms = int(time.time() * 1000)
    WEATHER["lastAttemptMs"] = now_ms
    WEATHER["lastSuccessMs"] = now_ms
    WEATHER["hasData"] = True
    WEATHER["syncing"] = False
    WEATHER["lastError"] = ""
    WEATHER["now"] = dict(WEATHER_NOW)
    return True


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
    seconds = f.get("seconds") or 0
    started = f.get("startedMs")
    if not seconds or started is None:
        return {"active": False, "finished": False, "seconds": seconds,
                "minutes": seconds // 60, "remainingMs": 0}
    elapsed = int(time.time() * 1000) - started
    total = seconds * 1000
    left = max(0, total - elapsed)
    return {
        "active": left > 0,
        "finished": left == 0 and elapsed < total + 6000,
        "seconds": seconds,
        "minutes": seconds // 60,
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
            payload["weather"] = weather_summary()
            self._json(200, payload)
            return
        if path == "/api/scan":
            self._json(200, {"ok": True, "networks": NETWORKS})
            return
        if path == "/api/spotify":
            self._json(200, spotify_payload())
            return
        if path in ("/api/calendar", "/api/calendar/calendars",
                    "/api/calendar/events", "/api/weather"):
            # En el firmware el calendario y el clima piden sesion incluso para
            # leer: el lugar es del dueno y la API lo publica.
            if not self._authed():
                self._unauthorized()
                return
            if path == "/api/calendar":
                self._json(200, calendar_payload())
            elif path == "/api/calendar/calendars":
                self._json(200, {"items": CALENDAR["calendars"]})
            elif path == "/api/calendar/events":
                self._json(200, calendar_events_payload())
            else:
                self._json(200, weather_payload())
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
            # Same rule as the firmware: seconds is the exact form, minutes is the
            # compatibility form, both together is a client bug, zero cancels and
            # above the ceiling it clamps.
            has_minutes = "minutes" in body
            has_seconds = "seconds" in body
            if has_minutes and has_seconds:
                self._fail(400, "manda minutes o seconds, no los dos")
                return
            seconds = 0
            field = "seconds" if has_seconds else "minutes"
            value = body.get(field)
            if isinstance(value, int) and value > 0:
                seconds = value if has_seconds else value * 60
                seconds = min(seconds, 5999)
            elif isinstance(value, int) and value < 0:
                self._fail(400, field + " no puede ser negativo")
                return
            elif value is not None and not isinstance(value, int):
                self._fail(400, field + " tiene que ser un entero")
                return
            STATE["focus"] = {
                "seconds": seconds,
                "startedMs": int(time.time() * 1000) if seconds else None,
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

        if path == "/api/calendar/config":
            client = (body.get("clientId") or "").strip()
            secret = (body.get("clientSecret") or "").strip()
            if client and len(client) < 8:
                self._fail(400, "Client ID invalido (8 a 120 caracteres sin espacios)")
                return
            if secret and len(secret) < 8:
                self._fail(400, "Client secret invalido (8 a 120 caracteres sin espacios)")
                return
            changed = bool(client) or bool(secret)
            if client:
                CALENDAR["clientId"] = client
            if secret:
                CALENDAR["secret"] = True
            if "fallbackLeadMin" in body:
                CALENDAR["fallbackLeadMin"] = int(body["fallbackLeadMin"])
            if "graceMin" in body:
                CALENDAR["graceMin"] = int(body["graceMin"])
            if changed:
                CALENDAR["authorized"] = False
                CALENDAR["pending"] = False
            self._json(200, calendar_payload())
            return

        if path == "/api/calendar/connect":
            if not CALENDAR["clientId"]:
                self._fail(409, "Guarda el Client ID y el secret primero")
                return
            CALENDAR["pending"] = True
            CALENDAR["pendingSince"] = time.time()
            CALENDAR["userCode"] = "ABCD-EFGH"
            self._json(200, calendar_payload())
            return

        if path == "/api/calendar/cancel":
            CALENDAR["pending"] = False
            CALENDAR["userCode"] = ""
            self._json(200, calendar_payload())
            return

        if path == "/api/calendar/select":
            calendar_id = (body.get("calendarId") or "").strip()
            if not calendar_id:
                self._fail(400, "Falta el calendario")
                return
            CALENDAR["selectedId"] = calendar_id
            CALENDAR["selectedName"] = (body.get("name") or "").strip() or calendar_id
            self._json(200, calendar_payload())
            return

        if path == "/api/calendar/refresh":
            if not CALENDAR["authorized"]:
                self._fail(409, "Falta conectar Google")
                return
            self._json(200, {"ok": True})
            return

        if path == "/api/calendar/forget":
            CALENDAR.update({"clientId": "", "secret": False, "authorized": False,
                             "pending": False, "userCode": "", "selectedId": "",
                             "selectedName": "", "lastError": "",
                             "fallbackLeadMin": 10, "graceMin": 5})
            self._json(200, calendar_payload())
            return

        if path == "/api/weather/location":
            lat = body.get("lat")
            lon = body.get("lon")
            raw_name = body.get("name")
            name = raw_name.strip() if isinstance(raw_name, str) else ""
            if (not isinstance(lat, (int, float)) or isinstance(lat, bool)
                    or not isinstance(lon, (int, float)) or isinstance(lon, bool)):
                self._fail(400, "Faltan las coordenadas")
                return
            if not -90.0 <= lat <= 90.0 or not -180.0 <= lon <= 180.0:
                self._fail(400, "Coordenadas o nombre invalidos")
                return
            if not name:
                self._fail(400, "Falta el nombre del lugar")
                return
            if len(name) > WEATHER_NAME_MAX:
                self._fail(400, "El nombre no puede superar los 48 caracteres")
                return
            weather_save_place(float(lat), float(lon), name)
            self._json(200, weather_payload())
            return

        if path == "/api/weather/detect":
            # ipwho.is sale a internet: sin WiFi el firmware devuelve 502, no un
            # lugar inventado.
            if not STATE["sta"]["connected"]:
                self._fail(502, "Conecta el equipo a tu WiFi primero")
                return
            weather_save_place(WEATHER_DETECTED["lat"], WEATHER_DETECTED["lon"],
                               WEATHER_DETECTED["name"])
            self._json(200, weather_payload())
            return

        if path == "/api/weather/search":
            raw_query = body.get("q")
            query = raw_query.strip() if isinstance(raw_query, str) else ""
            # El handler del firmware traduce cualquier fallo de la busqueda a
            # 404 con el error adentro, incluido el WiFi ausente.
            if not query:
                self._fail(404, "Escribi una ciudad para buscar")
                return
            if not STATE["sta"]["connected"]:
                self._fail(404, "Conecta el equipo a tu WiFi primero")
                return
            if len(query) > WEATHER_NAME_MAX:
                self._fail(404, "La busqueda es demasiado larga")
                return
            needle = query.lower()
            results = []
            for city in WEATHER_CITIES:
                haystack = " ".join((city["name"], city["region"],
                                     city["country"])).lower()
                if needle in haystack:
                    results.append({key: city[key] for key in
                                    ("name", "region", "country", "lat", "lon")})
            if not results:
                self._fail(404, "No encontre esa ciudad")
                return
            self._json(200, {"ok": True, "results": results})
            return

        if path == "/api/weather/refresh":
            if not weather_apply_refresh():
                self._fail(409, "Falta guardar la ubicacion")
                return
            self._json(200, weather_payload())
            return

        if path == "/api/weather/config":
            minutes = body.get("refreshMin")
            if minutes is not None:
                if (not isinstance(minutes, int) or isinstance(minutes, bool)
                        or not WEATHER_REFRESH_MIN_MIN <= minutes
                        <= WEATHER_REFRESH_MIN_MAX):
                    self._fail(400, "El refresco va de 5 a 180 minutos")
                    return
                WEATHER["refreshMin"] = minutes
            self._json(200, weather_payload())
            return

        if path == "/api/weather/forget":
            weather_forget()
            self._json(200, weather_payload())
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
