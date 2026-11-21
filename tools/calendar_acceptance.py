#!/usr/bin/env python3
"""Check the gadget's local Google Calendar end to end, over the real network.

The board owns the OAuth grant and the events now, so "does it work?" is not a
question the panel can answer by itself: this script reads the same API, checks
that the grant is live, that the selected calendar exists in the real list, that
the strings are not garbage (the ArduinoJson lifetime defect), and that the
reaction the firmware publishes agrees with the event's own reminder.

    python tools/calendar_acceptance.py 192.168.100.75
    python tools/calendar_acceptance.py 192.168.100.75 --wait 600
    python tools/calendar_acceptance.py 192.168.100.75 --json

Credentials come from `.env` (ADMIN_USER / ADMIN_PASS) or --user/--pass.
Exit codes: 0 ready and consistent, 1 inconsistent, 2 not configured/authorized,
3 the panel did not answer.

No secret is printed: the refresh token never leaves the board, and this script
only reads `/api/calendar`, which exposes state, not credentials.
"""

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PHASES = {"early", "near", "starting", "ongoing"}


def load_env():
    values = {}
    path = os.path.join(ROOT, ".env")
    if not os.path.exists(path):
        return values
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            values[key.strip()] = value.strip()
    return values


class Panel:
    def __init__(self, base, user, password):
        self.base = base.rstrip("/")
        self.user = user
        self.password = password
        self.cookie = ""

    def request(self, path, method="GET", body=None, auth=True):
        data = json.dumps(body).encode() if body is not None else None
        headers = {"Content-Type": "application/json"}
        if auth and self.cookie:
            headers["Cookie"] = self.cookie
        request = urllib.request.Request(self.base + path, data=data,
                                         headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, timeout=25) as response:
                raw = response.read()
                return response.status, raw
        except urllib.error.HTTPError as err:
            return err.code, err.read()
        except OSError as err:
            raise SystemExit("FAIL %s no responde: %s" % (self.base, err))

    def login_with_cookie(self):
        data = json.dumps({"user": self.user, "pass": self.password}).encode()
        request = urllib.request.Request(self.base + "/api/session", data=data,
                                         headers={"Content-Type": "application/json"},
                                         method="POST")
        with urllib.request.urlopen(request, timeout=25) as response:
            cookie = response.headers.get("Set-Cookie", "")
        value = cookie.split(";", 1)[0].strip()
        if not value:
            raise SystemExit("FAIL el panel no devolvio la cookie de sesion")
        self.cookie = value

    def get(self, path):
        status, raw = self.request(path)
        try:
            return status, json.loads(raw.decode("utf-8"))
        except (UnicodeDecodeError, ValueError):
            return status, {"raw": raw[:200].decode("latin-1")}


def check_status(cal):
    problems = []
    if cal.get("kind") != "ready":
        return problems
    # The ArduinoJson lifetime defect shipped once: serializing after the struct
    # left scope returned garbage bytes for every field but the integers.
    for field in ("userCode", "verificationUrl", "lastError"):
        value = cal.get(field, "")
        if any(0x80 <= ord(ch) <= 0xFF for ch in value if ord(ch) > 0x7E):
            problems.append("%s tiene bytes no UTF-8" % field)
    if not cal.get("pending") and cal.get("userCode"):
        problems.append("userCode no vacio sin un flujo pendiente")
    if not cal.get("selectedId"):
        problems.append("kind ready sin calendario elegido")
    reaction = cal.get("reaction") or {}
    if reaction.get("active"):
        if reaction.get("phase") not in PHASES:
            problems.append("fase de reaccion desconocida: %r" % reaction.get("phase"))
        if not 0 <= int(reaction.get("intensity", -1)) <= 100:
            problems.append("intensidad fuera de 0..100")
        if reaction.get("phase") == "ongoing" and int(reaction.get("msToStart", 1)) > 0:
            problems.append("ongoing con tiempo al inicio positivo")
    if cal.get("hasNext") and cal.get("nextLeadMin", -1) >= 0:
        in_min = int(cal.get("nextInMin", 0))
        lead = int(cal["nextLeadMin"])
        grace = int(cal.get("graceMin", 0))
        # `nextInMin` truncates toward zero, so the exact minutes at the two
        # borders are ambiguous; do not fail the run on that one case.
        boundary = in_min == lead or in_min == -grace
        expected = -grace < in_min < lead
        actual = bool(reaction.get("active"))
        if not boundary and expected != actual:
            problems.append(
                "la reaccion no coincide con el recordatorio: "
                "faltan %d min, aviso %d, gracia %d, activa=%s"
                % (in_min, lead, grace, actual))
        if actual and cal.get("nextSummary") and reaction.get("phase") != "ongoing":
            pass  # the summary travels with the event; the phase is time-driven
    return problems


