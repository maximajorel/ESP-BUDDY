fn main() {
    // El frontend no se lee del disco en tiempo de ejecución: `generate_context!`
    // lo embebe al compilar. Sin estas líneas, `cargo build` después de tocar
    // `ui/` no recompilaba nada y la app seguía mostrando el HTML/CSS viejo —
    // pasó de verdad, y buscando el bug en el CSS se perdió una sesión entera.
    // `tauri dev` lo maneja solo; compilando a mano, esto es lo que lo hace
    // honesto.
    for path in [
        "../ui",
        "../ui/index.html",
        "../ui/app.js",
        "../ui/styles.css",
    ] {
        println!("cargo:rerun-if-changed={path}");
    }
    tauri_build::build()
}
