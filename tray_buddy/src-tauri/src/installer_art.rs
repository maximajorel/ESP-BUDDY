//! Coherencia entre el instalador, sus imágenes y el `tauri.conf.json`.
//!
//! Las dos imágenes del NSIS son archivos generados por
//! `tools/make_installer_art.py`, y `tauri.conf.json` las referencia por ruta.
//! Si alguien mueve una carpeta, cambia el tamaño o edita el config a mano, el
//! build falla recién al final (después de compilar todo el release) con un
//! mensaje de makensis que no dice cuál de las dos piezas está mal. Estas
//! pruebas lo agarran en `cargo test`, sin Windows ni makensis.
//!
//! Los tamaños no son gusto: NSIS rechaza cualquier otra medida.

#![cfg(test)]

use std::path::{Path, PathBuf};

/// `src-tauri/`, que es el `CARGO_MANIFEST_DIR` de este crate.
fn crate_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn repo_dir() -> PathBuf {
    crate_dir().parent().expect("tray_buddy/").to_path_buf()
}

/// Lee ancho y alto de la cabecera de un BMP (offset 18, dos i32 little endian).
fn bmp_size(path: &Path) -> (i32, i32) {
    let bytes = std::fs::read(path).unwrap_or_else(|err| panic!("{}: {err}", path.display()));
    assert_eq!(&bytes[0..2], b"BM", "{} no es un BMP", path.display());
    assert!(bytes.len() > 54, "{} está truncado", path.display());
    let width = i32::from_le_bytes([bytes[18], bytes[19], bytes[20], bytes[21]]);
    let height = i32::from_le_bytes([bytes[22], bytes[23], bytes[24], bytes[25]]).abs();
    (width, height)
}

#[test]
fn el_asistente_del_instalador_tiene_las_medidas_que_nsis_exige() {
    let header = repo_dir().join("installer/header.bmp");
    let sidebar = repo_dir().join("installer/sidebar.bmp");
    assert_eq!(bmp_size(&header), (150, 57), "cabecera del NSIS");
    assert_eq!(bmp_size(&sidebar), (164, 314), "lateral del NSIS");
}

#[test]
fn el_config_apunta_a_las_imagenes_del_asistente() {
    let config = std::fs::read_to_string(crate_dir().join("tauri.conf.json")).expect("config");
    // `rsplit_once` y no `split().nth(1)`: el config menciona `"nsis"` dos
    // veces (el target y el bloque de opciones), y lo que interesa es el último.
    let nsis = config
        .rsplit_once("\"nsis\"")
        .map(|(_, tail)| tail)
        .expect("el bundle NSIS sigue en el config");
    assert!(nsis.contains("header.bmp"), "falta headerImage");
    assert!(nsis.contains("sidebar.bmp"), "falta sidebarImage");
    // Las rutas del config son relativas a `src-tauri/`: si el archivo se movió,
    // esta comprobación lo dice con el nombre.
    for name in ["header.bmp", "sidebar.bmp"] {
        let referenced = repo_dir().join("installer").join(name);
        assert!(referenced.exists(), "{} no existe", referenced.display());
    }
}

#[test]
fn el_instalador_es_por_usuario_y_no_pide_administrador() {
    // S2: instalar como cualquier programa, sin elevación.
    let config = std::fs::read_to_string(crate_dir().join("tauri.conf.json")).expect("config");
    assert!(config.contains("currentUser"), "installMode per-user");
    assert!(config.contains("\"nsis\""), "target NSIS activo");
}
