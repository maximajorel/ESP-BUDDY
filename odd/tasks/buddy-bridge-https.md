# Sofía over HTTPS (cloudflared)

The bridge was reachable only as a plain-HTTP LAN address. The owner now exposes
it through a Cloudflare Tunnel, so the same firmware has to keep the LAN path
alive and speak HTTPS to the public URL, without a second setting and without
turning certificate validation off.

## Specs

S1. Both transports, one field — verbatim: "La conexion con el bridge es por IP,
pero ahora voy a adicionar para que sea a traves de url por HTTPS, por lo que
seguramente necesitemos agregar la compatibilidad de ambas cosas en el firmware."
The stored value stays the single `bdUrl` setting: `http://<ip>:<port>` keeps
working exactly as today, `https://<host>` is the new case. No new NVS key, no
migration, no second field.

S2. No extra configuration on the device — verbatim: "Uso cloudflared, igual me
gustaria que simplemente con acceder sea suficiente". The URL and the token are
the only inputs. The HTTPS certificate is validated against the CA bundle
compiled into the core (`useBuiltinCACertBundle`), which already carries GTS Root
R1/R4 and ISRG Root X1 — the roots Cloudflare's edge uses. `setInsecure` is never
called and no PEM is shipped or uploaded.

S3. Poll cadence — verbatim: "1 s con sesión reusada (keep-alive)". The poll stays
at 1 s and the TLS session is reused, so the handshake is paid once per
connection instead of once per poll. The poll timeout must tolerate an internet
round trip.

S4. Cloudflare in front — verbatim: "creo que si va a tener bot fight lo
contemplemos". A Cloudflare block (403, a Bot Fight/MANAGED CHALLENGE, an Access
redirect to a login page) must be reported as its own cause, never as "the bridge
did not answer". A JS challenge cannot be solved by this firmware; the
documentation states the Cloudflare configuration that keeps the endpoint
reachable instead of pretending the device can pass it.

S5. Actionable failures. Each of these is reported distinctly, on the panel and in
`/api/status`: missing or invalid URL, no time sync (HTTPS certificate validation
needs a valid clock), TLS/hostname rejection, Cloudflare block, unauthorized
token, bridge answering an error status, bridge not answering at all.

S6. URL validation. Accept only `http://` or `https://` with a non-empty host and
a numeric port in range; reject anything else at the settings API with a reason
instead of storing a value that can only ever fail silently.

S7. Panel and docs tell the truth about the new case: the field accepts a URL,
the hint shows an `https://` example, and README/INSTALL/ARCHITECTURE describe
both transports.

S8. A dead link keeps the face — verbatim: "quiero que cuando no tenga conexion,
en vez de ahora que esta el logo de wifi. bastante feo encima, ponerlo en esa
esquina pero que los ojos sigan entendes? El logo de wifi que sea como los que
usan los celulares..". The eyes stay on the glass in the dim offline pose, and a
phone-style WiFi fan with a slash takes the HUD corner (top-right, the slot the
busy dots and the check already use) while the link is down. It appears for the
seven failure causes of S5 and never for `ok` or `unconfigured`, which keeps the
quiet face. One corner, one meaning: the mark replaces the phase animation while
it lasts and gives it back without a reset.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S6 | inline: pure `buddy_url.h` (parse, validate, endpoint build) + `tools/buddy_url_test.cpp`, test-first | 4673473 |
| T2 | S5,S4 | inline: pure `buddy_link.h` (cause classifier + wire names) + `tools/buddy_link_test.cpp`, test-first | 1d34370 |
| T3 | S1,S2,S3 | inline: `buddy_http.cpp/.h` transport — explicit scheme, plain client for http, one reused `WiFiClientSecure` with the built-in bundle for https, keep-alive, internet-sized timeouts, one mutex for the shared clients | 1d34370 |
| T4 | S1,S3,S5 | inline: `screen_buddy.cpp` polls and asks through the transport, 12 KB task stack, `ARDUINO_LOOP_STACK_SIZE` 16 KB for the ask, link cause in the snapshot and in `BuddyView` | 1d34370 |
| T5 | S6,S5 | inline: `settingsSetBuddyUrl` validates through `buddy_url.h`, `/api/status` exposes the cause, API returns the reason | c8f57c5 |
| T6 | S7 | inline: panel placeholder/hint/validation in `web_ui/`, repack `web_assets.h` | f6c0e76 |
| T7 | S7 | inline: README, INSTALL, ARCHITECTURE, and the Cloudflare configuration note | f6c0e76 |
| T9 | S8 | inline: `screen_buddy.cpp` corner mark (fan + slash, no arc rasteriser), `buddy_badge.h`/test comments, HUD slot precedence | pending |
| T8 | all | inline: host tests, build, flash, live check over HTTPS and over the LAN IP | 1d34370 |

