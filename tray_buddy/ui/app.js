// Buddy · popover de bandeja.
//
// Sin lógica de red acá a propósito (S12): el equipo no manda CORS, así que todo
// HTTP lo hace Rust (`src-tauri/src/api.rs`) y esta capa sólo llama `invoke` y
// pinta. El equipo es la única fuente de verdad (S10): no hay contadores propios,
// no se guardan tokens de Spotify ni de Calendar, y sin conexión se dice en vez
// de inventar.

const tauri = window.__TAURI__ ?? null;
const invoke = tauri?.core?.invoke ?? null;
const listen = tauri?.event?.listen ?? null;

// Abrir `ui/index.html` directo en un navegador es la forma de iterar el diseño
// sin compilar Rust; ahí no existe la API de Tauri y esta cáscara mantiene el
// popover usable (los botones no hacen nada, pero no rompen).
const win =
  tauri?.window?.getCurrentWindow?.() ?? {
    hide() {},
    setAlwaysOnTop: () => Promise.resolve(),
  };

// Los nombres de fábrica del firmware, para que el popover tenga algo que pintar
// en el primer frame. Los reales llegan de `GET /api/status` y los puede haber
// editado el dueño desde el panel.
const FALLBACK_SCREENS = [
  { name: "Reloj", icon: '<circle cx="12" cy="12" r="8"/><path d="M12 8v4l3 2"/>' },
  { name: "Tragamonedas", icon: '<rect x="4" y="5" width="16" height="14" rx="2"/><path d="M9 10v4M15 10v4"/>' },
  { name: "Meteoritos", icon: '<path d="M14 4l5 13-5-3-5 3 5-13z"/>' },
  { name: "Red", icon: '<circle cx="12" cy="12" r="8"/><path d="M4 12h16M12 4v16"/>' },
  { name: "Música", icon: '<path d="M9 18V6l10-2v12"/><circle cx="7" cy="18" r="2"/><circle cx="17" cy="16" r="2"/>' },
  { name: "Sofía", icon: '<path d="M5 12a7 7 0 0114 0"/><circle cx="9" cy="14" r="1"/><circle cx="15" cy="14" r="1"/>' },
  { name: "Config", icon: '<circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3"/>' },
];

/** Último `GET /api/status` y cuándo llegó. */
let status = null;
let selectedMinutes = 25;
let connection = null;
let wizardOpen = false;
let calendarAsksSession = false;

const $ = (id) => document.getElementById(id);

// ── conexión ────────────────────────────────────────────────────────────────

function setOnline(online, label, addr) {
  const head = $("winStatus");
  head.dataset.online = online ? "true" : "false";
  $("winStatusText").textContent = label;

  const pill = $("connPill");
  pill.classList.toggle("online", online);
  pill.classList.toggle("offline", !online);
  $("connText").textContent = label;
  $("connAddr").textContent = addr || "—";
}

function openWizard() {
  wizardOpen = true;
  $("wizard").hidden = false;
  $("wizErr").hidden = true;
  // El botón de cancelar sólo tiene sentido si ya hay un equipo guardado: en el
  // primer arranque no hay a dónde volver.
  $("wizCancel").hidden = !connection;
  if (connection) {
    $("wizAddr").value = connection.host;
    $("wizPort").value = connection.port === 80 ? "" : String(connection.port);
  }
  $("wizAddr").focus();
  $("wizAddr").select();
}

function closeWizard() {
  wizardOpen = false;
  $("wizard").hidden = true;
}

function wizError(message) {
  const box = $("wizErr");
  box.textContent = message;
  box.hidden = false;
}

