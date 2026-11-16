"""Exercise every HTTP route the panel uses, over the real network.

The firmware keeps two tables that have to agree: the WebServer route
registration in net.cpp and the dispatcher in netHandleApi. When they drift, a
route the panel needs becomes unreachable and the request falls through to
handleNotFound, which answers "Metodo no permitido" -- a 405 that blames the
method for what is really a missing registration. That is exactly how the
Spotify card shipped broken: the serial console called netHandleApi directly and
never touched the transport, so the handler looked fine while the network path
was dead.

This script tests the transport. It does not care what a route answers as long as
it is neither 404 ("no such resource") nor 405 ("wrong method").

    python3 tools/panel_http_test.py 192.168.100.75

Destructive routes (factory reset, restart, score reset, steer reset) are skipped
by default; pass --destructive to include them on a device you do not mind
wiping.
"""

import json
import sys
import time
import urllib.error
import urllib.request

GET_ROUTES = ["/api/status", "/api/spotify"]

# Not asserted with the others. A WiFi scan blocks the WebServer for ~7 s and the
# open socket is dropped before it answers (measured: HTTP 000 at 7.3 s, and the
# next request waits 7.27 s behind it). That is a real defect in the scan path,
# unrelated to the Spotify routes, and hiding it here would make this test lie.
SLOW_ROUTES = ["/api/scan"]

# Path + a body that is valid enough to reach the handler but does nothing
# harmful. An empty object makes most handlers answer 400, which is fine: the
# point is that the route exists and accepts POST.
POST_ROUTES = [
    ("/api/wifi", {}),
    ("/api/time", {}),
    ("/api/ap", {}),
    ("/api/settings", {}),
    ("/api/screen", {}),
    ("/api/screens/order", {}),
    ("/api/screens/names", {}),
    ("/api/panel/colors", {}),
    ("/api/spotify/config", {}),
    ("/api/spotify/url", {}),
    ("/api/spotify/code", {}),
    ("/api/spotify/forget", {}),
]

# Left out because they mutate state a test should not destroy, or because they
# cut the very link the test is running over: /api/wifi/forget drops the station
# connection, so the response would never arrive.
DESTRUCTIVE_POST_ROUTES = [
    ("/api/wifi/forget", {}),
    ("/api/press", {}),
    ("/api/scores/reset", {}),
    ("/api/steer/reset", {}),
    ("/api/steer/swap", {}),
    ("/api/restart", {}),
    ("/api/factory", {}),
]

BAD_ROUTE = "/api/no-existe-esta-ruta"


def call(base, path, method, body=None, timeout=5):
    data = None
    headers = {}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    request = urllib.request.Request(base + path, data=data, headers=headers,
                                     method=method)
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.status, response.read()[:200].decode("utf-8", "replace")
    except urllib.error.HTTPError as err:
        return err.code, err.read()[:200].decode("utf-8", "replace")
    except OSError as err:
        return 0, str(err)


def call_retry(base, path, method, body=None):
    """One retry: the board resets on every serial connection, so the first
    request after a flash can land while it is still bringing WiFi and the panel
    up. A retry is not a fudge; two failures in a row is the real signal."""
    status, text = call(base, path, method, body)
    if status == 0 and method == "GET":
        time.sleep(0.5)
        status, text = call(base, path, method, body)
    return status, text


def wait_for_panel(base, timeout=40):
    deadline = time.time() + timeout
    while time.time() < deadline:
        status, _ = call(base, "/api/status", "GET")
        if status == 200:
            return True
        time.sleep(1)
    return False


def main(argv):
    base = argv[1] if len(argv) > 1 else "192.168.100.75"
    if not base.startswith("http"):
        base = "http://" + base
    include_destructive = "--destructive" in argv

    if not wait_for_panel(base):
        print("FAIL el panel no responde en %s" % base)
        return 1

    checks = [(path, "GET", None) for path in GET_ROUTES]
    checks += [(path, "POST", body) for path, body in POST_ROUTES]
    if include_destructive:
        checks += [(path, "POST", body) for path, body in DESTRUCTIVE_POST_ROUTES]

    failures = 0
    for path, method, body in checks:
        status, text = call_retry(base, path, method, body)
        # A route that is registered reaches its handler, which answers 200 or a
        # validated 400. 404 and 405 both mean the request never got there.
        ok = status not in (0, 404, 405)
        print("%-4s %-26s %-5s HTTP %-3s %s" % ("ok" if ok else "FAIL", path,
                                                method, status,
                                                "" if ok else text.replace("\n", " ")),
              flush=True)
        if not ok:
            failures += 1

    status, text = call(base, BAD_ROUTE, "GET")
    ok = status == 404
    print("%-4s %-26s %-5s HTTP %-3s %s" % ("ok" if ok else "FAIL", BAD_ROUTE,
                                            "GET", status,
                                            "" if ok else "expected 404"))
    if not ok:
        failures += 1

    status, text = call(base, "/api/spotify", "POST", {})
    ok = status == 405
    print("%-4s %-26s %-5s HTTP %-3s %s" % ("ok" if ok else "FAIL", "/api/spotify",
                                            "POST", status,
                                            "" if ok else "expected 405"))
    if not ok:
        failures += 1

    print("\n%s (%d problems)" % ("PASSED" if failures == 0 else "FAILED", failures))

    for path in SLOW_ROUTES:
        status, text = call(base, path, "GET", timeout=12)
        if status == 200:
            print("warn %-26s GET   HTTP 200 (respondio)" % path)
        else:
            print("warn %-26s GET   HTTP %-3s bloquea el servidor ~7 s; "
                  "defecto aparte del transporte de /api/spotify" % (path, status))

    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