## Log

L1 (verbatim): "La conexion con el bridge es por IP, pero ahora voy a adicionar
para que sea a traves de url por HTTPS, por lo que seguramente necesitemos
agregar la compatibilidad de ambas cosas en el firmware. Lo revisas porfa?"

L2 (verbatim, certificate decision): "Uso cloudflared, igual me gustaria que
simplemente con acceder sea suficiente"

L3 (verbatim, cadence decision): "1 s con sesión reusada (keep-alive)"

L4 (verbatim, Cloudflare front): "No se aun, tampoco lo configure lo hare mas
tarde, pero creo que si va a tener bot fight lo contemplemos..."

L5 (verbatim, authorization): "Sí, implementá el plan completo"

Evidence from the review that shaped the tasks: `HTTPClient::begin(String)` in
core 3.3.12 falls back to `TLSTraits(NULL)` and calls `setInsecure()` for an
`https://` URL (`HTTPClient.cpp:237-255`, `HTTPClient.cpp:94-101`), which is why
the transport must choose its own client; the core's `libmbedtls.a` bundle
carries GTS Root R1/R4 and ISRG Root X1, so Cloudflare's edge validates;
`screen_buddy.cpp:202` created the poller with an 8 KB stack while
`spotify.cpp:32` uses 12 KB for the same TLS work.

L6 (evidence, host): `python3 tools/run_tests.py` → 21 passed. The two new tests
pin the parser (both schemes, base paths, IPv6 literal, every rejection) and the
cause classifier (precedence: unconfigured > bad URL > no clock > TLS > block >
token > status > no answer).

L7 (evidence, device 192.168.100.75, firmware 1424034 bytes / 45%, RAM 16%):
driven through the serial console, which calls the same `netHandleApi` the panel
does.

- `{"buddyUrl":"192.168.100.64:8787"}` → `400 La URL del buddy necesita http:// o https://`
- `{"buddyUrl":"http://192.168.100.64:8787"}` with port 99999 → `400 El puerto de la URL del buddy no es valido`
- `https://example.com` → `link=http` (the request completed, the host answered 404):
  TLS through a public CA works on this core, which is the whole point of S2.
- `https://self-signed.badssl.com` → `link=tls`, detail `X509 - A fatal error ...`,
  and the core log prints `Failed to verify certificate`. Validation is on, not
  `setInsecure`.
- back to `http://192.168.100.64:8787` → `link=ok`, `state=bored`, bridge reachable.
  The LAN path is untouched.
- `POST /api/buddy/ask` against a non-bridge host → `502 El bridge respondio con
  un error`; against the self-signed host → `502 Rechazaron el certificado del
  bridge`. No agent run was started.
- `assets` on the device matches `tools/verify_assets.py`.

L8 (defect found and fixed, verbatim from the device): `CORRUPT HEAP: Bad head at
0x3fcc7b9c. Expected 0xabba1234 got 0x3fca5a7c`, raised by
`POST /api/buddy/ask` over https. Cause: the loop task's stack is 8 KB
(`cores/esp32/main.cpp` default) and the ask performs the TLS handshake on it. Fix:
`esp32_gadget/build_opt.h` with `-DARDUINO_LOOP_STACK_SIZE=16384`. Re-measured: three
consecutive https asks return the bridge's cause and the heap ends at 207216 free,
same as before the asks.

