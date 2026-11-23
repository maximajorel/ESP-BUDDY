// Buddy en la bandeja del sistema.
//
// Una sola ventana, sin bordes ni decoración, que vive escondida y aparece
// anclada al icono de la bandeja. Cerrar y minimizar son lo mismo: esconder.
// Salir es una acción explícita del menú de la bandeja, porque una app de
// bandeja que muere al cerrar su ventana no es una app de bandeja.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod api;
mod autostart;
mod commands;
mod single_instance;
#[cfg(test)]
mod installer_art;
mod store;

use std::sync::Mutex;
use std::time::{Duration, Instant};

use tauri::{
    image::Image,
    menu::{CheckMenuItem, Menu, MenuItem},
    tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
    AppHandle, Emitter, Manager, PhysicalPosition, PhysicalRect, Position, Rect, RunEvent, Size,
    WebviewWindow, WindowEvent,
};

const POPOVER: &str = "popover";

// El ítem del menú de la bandeja, guardado para poder moverle el tilde: un
// `TrayIcon` no expone su menú, así que la referencia al ítem viaja como estado.
struct AutostartMenu(CheckMenuItem<tauri::Wry>);

/// Cuánto cuenta un clic en la bandeja como "el mismo gesto" que acaba de
/// esconder el popover. El clic en el icono también le saca el foco a la ventana:
/// sin esta ventana de tiempo, el clic que el dueño da para esconderla la
/// volvería a mostrar, porque cuando llega el evento ya está escondida.
const BLUR_TO_TRAY_CLICK: Duration = Duration::from_millis(400);

/// Cuándo se escondió el popover por perder el foco. Lo escribe el evento de
/// ventana y lo lee el clic de la bandeja, que corren en hilos distintos.
static HIDDEN_BY_BLUR: Mutex<Option<Instant>> = Mutex::new(None);

/// ¿Este clic en la bandeja es la consecuencia del clic afuera que recién
/// escondió el popover? Entonces no hay nada que hacer: el dueño pidió esconder.
fn tray_click_is_the_blur(hidden_at: Option<Instant>, now: Instant) -> bool {
    match hidden_at {
        Some(at) => now.duration_since(at) < BLUR_TO_TRAY_CLICK,
        None => false,
    }
}
const TRAY_ID: &str = "buddy";
/// Evento que le dice al popover que abra el asistente de conexión.
const OPEN_SETUP: &str = "buddy://setup";

/// Cuánto sobresale el pico del popover sobre la fila del icono.
const TAIL_OVERLAP: i32 = 4;
/// Del borde derecho de la ventana al centro del pico: `right: 24px` + 12/2.
const TAIL_INSET: i32 = 30;
/// Aire mínimo contra los bordes del área de trabajo.
const EDGE_MARGIN: i32 = 8;

fn popover(app: &AppHandle) -> Option<WebviewWindow> {
    app.get_webview_window(POPOVER)
}

/// El rectángulo del icono de la bandeja, si la plataforma lo sabe.
///
/// Windows y macOS lo informan; en Linux el icono lo dibuja otra aplicación
/// (AppIndicator / StatusNotifier) y no hay rectángulo que consultar, así que
/// ahí se cae al cursor del clic y, si tampoco hay, a la esquina del área de
/// trabajo.
fn tray_rect(app: &AppHandle) -> Option<Rect> {
    app.tray_by_id(TRAY_ID)?.rect().ok().flatten()
}

fn to_physical(rect: Rect, scale: f64) -> PhysicalRect<i32, u32> {
    let position = match rect.position {
        Position::Physical(p) => p,
        Position::Logical(p) => PhysicalPosition::new(
            (p.x * scale).round() as i32,
            (p.y * scale).round() as i32,
        ),
    };
    let size = match rect.size {
        Size::Physical(s) => s,
        Size::Logical(s) => tauri::PhysicalSize::new(
            (s.width * scale).round().max(1.0) as u32,
            (s.height * scale).round().max(1.0) as u32,
        ),
    };
    PhysicalRect { position, size }
}

/// Pone el popover con el pico apuntando al icono de la bandeja, sin salirse
/// del área de trabajo del monitor.
fn place_at(win: &WebviewWindow, anchor: Option<PhysicalRect<i32, u32>>) {
    let Ok(size) = win.outer_size() else {
        return;
    };
    let monitor = win
        .current_monitor()
        .ok()
        .flatten()
        .or_else(|| win.primary_monitor().ok().flatten());
    let Some(monitor) = monitor else {
        return;
    };
    let area = monitor.work_area();
    let width = size.width as i32;
    let height = size.height as i32;
    let (min_x, max_x) = (
        area.position.x + EDGE_MARGIN,
        area.position.x + area.size.width as i32 - width - EDGE_MARGIN,
    );
    let (min_y, max_y) = (
        area.position.y + EDGE_MARGIN,
        area.position.y + area.size.height as i32 - height - EDGE_MARGIN,
    );

    let (x, y) = match anchor {
        Some(rect) if rect.size.width > 0 && rect.size.height > 0 => {
            let icon_center_x = rect.position.x + rect.size.width as i32 / 2;
            let x = icon_center_x + TAIL_INSET - width;
            let y = rect.position.y - height + TAIL_OVERLAP;
            (x, y)
        }
        // Sin rectángulo: pegado a la esquina inferior derecha, que es donde
        // está la bandeja en la disposición por defecto de los tres sistemas.
        _ => (max_x, max_y),
    };

    let _ = win.set_position(PhysicalPosition::new(
        x.clamp(min_x, max_x.max(min_x)),
        y.clamp(min_y, max_y.max(min_y)),
    ));
}

