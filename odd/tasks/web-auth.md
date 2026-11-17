# Web authentication and the `/admin` configuration area

The panel served everything from `/` with no authentication at all: any device
that joined the access point could read the WiFi credentials and rewrite them.
It now has a login, keeps the owner's credentials in NVS seeded from a `.env`
file at build time, and moves every configuration page behind `/admin`, so `/`
is free for the screen picker and the games that come next.

## Specs

S1. Login with a session cookie — verbatim: "Login con cookie de sesión". A
dedicated page at `/login` posts the credentials, the device answers with an
`HttpOnly` session cookie, and a "Salir" action ends the session. Sessions live
in RAM, are absolute-time limited, and disappear on reboot; a reload of the page
keeps the session, a power cycle does not. No password is ever placed in the URL
or in client storage.

S2. Credentials configured in `.env` — verbatim: "user y contraseña se configura
en el .env". `ADMIN_USER` and `ADMIN_PASS` are read from the repository-root
`.env` by a build step that writes a git-ignored `esp32_gadget/admin_seed.h`.
The firmware stores and ships only a salted, iterated SHA-256 hash plus the
username: the plaintext password never appears in the binary, in NVS, in git, or
in the panel's HTML. `.env.example` documents both keys.

S3. Credentials changeable afterwards — verbatim: "se puede cambiar luego si se
desea". The panel's account card posts the current password, a new username and a
new password to `POST /api/admin/credentials`, the device re-hashes with a fresh
salt and persists it in NVS, and the new credentials keep working after a power
cycle. Changing them invalidates every other session. The `.env` value is only
the factory seed; it is never re-applied over credentials the owner already
changed.

S4. `/admin` holds every configuration — verbatim: "que el punto de acceso sea
/admin para todas las configuraciones". `GET /` serves a public placeholder that
links to `/admin`; every configuration page lives at `/admin`, and an
unauthenticated browser is sent to `/login` with its original target remembered.
`/login`, the placeholder and the static assets stay public; nothing else under
`/admin` does.

S5. Protection scope — verbatim: "/admin y todo lo que muta protegido; lecturas
públicas". Protected: `/admin` and every `POST /api/*` except `POST /api/session`
and the credential change (itself protected). Public: `GET /api/status`, `GET
/api/scan`, `GET /api/spotify`, `GET /api/session`, `/`, `/login` and the static
assets. An unauthenticated protected request answers `401` with
`{"ok":false,"error":"Sesion requerida"}` and mutates nothing, so a future
game page can read device state without a login while no anonymous client can
rewrite the WiFi or trigger a factory reset.

S6. No `.env` means `admin`/`admin` and a forced change — verbatim: "admin/admin
y forzar cambio". Without a `.env` the build still compiles and seeds
`admin`/`admin`, flagged as a default. `GET /api/session` reports `mustChange`,
the panel shows a warning and refuses to leave it, and only a successful
credential change clears the flag.

S7. Bounded brute force. Five consecutive failed logins lock the login endpoint
for 30 s; a successful login clears the counter. The lockout is global to the
device (no per-client identity is available behind the access point), and it
answers the same `401` shape as a wrong password so it is not an oracle.

S8. No plaintext, constant-time comparison. Credentials are stored as
`salt || iterated SHA-256(salt || user || pass)`; verification compares the
digests without an early exit, and no code path logs or emits the password.

S9. The serial console keeps its unrestricted in-process API — verbatim: "Consola
serie sin cambios". `api POST /api/...` in `console.cpp` calls `netHandleApi`
directly and must keep working without a session, because reaching the USB port
already means physical possession. Authentication is enforced in the HTTP
transport, never inside the pure dispatcher.

## Tasks

