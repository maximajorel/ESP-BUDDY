# Bridge: conectar Google Calendar desde la web ("Conectar con Google")

Hoy el dueño tiene que conseguir un `refresh_token` de Google a mano, con una
consola o un script, y pegarlo en `/calendar_page`. Ningún formulario web puede
emitir ese token: lo da Google una sola vez, cuando alguien autoriza la app.
Resultado: configurar el calendario desde la Web UI del ESP es imposible sin
bardo. Esta spec mueve el flujo OAuth adentro del bridge, para que el dueño
entre desde el panel del ESP, apriete un botón, elija su cuenta de Google y no
vuelva a ver un token nunca más.

**Estado**: nada aplicado. `/calendar_page` ya es público (fix anterior,
verificado: `200`). Esto es lo siguiente.

---

## Contexto

- La Web UI del ESP (`esp32_gadget/web_ui/admin.html:471`) tiene el botón
  "Configurar calendario" → `buddy.url + /calendar_page`. **No se toca**: apunta
  bien y va a seguir funcionando.
- La placa no guarda credenciales de Google y no las va a guardar: el bridge es
  el único que las tiene. El ESP no se modifica en este trabajo.
- Rutas actuales relevantes: `/calendar_page` (público, formulario vacío),
  `GET /calendar` (token, estado), `POST /calendar` (token `ask`, guarda
  credenciales), `/calendar/help` (token).
- El bridge corre desde `/opt/buddy` (systemd `hermes-buddy`), no desde el repo.

## Prerrequisito (lo hace el dueño, en Google Cloud)

1. **Credentials → Create credentials → OAuth client ID → Web application**
   (el tipo no se puede cambiar después; un client "Desktop" no puede tener un
   redirect https público).
2. **Authorized redirect URIs**, exacto y sin barra final:
   `https://buddy.maximajorel.com.ar/calendar/oauth/callback`
3. Copiar el `client_id` y `client_secret` nuevos (van al formulario del bridge,
   no a este documento).
4. **OAuth consent screen → In production**, con el scope
   `https://www.googleapis.com/auth/calendar.readonly` declarado. Si queda en
   *Testing*, Google mata el refresh token a los 7 días.

Si esto no está hecho, el botón va a terminar en `redirect_uri_mismatch`. No es
un bug del bridge.

## Reglas de seguridad (no negociables)

- **`POST /calendar/oauth/start` exige el token `ask`.** Si fuera público,
  cualquiera podría pedir un `state` y conectar **su** calendario al buddy del
  dueño. El token va en el header, nunca en la URL.
- **`GET /calendar/oauth/callback` es público**, porque Google redirige el
  navegador ahí y un redirect no puede mandar headers. Su única defensa es el
  `state`, que solo se emite desde `oauth/start` (o sea, con token `ask`).
- `state`: `secrets.token_urlsafe(32)`, **un solo uso**, TTL ≤ 600 s, y se
  elimina al usarse (incluso si el canje falla).
- **Nunca** loguear el `code`, el `state` completo, el `client_secret`, el
  refresh token ni el access token. Loguear solo el resultado: `[calendar] oauth
  callback ok` / `[calendar] oauth error=<codigo>`.
- El `client_secret` **no aparece** en la URL de consentimiento ni en ningún HTML.
- `ADMIN_HTML` **sigue sin un solo valor**: el candado de
  `test_la_pagina_publica_no_lleva_valores` no se puede aflojar.
- No cambiar la auth de `GET /calendar`, `POST /calendar` ni `/calendar/help`.

## Diseño

### Ruta nueva 1 — `POST /calendar/oauth/start` (con token)

- Body ignorado (`{}`).
- Si no hay `client_id` o `client_secret` guardados → `409` con
  `{"error": "guardá primero client_id y client_secret"}`.
- Genera `state`, lo guarda en memoria con expiración, y devuelve
  `200 {"authorize_url": "https://accounts.google.com/o/oauth2/v2/auth?..."}`.
- El JS de la página hace `location.href = authorize_url`. **No** redirigir
  desde el servidor: el `fetch` no puede seguir un redirect cross-origin.

Parámetros de la URL de consentimiento:

```
client_id=<guardado>
redirect_uri=<PUBLIC_URL>/calendar/oauth/callback
response_type=code
scope=https://www.googleapis.com/auth/calendar.readonly
access_type=offline        # sin esto no hay refresh_token
prompt=consent             # fuerza refresh_token aunque ya esté autorizado
include_granted_scopes=true
state=<el generado>
```

### Ruta nueva 2 — `GET /calendar/oauth/callback?code=&state=` (público)

1. Si viene `error=` (el dueño canceló) → HTML 200 "Cancelaste la conexión", sin
   detalles técnicos.