async function submitWizard(event) {
  event.preventDefault();
  const host = $("wizAddr").value.trim();
  const port = $("wizPort").value.trim();
  if (!host) {
    wizError("Falta la dirección del equipo");
    return;
  }
  const address = port ? `${host}:${port}` : host;

  const go = $("wizGo");
  go.disabled = true;
  go.textContent = "Probando…";
  $("wizErr").hidden = true;
  try {
    connection = await invoke("connection_save", { address });
    closeWizard();
    await refresh();
  } catch (error) {
    // Rust ya devolvió el texto en castellano del firmware o del transporte.
    wizError(error?.message ?? "No pude conectarme con el equipo");
  } finally {
    go.disabled = false;
    go.textContent = "Conectar";
  }
}

// ── pantalla del LCD (S9) ───────────────────────────────────────────────────

function screenName(index) {
  const names = status?.names ?? [];
  if (names[index]) return names[index];
  return FALLBACK_SCREENS[index]?.name ?? `#${index}`;
}

function renderScreens() {
  const host = $("screens");
  host.textContent = "";
  const online = Boolean(status);
  // El orden y los nombres los manda el equipo: puede haber renombrado
  // "Sofía" a "BICHO" desde el panel y el popover no tiene por qué saberlo.
  const order = status?.order?.length ? status.order : FALLBACK_SCREENS.map((_, i) => i);

  order.forEach((index) => {
    const fallback = FALLBACK_SCREENS[index] ?? FALLBACK_SCREENS[0];
    const button = document.createElement("button");
    button.className = "tile";
    button.type = "button";
    button.dataset.index = String(index);
    button.setAttribute("aria-pressed", status?.screen === index ? "true" : "false");
    button.disabled = !online;
    button.innerHTML =
      '<svg viewBox="0 0 24 24" aria-hidden="true">' +
      fallback.icon +
      "</svg>" +
      '<span class="label"></span>' +
      '<span class="flag">En pantalla</span>';
    button.querySelector(".label").textContent = screenName(index);
    button.addEventListener("click", () => chooseScreen(index));
    host.appendChild(button);
  });

  $("screenNow").textContent = status ? `${status.screen + 1}/${order.length}` : "—";
}

async function chooseScreen(index) {
  if (status?.screen === index) return;
  try {
    await invoke("device_screen", { index });
    // El equipo confirma con 200 pero el estado real se lee, no se supone.
    await refresh();
  } catch (error) {
    setOnline(false, "Error", error?.message);
  }
}

// ── temporizador (S4) ───────────────────────────────────────────────────────

function clock(ms) {
  const total = Math.max(0, Math.round(ms / 1000));
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;
  return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}

function renderFocus() {
  const focus = status?.focus;
  const active = Boolean(focus?.active);
  const finished = Boolean(focus?.finished);

  $("focusLabel").textContent = finished
    ? "Terminado"
    : active
      ? "Concentración"
      : "Pomodoro";

  // El número sale de `remainingMs` del equipo: el popover no lleva su propio
  // contador, sólo pinta lo último que le dijeron.
  const shown = active || finished ? (focus?.remainingMs ?? 0) : selectedMinutes * 60_000;
  $("focusClock").textContent = clock(shown);
  const totalMinutes = active || finished ? Math.max(1, Math.round((focus?.seconds ?? 0) / 60)) : selectedMinutes;
  $("focusTotal").textContent = ` /${totalMinutes}`;

  $("focusMeta").textContent = active
    ? `${totalMinutes} min · vuelve a Sofía al terminar`
    : status
      ? "Listo"
      : "—";

  document.querySelectorAll(".chip[data-min]").forEach((chip) => {
    const value = chip.dataset.min;
    const pressed = value === "custom" ? false : Number(value) === selectedMinutes;
    chip.setAttribute("aria-pressed", pressed ? "true" : "false");
    chip.disabled = !status;
  });

  const stop = $("btnFocusStop");
  stop.disabled = !status;
  stop.textContent = active ? "Parar" : "Arrancar";
  $("btnFocusPlus").disabled = !status || !active;
}

