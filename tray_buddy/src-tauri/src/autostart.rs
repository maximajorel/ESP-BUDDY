// Arrancar con Windows.
//
// Una app de bandeja que hay que abrir a mano cada mañana no es una app de
// bandeja. El dueño decide si Buddy arranca con la compu, y la decisión vive
// donde Windows la busca: `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`,
// el registro del usuario, sin permisos de administrador.
//
// Se escribe con `reg.exe` en vez de la API de registro o un plugin: no suma una
// dependencia, no toca COM y el proceso se lanza sin ventana. El nombre del valor
// (`Buddy`) no es inventado: es `${PRODUCTNAME}` del instalador NSIS, que borra
// justo esa entrada al desinstalar. Si cambia acá, el desinstalador deja basura
// que arranca una app que ya no existe, así que el contrato tiene test.
//
// Y hay un segundo lugar: el Administrador de tareas (el viejo `msconfig`) **no
// borra** la entrada del `Run` cuando el dueño la apaga; escribe un "no" aparte,
// en `Explorer\StartupApproved\Run`. Preguntar sólo por el `Run` diría que está
// prendido cuando Windows no la arranca, y el clic para prenderla la borraría:
// lo contrario de lo que el dueño pidió. Por eso la verdad se lee de los dos
// lados, y al prender se borra la marca del "no".

use std::path::Path;

/// La clave del registro donde Windows lee qué arrancar con la sesión.
pub const RUN_KEY: &str = r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run";

/// Donde el Administrador de tareas guarda "esta sí" / "esta no". El primer byte
/// del valor binario es el veredicto: impar es apagado.
pub const APPROVED_KEY: &str =
    r"HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\StartupApproved\Run";

/// El nombre del valor. Tiene que coincidir con `${PRODUCTNAME}` del instalador.
pub const VALUE_NAME: &str = "Buddy";

/// El ejecutable tal como va al registro, **entre comillas**.
///
/// Sin comillas Windows corta la línea en el primer espacio y el arranque falla
/// sin decir nada: una instalación en `C:\Program Files\Buddy\` no arrancaría.
/// Van siempre, aunque el camino no tenga espacios, porque la ruta cambia según
/// dónde haya instalado el dueño.
pub fn quoted_exe(exe: &Path) -> String {
    format!("\"{}\"", exe.display())
}

/// `reg add` que deja a Buddy arrancando con la sesión del usuario.
///
/// Devuelve `None` si el ejecutable no se conoce: escribir `/d ""` dejaría un
/// valor vacío, que se lee como "está prendido" y no arranca nada.
pub fn add_args_checked(exe: &Path) -> Option<Vec<String>> {
    if exe.as_os_str().is_empty() {
        return None;
    }
    Some(
        [
            "add",
            RUN_KEY,
            "/v",
            VALUE_NAME,
            "/t",
            "REG_SZ",
            "/d",
            &quoted_exe(exe),
            "/f",
        ]
        .iter()
        .map(|s| s.to_string())
        .collect(),
    )
}

/// `reg delete` que saca la entrada. Falla con código 1 si no estaba, que es
/// información, no un error.
pub fn delete_args() -> Vec<String> {
    ["delete", RUN_KEY, "/v", VALUE_NAME, "/f"]
        .iter()
        .map(|s| s.to_string())
        .collect()
}

/// `reg query` de la entrada. Se usa el código de salida y no la salida de texto:
/// el formato de `reg` cambia con el idioma de Windows.
pub fn query_args() -> Vec<String> {
    ["query", RUN_KEY, "/v", VALUE_NAME]
        .iter()
        .map(|s| s.to_string())
        .collect()
}

/// `reg export` de la clave del Administrador de tareas a un archivo. El `.reg`
/// en hexadecimal es lo único que se puede leer igual en cualquier idioma de
/// Windows, y sin inventar un parser de la salida de texto de `reg query`.
pub fn export_args(path: &Path) -> Vec<String> {
    ["export", APPROVED_KEY, &path.to_string_lossy(), "/y"]
        .iter()
        .map(|s| s.to_string())
        .collect()
}

