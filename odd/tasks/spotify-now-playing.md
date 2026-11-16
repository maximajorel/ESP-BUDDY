# Spotify now-playing screen

Fifth screen on the gadget that shows the track the owner is listening to on
Spotify, fed by the Spotify Web API over the station (STA) WiFi link, with its
credentials configured from the existing web admin panel.

## Specs

S1. New screen — verbatim: "En el firmware que estamos trabajando, podemos
agregar una pantalla que sea de la musica que estoy escuchando en Spotify? Y que
basicamente en la UI web ahi configure si necesito mi cuenta o la API key segun
corresponda? aprovechemos que tenemos la posibilidad de conectarnos a wifi"

S2. Configuration lives in the web panel — verbatim (S1): "en la UI web ahi
configure si necesito mi cuenta o la API key segun corresponda". The panel is the
only place credentials are entered; nothing is compiled in.

S3. Uses the STA WiFi link — verbatim (S1): "aprovechemos que tenemos la
posibilidad de conectarnos a wifi". The screen must degrade honestly, naming
which link is missing (AP-only, STA down, no token), never show a blank frame or
a fake track.

S4. Authentication is OAuth 2.0 authorization-code **with PKCE and no
client_secret**. Decided by the owner, option verbatim: "PKCE sin client_secret
(recomendado)" — "Panel pide client_id + pegar el code una vez. El secreto nunca
toca la placa." The `client_secret` must never be stored on the device.

S5. Reading only — decided by the owner, option verbatim: "Solo lectura" — "La
pantalla muestra; el botón corto no hace nada en esta pantalla. No requiere
Premium." No playback scopes, no `user-modify-playback-state`.

S6. Album art — decided by the owner, option verbatim: "Texto + carátula 64x64".
Track title, artist, album art, playback state and progress.

S7. Landscape — carried over verbatim from the earlier session: "otra
aclaracion, me gustaria usar en formato horizontal la pantalla!" The music screen
draws on the existing 320x172 canvas with the shared chrome.

S8. Install documentation — carried over verbatim: "crea una carpeta con todo
eso proyecto y luego documentacion sobre como instalarlo". The Spotify app
registration, redirect URI and the new dependency are documented in
`esp32_gadget/INSTALL.md`.

S9. The panel must be reachable over the home network, without leaving it —
verbatim: "no podemos una vez conectado al WIFI, indicar la IP del esp en el wifi
para acceder a la WEB UI desde alli sin desconectarme del wifi? Entonces o tengo
la opcion de conectarme localmente con la IP del wifi del ESP, y luego de
configurar el wifi, conectarme usando la ip local que asigna el router al ESP".
Both routes must exist at the same time, and the home-network address must be
the one the device advertises once the station link is up.

## Tasks

| ID | Spec | Route | Commit |
|----|------|-------|--------|
| T1 | S6,S4 | inline: pure `spotify_parse` + `spotify_pkce` with host tests (RFC 7636 vector) | — |
| T2 | S2,S4 | inline: NVS `spotify` namespace (client_id, tokens, expiry), never the settings blob | — |
| T3 | S4 | inline: OAuth PKCE — authorize URL, code exchange, refresh, token rotation, error mapping | — |
| T4 | S3,S4 | inline: HTTPS client for accounts/api.spotify.com, 204/401/429 handling with backoff | — |
| T5 | S3 | inline: background poll task + mutex snapshot so TLS never stalls the UI | — |
| T6 | S1,S3,S7 | inline: `screen_music` in the registry, one honest state per failure | — |
| T7 | S6 | delegated: 64x64 album art via JPEGDEC, cached per track id | — |
| T8 | S2 | delegated: Spotify card in the web panel (client_id, authorize link, code paste, forget) | — |
| T9 | S1,S2,S8 | inline: API endpoints, console commands, docs, repacked `web_assets.h` | — |
| T11 | S9 | inline: advertise the home-network address on the RED screen and in the panel | — |
| T12 | S9,S8 | inline: document both ways into the panel | — |
| T10 | all | inline: verification — host tests, mock panel, hardware when the board is back | — |

No `Commit` column values yet: the workspace is not a git repository, so there is
no work-unit commit to record. Raised with the owner; nothing is committed.

## Log

