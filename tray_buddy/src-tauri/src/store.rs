//! Estado local de la app: a dónde apuntamos y con qué sesión.
//!
//! Dos campos y nada más. La IPv4 y el puerto los escribe el asistente (S3); el
//! `sid` es el token que devuelve `POST /api/session` y que sólo sirve para las
//! rutas privadas del panel (`/api/calendar`, `/api/settings`). La contraseña
//! del panel **no se guarda nunca en disco**: se manda una vez, se recibe el
//! token y se olvida. Un token de sesión se revoca desde el panel o caduca; una
//! contraseña guardada en un JSON del usuario no se revoca de ningún lado.

use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

pub const DEFAULT_PORT: u16 = 80;

/// Nombre del archivo dentro del directorio de configuración de la app
/// (`%APPDATA%\ar.esp32gadget.buddy\` en Windows).
const CONFIG_FILE: &str = "config.json";

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Connection {
    /// IPv4 en texto, sin esquema ni puerto: `192.168.4.1`.
    pub host: String,
    pub port: u16,
}

impl Connection {
    pub fn base_url(&self) -> String {
        format!("http://{}:{}", self.host, self.port)
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct Store {
    pub connection: Option<Connection>,
    /// Cookie `sid` del panel, opaca. Nunca se muestra ni se loguea.
    pub session: Option<String>,
    /// Si el popover quedó grande (S5). Se recuerda entre arranques: el dueño
    /// eligió ese tamaño y volver al chico en cada inicio sería deshacerle la
    /// decisión. `#[serde(default)]` deja leer un `config.json` anterior a este
    /// campo, que se interpreta como popover chico.
    #[serde(default)]
    pub expanded: bool,
    /// Si el popover está "fijado": no se esconde al hacer clic afuera y queda
    /// arriba de las otras ventanas. Se recuerda entre arranques porque es una
    /// decisión del dueño, no un estado de un rato. Sin el campo (config
    /// anterior) se lee como desfijado, que es el comportamiento normal de un
    /// popover.
    #[serde(default)]
    pub pinned: bool,
}

/// Convierte lo que el dueño escribe en el asistente en una conexión.
///
/// Acepta lo que sale del navegador (`http://192.168.4.1/`, `192.168.4.1:8080`)
/// porque pegar la URL es lo natural. Exige IPv4 a propósito: es lo que el
/// equipo imprime en pantalla, y aceptar nombres de host acá haría que un typo
/// se vea como "no encuentro el equipo" en vez de "esa dirección no existe".
pub fn parse_connection(raw: &str) -> Result<Connection, String> {
    let mut text = raw.trim();
    for prefix in ["http://", "https://"] {
        if let Some(rest) = text.strip_prefix(prefix) {
            text = rest;
        }
    }
    // Sólo la barra final del host: `/admin` pegado por error se rechaza abajo.
    let text = text.split('/').next().unwrap_or_default();
    if text.is_empty() {
        return Err("Falta la dirección del equipo".to_string());
    }

    let (host, port_text) = match text.rsplit_once(':') {
        Some((host, port)) if port.is_empty() => (host, None),
        Some((host, port)) if port.chars().all(|c| c.is_ascii_digit()) => (host, Some(port)),
        Some(_) => return Err("El puerto tiene que ser un número".to_string()),
        None => (text, None),
    };

    let port = match port_text {
        Some(text) => text
            .parse::<u16>()
            .map_err(|_| "El puerto tiene que ir de 1 a 65535".to_string())?,
        None => DEFAULT_PORT,
    };
    if port == 0 {
        return Err("El puerto tiene que ir de 1 a 65535".to_string());
    }

    let host = host.trim();
    if host.is_empty() {
        return Err("Falta la dirección del equipo".to_string());
    }
    if host.parse::<std::net::Ipv4Addr>().is_err() {
        return Err("La dirección tiene que ser una IPv4, por ejemplo 192.168.4.1".to_string());
    }

    Ok(Connection {
        host: host.to_string(),
        port,
    })
}

pub fn config_path(dir: &Path) -> PathBuf {
    dir.join(CONFIG_FILE)
}

/// Lee el store del disco. Un archivo roto o de otra versión no rompe la app:
/// se arranca sin conexión y el asistente vuelve a pedirla.
pub fn load(dir: &Path) -> Store {
    match std::fs::read(config_path(dir)) {
        Ok(bytes) => parse(&bytes).unwrap_or_default(),
        Err(_) => Store::default(),
    }
}

pub fn parse(bytes: &[u8]) -> Option<Store> {
    serde_json::from_slice::<Store>(bytes).ok()
}

pub fn to_bytes(store: &Store) -> Vec<u8> {
    serde_json::to_vec_pretty(store).unwrap_or_else(|_| b"{}".to_vec())
}

/// Escribe el store con reemplazo atómico: primero un temporal en el mismo
/// directorio, después el rename. Así un corte a mitad de escritura no deja un
/// `config.json` trunco que el próximo arranque leería como "sin conexión".
pub fn save(dir: &Path, store: &Store) -> Result<(), String> {
    std::fs::create_dir_all(dir).map_err(|err| format!("No pude crear la carpeta de config: {err}"))?;
    let path = config_path(dir);
    let tmp = path.with_extension("json.tmp");
    std::fs::write(&tmp, to_bytes(store))
        .map_err(|err| format!("No pude guardar la conexión: {err}"))?;
    std::fs::rename(&tmp, &path).map_err(|err| {
        let _ = std::fs::remove_file(&tmp);
        format!("No pude guardar la conexión: {err}")
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn conn(host: &str, port: u16) -> Connection {
        Connection {
            host: host.to_string(),
            port,
        }
    }

    #[test]
    fn acepta_la_ipv4_pelada_con_puerto_por_defecto() {
        assert_eq!(parse_connection("192.168.4.1"), Ok(conn("192.168.4.1", 80)));
    }

    #[test]
    fn acepta_lo_que_se_pega_del_navegador() {
        assert_eq!(
            parse_connection("  http://192.168.4.1/  "),
            Ok(conn("192.168.4.1", 80))
        );
        assert_eq!(
            parse_connection("http://192.168.4.1:8080/admin"),
            Ok(conn("192.168.4.1", 8080))
        );
    }

    #[test]
    fn acepta_puerto_explicito() {
        assert_eq!(
            parse_connection("192.168.4.1:8080"),
            Ok(conn("192.168.4.1", 8080))
        );
        assert_eq!(
            parse_connection("192.168.4.1:"),
            Ok(conn("192.168.4.1", 80))
        );
    }

    #[test]
    fn rechaza_lo_que_no_es_una_direccion() {
        assert!(parse_connection("").is_err());
        assert!(parse_connection("   ").is_err());
        assert!(parse_connection("hola").is_err());
        assert!(parse_connection("192.168.4").is_err());
        assert!(parse_connection("192.168.4.1:0").is_err());
        assert!(parse_connection("192.168.4.1:99999").is_err());
        assert!(parse_connection("192.168.4.1:abc").is_err());
    }

    #[test]
    fn el_store_sobrevive_una_ida_y_vuelta_por_disco() {
        let dir = std::env::temp_dir().join(format!("buddy-store-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let original = Store {
            connection: Some(conn("10.0.0.7", 8080)),
            session: Some("a1b2c3".to_string()),
            expanded: true,
            pinned: true,
        };
        save(&dir, &original).expect("guardar");
        assert_eq!(load(&dir), original);
        // El temporal no queda tirado al lado del archivo bueno.
        assert!(!config_path(&dir).with_extension("json.tmp").exists());
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn un_config_roto_o_vacio_no_rompe_el_arranque() {
        let dir = std::env::temp_dir().join(format!("buddy-store-roto-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        assert_eq!(load(&dir), Store::default()); // sin archivo

        std::fs::write(config_path(&dir), b"no soy json").unwrap();
        assert_eq!(load(&dir), Store::default());
        assert_eq!(parse(b"no soy json"), None);
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn un_store_viejo_sin_session_sigue_sirviendo() {
        // Tolerancia hacia adelante: si mañana el archivo suma campos, un archivo
        // de hoy se lee igual.
        let store = parse(br#"{"connection":{"host":"192.168.4.1","port":80}}"#).unwrap();
        assert_eq!(store.session, None);
        assert_eq!(store.connection, Some(conn("192.168.4.1", 80)));
    }

    #[test]
    fn el_pin_se_recuerda_y_un_config_viejo_arranca_desfijado() {
        // Fijar es una decisión del dueño, no un estado de un rato: se guarda.
        // Sin el campo (config anterior) se lee desfijado, que es lo normal de un
        // popover: si no, todas las instalaciones viejas quedarían fijas.
        let dir = std::env::temp_dir().join(format!("buddy-pin-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let original = Store {
            pinned: true,
            ..Store::default()
        };
        save(&dir, &original).expect("guardar");
        assert!(load(&dir).pinned);

        std::fs::write(dir.join("config.json"), r#"{"connection":{"host":"10.0.0.7","port":80}}"#)
            .expect("escribir el config viejo");
        assert!(!load(&dir).pinned);
    }

    #[test]
    fn un_config_sin_expanded_arranca_con_el_popover_chico() {
        // El archivo anterior a S5 no tiene `expanded`: se lee sin romper y el
        // popover arranca en 388x660, que es el tamaño de siempre.
        let store = parse(br#"{"session":"a1b2c3"}"#).unwrap();
        assert!(!store.expanded);

        let guardado = Store {
            expanded: true,
            ..Store::default()
        };
        let vuelta = parse(&to_bytes(&guardado)).unwrap();
        assert!(vuelta.expanded);
    }
}