/// `reg delete` de la marca del Administrador de tareas: es lo que hace que
/// Windows vuelva a arrancar a Buddy después de que el dueño lo apagó ahí.
pub fn delete_approved_args() -> Vec<String> {
    ["delete", APPROVED_KEY, "/v", VALUE_NAME, "/f"]
        .iter()
        .map(|s| s.to_string())
        .collect()
}

/// Los bytes del valor `value` dentro de un archivo `.reg` exportado, o `None` si
/// ese valor no está.
///
/// `reg export` escribe UTF-16LE con BOM y los binarios como
/// `"Buddy"=hex:03,00,00,...`. Los nombres de los tipos no se traducen, pero el
/// formato del archivo tampoco depende del idioma: por eso se parsea esto y no la
/// salida de `reg query`.
pub fn parse_export(bytes: &[u8], value: &str) -> Option<Vec<u8>> {
    let text = if bytes.len() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE {
        let units: Vec<u16> = bytes[2..]
            .chunks_exact(2)
            .map(|c| u16::from_le_bytes([c[0], c[1]]))
            .collect();
        String::from_utf16_lossy(&units)
    } else {
        String::from_utf8_lossy(bytes).into_owned()
    };
    // Un valor largo viene partido con una barra invertida al final de la línea.
    // Sin unir, el valor se leería como ausente, y ausente se lee como
    // "prendido": el bug de B1 por la puerta de atrás.
    let mut unido = String::with_capacity(text.len());
    for line in text.lines() {
        let limpia = line.trim_end();
        match limpia.strip_suffix('\\') {
            Some(resto) => unido.push_str(resto),
            None => {
                unido.push_str(line);
                unido.push('\n');
            }
        }
    }
    let needle = format!("\"{value}\"=hex:");
    let line = unido
        .lines()
        .find(|line| line.trim_start().starts_with(&needle))?;
    let mut out = Vec::new();
    for part in line.trim_start()[needle.len()..].split(',') {
        let part = part.trim();
        if part.is_empty() {
            break;
        }
        out.push(u8::from_str_radix(part, 16).ok()?);
    }
    Some(out)
}

/// ¿El Administrador de tareas tiene a Buddy apagado? Sin valor, Windows lo
/// arranca: el veredicto por omisión es "sí".
pub fn startup_approved_disabled(bytes: &[u8]) -> bool {
    match parse_export(bytes, VALUE_NAME) {
        Some(data) => data.first().map(|b| b & 1 == 1).unwrap_or(false),
        None => false,
    }
}

/// Corre `reg.exe` sin ventana. Devuelve si terminó bien.
#[cfg(windows)]
fn run(args: &[String]) -> bool {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    std::process::Command::new("reg")
        .args(args)
        .creation_flags(CREATE_NO_WINDOW)
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
}

#[cfg(not(windows))]
fn run(_args: &[String]) -> bool {
    false
}

/// El `.reg` de la clave del Administrador de tareas, si se pudo exportar.
fn approved_export() -> Option<Vec<u8>> {
    let path = std::env::temp_dir().join(format!("buddy-approved-{}.reg", std::process::id()));
    let ok = run(&export_args(&path));
    let bytes = if ok { std::fs::read(&path).ok() } else { None };
    let _ = std::fs::remove_file(&path);
    bytes
}

fn executable() -> Option<std::path::PathBuf> {
    std::env::current_exe()
        .ok()
        .filter(|path| !path.as_os_str().is_empty())
}

/// ¿Buddy arranca con Windows de verdad? La fuente de verdad es el registro, y
/// son dos claves: la entrada del `Run` y la marca del Administrador de tareas.
pub fn is_enabled() -> bool {
    if !run(&query_args()) {
        return false;
    }
    match approved_export() {
        Some(bytes) => !startup_approved_disabled(&bytes),
        None => true,
    }
}