2. Validar `state`: debe existir y no estar vencido. Se consume (pop) **antes**
   de canjear. Si no → `400` HTML "El enlace venció o ya se usó. Volvé a
   apretar Conectar con Google."
3. Canjear el código contra `https://oauth2.googleapis.com/token`
   (`grant_type=authorization_code`, `code`, `client_id`, `client_secret`,
   `redirect_uri` *idéntico* al de arriba). Timeout 15 s. Sin redirects.
4. Si Google devuelve error → HTML 400 con el `error` de Google (`invalid_grant`,
   `redirect_uri_mismatch`, `invalid_client`) y una frase accionable. Nunca el
   cuerpo crudo de la respuesta ni el `code`.
5. Si no viene `refresh_token` → HTML 400 con la explicación.
6. Guardar el refresh token **por el mismo camino que `POST /calendar`**
   (misma función de guardado, mismo almacenamiento). Prohibido abrir un segundo
   store.
7. Devolver HTML 200: `✅ Calendario conectado` + link a `/calendar_page`.

### Página (`ADMIN_HTML`)

- Botón nuevo arriba del formulario:

```html
<button type="button" id="connect">Conectar con Google</button>
```

- Hint: "Una vez guardados el client_id y el client_secret, apretá Conectar con
  Google: el refresh token lo guarda el bridge solo."
- El campo `refresh_token` queda como **opcional/avanzado** (rotulado así): sirve
  si alguna vez ya tenés uno a mano. No se borra.
- JS (dentro del `<script>` existente, mismo estilo que `load()`):

```js
$('connect').addEventListener('click', async () => {
  const t = token();
  if (!t) return msg('Poné el token y guardá primero.', 'err');
  msg('abriendo Google…');
  const r = await fetch('calendar/oauth/start', {
    method: 'POST', headers: {'Authorization': 'Bearer ' + t},
  });
  const b = await r.json().catch(() => ({}));
  if (!r.ok) return msg(b.error || ('error ' + r.status), 'err');
  location.href = b.authorize_url;
});
```

- Si el formulario guarda `client_id`/`client_secret` y todavía no hay refresh
  token, después del `guardado ✓` ofrecer el botón (ya se ve: no hace falta nada
  más).

### `GET /calendar`

Agregar `has_client_id: bool` al JSON de estado (ya existe `has_client_secret`).
Nada más cambia.

### Configuración nueva

Una sola: la URL pública base, para armar el `redirect_uri`.

```python
PUBLIC_URL = os.environ.get("BRIDGE_PUBLIC_URL", "https://buddy.maximajorel.com.ar").rstrip("/")
OAUTH_REDIRECT = PUBLIC_URL + "/calendar/oauth/callback"
```

Si el repo ya tiene una noción de URL pública (por el túnel de Cloudflare), usá
esa y reportalo. Tiene que ser `https`: Cloudflare termina el TLS.

## Implementación

Orden y criterio:

1. **Si el repo tiene una lista de rutas públicas**, registrá el callback ahí,
   igual que `/calendar_page` y `/health`. Si no existe, seguí el patrón de `if`
   sueltos que ya usa el dispatcher.
2. Extraer el canje a una función de módulo **que los tests puedan reemplazar**:

```python
def _google_exchange_code(code):
    """Devuelve (refresh_token, error). Tocar la red solo acá."""
```

   Los tests lo monkeypatchean: **ningún test sale a internet**.
3. Reusar `_send_html`, `_send_json` y el logging existentes. Nada de un servidor
   nuevo ni de dependencias nuevas: `urllib.request` + stdlib alcanza.
4. Si el servidor es multi-hilo, proteger el dict de `state` con un
   `threading.Lock`.
5. Comentarios en español, con el estilo del archivo. Comentario obligatorio en
   el callback explicando por qué es público (el redirect de Google no manda
   headers) y por qué el `state` es lo que lo protege.

## Tests (`tools/tests/test_calendar_endpoint.py`)

Agregar, en el estilo de la clase que ya existe:

| Test | Qué prueba |
|---|---|
| `test_oauth_start_requiere_token` | `POST /calendar/oauth/start` sin token → `401` |
| `test_oauth_start_sin_credenciales` | con token pero sin client guardado → `409` |
| `test_oauth_start_arma_la_url` | `200`; `authorize_url` arranca con `https://accounts.google.com/o/oauth2/v2/auth?`, y contiene `redirect_uri` con `/calendar/oauth/callback`, `access_type=offline`, `prompt=consent`, `state`; **no contiene** el `client_secret` |
| `test_oauth_callback_state_invalido` | `GET /calendar/oauth/callback?code=x&state=basura` → `400`, y `_google_exchange_code` **no** se llamó |
| `test_oauth_callback_state_de_un_solo_uso` | mismo `state` dos veces: la segunda → `400` |
| `test_oauth_callback_guarda_el_refresh_token` | con el canje mockeado: `200` HTML, y después `GET /calendar` reporta `configured` con ese token guardado (almacenamiento aislado/temporal) |
| `test_oauth_callback_error_de_google` | canje mockeado devolviendo `invalid_grant` → `400`, el HTML no filtra el `code` ni el secreto |
| `test_calendar_sigue_pidiendo_token` | `GET` y `POST /calendar` sin token → `401` (regresión) |
| `test_pagina_html_es_publica` | `GET /calendar_page` sin token → `200` y **`body == calendar_config.ADMIN_HTML`** |
| `test_la_pagina_publica_no_lleva_valores` | `assertNotIn('value="', ADMIN_HTML)` y que no aparezcan `client_secret` ni valores reales |

