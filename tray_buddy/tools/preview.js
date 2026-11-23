#!/usr/bin/env node
// Arnés de verificación de la UI del popover, sin Tauri y sin la placa.
//
// Copia `ui/index.html` + `app.js` + `styles.css` a un directorio temporal,
// inyecta un `window.__TAURI__` de mentira con datos de calendario y te imprime
// la URL y los dos comandos de Chrome headless: `--dump-dom` para leer el HTML
// resultante y `--screenshot` para mirarlo.
//
//   node tray_buddy/tools/preview.js day
//   node tray_buddy/tools/preview.js late-night
//
// Escenarios: day (default), tomorrow, old (firmware sin dayStart/dayEnd),
// empty, unauthorized, session (401), busy (24 eventos), multiday, late-night y
// after-tomorrow (la próxima cita es un día después de mañana).
//
// El reloj se congela en las 10:20 (o 23:50 en `late-night`) del día de hoy en
// -03, así el atenuado de "ya terminó" y las secciones salen iguales sin
// importar cuándo se corre. No es una prueba de la app compilada: lo que
// verifica es el render de la UI con el contrato del equipo.

const fs = require("node:fs");
const path = require("node:path");

const SCENARIOS = [
  "day",
  "tomorrow",
  "old",
  "empty",
  "unauthorized",
  "session",
  "busy",
  "multiday",
  "late-night",
  "after-tomorrow",
];

const scenario = process.argv[2] ?? "day";
if (!SCENARIOS.includes(scenario)) {
  console.error(`Escenario desconocido: ${scenario}`);
  console.error(`Disponibles: ${SCENARIOS.join(", ")}`);
  process.exit(2);
}

const UI = path.resolve(__dirname, "..", "ui");
// Dentro del repo y en una carpeta ignorada (`.build-*`): el Chrome de Windows
// no puede leer los temporales de WSL, y el repo vive en /mnt/c.
const ROOT = path.resolve(__dirname, "..", "..");
const OUT = path.join(ROOT, ".build-preview", "ui");

/** La misma ruta como la ve Windows, para el `file://` del Chrome de Windows. */
function windowsPath(target) {
  const mounted = target.match(/^\/mnt\/([a-z])\/(.*)$/);
  const plain = mounted ? `${mounted[1].toUpperCase()}:/${mounted[2]}` : target;
  return "file:///" + encodeURI(plain);
}

fs.rmSync(OUT, { recursive: true, force: true });
fs.mkdirSync(OUT, { recursive: true });
for (const file of ["index.html", "app.js", "styles.css"]) {
  fs.copyFileSync(path.join(UI, file), path.join(OUT, file));
}

const html = fs
  .readFileSync(path.join(OUT, "index.html"), "utf8")
  .replace(
    '<script src="app.js"></script>',
    '<script src="fixtures.js"></script>\n<script src="app.js"></script>',
  );
fs.writeFileSync(path.join(OUT, "index.html"), html);

// Los datos: el mismo contrato que `GET /api/calendar*` y `/api/status`.
const TZ = -3;
const nowSec = Math.floor(Date.now() / 1000);
const local = nowSec + TZ * 3600;
const DAY_START = local - (local % 86400) - TZ * 3600;
const DAY_END = DAY_START + 86400;
const LATE = scenario === "late-night" || scenario === "after-tomorrow";
const NOW = LATE ? DAY_START + 23 * 3600 + 50 * 60 : DAY_START + 10 * 3600 + 20 * 60;

const ev = (summary, location, start, end, leadMin, allDay = false) => ({
  summary,
  location,
  startEpoch: start,
  endEpoch: end,
  leadMin,
  allDay,
});
const payload = (events) => ({
  ok: true,
  tz: TZ,
  dayStart: DAY_START,
  dayEnd: DAY_END,
  count: events.length,
  events,
});

const TODAY_ALLDAY = ev("Feriado municipal", "", DAY_START, DAY_END, -1, true);
const TOMORROW_ALLDAY = ev("Feriado provincial", "", DAY_END, DAY_END + 86400, -1, true);
const TOMORROW_TIMED = ev("Cumpleaños de Ana", "", DAY_END + 10 * 3600, DAY_END + 11 * 3600, 30);
const DAY_EVENTS = [
  TODAY_ALLDAY,
  ev("Daily con el equipo", "Meet", DAY_START + 8 * 3600, DAY_START + 8 * 3600 + 2700, 5),
  ev("Revisión de la app", "Oficina", DAY_START + 10 * 3600, DAY_START + 11 * 3600, 10),
  ev("Turno con la Dra. Paz", "Consultorio", DAY_START + 15 * 3600, DAY_START + 16 * 3600, 20),
];

const bus = [];
for (let i = 0; i < 24; i++) {
  const start = DAY_START + i * 3600;
  bus.push(ev(`Compromiso ${i + 1}`, i % 3 ? "Sala 2" : "", start, start + 2700, 10));
}