async function setFocus(seconds) {
  try {
    await invoke("device_focus", { seconds });
    await refresh();
  } catch (error) {
    setOnline(false, "Error", error?.message);
  }
}

function wireFocus() {
  document.querySelectorAll(".chip[data-min]").forEach((chip) => {
    chip.addEventListener("click", () => {
      const value = chip.dataset.min;
      if (value === "custom") {
        $("focusCustom").hidden = !$("focusCustom").hidden;
        if (!$("focusCustom").hidden) $("focusMinutes").focus();
        return;
      }
      selectedMinutes = Number(value);
      $("focusCustom").hidden = true;
      renderFocus();
      setFocus(selectedMinutes * 60);
    });
  });

  $("btnFocusCustom").addEventListener("click", () => {
    const minutes = Number($("focusMinutes").value);
    if (!Number.isFinite(minutes) || minutes < 1) {
      $("focusMinutes").focus();
      return;
    }
    selectedMinutes = Math.min(240, Math.round(minutes));
    $("focusCustom").hidden = true;
    renderFocus();
    setFocus(selectedMinutes * 60);
  });

  $("btnFocusStop").addEventListener("click", () => {
    // Arrancar sin sesión usa el chip elegido; parar manda 0, que cancela.
    if (status?.focus?.active) setFocus(0);
    else setFocus(selectedMinutes * 60);
  });

  $("btnFocusPlus").addEventListener("click", () => {
    // "+5 MIN suma sobre el restante informado" (S4), no sobre el total.
    const remaining = Math.round((status?.focus?.remainingMs ?? 0) / 1000);
    setFocus(remaining + 300);
  });
}

// ── calendario (S6) ─────────────────────────────────────────────────────────

function minutesLabel(minutes) {
  if (minutes <= 0) return "ahora";
  if (minutes < 60) return `en ${minutes} min`;
  const hours = Math.floor(minutes / 60);
  const rest = minutes % 60;
  return rest ? `en ${hours} h ${rest} min` : `en ${hours} h`;
}

function renderCalendar(data) {
  $("calLogin").hidden = true;
  $("calNextWhat").textContent = data?.hasNext ? data.nextSummary || "Sin título" : "Sin citas próximas";
  $("calNextWhere").textContent = data?.hasNext
    ? `${data.selectedName || "el calendario elegido"} · ${minutesLabel(data.nextInMin ?? 0)}`
    : data?.authorized
      ? "Google Calendar no tiene nada a la vista"
      : "Google Calendar no está autorizado en el equipo";
  $("calNextHour").textContent = data?.hasNext ? `${data.nextLeadMin ?? 0}′` : "--:--";
  $("calNextDay").textContent = data?.hasNext ? "Próxima cita" : "Calendario";
  $("calMeta").textContent = data?.authorized ? `${data.eventCount ?? 0} eventos` : "sin sesión";

  // El firmware publica la próxima cita y los contadores, no la lista del día:
  // el popover dice lo que hay en vez de inventar una agenda.
  const agenda = $("calAgenda");
  agenda.textContent = "";
  const row = document.createElement("div");
  row.className = "agenda-empty";
  row.textContent = data?.authorized
    ? "El equipo no publica la lista del día."
    : data?.lastError || "El equipo no está conectado a Google.";
  agenda.appendChild(row);
}

function showCalendarSession(message) {
  const form = $("calLogin");
  form.hidden = false;
  $("calErr").hidden = !message;
  if (message) $("calErr").textContent = message;
  $("calNextWhat").textContent = "Pide sesión del panel";
  $("calNextWhere").textContent = "El calendario es privado, incluso para leer";
  $("calNextHour").textContent = "--:--";
  $("calNextDay").textContent = "Calendario";
  $("calMeta").textContent = "privado";
  const agenda = $("calAgenda");
  agenda.textContent = "";
}