/// Prende o apaga el arranque automático. Devuelve si el registro quedó como se
/// pidió, para que el menú no muestre un tilde que no es cierto.
pub fn set_enabled(on: bool) -> bool {
    if on {
        let Some(exe) = executable() else {
            return false;
        };
        let Some(args) = add_args_checked(&exe) else {
            return false;
        };
        run(&args);
        // Si el dueño lo había apagado desde el Administrador de tareas, el "no"
        // vive acá: sin borrarlo, Windows no arranca la app aunque la entrada del
        // `Run` esté escrita.
        run(&delete_approved_args());
    } else {
        run(&delete_args());
        run(&delete_approved_args());
    }
    is_enabled() == on
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn exe() -> PathBuf {
        PathBuf::from(r"C:\Program Files\Buddy\Buddy.exe")
    }

    /// Los bytes reales que escribe `reg export` para un valor REG_BINARY, con
    /// su BOM y su UTF-16: el parser se prueba contra el formato, no contra una
    /// idea del formato.
    fn export_utf16(linea: &str) -> Vec<u8> {
        let texto = format!(
            "Windows Registry Editor Version 5.00\r\n\r\n[HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run]\r\n{linea}\r\n"
        );
        let mut out = vec![0xFF, 0xFE];
        for unit in texto.encode_utf16() {
            out.extend_from_slice(&unit.to_le_bytes());
        }
        out
    }

    #[test]
    fn el_camino_va_entre_comillas_aunque_no_tenga_espacios() {
        // Es lo que evita que "C:\Program Files\..." se corte en el primer
        // espacio y el arranque automático falle sin avisar.
        assert_eq!(quoted_exe(&exe()), r#""C:\Program Files\Buddy\Buddy.exe""#);
        assert_eq!(
            quoted_exe(&PathBuf::from(r"C:\Buddy\Buddy.exe")),
            r#""C:\Buddy\Buddy.exe""#
        );
        // Una ruta de red también es válida entre comillas.
        assert_eq!(
            quoted_exe(&PathBuf::from(r"\\server\share\Buddy.exe")),
            r#""\\server\share\Buddy.exe""#
        );
    }

    #[test]
    fn el_valor_del_registro_es_el_que_borra_el_desinstalador() {
        // `${PRODUCTNAME}` del instalador NSIS sale de `productName`. Si el
        // producto se renombra y el valor no, desinstalar deja una entrada que
        // arranca un ejecutable que ya no existe.
        let conf = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/tauri.conf.json"))
            .expect("tauri.conf.json");
        let valor: serde_json::Value = serde_json::from_str(&conf).expect("json");
        assert_eq!(valor["productName"], VALUE_NAME);
    }

    #[test]
    fn add_deja_la_entrada_con_la_ruta_entre_comillas_y_pisa_la_vieja() {
        let args = add_args_checked(&exe()).expect("args");
        assert_eq!(
            args,
            vec![
                "add",
                RUN_KEY,
                "/v",
                "Buddy",
                "/t",
                "REG_SZ",
                "/d",
                r#""C:\Program Files\Buddy\Buddy.exe""#,
                "/f",
            ]
        );
        // `/f` es lo que hace que reinstalar no pregunte ni falle si ya estaba.
        assert!(args.iter().any(|a| a == "/f"));
    }

    #[test]
    fn sin_ejecutable_conocido_no_se_escribe_un_valor_vacio() {
        // Un `/d ""` deja un valor que existe (el menú diría "prendido") y no
        // arranca nada: el peor de los dos mundos.
        assert!(add_args_checked(&PathBuf::new()).is_none());
    }

    #[test]
    fn delete_borra_solo_esa_entrada() {
        assert_eq!(delete_args(), vec!["delete", RUN_KEY, "/v", "Buddy", "/f"]);
        assert_eq!(
            delete_approved_args(),
            vec!["delete", APPROVED_KEY, "/v", "Buddy", "/f"]
        );
    }

    #[test]
    fn query_pregunta_por_esa_entrada() {
        assert_eq!(query_args(), vec!["query", RUN_KEY, "/v", "Buddy"]);
    }

    #[test]
    fn el_export_se_lee_igual_en_cualquier_idioma_de_windows() {
        // Con un byte impar (0x03) el Administrador de tareas la tiene apagada,
        // que es exactamente el caso en que la entrada del Run sigue existiendo
        // pero Windows no arranca la app.
        let apagada = export_utf16("\"Buddy\"=hex:03,00,00,00,00,00,00,00,00,00,00,00");
        assert_eq!(
            parse_export(&apagada, "Buddy"),
            Some(vec![3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
        );
        assert!(startup_approved_disabled(&apagada));

        let prendida = export_utf16("\"Buddy\"=hex:02,00,00,00,00,00,00,00,00,00,00,00");
        assert!(!startup_approved_disabled(&prendida));

        // Otro programa en la misma clave no se confunde con Buddy.
        let ajena = export_utf16("\"BuddyOtra\"=hex:03,00,00,00");
        assert!(!startup_approved_disabled(&ajena));

        // Si el dueño nunca la tocó, el valor no está y Windows la arranca.
        let vacia = export_utf16("");
        assert_eq!(parse_export(&vacia, "Buddy"), None);
        assert!(!startup_approved_disabled(&vacia));

        // Un archivo que no es un .reg no rompe: se lee como "no está".
        assert_eq!(parse_export(b"cualquier cosa", "Buddy"), None);
        assert!(parse_export(b"", "Buddy").is_none());
    }

    #[test]
    fn el_primer_byte_decide_igual_para_los_cuatro_valores_de_windows() {
        // El Administrador de tareas escribe 0x02 y 0x06 cuando la deja prendida
        // y 0x03 y 0x07 cuando la apaga. Con un byte en vez de una tabla, los
        // cuatro caen solos.
        for (byte, apagada) in [(0x02u8, false), (0x03, true), (0x06, false), (0x07, true)] {
            let reg = export_utf16(&format!(
                "\"Buddy\"=hex:{byte:02x},00,00,00,00,00,00,00,00,00,00,00"
            ));
            assert_eq!(startup_approved_disabled(&reg), apagada, "byte {byte:#04x}");
        }
    }

    #[test]
    fn sin_archivo_no_hay_apagado() {
        // Una clave que no existe no deja archivo: el veredicto por omisión es
        // que Windows la arranca.
        assert_eq!(parse_export(b"", "Buddy"), None);
        assert!(!startup_approved_disabled(b""));
    }

    #[test]
    fn un_valor_partido_en_dos_lineas_se_lee_igual() {
        // `reg export` corta los valores largos con una barra invertida. Si se
        // leyera como ausente, se interpretaría "prendido" cuando puede estar
        // apagado: el bug de B1 por atrás.
        let partido = export_utf16("\"Buddy\"=hex:03,00,00,00,\\\r\n  00,00,00,00");
        assert_eq!(parse_export(&partido, "Buddy"), Some(vec![3, 0, 0, 0, 0, 0, 0, 0]));
        assert!(startup_approved_disabled(&partido));
    }

    #[test]
    fn el_hook_apaga_la_entrada_vieja_cuando_el_dueno_dice_que_no() {
        // Un "no" explícito tiene que dejar el registro apagado aunque la
        // instalación anterior hubiera dejado la entrada prendida: el camino "no
        // desinstalar" de la página de mantenimiento no corre el desinstalador
        // viejo, así que nadie más la borra.
        let hooks = std::fs::read_to_string(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../installer/hooks.nsh"
        ))
        .expect("installer/hooks.nsh");
        let desde = hooks.find("buddyAutostartNo:").expect("etiqueta del no");
        let hasta = hooks.find("buddyAutostartDone:").expect("etiqueta de fin");
        let camino_no = &hooks[desde..hasta];
        assert!(
            camino_no.contains(
                r#"DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "Buddy""#
            ),
            "el camino 'no' tiene que borrar una entrada de una instalación anterior"
        );
        assert!(
            camino_no.contains("StartupApproved"),
            "y también la marca del Administrador de tareas"
        );
    }

    #[test]
    fn la_clave_es_la_del_usuario_y_no_la_de_la_maquina() {
        // HKCU: sin administrador. HKLM exigiría elevar y el instalador instala
        // para el usuario actual.
        assert!(RUN_KEY.starts_with("HKCU\\"));
        assert!(RUN_KEY.ends_with(r"\CurrentVersion\Run"));
        assert!(APPROVED_KEY.starts_with("HKCU\\"));
        assert!(APPROVED_KEY.ends_with(r"\StartupApproved\Run"));
    }
}
