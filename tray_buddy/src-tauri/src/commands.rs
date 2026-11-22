//! Los comandos que el popover llama con `invoke`.
//!
//! Regla de la casa: ninguna función de acá toca HTTP antes de soltar el lock
//! del store. Un `MutexGuard` a través de un `await` no compila (`AppState` no es
//! `Sync` con el guard vivo), y aunque compilara sería un deadlock esperando el
//! turno.

use crate::api::{Api, ApiError, Status};
use crate::store::{self, Connection, Store};
use serde_json::Value;
use std::path::PathBuf;
use std::sync::Mutex;
use tauri::State;

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