| ID | Spec | Route | Commit | Evidence |
| --- | --- | --- | --- | --- |
| T1 | S5, S7, S8 | inline: `admin_auth.*` + `tools/admin_auth_test.cpp` | `a47c28b` | 109 host checks green |
| T2 | S2, S3, S6 | inline: `tools/gen_admin_defaults.py`, `.env.example`, `.gitignore`, `settings.*` seeding | `a47c28b` | 33 host checks green, shared vector pinned |
| T3 | S1, S4, S5, S9 | inline: `net.cpp`/`net.h` transport, routes, session API | `470853c` | compile 46% flash, mock contract (L6) |
| T4 | S3, S4, S6 | inline: `web_ui/` login + admin + placeholder, `app.js`, repack | `470853c` | `node --check`, id cross-check, asset round trip |
| T5 | all | inline: docs (`INSTALL.md`, `ARCHITECTURE.md`), `run_tests.py`, arduino-cli compile | `7ca1b40` | 26 host tests, compile clean |

## Log

L1 (user, verbatim):
"Quiero mejorar el temita de la interfaz web. Me gustaria que tenga autenticacion, user y contraseña se configura en el .env, se puede cambiar luego si se desea, y que el punto de acceso sea /admin para todas las configuraciones

Esto con el motivo que el punto de entrada de la web, este el selector de pantalla, y que tambien alli haya juegos, que segun lo que pase en esos juegos reaccionará el buddy... 

Pero vamos primero con la autenticación."

