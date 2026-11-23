//! Los comandos que el popover llama con `invoke`.
//!
//! Regla de la casa: ninguna función de acá toca HTTP antes de soltar el lock
//! del store. Un `MutexGuard` a través de un `await` no compila (`AppState` no es
//! `Sync` con el guard vivo), y aunque compilara sería un deadlock esperando el
//! turno.

use crate::api::{Api, ApiError, CalendarEventInput, CalendarEvents, CalendarMonth, Status};
use crate::store::{self, Connection, Store};
use serde::Serialize;
use serde_json::Value;
use std::path::PathBuf;
use std::sync::Mutex;
use tauri::{LogicalSize, PhysicalPosition, State, WebviewWindow};

/// Tamaños lógicos del popover: el de `tauri.conf.json` y el de la agenda
/// desplegada (S5). La ventana mide 8px más de ancho que la losa porque la losa
/// negra se dibuja desplazada 7px dentro del rectángulo transparente.
pub const POPOVER_COMPACT: (f64, f64) = (388.0, 660.0);
pub const POPOVER_EXPANDED: (f64, f64) = (520.0, 820.0);

fn popover_logical_size(expanded: bool) -> LogicalSize<f64> {
    let (width, height) = if expanded {
        POPOVER_EXPANDED
    } else {
        POPOVER_COMPACT
    };
    LogicalSize::new(width, height)
}

/// Deja la ventana en el tamaño que pide la preferencia guardada. No la mueve:
/// el popover se ancla y se muestra en `main.rs`, y ahí el pico se recoloca con
/// el tamaño real recién leído.
pub fn apply_popover_size(window: &WebviewWindow, expanded: bool) {
    let _ = window.set_size(popover_logical_size(expanded));
}

/// Lo que devuelve `popover_expand`: el tamaño lógico aplicado.
#[derive(Debug, Clone, Copy, Serialize)]
pub struct PopoverSize {
    pub width: f64,
    pub height: f64,
    pub expanded: bool,
    /// Si está fijado (no se esconde al hacer clic afuera).
    pub pinned: bool,
}

impl PopoverSize {
    fn of(expanded: bool, pinned: bool) -> Self {
        let (width, height) = if expanded {
            POPOVER_EXPANDED
        } else {
            POPOVER_COMPACT
        };
        Self {
            width,
            height,
            expanded,
            pinned,
        }
    }
}

pub struct AppState {
    dir: PathBuf,
    store: Mutex<Store>,
}

impl AppState {
    pub fn new(dir: PathBuf) -> Self {
        let store = store::load(&dir);
        Self {
            dir,
            store: Mutex::new(store),
        }
    }

    /// Si el dueño dejó el popover grande (S5). `main.rs` lo consulta al abrir.
    pub fn expanded(&self) -> bool {
        self.lock().expanded
    }

    /// Si el popover está fijado. `main.rs` lo consulta cuando la ventana pierde
    /// el foco: fijado no se esconde, sin fijar sí.
    pub fn pinned(&self) -> bool {
        self.lock().pinned
    }

    fn lock(&self) -> std::sync::MutexGuard<'_, Store> {
        // Un poison no puede perder la conexión del dueño: se toma el valor y
        // sigue. Lo peor que queda es un store a medio escribir en memoria, y
        // eso lo reemplaza el próximo `connection_save`.
        self.store.lock().unwrap_or_else(|poison| poison.into_inner())
    }

    fn snapshot(&self) -> Result<(Connection, Option<String>), ApiError> {
        let store = self.lock();
        match &store.connection {
            Some(connection) => Ok((connection.clone(), store.session.clone())),
            None => Err(ApiError {
                code: 0,
                message: "Todavía no configuraste el equipo".to_string(),
            }),
        }
    }

    fn api(&self) -> Result<Api, ApiError> {
        let (connection, session) = self.snapshot()?;
        Api::new(&connection, session)
    }

    fn persist(&self, store: &Store) -> Result<(), ApiError> {
        store::save(&self.dir, store).map_err(ApiError::transport)
    }
}

#[tauri::command]
pub async fn connection_load(state: State<'_, AppState>) -> Result<Option<Connection>, ApiError> {
    Ok(state.lock().connection.clone())
}

/// Valida contra el equipo antes de guardar (S3): una IPv4 que no responde no se
/// escribe en disco, así el popover no arranca apuntando a la nada.
#[tauri::command]
pub async fn connection_save(
    state: State<'_, AppState>,
    address: String,
) -> Result<Connection, ApiError> {
    let connection = store::parse_connection(&address).map_err(ApiError::transport)?;
    let probe = Api::new(&connection, None)?;
    // Si el equipo no contesta, esto corta acá y el disco no se toca.
    probe.status().await?;

    {
        let mut store = state.lock();
        adopt_connection(&mut store, connection.clone());
        state.persist(&store)?;
    }
    Ok(connection)
}

/// Adoptar un equipo nuevo invalida la sesión: el `sid` pertenece al equipo que
/// la abrió, y mandárselo a otro sólo puede terminar en un 401 confuso.
fn adopt_connection(store: &mut Store, connection: Connection) {
    let same_device = store.connection.as_ref() == Some(&connection);
    if !same_device {
        store.session = None;
    }
    store.connection = Some(connection);
}