async function loadCalendar() {
  if (!status) return;
  try {
    const data = await invoke("panel_calendar");
    calendarAsksSession = false;
    renderCalendar(data);
  } catch (error) {
    if (error?.code === 401) {
      calendarAsksSession = true;
      showCalendarSession(null);
    } else {
      renderCalendar({ lastError: error?.message });
    }
  }
}

function wireCalendar() {
  $("calLogin").addEventListener("submit", async (event) => {
    event.preventDefault();
    const user = $("calUser").value.trim();
    const pass = $("calPass").value;
    if (!user || !pass) {
      $("calErr").hidden = false;
      $("calErr").textContent = "Usuario y clave del panel";
      return;
    }
    try {
      const opened = await invoke("panel_login", { user, pass });
      // La contraseña no se guarda en ningún lado: se manda y se olvida.
      $("calPass").value = "";
      if (!opened) {
        showCalendarSession("El equipo no abrió la sesión");
        return;
      }
      await loadCalendar();
    } catch (error) {
      showCalendarSession(error?.message ?? "No pude iniciar sesión");
    }
  });
}

// ── ciclo de vida ───────────────────────────────────────────────────────────

async function refresh() {
  if (!connection) {
    setOnline(false, "Sin conectar", "—");
    renderScreens();
    renderFocus();
    return;
  }
  try {
    status = await invoke("device_status");
    setOnline(true, "En línea", connection.port === 80 ? connection.host : `${connection.host}:${connection.port}`);
    renderScreens();
    renderFocus();
    await loadCalendar();
  } catch (error) {
    // Un fallo no borra el último estado bueno: se marca sin conexión y se dice
    // por qué. Los tiles quedan deshabilitados porque no hay a quién mandarles.
    status = null;
    setOnline(false, "Sin conexión", error?.message ?? "—");
    renderScreens();
    renderFocus();
  }
}

function hide() {
  win.hide();
}

function wireCaption() {
  $("btnHide").addEventListener("click", hide);
  $("btnClose").addEventListener("click", hide);

  const pin = $("btnPin");
  pin.addEventListener("click", async () => {
    const pinned = pin.getAttribute("aria-pressed") !== "true";
    pin.setAttribute("aria-pressed", pinned ? "true" : "false");
    await win.setAlwaysOnTop(pinned);
  });

  // Escape esconde el popover: es la salida que espera cualquiera que abrió una
  // ventana sin decoración y no encuentra una cruz.
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") {
      if (wizardOpen && connection) closeWizard();
      else hide();
    }
  });
}

function wireWizard() {
  $("wizardForm").addEventListener("submit", submitWizard);
  $("wizCancel").addEventListener("click", closeWizard);
  $("connPill").addEventListener("click", openWizard);
  // El menú de la bandeja no puede dibujar el formulario: pide abrir el
  // asistente, que vive acá.
  listen?.("buddy://setup", () => openWizard());
}

function wirePanel() {
  $("btnPanel").addEventListener("click", async () => {
    if (!status) return;
    try {
      await invoke("open_panel");
    } catch (error) {
      setOnline(false, "Error", error?.message);
    }
  });
}

async function boot() {
  renderScreens();
  renderFocus();
  wireCaption();
  wireWizard();
  wireFocus();
  wireCalendar();
  wirePanel();

  if (!invoke) {
    setOnline(false, "Sin conexión", "—");
    return;
  }

  try {
    connection = await invoke("connection_load");
  } catch {
    connection = null;
  }

  if (!connection) {
    setOnline(false, "Sin conectar", "—");
    openWizard();
    return;
  }

  setOnline(false, "Conectando…", connection.host);
  await refresh();

  // Diez segundos: el mismo ritmo que usa la landing del equipo. Con el
  // temporizador corriendo se apura a cinco, que es el único número que el dueño
  // mira cambiar.
  setInterval(() => {
    // Una ventana escondida no obliga al equipo a contestar nada.
    if (document.hidden || wizardOpen) return;
    refresh();
  }, 5000);
}

boot();