L2 (user decisions, via choice prompt):
- Mechanism: "Login con cookie de sesión" — own login page, HttpOnly cookie, "Salir"; sessions in RAM, gone on reboot.
- Scope: "Admin + todo lo que muta" — /admin and every mutating POST /api/* protected; GET /api/status, /api/scan, GET /api/spotify public so the future selector/games can read state.
- Root: "Placeholder + link a /admin" — `/` is a public placeholder; the whole configuration panel moves to /admin.
- Without `.env`: "admin/admin y forzar cambio" — the build still compiles, the panel forces the change before use.

L3 (decision, build seed): the seed must not sit in a tracked file, so the
generator writes `esp32_gadget/admin_seed.h` (git-ignored) and `settings.cpp`
includes it behind `__has_include`. A clean checkout compiles with the
admin/admin fallback and no generated file.

L4 (decision, hashing): reuse the pure `spotifySha256` already pinned by
`tools/spotify_core_test.cpp`, wrapped in an iterated derivation, rather than
pulling mbedtls into a module that must also compile on the host.

L5 (slice boundary, C1): 1506 authored changed lines in one commit, over the
~400-line review slice. Kept together on purpose: the pure core, its two host
tests (about 700 of those lines) and the build step that must derive the same
digest are one reviewable unit — splitting the tests from the code they pin, or
the generator from the derivation it mirrors, would ship a slice whose whole
point (the two implementations agree) is unproven at that commit. C2 and C3 stay
inside the slice size.

L6 (evidence so far): host tests `python tools/run_tests.py` → 26 passed
(admin_auth_test 109 checks, admin_seed_test 33 checks, asset round trip clean).
Firmware: arduino-cli compile 46% flash / 16% RAM, no warnings from the new
modules. HTTP contract exercised against `tools/mock_gadget_api.py` on loopback:
`GET /` 200 (public placeholder), `GET /admin` 302 → `/login?next=/admin`,
`GET /api/status` 200 without a session, `POST /api/wifi` 401
`{"ok":false,"error":"Sesion requerida"}`, wrong password 401 with a fixed
message, right password 200 with `Set-Cookie: sid=…; HttpOnly; SameSite=Strict;
Max-Age=43200` and `mustChange:true`, `POST /api/admin/credentials` 200 clears
`mustChange`, logout 200 then `/admin` 302 again, and the new pair logs in.
`node --check` clean on `app.js` and `login.js`.
L7 (unavailable proof): the browser walkthrough could not run. The delegated
`gentle-ai-verify` task hung with no visible progress and was cancelled, and the
Chrome DevTools MCP answers every call with a 60 s timeout in this session, so
there is no browser here to drive. What stood in for it: `node --check` on both
scripts, a static cross-check that every id `app.js` touches exists in
`admin.html` (82 referenced, none missing) and in `login.js`/`login.html` (5, none
missing), that `#mustChangeBanner` contains no dismiss control, and the HTTP
contract checks in L6. On-board verification is unavailable from WSL too (the
board is on Windows COM5); flashing and the real `/admin` login still need one
manual pass on Windows.

L8 (slice boundary, C2): the code commit reports ~12k changed lines, of which
about 11k are `web_assets.h`, the generated byte arrays of the packed pages.
Hand-written code in that commit is around 800 lines across `net.*`, three pages
and two scripts, and it cannot be split further without a commit that either
serves `/admin` as a 404 or ships an asset header the directory no longer matches
(the route map, the pages and the packed header are one unit). The generated
header is not review reading: `tools/pack_web.py` produces it and
`tools/verify_assets.py` proves the round trip on every test run.

L9 (native review blocked, environment defect): `gentle_review inspect` reached
`start`, and START refused with `candidate-owner-parent-chmod-ineffective`: the
repository lives on a Windows mount (`/mnt/c`) that does not honor POSIX
permission changes, so the provider cannot secure its candidate views. The
provider reported `lineage_created: false` and `mutation_performed: false`, so no
authority was created or burned. Remediation, which is the owner's decision
because it changes the workspace layout: move the Git common directory to a
POSIX-metadata filesystem (for example a WSL-native checkout) or mount the drive
with metadata support, then START again. The lenses chosen for that review are
risk, resilience and reliability.

L10 (close-out): host tests 26 passed, firmware compiles at 46% flash / 16% RAM,
working tree clean. Nothing was flashed: the board is on Windows COM5 and
unreachable from this WSL session, so the first real login, the cookie round trip
and the two pages on a phone still need one manual pass after flashing.

L11 (on-board verification, COM5): flashed `470853c`+docs at 21:0x and the panel
looked right on the serial console (`api POST /api/session` with admin/admin → 200
`mustChange:true`, wrong password → 401) while every session was dead over HTTP:
after a successful login, `GET /admin` still redirected and `GET /api/session`
answered `authenticated:false`. The cause was in the ESP32 `WebServer`, not in the
code under test: `header()` only returns the headers registered with
`collectHeaders()`, and the default set is Authorization and ETag. `header
("Cookie")` was therefore always empty, so `adminSessionValid` never saw a token.

Fix (defect, not scope): register the one header the panel uses before `begin()`,
with the reason written next to it. Reflashed and repeated every check over the
LAN address of the board (192.168.100.75), which is the real transport:

| Check | Result |
| --- | --- |
| `GET /admin` sin sesión | 302 → `/login` |
| `GET /` | 200, placeholder with the selector notice |
| `GET /login` | 200, form |
| `GET /api/status` sin sesión | 200 (public read) |
| `POST /api/wifi` sin sesión | 401 `Sesion requerida` |
| `POST /api/factory` sin sesión | 401 `Sesion requerida` (**no** wiped the device) |
| `POST /api/session` admin/admin | 200, `Set-Cookie: sid=…; HttpOnly; SameSite=Strict; Max-Age=43200` |
| `GET /admin` con cookie | 200, panel with the Cuenta card |
| `GET /api/session` con cookie | `authenticated:true, user:admin, mustChange:true` |
| `POST /api/settings` con cookie | 200 |
| `POST /api/session/logout` → `GET /admin` | 200 → 302 |
| `POST /api/admin/credentials` con clave actual mala | 403 |
| … con clave nueva de 5 caracteres | 400 |
| … con usuario con espacio | 400 |
| 5 fallos de login seguidos | 401 los cinco, y la **clave correcta también 401** durante el lock |
| La misma clave 31 s después | 200 |

Not yet exercised on hardware: the *successful* credential change (S3) and the
`mustChange` banner clearing, because that leaves the board on a password this
session chose. Evidence for that path is the mock plus the host tests; S6 asked
for the owner's own password, so the change is the owner's to make or approve.