#[tauri::command]
pub async fn connection_forget(state: State<'_, AppState>) -> Result<(), ApiError> {
    let mut store = state.lock();
    store.connection = None;
    store.session = None;
    state.persist(&store)
}

#[tauri::command]
pub async fn device_status(state: State<'_, AppState>) -> Result<Status, ApiError> {
    state.api()?.status().await
}

#[tauri::command]
pub async fn device_screen(state: State<'_, AppState>, index: u8) -> Result<(), ApiError> {
    state.api()?.set_screen(index).await
}

#[tauri::command]
pub async fn device_focus(state: State<'_, AppState>, seconds: u32) -> Result<(), ApiError> {
    state.api()?.set_focus(seconds).await
}

/// Brillo del LCD sin sesión del panel (S3). El firmware lo trata como escritura
/// pública y angosta: no hay nada que loguear ni credencial que guardar.
#[tauri::command]
pub async fn device_lcd(state: State<'_, AppState>, brightness: u8) -> Result<(), ApiError> {
    state.api()?.set_lcd(brightness).await
}

/// Color, nivel y efecto de la tira RGB, también sin sesión (S3).
#[tauri::command]
pub async fn device_led(
    state: State<'_, AppState>,
    red: u8,
    green: u8,
    blue: u8,
    level: u8,
    effect: u8,
) -> Result<(), ApiError> {
    state.api()?.set_led(red, green, blue, level, effect).await
}

/// Abre la sesión del panel una vez y guarda sólo el token (S8). La contraseña
/// se manda y se olvida: no toca el disco ni el log.
#[tauri::command]
pub async fn panel_login(
    state: State<'_, AppState>,
    user: String,
    pass: String,
) -> Result<bool, ApiError> {
    let api = state.api()?;
    let token = api.login(&user, &pass).await?;
    match token {
        Some(token) => {
            let mut store = state.lock();
            store.session = Some(token);
            state.persist(&store)?;
            Ok(true)
        }
        // El equipo contestó 200 sin cookie: no hay sesión que guardar.
        None => Ok(false),
    }
}

#[tauri::command]
pub async fn panel_logout(state: State<'_, AppState>) -> Result<(), ApiError> {
    let mut store = state.lock();
    store.session = None;
    state.persist(&store)
}

/// `/api/calendar*` es privado incluso para leer (los títulos de los eventos no
/// son para cualquiera en el AP), así que esto puede volver 401 y el popover
/// pide la sesión del panel en vez de inventar datos.
#[tauri::command]
pub async fn panel_calendar(state: State<'_, AppState>) -> Result<Value, ApiError> {
    state.api()?.calendar().await
}

/// La agenda del día (S4) sale del mismo prefijo privado, así que un 401 también
/// llega con `code: 401` y el popover pide la sesión en vez de inventar filas.
#[tauri::command]
pub async fn panel_calendar_events(
    state: State<'_, AppState>,
) -> Result<CalendarEvents, ApiError> {
    state.api()?.events().await
}

/// Alta de un evento desde el tray (S2b). El equipo es el que tiene el permiso de
/// Google; la app sólo manda el evento y muestra la frase que vuelve cuando algo
/// sale mal ("Ese evento cambió en otro lado", "El permiso es de sólo lectura").
#[tauri::command]
pub async fn panel_event_create(
    state: State<'_, AppState>,
    event: CalendarEventInput,
) -> Result<(), ApiError> {
    state.api()?.create_event(&event).await
}

#[tauri::command]
pub async fn panel_event_update(
    state: State<'_, AppState>,
    event: CalendarEventInput,
) -> Result<(), ApiError> {
    state.api()?.update_event(&event).await
}

#[tauri::command]
pub async fn panel_event_delete(
    state: State<'_, AppState>,
    id: String,
    etag: String,
) -> Result<(), ApiError> {
    state.api()?.delete_event(&id, &etag).await
}

/// El mes de la grilla (S2): el equipo lo pide a Google en ese momento, así que
/// puede tardar segundos. La navegación manda el año y el mes que el dueño
/// eligió; nada se calcula acá.
#[tauri::command]
pub async fn panel_calendar_month(
    state: State<'_, AppState>,
    year: i32,
    month: i32,
) -> Result<CalendarMonth, ApiError> {
    state.api()?.month(year, month).await
}

/// Preferencia guardada, sin tocar la ventana: el popover la lee al arrancar para
/// pintar el botón correcto antes de que el dueño lo toque.
#[tauri::command]
pub async fn popover_state(state: State<'_, AppState>) -> Result<PopoverSize, ApiError> {
    let store = state.lock();
    Ok(PopoverSize::of(store.expanded, store.pinned))
}