L9 (finding for the Cloudflare plan): the core's CA bundle (3.3.12,
`libmbedtls.a`) carries ISRG Root X1/X2, GTS Root R1/R4 and DigiCert Global Root
G2, but not Let's Encrypt's new `Root YE`. A host whose chain terminates there is
refused by the device while `curl` on a PC accepts it — that is what
`httpstat.us` showed. Consequence for the tunnel: after configuring the hostname,
confirm `link` in `/api/status`; a `tls` there with a chain that validates on a PC
means the bundle predates that root, and the answer is a chain the device trusts
(Cloudflare's GTS-backed edge) rather than `setInsecure`.

L10 (slice boundaries): 4673473 parser + tests; c8f57c5 settings/API validation;
1d34370 the transport, the lock, the loop stack and the cause classifier (Risk:
item 2, so it lands alone); f6c0e76 panel, packed assets and docs. The branch also
carries earlier work from `feat/buddy-life-pass`, which is why a "since master"
line count overstates this feature: the authored change here is ~1050 lines, and
the dominant diff is the regenerated `web_assets.h`.

L11 (not verified live): the `no-time` cause. Its classifier case is pinned by
`tools/buddy_link_test.cpp`, but forcing the device's SNTP state off on the bench
would have meant breaking the clock the other checks depend on. The wiring is the
three lines in `pollOnce`/`buddyAsk` that consult `netStatus().timeSynced`.

L12 (independent verification, gentle-ai-verify): one blocker, five advisories.
Verdicts: S1/S2 pass (`setInsecure` 0 hits; only explicit-client `begin`), S3 pass
on code (`setReuse(true)`, static client) and cannot-verify on the device, S4
partial, S5/S6/S7 pass with advisories, LAN path unchanged, `tools/run_tests.py`
21 passed.

The blocker was mine and real: an unreachable https host was reported as a
rejected certificate. `NetworkClientSecure` stores the whole-connect result in
`last_error`, and `ssl_client.cpp` returns `-1` for a TCP connect that never
reached a handshake, so "any negative code" was the wrong test. Fixed in 8d3184a
and the test gained the exact cases the verifier asked for (`-1`, `0`, `-11`,
`-0x7280` false; X509 codes true; plus `https, no answer -> offline`).

The first fix was still wrong and the device caught it: the band I guessed
(-0x2700..-0x2900) excluded `MBEDTLS_ERR_X509_FATAL_ERROR` (-0x3000), which is
exactly what the CA bundle's verify callback returns for a rejected chain, so
self-signed went back to reading `offline`. The band is the X509 module's own,
-0x2080..-0x3000, and the test now pins -0x3000.

b291c3a carries the other advisories: a base path with a space, query or fragment
is refused (it can only build a malformed request line), and a URL that parses but
does not persist reports a storage failure instead of "the URL cannot be empty".
The panel hint now shows both shapes as examples.

Left as decisions, not defects: a 503 is reported as `http` rather than `blocked`
(Cloudflare's documented status for a managed challenge is 403, and a 503 also
means a bridge that is down), and a failed lock take answers "no pude conectar"
instead of "busy".

L13 (re-measured on the device after both fixes, COM5, same binary as 8d3184a +
b291c3a): `https://192.168.100.64:9999` (nothing listening) → `link=offline`,
ask → `502 No pude conectar al bridge`; unresolvable host → `link=offline`;
`self-signed.badssl.com` → `link=tls` with the X509 text; LAN address → `link=ok`,
`state=bored`. A host switch sampled poll by poll (example.com `http` → self-signed
`tls`, seven samples, stable) rules out a keep-alive socket being reused across
hosts.

L14 (flakiness worth knowing): reading `/api/status` a fixed five seconds after
changing the URL can still show the previous cause, because a TLS attempt to a
host that has to fail costs a few seconds and the snapshot only changes when a
poll lands. Measuring a transition needs either a longer wait or sampling until
the value changes; the five-second read in the first live script produced one
false "regression" this way.

L15 (second verifier pass, gentle-ai-verify): one residual blocker (B1) and one
inaccurate comment, everything else pass.

B1: the X509 band fix was right but not sufficient. A transport failure does not
always come with a fresh TLS reason — `NetworkClientSecure::connect` returns on a
DNS failure *before* assigning `last_error`, and `stop_ssl_socket` preserves the old
value — so a stale X509 code from an earlier certificate failure could be blamed on
a host that does not resolve. My own L13 sequence could not catch it: the refused
port wrote `-1` first, so the stale code was already overwritten.

Fixed in 8ce9069 by resolving the https host before the request (the same lookup the
client performs, cached by lwIP; an IP literal skips it). A name that does not
resolve now fails before the TLS client is consulted at all. Re-measured on the
device in the order that used to break: `self-signed.badssl.com` → `tls`, then
`nohay-tal-host.invalid` → `offline` (stable over six samples) and the ask answers
"No pude conectar al bridge"; the same certificate failure afterwards still reads
`tls`, and `https://192.168.100.64:9999` reads `offline`.

The comment the verifier caught was true-but-wrong: PKCS5's codes (-0x2E00..-0x2F80)
do sit inside the X509 band numerically. They never appear on a handshake path, so
the band keeps its shape and the overlap is documented and pinned in
`tools/buddy_link_test.cpp` (band edges -0x0C80/-0x3080, PKCS5 inside).

Still open, recorded as decisions rather than defects: a 503 reads as `http` (a
managed challenge is documented as 403, and a 503 also means a bridge that is down);
a failed transport-lock take answers "no pude conectar" instead of "busy"; and the
keep-alive socket is trusted for a host the client no longer compares, which the
seven-sample host switch in L13 did not reproduce.

L16 (measurement lesson, again): reading the cause right after a URL change shows the
previous value for one or two seconds, and a "wait until it stops changing" loop
returns early when the change has not arrived yet. A patient fixed window (10 samples
at 2 s) is what distinguishes a wrong cause from a stale one; both false alarms in
this feature came from reading too early.

L17 (follow-up, requested after the feature landed): with the link down, the glass
draws a WiFi badge with a pulsing `!` instead of the eyes. Verbatim: "Necesito que
el buddy, si no se puede conectar a la api, no dibuje los ojos si no un simbolo del
icono del wifi con un ! al lado, puede ser?".

Decision recorded, not asked: `unconfigured` keeps the dim calm face instead of the
badge. The badge is an alarm, and "nobody paired me yet" is a pending setup — the
existing `buddy_anim.h` comment already designed that face as "not an error face".
`buddyShowsLinkBadge` (new `buddy_badge.h`) therefore answers yes for the seven
failure causes and no for `ok` and `unconfigured`; the eight/seven arithmetic was
the first thing the test caught (it is 7 of 9).

Drawing: three concentric filled discs with a black disc punched out and everything
below the centre cleared, which leaves the upper half of each ring — the WiFi glyph
without an arc rasteriser. The `!` is drawn as a thick bar plus a dot rather than
with the 5x7 font, because at scale 8 the font's one-pixel stem read as a hair next
to the arcs. It blinks on a 1200/1700 ms duty cycle. Arcs use the owner's colour
dimmed to 45%, the mark uses the existing hot colour.

The badge replaces the face before the pose, the anti-flicker hold and the HUD,
because all three describe a bridge state the device does not have; the HUD's step
is skipped rather than reset, and the pose ease already clamps its dt to 200 ms, so
the return to the eyes does not jump.

Device evidence (COM5, `dump` renders the framebuffer as ASCII, which is how this
board is verified): with the link broken the dump shows the three arcs and the mark;
five dumps in a row read `mark, mark, plain, mark, mark`, matching the blink duty
cycle; after restoring the LAN URL the dump shows the two eyes again (37 eye rows).
`tools/run_tests.py` 21 passed plus the new `buddy_badge_test`.

L18 (spec, added after the fact): the badge decision above became S8 in the Specs
section; it was written as prose in this log first, which is why the numbering
starts at eight.

L19 (verbatim, requirement change): "Viste que cuando le hablo a sofia en la
esquina superior derecha se ponen los tres puntitos, o el tilde, quiero que
cuando no tenga conexion, en vez de ahora que esta el logo de wifi. bastante feo
encima, ponerlo en esa esquina pero que los ojos sigan entendes? El logo de wifi
que sea como los que usan los celulares.."

The full-screen badge is gone. A dead link now keeps the eyes (the dim offline
pose the state ladder already draws) and puts a phone-style WiFi fan in the HUD
corner, slashed, in place of the phase animation. The slot keeps one meaning at a
time: link failure wins while it lasts, because a phase animation describes a
bridge the device cannot reach; the HUD step is still skipped, not reset, so the
animation resumes where it was. The game face still suppresses the mark, the same
decision L17 recorded.

Drawing: a fan is a pixel test, not an arc rasteriser — a pixel belongs to a band
when its distance from the apex sits between the two radii and its direction is
inside the ±45° wedge. Three bands (4-6, 9-11, 14-16 px) plus a 2 px dot at the
apex, in the owner's colour at half light so the corner never outshines the face.
The slash is a black 5 px gap with a 2 px hot line inside it, pulsing on a ~1.7 s
cosine: a steady alarm reads as a hung device, a pulse reads as something to fix.
The glyph occupies x 291-318, y 6-25 at the shipped 320x172 geometry.

Evidence: `tools/run_tests.py` 31 passed and the firmware compiles with the
shipped FQBN. Flashed to COM5 (hash of data verified) after the owner's explicit
authorization, then probed with `tools/probes/buddy_link_corner.txt`, which breaks
`buddyUrl` to `http://192.0.2.1:9` (TEST-NET-1, unroutable), waits 12 s and
restores the real URL in the same run.

- Healthy baseline (`link":"ok"`): `fbpix 285 4 36 22` is 22 rows of `0000`, i.e.
  the corner is clean and the phase HUD owns it.
- Broken link (`"online":false,"state":"offline","link":"offline"`): the same
  region shows 108 lit pixels in exactly two colours, `02cf` (the owner's
  `#00B4FF` at 50% = `(0,88,120)`) for the fan and `9102` (the hot `#FF3C1E` at a
  ~56% pulse = `(144,32,16)`) for the slash; the ASCII `dump 2` keeps both eye
  blobs on the glass with the mark in the top-right corner.
- Restored URL (`"link":"ok"`): corner back to `0000`, full-brightness eyes, and
  the check's green `04a3` still shows when the phase drops busy to idle, so the
  refactored `drawHud` did not cost the HUD its other states.
