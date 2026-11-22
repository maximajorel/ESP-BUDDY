// Buddy en la bandeja del sistema.
//
// Una sola ventana, sin bordes ni decoración, que vive escondida y aparece
// anclada al icono de la bandeja. Cerrar y minimizar son lo mismo: esconder.
// Salir es una acción explícita del menú de la bandeja, porque una app de
// bandeja que muere al cerrar su ventana no es una app de bandeja.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod api;
mod commands;
mod store;

use tauri::{
    image::Image,
    menu::{Menu, MenuItem},
    tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent},
    AppHandle, Emitter, Manager, PhysicalPosition, PhysicalRect, Position, Rect, RunEvent, Size,
    WebviewWindow, WindowEvent,
};

const POPOVER: &str = "popover";
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

fn show_popover(app: &AppHandle) {
    let Some(win) = popover(app) else {
        return;
    };
    let scale = scale_of(&win);
    place_at(&win, tray_rect(app).map(|r| to_physical(r, scale)));
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
        show_popover(app);
    }
}

fn build_tray(app: &AppHandle) -> tauri::Result<()> {
    let open = MenuItem::with_id(app, "open", "Abrir Buddy", true, None::<&str>)?;
    let setup = MenuItem::with_id(app, "setup", "Cambiar dirección del equipo", true, None::<&str>)?;
    let quit = MenuItem::with_id(app, "quit", "Salir", true, None::<&str>)?;
    let menu = Menu::with_items(app, &[&open, &setup, &quit])?;

    let icon = Image::from_bytes(include_bytes!("../icons/tray.png"))?;

    let mut builder = TrayIconBuilder::with_id(TRAY_ID)
        .icon(icon)
        .tooltip("Buddy")
        .menu(&menu)
        // El clic izquierdo abre el popover; el menú queda para el derecho, que
        // es lo que hace cualquier icono de bandeja en Windows.
        .show_menu_on_left_click(false)
        .on_menu_event(|app, event| match event.id().as_ref() {
            "open" => show_popover(app),
            // El asistente vive en el popover. El menú no puede abrir un diálogo
            // nativo propio sin duplicar el formulario, así que muestra el
            // popover y le avisa qué hacer.
            "setup" => {
                show_popover(app);
                let _ = app.emit(OPEN_SETUP, ());
            }
            "quit" => app.exit(0),
            _ => {}
        })
        .on_tray_icon_event(|tray, event| {
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
                if let Some(win) = popover(app) {
                    if !win.is_visible().unwrap_or(false) {
                        place_at(&win, Some(to_physical(rect, scale_of(&win))));
                        let _ = win.show();
                        let _ = win.set_focus();
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
    tauri::Builder::default()
        .setup(|app| {
            // El store se carga una vez, antes de que exista la ventana: si la
            // conexión ya está guardada, el primer `device_status` del popover
            // sale con datos en vez de con "sin conectar" y un salto después.
            let dir = app.path().app_config_dir()?;
            app.manage(commands::AppState::new(dir));
            build_tray(app.handle())?;
            Ok(())
        })
        .invoke_handler(tauri::generate_handler![
            commands::connection_load,
            commands::connection_save,
            commands::connection_forget,
            commands::device_status,
            commands::device_screen,
            commands::device_focus,
            commands::panel_login,
            commands::panel_logout,
            commands::panel_calendar,
            commands::open_panel,
        ])
        // Cerrar la ventana no cierra el programa: la esconde. Con `prevent_close`
        // el evento no llega al runtime y no hay forma de que el cierre destruya
        // la única ventana que la app sabe volver a mostrar.
        .on_window_event(|window, event| {
            if let WindowEvent::CloseRequested { api, .. } = event {
                api.prevent_close();
                let _ = window.hide();
            }
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