/// Escala del monitor donde vive la ventana; el rect de la bandeja puede venir
/// en píxeles físicos o lógicos según la plataforma.
fn scale_of(win: &WebviewWindow) -> f64 {
    win.scale_factor().unwrap_or(1.0)
}

fn show_popover(app: &AppHandle, anchor: Option<Rect>) {
    let Some(win) = popover(app) else {
        return;
    };
    // La preferencia guardada (S5) manda sobre el tamaño de `tauri.conf.json`: si
    // el dueño dejó el popover grande, se abre grande. Se aplica ANTES de
    // anclar, porque `place_at` mide la ventana con `outer_size`.
    if let Some(state) = app.try_state::<commands::AppState>() {
        commands::apply_popover_size(&win, state.expanded());
    }
    let anchor = anchor.or_else(|| tray_rect(app));
    let scale = scale_of(&win);
    place_at(&win, anchor.map(|rect| to_physical(rect, scale)));
    let _ = win.show();
    let _ = win.set_focus();
}

fn toggle_popover(app: &AppHandle) {
    let Some(win) = popover(app) else {
        return;
    };
    if win.is_visible().unwrap_or(false) {
        let _ = win.hide();
    } else {
        show_popover(app, None);
    }
}

fn build_tray(app: &AppHandle) -> tauri::Result<()> {
    let open = MenuItem::with_id(app, "open", "Abrir Buddy", true, None::<&str>)?;
    let setup = MenuItem::with_id(app, "setup", "Cambiar dirección del equipo", true, None::<&str>)?;
    // El tilde sale del registro, no de una preferencia propia: si el dueño lo
    // apagó con `msconfig`, el menú tiene que decir la verdad.
    let autostart = CheckMenuItem::with_id(
        app,
        "autostart",
        "Iniciar con Windows",
        true,
        autostart::is_enabled(),
        None::<&str>,
    )?;
    app.manage(AutostartMenu(autostart.clone()));
    let quit = MenuItem::with_id(app, "quit", "Salir", true, None::<&str>)?;
    let menu = Menu::with_items(app, &[&open, &setup, &autostart, &quit])?;

    let icon = Image::from_bytes(include_bytes!("../icons/tray.png"))?;

    let mut builder = TrayIconBuilder::with_id(TRAY_ID)
        .icon(icon)
        .tooltip("Buddy")
        .menu(&menu)
        // El clic izquierdo abre el popover; el menú queda para el derecho, que
        // es lo que hace cualquier icono de bandeja en Windows.
        .show_menu_on_left_click(false)
        .on_menu_event(|app, event| match event.id().as_ref() {
            "open" => show_popover(app, None),
            // El asistente vive en el popover. El menú no puede abrir un diálogo
            // nativo propio sin duplicar el formulario, así que muestra el
            // popover y le avisa qué hacer.
            "setup" => {
                show_popover(app, None);
                let _ = app.emit(OPEN_SETUP, ());
            }
            // Se prende o se apaga según lo que dice el registro ahora, no lo que
            // el menú mostraba: entre abrir el menú y hacer clic el dueño pudo
            // haberlo tocado en otro lado.
            "autostart" => {
                let quiere = !autostart::is_enabled();
                autostart::set_enabled(quiere);
                let item = app.state::<AutostartMenu>();
                let _ = item.0.set_checked(autostart::is_enabled());
            }
            "quit" => app.exit(0),
            _ => {}
        })
        .on_tray_icon_event(|tray, event| {
            // El tilde del arranque automático se refresca antes de que el menú
            // se dibuje: el registro lo puede haber cambiado otra cosa
            // (`msconfig`, otra versión del instalador) desde que arrancamos.
            if let TrayIconEvent::Click {
                button: MouseButton::Right,
                ..
            } = event
            {
                if let Some(item) = tray.app_handle().try_state::<AutostartMenu>() {
                    let _ = item.0.set_checked(autostart::is_enabled());
                }
            }
            // El evento del clic trae el rectángulo del icono, que es más
            // confiable que preguntarlo después: es el mismo dato, medido en el
            // momento exacto en que el dueño hizo clic.
            if let TrayIconEvent::Click {
                button: MouseButton::Left,
                button_state: MouseButtonState::Up,
                rect,
                ..
            } = event
            {
                let app = tray.app_handle();
                // El clic que viene justo después de esconderse por perder el
                // foco es el mismo gesto, no un pedido de abrir.
                let hidden_at = *HIDDEN_BY_BLUR.lock().unwrap_or_else(|p| p.into_inner());
                if tray_click_is_the_blur(hidden_at, Instant::now()) {
                    return;
                }
                if let Some(win) = popover(app) {
                    if !win.is_visible().unwrap_or(false) {
                        show_popover(app, Some(rect));
                        return;
                    }
                }
                toggle_popover(app);
            }
        });

    // El icono de ojos se dibuja a color; en la barra de menú de macOS se usa la
    // silueta monocroma, que es la convención de esa plataforma.
    if cfg!(target_os = "macos") {
        builder = builder.icon_as_template(true);
    }

    builder.build(app)?;
    Ok(())
}

