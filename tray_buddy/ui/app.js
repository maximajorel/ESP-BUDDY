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
/** Último `GET /api/calendar`: la agenda lo usa para las palabras de S6. */
let lastCalendar = null;
/** Si el popover está agrandado (S5). Lo manda el backend, no se adivina. */
let popoverExpanded = false;

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

/** Las tres tarjetas del popover, que se ocultan mientras el asistente manda. */
function sections() {
  return Array.from(document.querySelectorAll(".body > section:not(.wizard)"));
}

function openWizard() {
  wizardOpen = true;
  // Se ocultan, no se tapan: un formulario encima de botones vivos invita a
  // tocar lo que hay detrás y a ver la UI vieja a través de los bordes.
  sections().forEach((section) => {
    section.hidden = true;
  });
  // El pie también: sin conexión validada, "Panel" no lleva a ningún lado y la
  // píldora de estado repetiría lo que el formulario ya dice.
  document.querySelector(".foot").hidden = true;
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
  sections().forEach((section) => {
    section.hidden = false;
  });
  document.querySelector(".foot").hidden = false;
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

function renderCalendar(data, events) {
  lastCalendar = data;
  $("calLogin").hidden = true;
  $("calNextWhat").textContent = data?.hasNext ? data.nextSummary || "Sin título" : "Sin citas próximas";
  $("calNextWhere").textContent = data?.hasNext
    ? `${data.selectedName || "el calendario elegido"} · ${minutesLabel(data.nextInMin ?? 0)}`
    : data?.authorized
      ? "Google Calendar no tiene nada a la vista"
      : "Google Calendar no está autorizado en el equipo";
  // La columna grande es una HORA, no un recordatorio: mostrar el aviso (10′)
  // al lado de "en 12 min" repetía la misma información con otro número.
  // La ventana del equipo ahora arranca en la medianoche local, así que el
  // primer evento de la lista puede ser uno que ya pasó: la hora se toma del
  // mismo evento que el equipo llama "próxima cita", y si no se puede casar
  // (relojes desfasados) se cae al aviso en minutos en vez de mentir.
  const list = events?.events ?? [];
  const tz = events?.tz ?? 0;
  const day = deviceDay(events);
  const nowSec = Math.floor(Date.now() / 1000);
  const next = nextEvent(list, nowSec);
  const labelled =
    Boolean(next) &&
    data?.hasNext &&
    (next.summary || "Sin título") === (data.nextSummary || "Sin título");
  $("calNextHour").textContent = data?.hasNext
    ? labelled
      ? next.allDay
        ? isToday(next, day)
          ? "hoy"
          : dayLabel(next.startEpoch, day, tz)
        : eventClock(next.startEpoch, tz)
      : `${data.nextLeadMin ?? 0}′`
    : "--:--";
  $("calNextDay").textContent = data?.hasNext ? "Próxima cita" : "Calendario";
  // El contador es de hoy, no de la ventana de 26 h: el dueño pidió el día.
  $("calMeta").textContent = calendarMeta(data, events);

  // La lista del día viaja en otro endpoint (`GET /api/calendar/events`), que
  // se pide después; acá se deja el espacio para no taparlo con un renglón
  // provisorio.
  $("calAgenda").textContent = "";
}

/**
 * El contador de la cabecera del calendario.
 *
 * "sin sesión" sólo vale cuando falta la del panel. Un equipo configurado pero
 * sin cuenta de Google no tiene sesión que pedir: ahí el problema es la
 * autorización y la cabecera lo dice con otra palabra.
 */
function calendarMeta(data, events) {
  if (!data || typeof data.kind !== "string") return "sin datos";
  if (!data.configured) return "sin configurar";
  if (!data.authorized) return "sin Google";
  if (!events) return `${data.eventCount ?? 0} eventos`;
  const day = deviceDay(events);
  const today = (events.events ?? []).filter((event) => isToday(event, day)).length;
  return today ? `${today} hoy` : "sin eventos hoy";
}

/**
 * La medianoche local de un epoch, con el offset fijo del equipo.
 *
 * Es la misma aritmética que `calendarLocalMidnight` del firmware: el día que
 * vale es el del dueño, no el de la PC que corre el popover.
 */
function localMidnight(epoch, tz) {
  const offset = tz * 3600;
  const local = epoch + offset;
  const intoDay = ((local % 86400) + 86400) % 86400;
  return local - intoDay - offset;
}

/**
 * Los bordes del día que manda el equipo (`dayStart`/`dayEnd`).
 *
 * Un firmware viejo no los publica y llegan en 0: ahí se derivan del `tz`, para
 * que actualizar la app sola no deje la agenda sin secciones.
 */
function deviceDay(data) {
  const tz = data?.tz ?? 0;
  if (data?.dayStart > 0 && data?.dayEnd > data.dayStart) {
    return { start: data.dayStart, end: data.dayEnd };
  }
  const start = localMidnight(Math.floor(Date.now() / 1000), tz);
  return { start, end: start + 86400 };
}

/** Si el evento toca el día local: lo que empieza hoy y lo que ya está corriendo. */
function isToday(event, day) {
  const start = event.startEpoch ?? 0;
  const end = event.endEpoch ?? start;
  return start < day.end && end > day.start;
}

/** El próximo evento, con la misma regla que `calendarStatusInfo`: lo que
 *  empezó hace más de una hora ya no es "próximo", y la lista viene ordenada. */
function nextEvent(list, nowSec) {
  return list.find((event) => (event.startEpoch ?? 0) >= nowSec - 3600) ?? null;
}

/**
 * Hora local del evento a partir del epoch UTC y del offset del equipo.
 *
 * `tz` son horas (puede ser 5.5): se suma el offset en segundos y se leen los
 * componentes en UTC, así el resultado no depende de la zona horaria del PC —
 * el equipo es el que sabe dónde está el dueño.
 */
function eventClock(epoch, tz) {
  const shifted = new Date((epoch + tz * 3600) * 1000);
  const hours = String(shifted.getUTCHours()).padStart(2, "0");
  const minutes = String(shifted.getUTCMinutes()).padStart(2, "0");
  return `${hours}:${minutes}`;
}

/** El renglón de una agenda sin filas, con las palabras de S6. */
function agendaEmptyText(failure) {
  if (failure) return failure;
  if (lastCalendar && lastCalendar.authorized === false) {
    return lastCalendar.lastError || "El equipo no está conectado a Google.";
  }
  return "Sin eventos para hoy.";
}

function agendaRow(hourText, allDay, summary, location, past) {
  const row = document.createElement("div");
  row.className = past ? "agenda-row is-past" : "agenda-row";

  const hour = document.createElement("span");
  hour.className = allDay ? "hour is-allday" : "hour";
  hour.textContent = hourText;

  const text = document.createElement("span");
  const what = document.createElement("span");
  what.className = "what";
  what.textContent = summary || "Sin título";
  text.appendChild(what);
  if (location) {
    const where = document.createElement("span");
    where.className = "where";
    where.textContent = location;
    text.appendChild(where);
  }

  row.append(hour, text);
  return row;
}

/** El rótulo de un día dentro de la agenda, con la regla que lo separa. */
function agendaDayTag(title) {
  const tag = document.createElement("div");
  tag.className = "daytag";
  const label = document.createElement("span");
  label.textContent = title;
  const rule = document.createElement("span");
  rule.className = "rule";
  tag.append(label, rule);
  return tag;
}

const WEEKDAYS = [
  "domingo",
  "lunes",
  "martes",
  "miércoles",
  "jueves",
  "viernes",
  "sábado",
];

/** El número de día local de un epoch: días desde 1970 en el offset del equipo. */
function localDayIndex(epoch, tz) {
  return Math.floor((epoch + tz * 3600) / 86400);
}

/**
 * El día de la semana de un número de día local.
 *
 * 1970-01-01 (día 0) fue jueves, así que el índice 0 es el cuarto día de la
 * semana si el domingo es 0. Se evita `Date` a propósito: el equipo ya dijo qué
 * día es, no hay zona horaria que resolver acá.
 */
function weekdayName(index) {
  return WEEKDAYS[(((index + 4) % 7) + 7) % 7];
}

/**
 * El rótulo del día de un evento: "hoy", "mañana" o su día de la semana.
 *
 * Lo usan la lista y la tarjeta de "próxima cita": son el mismo dato, no pueden
 * decir cosas distintas. En minúsculas porque el rótulo de la lista se pasa a
 * mayúsculas por CSS y la hora de la tarjeta no.
 */
function dayLabel(epoch, day, tz) {
  const index = localDayIndex(epoch, tz);
  const todayIndex = localDayIndex(day.start, tz);
  if (index === todayIndex) return "hoy";
  if (index === todayIndex + 1) return "mañana";
  return weekdayName(index);
}

/**
 * La agenda agrupada por día local, en el orden del equipo.
 *
 * El primero se rotula **Hoy** y el siguiente **Mañana**, pero la ventana del
 * equipo llega 26 h adelante: a las 23:50 entra el día siguiente al de mañana, y
 * llamarlo "Mañana" sería mentir. Ese caso (y cualquier otro) se rotula con su
 * día de la semana. Un evento que arrancó ayer y sigue abierto es de hoy; uno que
 * ya terminó antes de la medianoche local no es de nadie y se descarta.
 */
function agendaGroups(events, day, tz) {
  const todayIndex = localDayIndex(day.start, tz);
  const groups = new Map();
  events.forEach((event) => {
    const start = event.startEpoch ?? 0;
    const inToday = isToday(event, day);
    if (!inToday && (event.endEpoch ?? start) <= day.start) return;
    const index = inToday ? todayIndex : localDayIndex(start, tz);
    if (!groups.has(index)) groups.set(index, []);
    groups.get(index).push(event);
  });
  return Array.from(groups.entries())
    .sort((a, b) => a[0] - b[0])
    .map(([index, rows]) => ({
      label: index === todayIndex ? "hoy" : index === todayIndex + 1 ? "mañana" : weekdayName(index),
      rows,
    }));
}

/** El renglón sin filas, con las palabras de S6. */
function agendaEmpty(match) {
  const row = document.createElement("div");
  row.className = "agenda-empty";
  row.textContent = match;
  return row;
}

/**
 * Agenda del día (S4). El orden es el que manda el equipo: no se reordena.
 *
 * Dos secciones reales, no una lista mezclada: HOY con todos los eventos de hoy
 * (los que ya terminaron, atenuados; un evento de todo el día, rotulado "todo el
 * día") y MAÑANA con el resto de la ventana que el equipo consultó. Un evento de
 * otro día nunca aparece dentro de "Hoy".
 */
function renderAgenda(data, failure) {
  const agenda = $("calAgenda");
  agenda.textContent = "";
  if (failure) {
    agenda.appendChild(agendaEmpty(agendaEmptyText(failure)));
    return;
  }
  if (lastCalendar && lastCalendar.authorized === false) {
    // Sin autorización no hay días que separar: se dice el motivo y nada más.
    agenda.appendChild(agendaEmpty(agendaEmptyText(null)));
    return;
  }

  const tz = data?.tz ?? 0;
  const events = data?.events ?? [];
  const day = deviceDay(data);
  const nowSec = Math.floor(Date.now() / 1000);
  const groups = agendaGroups(events, day, tz);

  const row = (event) => {
    const hour = event.allDay ? "todo el día" : eventClock(event.startEpoch ?? 0, tz);
    const end = event.endEpoch ?? event.startEpoch ?? 0;
    return agendaRow(hour, Boolean(event.allDay), event.summary, event.location, end <= nowSec);
  };

  const today = groups.find((group) => group.label === "hoy");
  agenda.appendChild(agendaDayTag("hoy"));
  if (today) {
    today.rows.forEach((event) => agenda.appendChild(row(event)));
  } else {
    agenda.appendChild(agendaEmpty("Sin eventos para hoy."));
  }
  groups
    .filter((group) => group.label !== "hoy")
    .forEach((group) => {
      agenda.appendChild(agendaDayTag(group.label));
      group.rows.forEach((event) => agenda.appendChild(row(event)));
    });
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
  // La agenda no se inventa: sin sesión no hay lista, y el renglón usa las
  // mismas palabras que la tarjeta.
  const agenda = $("calAgenda");
  agenda.textContent = "";
  const row = document.createElement("div");
  row.className = "agenda-empty";
  row.textContent = "El equipo pide sesión del panel";
  agenda.appendChild(row);
}

async function loadCalendar() {
  if (!status) return;
  let next = null;
  let events = null;
  let failure = null;

  try {
    next = await invoke("panel_calendar");
  } catch (error) {
    failure = error;
  }
  if (!failure) {
    try {
      events = await invoke("panel_calendar_events");
    } catch (error) {
      failure = error;
    }
  }

  if (failure?.code === 401) {
    calendarAsksSession = true;
    lastCalendar = null;
    showCalendarSession(null);
    return;
  }

  calendarAsksSession = false;
  // Un endpoint que falla no borra al otro: la próxima cita y la agenda son dos
  // pedidos distintos, y con un firmware viejo (sin `/api/calendar/events`) el
  // dueño igual tiene que ver su próxima cita con la tarjeta entera.
  if (next) {
    renderCalendar(next, events);
  } else {
    renderCalendar({ lastError: failure?.message }, null);
  }
  renderAgenda(events, events ? null : agendaFailureText(failure));
}

/** El motivo por el que no hay agenda, con las palabras del dueño. */
function agendaFailureText(failure) {
  // 404 del equipo = firmware sin el endpoint: no hay nada que reintentar, hay
  // que actualizarlo. Sin esto el popover muestra el texto crudo del firmware.
  if (failure?.code === 404) {
    return "Actualizá el firmware del equipo para ver la agenda del día.";
  }
  return failure?.message ?? "No pude pedir la agenda del día";
}

function wireCalendar() {
  $("btnCalAgenda").addEventListener("click", () => setPopoverExpanded(!popoverExpanded));

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

// ── tamaño del popover (S5) ─────────────────────────────────────────────────

/** Pinta el estado del botón y el ancho de la losa sin mover la ventana. */
function paintPopoverSize(expanded) {
  popoverExpanded = Boolean(expanded);
  document.querySelector(".win-wrap").classList.toggle("is-expanded", popoverExpanded);
  const button = $("btnCalAgenda");
  button.textContent = popoverExpanded ? "Achicar" : "Agenda";
  button.setAttribute("aria-pressed", popoverExpanded ? "true" : "false");
  button.title = popoverExpanded ? "Volver al tamaño chico" : "Ver la agenda del día";
}

/**
 * Agranda o achica la ventana. El redimensionado y el reposicionamiento los
 * hace Rust (`popover_expand`): el frontend no puede mover la ventana sin
 * permiso de Tauri, y la esquina inferior derecha la cuida el nativo.
 */
async function setPopoverExpanded(expanded) {
  // Abierto en un navegador para iterar diseño no hay a quién pedirle la
  // ventana: se cambia sólo la clase, que es lo que se quiere ver.
  if (!invoke) {
    paintPopoverSize(expanded);
    return;
  }
  const button = $("btnCalAgenda");
  button.disabled = true;
  try {
    const size = await invoke("popover_expand", { expanded });
    paintPopoverSize(size?.expanded ?? expanded);
  } catch (error) {
    setOnline(false, "Error", error?.message);
  } finally {
    button.disabled = false;
  }
}

async function loadPopoverSize() {
  if (!invoke) return;
  try {
    const size = await invoke("popover_state");
    paintPopoverSize(size?.expanded);
  } catch {
    // El tamaño es cosmético: si el store no contesta, arranca chico.
  }
}

// ── ciclo de vida ───────────────────────────────────────────────────────────

async function refresh() {
  if (!connection) {
    setOnline(false, "Offline", "—");
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
    setOnline(false, "Caído", error?.message ?? "—");
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
  await loadPopoverSize();

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
    setOnline(false, "Offline", "—");
    openWizard();
    return;
  }

  setOnline(false, "Conectando", connection.host);
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