const FIXTURES = {
  day: payload([...DAY_EVENTS, TOMORROW_TIMED]),
  tomorrow: payload([TODAY_ALLDAY, TOMORROW_ALLDAY, TOMORROW_TIMED]),
  old: { ...payload([...DAY_EVENTS, TOMORROW_TIMED]), dayStart: 0, dayEnd: 0 },
  empty: payload([]),
  unauthorized: payload([]),
  session: null,
  busy: payload(bus),
  multiday: payload([
    ev("Feriado largo", "", DAY_START - 86400, DAY_END, -1, true),
    ev("Guardia nocturna", "Sanatorio", DAY_START - 2 * 3600, DAY_START + 2 * 3600, 10),
    ev("Cena de cierre", "", DAY_START + 23 * 3600, DAY_END + 2 * 3600, 20),
    ev("Reunión de mañana", "Zoom", DAY_END + 9 * 3600, DAY_END + 10 * 3600, 10),
  ]),
  // La próxima cita es un evento de todo el día de pasado mañana: la tarjeta
  // tampoco puede rotularlo "mañana".
  "after-tomorrow": payload([
    TODAY_ALLDAY,
    ev("Feriado largo", "", DAY_END + 86400, DAY_END + 2 * 86400, -1, true),
  ]),
  "late-night": payload([
    ev("Reunión de madrugada", "Zoom", DAY_END + 70 * 60, DAY_END + 130 * 60, 10),
    ev("Trámite pasado mañana", "", DAY_END + 86400 + 3600, DAY_END + 86400 + 7200, 10),
  ]),
};

const events = FIXTURES[scenario]?.events ?? [];
const next = events.find((event) => event.startEpoch >= NOW - 3600);

const fixtures = `(function () {
  const status = {
    firmware: "esp32-gadget", version: "1.0.0",
    screen: 0, screenName: "RELOJ",
    order: [0, 1, 2, 3, 4, 5, 6],
    names: ["RELOJ", "TRAGAMONEDAS", "METEORITOS", "RED", "MUSICA", "SOFIA", "CONFIG"],
    focus: { active: false, finished: false, seconds: 0, minutes: 0, remainingMs: 0 },
    sta: { configured: true, connected: true, ssid: "CASA", ip: "192.168.100.75", rssi: -60 },
    time: { synced: true, tz: ${TZ}, local: "—" },
    display: { brightness: 100, orientation: 1, rotationMode: "horizontal", rotationLocked: false, awake: true },
  };
  const calendar = {
    ok: true, kind: ${scenario === "unauthorized" ? '"needs_auth"' : '"ready"'},
    // El cliente OAuth está configurado; lo que falta en el escenario sin
    // autorizar es el permiso de Google, como kind: needs_auth en el equipo.
    configured: true,
    authorized: ${scenario !== "unauthorized"},
    pending: false, syncing: false,
    selectedName: "maximomajorel@gmail.com",
    fallbackLeadMin: 10, graceMin: 5, lastError: "",
    eventCount: ${events.length},
    hasNext: ${Boolean(next)},
    nextSummary: ${JSON.stringify(next?.summary ?? "")},
    nextInMin: ${next ? Math.round((next.startEpoch - NOW) / 60) : 0},
    nextLeadMin: ${next?.leadMin ?? -1},
  };
  const events = ${JSON.stringify(FIXTURES[scenario])};
  const auth = () => { const e = new Error("Sesion requerida"); e.code = 401; throw e; };
  window.Date.now = () => ${NOW} * 1000;
  window.__TAURI__ = {
    core: {
      invoke: async (cmd) => {
        if (cmd === "connection_load") return { host: "192.168.100.75", port: 80 };
        if (cmd === "popover_state") return { expanded: true, width: 520, height: 820 };
        if (cmd === "device_status") return status;
        if (cmd === "panel_calendar") return ${scenario === "session"} ? auth() : calendar;
        if (cmd === "panel_calendar_events") return ${scenario === "session"} ? auth() : events;
        return null;
      },
    },
    event: { listen: async () => () => {} },
    window: {
      getCurrentWindow: () => ({
        hide() {}, startDragging() {},
        setAlwaysOnTop: async () => {},
        isAlwaysOnTop: async () => true,
        onFocusChanged: async () => () => {},
      }),
    },
  };
})();
`;
fs.writeFileSync(path.join(OUT, "fixtures.js"), fixtures);

const url = `${windowsPath(path.join(OUT, "index.html"))}#${scenario}`;
const chrome = process.env.CHROME ?? "chrome";
console.log(`escenario ${scenario} → ${url}`);
console.log("");
console.log("# HTML resultante (la agenda, los rótulos y el atenuado):");
console.log(
  `${chrome} --headless=new --disable-gpu --allow-file-access-from-files \\\n` +
    `  --force-device-scale-factor=1 --window-size=520,820 --virtual-time-budget=4000 \\\n` +
    `  --dump-dom "${url}" > /tmp/dom-${scenario}.html`,
);
console.log("");
console.log("# Captura:");
console.log(
  `${chrome} --headless=new --disable-gpu --allow-file-access-from-files \\\n` +
    `  --force-device-scale-factor=1 --window-size=520,820 --virtual-time-budget=4000 \\\n` +
    `  --screenshot="${path.join(ROOT, ".build-preview", `agenda-${scenario}.png`)}" "${url}"`,
);