fn main() {
    // Antes de construir nada: si ya hay un Buddy abierto, este proceso avisa y
    // se va. Un segundo icono en la bandeja no es "abrir la app dos veces", es un
    // dueño confundido con dos pollers del mismo equipo.
    if single_instance::claim() == single_instance::Claim::AlreadyRunning {
        single_instance::signal_existing();
        return;
    }

    tauri::Builder::default()
        .setup(|app| {
            // El store se carga una vez, antes de que exista la ventana: si la
            // conexión ya está guardada, el primer `device_status` del popover
            // sale con datos en vez de con "sin conectar" y un salto después.
            let dir = app.path().app_config_dir()?;
            app.manage(commands::AppState::new(dir));
            build_tray(app.handle())?;
            // La primera instancia escucha a las que llegaron después: mostrar el
            // popover es lo que el dueño esperaba al tocar el acceso directo.
            let handle = app.handle().clone();
            single_instance::on_second_instance(move || show_popover(&handle, None));
            // El popover nace con el tamaño de `tauri.conf.json`; si el dueño lo
            // dejó grande en la sesión anterior, se agranda antes de mostrarlo
            // por primera vez (S5).
            if let Some(win) = popover(app.handle()) {
                let expanded = app.state::<commands::AppState>().expanded();
                commands::apply_popover_size(&win, expanded);
            }
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::connection_load,
            commands::connection_save,
            commands::connection_forget,
            commands::device_status,
            commands::device_screen,
            commands::device_focus,
            commands::device_lcd,
            commands::device_led,
            commands::panel_login,
            commands::panel_logout,
            commands::panel_calendar,
            commands::panel_calendar_events,
            commands::panel_calendar_month,
            commands::popover_state,
            commands::popover_expand,
            commands::popover_pin,
            commands::open_panel,
        ])
        // Cerrar la ventana no cierra el programa: la esconde. Con `prevent_close`
        // el evento no llega al runtime y no hay forma de que el cierre destruya
        // la única ventana que la app sabe volver a mostrar.
        .on_window_event(|window, event| match event {
            WindowEvent::CloseRequested { api, .. } => {
                api.prevent_close();
                let _ = window.hide();
            }
            // Clic afuera: se esconde, salvo que el dueño lo haya fijado. Es el
            // pin el que decide, no la costumbre: fijado queda a la vista aunque
            // se trabaje en otra ventana.
            WindowEvent::Focused(false) => {
                let app = window.app_handle();
                let pinned = app.state::<commands::AppState>().pinned();
                if !pinned && window.is_visible().unwrap_or(false) {
                    let _ = window.hide();
                    *HIDDEN_BY_BLUR.lock().unwrap_or_else(|p| p.into_inner()) = Some(Instant::now());
                }
            }
            _ => {}
        })
        .build(tauri::generate_context!())
        .expect("no pude arrancar Buddy")
        .run(|_app, event| {
            // `code == None` es el cierre implícito: se quedó sin ventanas
            // visibles. Se cancela para que la bandeja sobreviva. `app.exit(0)`
            // trae `Some(0)` y pasa, que es el "Salir" del menú.
            if let RunEvent::ExitRequested { api, code, .. } = event {
                if code.is_none() {
                    api.prevent_exit();
                }
            }
        });
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn un_clic_en_la_bandeja_justo_despues_del_blur_es_el_mismo_gesto() {
        // El clic en el icono le saca el foco a la ventana, así que el que
        // esconde y el que llega después son el mismo gesto. Sin esto, esconder
        // el popover desde la bandeja lo volvería a mostrar.
        let now = Instant::now();
        let justo = now.checked_sub(Duration::from_millis(50));
        assert!(tray_click_is_the_blur(justo, now));

        // Pasado el margen es un pedido nuevo: el dueño quiere volver a abrirlo.
        let tarde = now.checked_sub(Duration::from_millis(900));
        assert!(!tray_click_is_the_blur(tarde, now));

        // Si nunca se escondió por perder el foco, el clic siempre abre.
        assert!(!tray_click_is_the_blur(None, now));
    }
}
