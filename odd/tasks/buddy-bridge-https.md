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

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S6 | inline: pure `buddy_url.h` (parse, validate, endpoint build) + `tools/buddy_url_test.cpp`, test-first | — |
| T2 | S5,S4 | inline: pure `buddy_link.h` (cause classifier + wire names) + `tools/buddy_link_test.cpp`, test-first | — |
| T3 | S1,S2,S3 | inline: `buddy_http.cpp/.h` transport — explicit scheme, plain client for http, one reused `WiFiClientSecure` with the built-in bundle for https, keep-alive, internet-sized timeouts | — |
| T4 | S1,S3,S5 | inline: `screen_buddy.cpp` polls and asks through the transport, 12 KB task stack, link cause in the snapshot and in `BuddyView` | — |
| T5 | S6,S5 | inline: `settingsSetBuddyUrl` validates through `buddy_url.h`, `/api/status` exposes the cause, API returns the reason | — |
| T6 | S7 | inline: panel placeholder/hint/validation in `web_ui/`, repack `web_assets.h` | — |
| T7 | S7 | inline: README, INSTALL, ARCHITECTURE, and the Cloudflare configuration note | — |
| T8 | all | inline: host tests, build, flash, live check over HTTPS and over the LAN IP | — |

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
