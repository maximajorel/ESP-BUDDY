//! La única capa que habla HTTP con el equipo (S12).
//!
//! El firmware no manda `Access-Control-Allow-Origin`, así que un `fetch` desde
//! el webview no puede llegarle nunca. Todo sale de acá, y el frontend sólo ve
//! JSON ya convertido a tipos.
//!
//! Qué es público y qué pide sesión lo decide el equipo, no la app
//! (`adminRequiresSession` en `admin_auth.cpp`): `GET /api/status`, `POST
//! /api/screen` y `POST /api/focus` son públicos; todo `/api/calendar*` y
//! `POST /api/settings` piden la cookie `sid`.

use crate::store::Connection;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::time::Duration;

/// Corto a propósito: el popover es una ventana que el dueño abre y mira. Una
/// espera de treinta segundos con la ventana en blanco no es paciencia, es un
/// cuelgue. El equipo contesta en milisegundos en la LAN.
const CONNECT_TIMEOUT: Duration = Duration::from_secs(3);
const REQUEST_TIMEOUT: Duration = Duration::from_secs(5);

/// Error de una llamada al equipo, tal como lo va a leer el popover.
#[derive(Debug, Clone, Serialize)]
pub struct ApiError {
    /// HTTP status del equipo, o 0 si ni siquiera hubo respuesta.
    pub code: u16,
    pub message: String,
}

impl ApiError {
    pub(crate) fn transport(message: impl Into<String>) -> Self {
        Self {
            code: 0,
            message: message.into(),
        }
    }
}

/// Lo que el popover necesita de `GET /api/status`. Los nombres son los del
/// firmware (`screenName`, `remainingMs`), no una traducción.
#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Status {
    pub screen: u8,
    pub screen_name: String,
    #[serde(default)]
    pub names: Vec<String>,
    #[serde(default)]
    pub order: Vec<u8>,
    #[serde(default = "default_version")]
    pub version: String,
    pub focus: Focus,
    #[serde(default)]
    pub sta: Sta,
}

fn default_version() -> String {
    "—".to_string()
}

#[derive(Debug, Clone, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Focus {
    pub active: bool,
    #[serde(default)]
    pub finished: bool,
    /// Duración pedida, en segundos. Es el número exacto; `minutes` es el mismo
    /// dato redondeado para los chips del popover.
    #[serde(default)]
    pub seconds: u32,
    #[serde(default)]
    pub remaining_ms: u32,
}

#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Sta {
    #[serde(default)]
    pub configured: bool,
    #[serde(default)]
    pub connected: bool,
    #[serde(default)]
    pub ssid: String,
    #[serde(default)]
    pub ip: String,
    #[serde(default)]
    pub rssi: i32,
}

/// Un evento de la agenda tal como lo publica `GET /api/calendar/events` (S3).
///
/// Las horas viajan como epoch UTC y el formato lo pone la app con el `tz` del
/// contador: el equipo no manda texto formateado ni la app inventa zonas.
#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CalendarEvent {
    #[serde(default)]
    pub summary: String,
    #[serde(default)]
    pub location: String,
    pub start_epoch: i64,
    pub end_epoch: i64,
    #[serde(default)]
    pub lead_min: i32,
    #[serde(default)]
    pub all_day: bool,
}

/// Lo que devuelve `GET /api/calendar/events`: los eventos que el equipo ya
/// tiene en memoria y su offset horario. `count` es el del firmware; la app
/// dibuja `events` en el orden que llega.
#[derive(Debug, Clone, Default, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct CalendarEvents {
    #[serde(default)]
    pub tz: i32,
    /// La medianoche local del equipo, hoy y mañana, en epoch UTC. El día lo
    /// dice el equipo porque él conoce el offset del dueño; un equipo con el
    /// firmware viejo no los manda (0) y la app los deriva de `tz`.
    #[serde(default)]
    pub day_start: i64,
    #[serde(default)]
    pub day_end: i64,
    #[serde(default)]
    pub count: u32,
    /// Ausente se lee como lista vacía: un equipo sin eventos no puede dejar el
    /// popover sin tarjeta.
    #[serde(default)]
    pub events: Vec<CalendarEvent>,
}

pub struct Api {
    base: String,
    http: reqwest::Client,
    session: Option<String>,
}

impl Api {
    pub fn base_url(&self) -> &str {
        &self.base
    }

    pub fn new(connection: &Connection, session: Option<String>) -> Result<Self, ApiError> {
        let http = reqwest::Client::builder()
            .connect_timeout(CONNECT_TIMEOUT)
            .timeout(REQUEST_TIMEOUT)
            // Sin cookie jar (la feature `cookies` no está habilitada): el `sid`
            // lo guarda la app y lo manda explícito, así se puede mostrar la
            // sesión y borrarla desde el popover.
            .build()
            .map_err(|err| ApiError::transport(format!("No pude preparar el cliente HTTP: {err}")))?;
        Ok(Self {
            base: connection.base_url(),
            http,
            session,
        })
    }

