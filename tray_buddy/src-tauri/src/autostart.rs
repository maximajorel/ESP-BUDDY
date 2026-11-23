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

use std::path::Path;

/// La clave del registro donde Windows lee qué arrancar con la sesión.
pub const RUN_KEY: &str = r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run";

/// El nombre del valor. Tiene que coincidir con `${PRODUCTNAME}` del instalador.
pub const VALUE_NAME: &str = "Buddy";

/// El ejecutable tal como va al registro, **entre comillas**.
///
/// Sin comillas Windows corta la línea en el primer espacio y el arranque falla
/// sin decir nada: una instalación en `C:\Program Files\Buddy\` no arrancaría.
/// Van siempre, aunque el camino no tenga espacios, porque el instalador puede
/// mover la carpeta después.
pub fn quoted_exe(exe: &Path) -> String {
    format!("\"{}\"", exe.display())
}

/// `reg add` que deja a Buddy arrancando con la sesión del usuario.
pub fn add_args(exe: &Path) -> Vec<String> {
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
    .collect()
}

/// `reg delete` que saca la entrada. Falla con código 1 si no estaba, que es
/// información, no un error.
pub fn delete_args() -> Vec<String> {
    [
        "delete", RUN_KEY, "/v", VALUE_NAME, "/f",
    ]
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

/// ¿Buddy ya arranca con Windows? La fuente de verdad es el registro: el dueño
/// puede haberlo tocado con `msconfig` o con otro programa.
pub fn is_enabled() -> bool {
    run(&query_args())
}

/// Prende o apaga el arranque automático. Devuelve si el registro quedó como se
/// pidió, para que el menú no muestre un tilde que no es cierto.
pub fn set_enabled(on: bool) -> bool {
    let ok = if on {
        let exe = std::env::current_exe().unwrap_or_default();
        run(&add_args(&exe))
    } else {
        // Si no estaba, `reg delete` sale con 1 y la intención ya está cumplida.
        run(&delete_args());
        !is_enabled()
    };
    ok && is_enabled() == on
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn exe() -> PathBuf {
        PathBuf::from(r"C:\Program Files\Buddy\Buddy.exe")
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
        let args = add_args(&exe());
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
    fn delete_borra_solo_esa_entrada() {
        assert_eq!(delete_args(), vec!["delete", RUN_KEY, "/v", "Buddy", "/f"]);
    }

    #[test]
    fn query_pregunta_por_esa_entrada() {
        assert_eq!(query_args(), vec!["query", RUN_KEY, "/v", "Buddy"]);
    }

    #[test]
    fn la_clave_es_la_del_usuario_y_no_la_de_la_maquina() {
        // HKCU: sin administrador. HKLM exigiría elevar y el instalador instala
        // para el usuario actual.
        assert!(RUN_KEY.starts_with("HKCU\\"));
        assert!(RUN_KEY.ends_with(r"\CurrentVersion\Run"));
    }
}