/// Fija o suelta el popover. Fijado no se esconde al hacer clic afuera y queda
/// arriba; suelto se comporta como cualquier popover.
///
/// Se guarda antes de tocar la ventana: si el guardado falla, el botón no miente
/// —el error vuelve al popover y el tilde no cambia—.
#[tauri::command]
pub async fn popover_pin(
    state: State<'_, AppState>,
    window: WebviewWindow,
    pinned: bool,
) -> Result<bool, ApiError> {
    let store = {
        let mut store = state.lock();
        store.pinned = pinned;
        store.clone()
    };
    state.persist(&store)?;
    // Fijado también significa arriba: si no, el pin no se ve como "lo quiero a
    // mano" y la ventana se va detrás de la siguiente.
    let _ = window.set_always_on_top(pinned);
    Ok(pinned)
}

/// Agranda o achica el popover (S5) y recuerda la elección.
///
/// La esquina inferior derecha no se mueve, porque es donde vive el pico que
/// apunta al icono de la bandeja: la ventana crece hacia arriba y hacia la
/// izquierda. El delta se mide en píxeles físicos porque `outer_position` viene
/// en físicos; mezclar unidades corre el pico varios píxeles con escalado 125%.
#[tauri::command]
pub async fn popover_expand(
    state: State<'_, AppState>,
    window: WebviewWindow,
    expanded: bool,
) -> Result<PopoverSize, ApiError> {
    let pinned = state.lock().pinned;
    let target = popover_logical_size(expanded);
    let scale = window.scale_factor().unwrap_or(1.0);
    let target_px = target.to_physical::<u32>(scale);
    let current_px = window.outer_size().map_err(resize_failed)?;
    let origin = window.outer_position().map_err(resize_failed)?;

    window.set_size(target).map_err(resize_failed)?;

    let dx = target_px.width as i32 - current_px.width as i32;
    let dy = target_px.height as i32 - current_px.height as i32;
    let corner = PhysicalPosition::new(origin.x - dx, origin.y - dy);
    window.set_position(corner).map_err(resize_failed)?;

    {
        let mut store = state.lock();
        if store.expanded != expanded {
            store.expanded = expanded;
            state.persist(&store)?;
        }
    }
    Ok(PopoverSize::of(expanded, pinned))
}

fn resize_failed(err: impl std::fmt::Display) -> ApiError {
    ApiError::transport(format!("No pude cambiar el tamaño del popover: {err}"))
}

/// Abre `http://<ip>/admin` en el navegador del sistema (S7). Sin shell: el
/// `rundll32` de Windows recibe la URL como argumento, no como línea de comando
/// interpretada, así que un host raro no puede convertirse en ejecución.
#[tauri::command]
pub async fn open_panel(state: State<'_, AppState>) -> Result<(), ApiError> {
    let url = format!("{}/admin", state.api()?.base_url());
    open_url(&url).map_err(ApiError::transport)
}

fn open_url(url: &str) -> Result<(), String> {
    #[cfg(target_os = "windows")]
    let result = std::process::Command::new("rundll32")
        .arg("url.dll,FileProtocolHandler")
        .arg(url)
        .spawn();
    #[cfg(target_os = "macos")]
    let result = std::process::Command::new("open").arg(url).spawn();
    #[cfg(all(unix, not(target_os = "macos")))]
    let result = std::process::Command::new("xdg-open").arg(url).spawn();

    result
        .map(|_| ())
        .map_err(|err| format!("No pude abrir el panel en el navegador: {err}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn state_with(connection: Option<Connection>) -> AppState {
        let dir = std::env::temp_dir().join(format!(
            "buddy-state-{}-{:?}",
            std::process::id(),
            connection
        ));
        let _ = std::fs::remove_dir_all(&dir);
        let state = AppState::new(dir);
        {
            let mut store = state.lock();
            store.connection = connection;
        }
        state
    }

    #[test]
    fn sin_conexion_los_comandos_explican_que_falta() {
        let state = state_with(None);
        let error = state.api().err().expect("sin conexión");
        assert_eq!(error.code, 0);
        assert!(error.message.contains("Todavía no configuraste"));
    }

    #[test]
    fn con_conexion_el_cliente_arma_la_url_del_equipo() {
        let state = state_with(Some(Connection {
            host: "192.168.4.1".to_string(),
            port: 80,
        }));
        let api = state.api().expect("cliente");
        assert_eq!(api.base_url(), "http://192.168.4.1:80");
    }

    #[test]
    fn cambiar_de_equipo_borra_la_sesion_y_el_mismo_equipo_la_conserva() {
        let uno = Connection {
            host: "192.168.4.1".to_string(),
            port: 80,
        };
        let mut store = Store {
            connection: Some(uno.clone()),
            session: Some("sid-vivo".to_string()),
            expanded: false,
            pinned: false,
        };

        // Reconectar al mismo equipo no expulsa la sesión.
        adopt_connection(&mut store, uno);
        assert_eq!(store.session.as_deref(), Some("sid-vivo"));

        // Otro equipo sí: el token de uno no sirve en el otro.
        adopt_connection(
            &mut store,
            Connection {
                host: "192.168.4.2".to_string(),
                port: 80,
            },
        );
        assert_eq!(store.session, None);
        assert_eq!(store.connection.unwrap().host, "192.168.4.2");
    }
}
