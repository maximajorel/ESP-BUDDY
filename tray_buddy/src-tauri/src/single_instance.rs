// Una sola copia de Buddy.
//
// Para una app que vive en la bandeja, una segunda instancia es un segundo
// icono, dos pollers del equipo y dos veces el mismo menú. El dueño toca el
// acceso directo creyendo que "abre" lo que ya está abierto, y no ve nada
// parecido a lo que esperaba.
//
// El candado es una mutex con nombre, en el espacio de la sesión del usuario
// (`Local\`): Windows la libera solo cuando el proceso muere, así que no queda un
// archivo rancio si la app se cuelga. El segundo proceso no se va en silencio:
// levanta un evento con nombre y el primero muestra su popover, que es lo que el
// dueño buscaba al hacer doble clic.

/// Qué encontró esta instancia al arrancar.
#[derive(Debug, PartialEq, Eq)]
pub enum Claim {
    /// No había otra copia: esta manda y se queda con el candado.
    First,
    /// Ya hay una corriendo: esta se tiene que ir.
    AlreadyRunning,
}

#[cfg(not(windows))]
pub fn claim() -> Claim {
    Claim::First
}

#[cfg(not(windows))]
pub fn signal_existing() {}

#[cfg(not(windows))]
pub fn on_second_instance<F: Fn() + Send + 'static>(_f: F) {}

#[cfg(windows)]
pub use win::{claim, on_second_instance, signal_existing};

#[cfg(windows)]
mod win {
    use super::Claim;
    use std::sync::OnceLock;
    use windows_sys::Win32::Foundation::{
        CloseHandle, GetLastError, ERROR_ALREADY_EXISTS, HANDLE,
    };
    use windows_sys::Win32::System::Threading::{
        CreateEventW, CreateMutexW, OpenEventW, SetEvent, WaitForSingleObject, EVENT_MODIFY_STATE,
    };

    /// El candado. `Local\` lo deja por sesión del usuario: dos sesiones de
    /// Windows (dos escritorios remotos, por ejemplo) pueden tener el suyo.
    pub const MUTEX_NAME: &str = "Local\\ar.esp32gadget.buddy.instance";
    /// El aviso de "mostrate". Auto-reset: cada `SetEvent` despierta al primero
    /// una vez, así que tres dobles clics seguidos son tres popovers, no uno.
    pub const EVENT_NAME: &str = "Local\\ar.esp32gadget.buddy.show";

    /// El handle de la mutex vive en un `static`: si se cerrara, el candado se
    /// liberaría y la próxima instancia se creería la primera.
    static MUTEX: OnceLock<usize> = OnceLock::new();
    static SHOW_EVENT: OnceLock<usize> = OnceLock::new();

    fn wide(s: &str) -> Vec<u16> {
        s.encode_utf16().chain(std::iter::once(0)).collect()
    }

    fn handle(raw: usize) -> HANDLE {
        raw as HANDLE
    }

    /// Pide el candado. La mutex se crea sin dueño: lo que importa es que exista
    /// y que este proceso la tenga abierta mientras viva.
    pub fn claim() -> Claim {
        let name = wide(MUTEX_NAME);
        let mutex = unsafe { CreateMutexW(std::ptr::null(), 0, name.as_ptr()) };
        if mutex.is_null() {
            // Sin candado no se puede saber si hay otra copia. Arrancar es menos
            // malo que no arrancar: el dueño al menos tiene su app.
            return Claim::First;
        }
        let already = unsafe { GetLastError() } == ERROR_ALREADY_EXISTS;
        if already {
            unsafe { CloseHandle(mutex) };
            return Claim::AlreadyRunning;
        }
        let _ = MUTEX.set(mutex as usize);
        Claim::First
    }

    /// El segundo proceso avisa y se va. Si nadie contesta (el primero está
    /// arrancando y todavía no creó el evento) no hay nada que hacer: se va igual.
    pub fn signal_existing() {
        let name = wide(EVENT_NAME);
        let event = unsafe { OpenEventW(EVENT_MODIFY_STATE, 0, name.as_ptr()) };
        if event.is_null() {
            return;
        }
        unsafe {
            SetEvent(event);
            CloseHandle(event);
        }
    }

    /// Primer proceso: escucha los avisos de las copias que llegaron después.
    pub fn on_second_instance<F: Fn() + Send + 'static>(f: F) {
        let name = wide(EVENT_NAME);
        let event = unsafe { CreateEventW(std::ptr::null(), 0, 0, name.as_ptr()) };
        if event.is_null() {
            return;
        }
        let _ = SHOW_EVENT.set(event as usize);
        let raw = event as usize;
        std::thread::spawn(move || loop {
            // `INFINITE`: el hilo duerme hasta que alguien toca el acceso
            // directo, no consulta nada mientras tanto.
            unsafe { WaitForSingleObject(handle(raw), u32::MAX) };
            f();
        });
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        #[test]
        fn los_nombres_son_de_la_sesion_del_usuario_y_no_de_toda_la_maquina() {
            // `Local\` y no `Global\`: dos usuarios con sesión abierta pueden
            // tener cada uno su Buddy, pero uno solo por sesión.
            assert!(MUTEX_NAME.starts_with("Local\\"));
            assert!(EVENT_NAME.starts_with("Local\\"));
            assert_ne!(MUTEX_NAME, EVENT_NAME);
            // Sin NUL interno: el nombre se pasa como cadena ancha terminada en 0.
            assert!(!MUTEX_NAME.contains('\0'));
            assert_eq!(wide("ab"), vec![97, 98, 0]);
        }
    }
}