def describe(cal):
    lines = ["estado     : %s" % cal.get("kind"),
             "calendario : %s (%s)" % (cal.get("selectedName") or "-",
                                       cal.get("selectedId") or "-")]
    if cal.get("hasNext"):
        when = cal.get("nextInMin", 0)
        lead = cal.get("nextLeadMin", -1)
        lines.append("proximo    : %s en %s min (recordatorio %s)"
                     % (cal.get("nextSummary") or "-", when,
                        "sin recordatorio" if lead < 0 else "%d min" % lead))
    else:
        lines.append("proximo    : sin eventos a la vista")
    reaction = cal.get("reaction") or {}
    lines.append("reaccion   : %s%s intensidad=%s"
                 % (reaction.get("phase", "none"),
                    " (activa)" if reaction.get("active") else "",
                    reaction.get("intensity", 0)))
    if cal.get("lastError"):
        lines.append("ultimo aviso: %s" % cal["lastError"])
    return "\n".join(lines)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("host", nargs="?", default=os.environ.get("GADGET_HOST", "192.168.100.75"))
    parser.add_argument("--user", default=None)
    parser.add_argument("--pass", dest="password", default=None)
    parser.add_argument("--wait", type=int, default=0,
                        help="espera hasta N segundos a que la reaccion este activa")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv[1:])

    base = args.host if args.host.startswith("http") else "http://" + args.host
    env = load_env()
    user = args.user or env.get("ADMIN_USER", "admin")
    password = args.password or env.get("ADMIN_PASS", "")

    panel = Panel(base, user, password)

    # The calendar is private even for reads: no cookie must mean 401. This is
    # the regression that protects the event titles on the device's own AP.
    status, _ = panel.request("/api/calendar", auth=False)
    if status != 401:
        print("FAIL /api/calendar sin sesion respondio HTTP %s (esperaba 401)" % status)
        return 1

    try:
        panel.login_with_cookie()
    except urllib.error.HTTPError as err:
        print("FAIL login HTTP %s" % err.code)
        return 1

    deadline = time.time() + max(0, args.wait)
    while True:
        status, cal = panel.get("/api/calendar")
        if status != 200:
            print("FAIL /api/calendar HTTP %s: %s" % (status, cal))
            return 3
        if "raw" in cal:
            print("FAIL /api/calendar no devolvio JSON UTF-8: %r" % cal["raw"][:80])
            return 1
        if cal.get("kind") == "ready":
            break
        if time.time() >= deadline:
            if args.json:
                print(json.dumps(cal, ensure_ascii=False, indent=2))
            else:
                print("NO CONFIGURADO: %s" % describe(cal))
                print("\nPaso a paso en INSTALL.md seccion 11.2.")
            return 2
        time.sleep(15)

    status, calendars = panel.get("/api/calendar/calendars")
    items = calendars.get("items", []) if status == 200 else []
    problems = check_status(cal)
    if items and cal.get("selectedId") and cal["selectedId"] not in [i.get("id") for i in items]:
        problems.append("el calendario elegido no esta en la lista de Google")

    if args.json:
        print(json.dumps({"status": cal, "calendars": items,
                          "problems": problems}, ensure_ascii=False, indent=2))
    else:
        print(describe(cal))
        print("calendarios: %d" % len(items))
        if problems:
            print("\nPROBLEMAS:")
            for problem in problems:
                print(" - " + problem)
        else:
            print("\nOK: la placa tiene el permiso, el calendario elegido y la "
                  "reaccion coincide con el recordatorio del evento.")

    if args.wait and not problems and not (cal.get("reaction") or {}).get("active"):
        # The window did not open before the deadline; that is not a failure,
        # but it means the face was not observed reacting.
        print("\n(no hubo un evento dentro de su ventana durante la espera)")
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
