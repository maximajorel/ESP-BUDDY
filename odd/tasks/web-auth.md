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

| ID | Spec | Route | Commit |
| --- | --- | --- | --- |
| T1 | S5, S7, S8 | inline: `admin_auth.*` + `tools/admin_auth_test.cpp` | C1 |
| T2 | S2, S3, S6 | inline: `tools/gen_admin_defaults.py`, `.env.example`, `.gitignore`, `settings.*` seeding | C1 |
| T3 | S1, S4, S5, S9 | inline: `net.cpp`/`net.h` transport, routes, session API | C2 |
| T4 | S3, S4, S6 | inline: `web_ui/` login + admin + placeholder, `app.js`, repack | C3 |
| T5 | all | inline: docs (`INSTALL.md`, `ARCHITECTURE.md`), `run_tests.py`, arduino-cli compile | C3 |

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