L1 (verbatim): "Excelente! En el firmware que estamos trabajando, podemos
agregar una pantalla que sea de la musica que estoy escuchando en Spotify? Y que
basicamente en la UI web ahi configure si necesito mi cuenta o la API key segun
corresponda? aprovechemos que tenemos la posibilidad de conectarnos a wifi"

L2 (verbatim, decisions): "1. ¿Cómo autenticamos contra Spotify? — PKCE sin
client_secret (recomendado) / 2. ¿Qué muestra la pantalla de música? — Texto +
carátula 64x64 / 3. ¿El botón BOOT controla la reproducción? — Solo lectura /
4. Para verificar en hardware hace falta un WiFi con internet. ¿Cuál usamos? —
(custom) Ya esta conectado al wifi! Asi que podemos continuar sin problema"

L3: Researched against the live Spotify Web API documentation. Findings that
shape the design:
- Spotify has no API key. User-scoped data needs OAuth 2.0 authorization code.
- PKCE (`S256`) is supported and the token endpoint needs only `client_id`, so no
  secret has to live on the device (S4).
- Redirect URIs must be HTTPS **or** an explicit loopback IPv4/IPv6
  (`http://127.0.0.1:PORT`); `localhost` is rejected. The ESP32 cannot receive
  that redirect, so the code is pasted into the panel once and exchanged with the
  verifier the device already holds.
- `GET /me/player/currently-playing` still returns `item.name`,
  `item.artists[].name`, `item.album.images[]`, `duration_ms`, `progress_ms` and
  `is_playing`. The February 2026 removals hit `popularity`,
  `available_markets`, `album_group`, `label` and similar, not these. `preview_url`
  is deprecated, which does not matter because playback is out of scope (S5).
- Rate limiting is a rolling 30 s window; a 429 carries `Retry-After`.

L4: Environment facts. `JPEGDEC` 1.8.5 (Larry Bank) is available through
`arduino-cli lib search JPEGDEC` and is the chosen decoder for T7. Installed
libraries are currently Adafruit NeoPixel 1.15.5 and ArduinoJson 7.4.3 only, so
T7 adds a dependency that INSTALL.md must document (S8).

L6: Board reconnected by the owner (Windows enumerates COM5 again) and the
station link verified by serial: `sta state conectado`, `ssid=VICTORWIFI`,
`ip=192.168.100.75`, `rssi=-58`, NTP synced (`2026-11-16 09:12:33`). The earlier
`reason 15` was a stale password from a previous session. S3's premise holds, and
hardware verification is unblocked.

L7: TLS decision, from the installed core rather than a guess.
`NetworkClientSecure.h` publishes `useBuiltinCACertBundle()`, so the device
validates against the CA bundle compiled into the Arduino core. That rules out
both shipping PEMs that silently expire and degrading to `setInsecure()`.
One shared `WiFiClientSecure` is reused for every Spotify call: a handshake on
this board costs ~40 KB of heap for over a second, which an 8-10 s poll cadence
would otherwise pay every time.

L9 (verbatim): "Ok osea esta flasheandose el firmware? Puedo configurar en la web
ui las credenciales? Luego otro requisito que no se si se puede, es medio un
embole desconectarme del wifi de mi casa para conectarme al ESP, no podemos una
vez conectado al WIFI, indicar la IP del esp en el wifi para acceder a la WEB UI
desde alli sin desconectarme del wifi? Entonces o tengo la opcion de conectarme
localmente con la IP del wifi del ESP, y luego de configurar el wifi, conectarme
usando la ip local que asigna el router al ESP"

L10: The capability S9 asks for already exists — `WiFi.mode(WIFI_AP_STA)` has run
both radios since the first build. Measured from WSL while the board was up:
`curl http://192.168.100.75/` returned **HTTP 200 in 32 ms** and
`/api/status` answered with `sta.connected=true` and `screenName=MUSICA`, with
the access point still active. The gap was not capability, it was
advertisement: the RED screen showed only `http://192.168.4.1` and hid the home
IP in a small status line. T11 fixes the disclosure, not the networking.

L8: Implementation status. T1-T6 implemented; T1 verified by host tests
(`tools/spotify_core_test.cpp`: 14 groups, 51 checks, including the RFC 7636
challenge vector and the FIPS 180-2 SHA-256 vectors). T2-T6 verified by compile
plus console/hardware checks, see T9/T10. Album art (T7) and the panel card (T8)
delegated to two bounded writers with disjoint edit surfaces.