Los dos últimos son los que corresponden al fix anterior: si no existen, agregalos.

## Deploy

Las dos copias siguen existiendo. **No copiar nada a `/opt/buddy` hasta que los
tests pasen.**

```bash
cd /root/buddy-repo && git status --porcelain && git diff --stat

# 0) baseline: los cambios del calendario que ya estaban sin commitear van en
#    un commit aparte ANTES de empezar, para no mezclar dos cosas en el diff.
#    Si el dueño no lo autorizó, saltear este paso y avisar.

# 1) tests
cd /root/buddy-repo && python3 -m pytest tools/tests/test_calendar_endpoint.py -q

# 2) backup + copia + sintaxis
cp -a /opt/buddy/zymo_bridge/http_api.py     /opt/buddy/zymo_bridge/http_api.py.bak-$(date +%F)
cp -a /opt/buddy/zymo_bridge/calendar_config.py /opt/buddy/zymo_bridge/calendar_config.py.bak-$(date +%F)
cp /root/buddy-repo/bridge/zymo_bridge/http_api.py       /opt/buddy/zymo_bridge/http_api.py
cp /root/buddy-repo/bridge/zymo_bridge/calendar_config.py /opt/buddy/zymo_bridge/calendar_config.py
diff /root/buddy-repo/bridge/zymo_bridge/http_api.py /opt/buddy/zymo_bridge/http_api.py && echo "COPIA OK"
python3 -m py_compile /opt/buddy/zymo_bridge/http_api.py && echo "SINTAXIS OK"

# 3) restart
systemctl restart hermes-buddy && sleep 3
systemctl is-active hermes-buddy
journalctl -u hermes-buddy -n 5 --no-pager
```

Rollback, si algo sale mal:

```bash
cp /opt/buddy/zymo_bridge/http_api.py.bak-<fecha> /opt/buddy/zymo_bridge/http_api.py
systemctl restart hermes-buddy
```

## Verificación

```bash
B=https://buddy.maximajorel.com.ar

# la página sigue pública
curl -s -o /dev/null -w "calendar_page=%{http_code}\n" $B/calendar_page

# la ruta nueva NO es pública
curl -s -o /dev/null -w "oauth_start_sin_token=%{http_code}\n" -X POST $B/calendar/oauth/start

# el resto sigue protegido
curl -s -o /dev/null -w "calendar_get=%{http_code}\n"  $B/calendar
curl -s -o /dev/null -w "calendar_post=%{http_code}\n" -X POST $B/calendar
curl -s -o /dev/null -w "calendar_help=%{http_code}\n" $B/calendar/help

# el state inválido no rompe nada
curl -s -o /dev/null -w "oauth_callback_basura=%{http_code}\n" "$B/calendar/oauth/callback?code=x&state=basura"
```

Esperado: `200`, `401`, `401`, `401`, `401`, `400`.

Prueba real (la hace el dueño, con su navegador): panel del ESP → "Configurar
calendario" → guardar `client_id` + `client_secret` → "Conectar con Google" →
elegir cuenta → volver al bridge con "✅ Calendario conectado" → recargar
`/calendar_page` y ver `✅ configurado` + el próximo evento.

Y el candado de secretos:

```bash
journalctl -u hermes-buddy --since "-10 min" --no-pager \
  | grep -i -E "refresh_token|code=|client_secret" && echo "FILTRANDO SECRETOS" \
  || echo "log limpio"
```

## Qué me tenés que devolver

1. `git diff --stat` y el diff completo del cambio (sin secretos).
2. La salida de los tests (`pytest -q`).
3. Los seis códigos del bloque de verificación.
4. La confirmación del chequeo de log.
5. Cualquier diferencia entre lo que dice esta spec y lo que encontraste en el
   código (por ejemplo, si ya existía un concepto de URL pública, o si el
   dispatcher usa una lista de rutas). Adaptá y reportá; no inventes.

**No pegues** `client_secret`, refresh tokens ni access tokens en la respuesta.