    async fn call(&self, method: reqwest::Method, path: &str, body: Option<&Value>) -> Result<Value, ApiError> {
        let url = format!("{}{}", self.base, path);
        let mut request = self.http.request(method, &url);
        if let Some(token) = &self.session {
            request = request.header(reqwest::header::COOKIE, format!("sid={token}"));
        }
        if let Some(payload) = body {
            request = request.json(payload);
        }
        let response = request.send().await.map_err(|err| describe(&err))?;
        let code = response.status().as_u16();
        let text = response.text().await.map_err(|err| describe(&err))?;
        let parsed: Value = serde_json::from_str(&text).unwrap_or(Value::Null);

        if !(200..300).contains(&code) {
            return Err(ApiError {
                code,
                message: device_message(&parsed, code),
            });
        }
        Ok(parsed)
    }

    pub async fn status(&self) -> Result<Status, ApiError> {
        let value = self.call(reqwest::Method::GET, "/api/status", None).await?;
        serde_json::from_value(value)
            .map_err(|_| ApiError::transport("El equipo contestó algo que no entiendo"))
    }

    pub async fn set_screen(&self, index: u8) -> Result<(), ApiError> {
        let body = serde_json::json!({ "index": index });
        self.call(reqwest::Method::POST, "/api/screen", Some(&body))
            .await
            .map(|_| ())
    }

    /// `seconds = 0` cancela, igual que `POST /api/focus {"seconds":0}`.
    pub async fn set_focus(&self, seconds: u32) -> Result<(), ApiError> {
        let body = serde_json::json!({ "seconds": seconds });
        self.call(reqwest::Method::POST, "/api/focus", Some(&body))
            .await
            .map(|_| ())
    }

    /// Todavía sin llamador: el ta-te-ti (T6) es quien hace reaccionar la cara.
    #[allow(dead_code)]
    pub async fn set_face(&self, mood: &str) -> Result<(), ApiError> {
        let body = serde_json::json!({ "mood": mood });
        self.call(reqwest::Method::POST, "/api/face", Some(&body))
            .await
            .map(|_| ())
    }

    /// `POST /api/session` y devuelve el `sid` de la cookie. `None` significa
    /// que el equipo aceptó pero no abrió sesión, cosa que no debería pasar.
    pub async fn login(&self, user: &str, pass: &str) -> Result<Option<String>, ApiError> {
        let url = format!("{}/api/session", self.base);
        let response = self
            .http
            .post(&url)
            .json(&serde_json::json!({ "user": user, "pass": pass }))
            .send()
            .await
            .map_err(|err| describe(&err))?;
        let code = response.status().as_u16();
        let cookies: Vec<String> = response
            .headers()
            .get_all(reqwest::header::SET_COOKIE)
            .iter()
            .filter_map(|value| value.to_str().ok().map(str::to_string))
            .collect();
        let text = response.text().await.map_err(|err| describe(&err))?;
        let parsed: Value = serde_json::from_str(&text).unwrap_or(Value::Null);

        if !(200..300).contains(&code) {
            return Err(ApiError {
                code,
                message: device_message(&parsed, code),
            });
        }
        Ok(cookies.iter().find_map(|value| extract_sid(value)))
    }

    pub async fn calendar(&self) -> Result<Value, ApiError> {
        self.call(reqwest::Method::GET, "/api/calendar", None).await
    }

    /// `GET /api/calendar/events` (S3): la agenda del día que el equipo tiene en
    /// RAM. Cae bajo `/api/calendar`, así que también pide sesión del panel.
    pub async fn events(&self) -> Result<CalendarEvents, ApiError> {
        let value = self
            .call(reqwest::Method::GET, "/api/calendar/events", None)
            .await?;
        serde_json::from_value(value)
            .map_err(|_| ApiError::transport("El equipo contestó algo que no entiendo"))
    }
}

/// El mensaje de error del equipo es texto para el dueño ("Falta el indice de
/// pantalla"), así que se muestra tal cual. Un cuerpo sin `error` cae en un
/// mensaje por código.
fn device_message(body: &Value, code: u16) -> String {
    if let Some(message) = body.get("error").and_then(Value::as_str) {
        if !message.is_empty() {
            return message.to_string();
        }
    }
    match code {
        401 => "El equipo pide sesión del panel".to_string(),
        404 => "Ese recurso no existe en el firmware instalado".to_string(),
        _ => format!("El equipo contestó {code}"),
    }
}

fn describe(err: &reqwest::Error) -> ApiError {
    if err.is_timeout() {
        ApiError::transport("El equipo no contestó a tiempo")
    } else if err.is_connect() {
        ApiError::transport("No encuentro el equipo en esa dirección")
    } else if err.is_decode() {
        ApiError::transport("El equipo contestó algo que no entiendo")
    } else {
        ApiError::transport(format!("No pude hablar con el equipo: {err}"))
    }
}

/// `sid=abc123; Path=/; HttpOnly` → `abc123`. Se parsea a mano porque el valor
/// es el único dato que hace falta de toda la cookie.
fn extract_sid(set_cookie: &str) -> Option<String> {
    let first = set_cookie.split(';').next()?.trim();
    let value = first.strip_prefix("sid=")?;
    if value.is_empty() {
        return None;
    }
    Some(value.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};
    use std::net::TcpListener;

    const STATUS_JSON: &str = r#"{
        "firmware":"esp32-gadget","version":"1.9.0","screen":3,"screenName":"RED",
        "order":[0,1,2,3,4,5,6],"names":["RELOJ","TRAGAMONEDAS","METEORITOS","RED","MÚSICA","SOFÍA","CONFIG"],
        "focus":{"active":true,"finished":false,"seconds":1500,"minutes":25,"remainingMs":1480000},
        "sta":{"configured":true,"connected":true,"ssid":"casa","ip":"192.168.0.31","rssi":-58,"state":"ok"}
    }"#;

    #[test]
    fn parsea_el_status_del_firmware() {
        let status: Status = serde_json::from_str(STATUS_JSON).expect("status");
        assert_eq!(status.screen, 3);
        assert_eq!(status.screen_name, "RED");
        assert_eq!(status.names.len(), 7);
        assert_eq!(status.names[3], "RED");
        assert_eq!(status.order, vec![0, 1, 2, 3, 4, 5, 6]);
        assert!(status.focus.active);
        assert_eq!(status.focus.seconds, 1500);
        assert_eq!(status.focus.remaining_ms, 1_480_000);
        assert_eq!(status.sta.ssid, "casa");
        assert!(status.sta.connected);
    }

    #[test]
    fn un_status_viejo_sin_campos_nuevos_sigue_leyendo() {
        // Tolerancia hacia atrás: un firmware anterior que no publique `sta` no
        // puede dejar el popover sin pantalla ni reloj.
        let status: Status = serde_json::from_str(
            r#"{"screen":1,"screenName":"TRAGAMONEDAS","focus":{"active":false}}"#,
        )
        .expect("status mínimo");
        assert_eq!(status.screen_name, "TRAGAMONEDAS");
        assert!(!status.sta.connected);
        assert!(status.names.is_empty());
    }

    /// El JSON de S3, congelado: es la única interfaz entre el firmware y la
    /// app, así que el nombre exacto de cada campo se prueba acá.
    const EVENTS_JSON: &str = r#"{
        "ok":true,"tz":-3,"dayStart":1795402800,"dayEnd":1795489200,"count":2,
        "events":[
            {"summary":"Reunión","location":"Zoom","startEpoch":1764000000,
             "endEpoch":1764003600,"leadMin":10,"allDay":false},
            {"summary":"Feriado","location":"","startEpoch":1764000000,
             "endEpoch":1764086400,"leadMin":0,"allDay":true}
        ]
    }"#;

    #[test]
    fn parsea_la_agenda_del_dia_del_firmware() {
        let events: CalendarEvents = serde_json::from_str(EVENTS_JSON).expect("agenda");
        assert_eq!(events.tz, -3);
        assert_eq!(events.count, 2);
        assert_eq!(events.events.len(), 2);
        // El día lo dice el equipo: la app no lo deriva de su propio reloj.
        assert_eq!(events.day_start, 1_795_402_800);
        assert_eq!(events.day_end, 1_795_489_200);

        let first = &events.events[0];
        assert_eq!(first.summary, "Reunión");
        assert_eq!(first.location, "Zoom");
        assert_eq!(first.start_epoch, 1_764_000_000);
        assert_eq!(first.end_epoch, 1_764_003_600);
        assert_eq!(first.lead_min, 10);
        assert!(!first.all_day);
        assert!(events.events[1].all_day);

        // El popover recibe camelCase: `rename_all` tiene que seguir puesto.
        let again = serde_json::to_value(&events).expect("serializar");
        assert_eq!(again["events"][0]["startEpoch"], 1_764_000_000);
        assert_eq!(again["events"][0]["leadMin"], 10);
        assert_eq!(again["tz"], -3);
        assert_eq!(again["dayStart"], 1_795_402_800);
    }

    #[test]
    fn una_agenda_sin_events_es_una_lista_vacia() {
        let events: CalendarEvents =
            serde_json::from_str(r#"{"ok":true,"tz":-3,"count":0}"#).expect("agenda");
        assert_eq!(events.tz, -3);
        assert!(events.events.is_empty());
    }

    /// Compatibilidad: un equipo con el firmware viejo no manda el día, y el
    /// popover tiene que seguir mostrando la agenda en vez de romperse.
    #[test]
    fn una_agenda_de_un_equipo_viejo_no_rompe() {
        let events: CalendarEvents = serde_json::from_str(
            r#"{"ok":true,"tz":-3,"count":0,"events":[]}"#,
        )
        .expect("agenda vieja");
        assert_eq!(events.tz, -3);
        assert_eq!(events.day_start, 0);
        assert_eq!(events.day_end, 0);
    }

    #[test]
    fn saca_el_sid_de_la_cookie() {
        assert_eq!(
            extract_sid("sid=9f2a; Path=/; HttpOnly; SameSite=Lax"),
            Some("9f2a".to_string())
        );
        assert_eq!(extract_sid("otra=1; Path=/"), None);
        assert_eq!(extract_sid("sid=; Path=/"), None);
        assert_eq!(extract_sid(""), None);
    }

    #[test]
    fn el_error_del_equipo_se_muestra_tal_cual() {
        let body: Value = serde_json::from_str(r#"{"ok":false,"error":"Indice de pantalla fuera de rango"}"#).unwrap();
        assert_eq!(device_message(&body, 400), "Indice de pantalla fuera de rango");
        assert_eq!(device_message(&Value::Null, 401), "El equipo pide sesión del panel");
        assert_eq!(device_message(&Value::Null, 500), "El equipo contestó 500");
    }

    /// Servidor HTTP de un solo request: alcanza para probar el transporte real
    /// (URL, método, cuerpo y parseo) sin un ESP32 en la mesa.
    fn fake_device(response: &'static str) -> u16 {
        let listener = TcpListener::bind("127.0.0.1:0").expect("bind");
        let port = listener.local_addr().unwrap().port();
        std::thread::spawn(move || {
            if let Ok((mut socket, _)) = listener.accept() {
                let mut buf = [0u8; 2048];
                let _ = socket.read(&mut buf);
                let _ = socket.write_all(response.as_bytes());
            }
        });
        port
    }

    fn http_ok(body: &str) -> String {
        format!(
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
            body.len(),
            body
        )
    }

    fn api_for(port: u16) -> Api {
        Api::new(
            &Connection {
                host: "127.0.0.1".to_string(),
                port,
            },
            None,
        )
        .expect("cliente")
    }

    #[tokio::test]
    async fn lee_el_status_de_un_equipo_por_http() {
        let response: &'static str = Box::leak(http_ok(STATUS_JSON).into_boxed_str());
        let port = fake_device(response);
        let status = api_for(port).status().await.expect("status");
        assert_eq!(status.screen_name, "RED");
    }

    #[tokio::test]
    async fn lee_la_agenda_de_un_equipo_por_http() {
        let response: &'static str = Box::leak(http_ok(EVENTS_JSON).into_boxed_str());
        let port = fake_device(response);
        let events = api_for(port).events().await.expect("agenda");
        assert_eq!(events.events.len(), 2);
        assert_eq!(events.events[0].summary, "Reunión");
    }

    #[tokio::test]
    async fn la_agenda_sin_sesion_llega_como_401() {
        let body = r#"{"ok":false,"error":"Necesita sesion"}"#;
        let response: &'static str = Box::leak(
            format!(
                "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
                body.len(),
                body
            )
            .into_boxed_str(),
        );
        let port = fake_device(response);
        let error = api_for(port).events().await.expect_err("401");
        assert_eq!(error.code, 401);
    }

    #[tokio::test]
    async fn un_401_se_informa_como_pide_sesion() {
        let body = r#"{"ok":false,"error":"Necesita sesion"}"#;
        let response: &'static str = Box::leak(
            format!(
                "HTTP/1.1 401 Unauthorized\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{}",
                body.len(),
                body
            )
            .into_boxed_str(),
        );
        let port = fake_device(response);
        let error = api_for(port).calendar().await.expect_err("401");
        assert_eq!(error.code, 401);
        assert_eq!(error.message, "Necesita sesion");
    }

    #[tokio::test]
    async fn sin_servidor_el_error_no_es_un_cuelgue() {
        // Puerto que nadie escucha: connect refused, no timeout.
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        drop(listener);
        let error = api_for(port).status().await.expect_err("sin servidor");
        assert_eq!(error.code, 0);
        assert!(error.message.contains("No encuentro el equipo"), "{}", error.message);
    }
}
