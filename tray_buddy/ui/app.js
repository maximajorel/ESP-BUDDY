// Buddy · popover de bandeja.
//
// Sin lógica de red acá a propósito: el equipo no manda CORS, así que todo HTTP
// lo hace Rust y esta capa sólo llama `invoke`. Por ahora (T1) el popover es el
// esqueleto visual con la ventana y la bandeja: los datos llegan en T2.

const tauri = window.__TAURI__ ?? {
  // Abrir `ui/index.html` directo en un navegador es la forma de iterar el
  // diseño sin compilar Rust; ahí no existe la API de Tauri y esta cáscara
  // mantiene el popover usable (los botones no hacen nada, pero no rompen).
  window: {
    getCurrentWindow: () => ({
      hide() {},
      setAlwaysOnTop: () => Promise.resolve(),
    }),
  },
};
const win = tauri.window.getCurrentWindow();

// Las siete pantallas del firmware, con el nombre que traen de fábrica. T4 las
// reemplaza por lo que responda `GET /api/screens/names` y `GET /api/status`,
// así que esto es sólo el punto de partida del esqueleto.
const SCREENS = [
  { name: "Reloj", icon: '<circle cx="12" cy="12" r="8"/><path d="M12 8v4l3 2"/>' },
  { name: "Tragamonedas", icon: '<rect x="4" y="5" width="16" height="14" rx="2"/><path d="M9 10v4M15 10v4"/>' },
  { name: "Meteoritos", icon: '<path d="M14 4l5 13-5-3-5 3 5-13z"/>' },
  { name: "Red", icon: '<circle cx="12" cy="12" r="8"/><path d="M4 12h16M12 4v16"/>' },
  { name: "Música", icon: '<path d="M9 18V6l10-2v12"/><circle cx="7" cy="18" r="2"/><circle cx="17" cy="16" r="2"/>' },
  { name: "Sofía", icon: '<path d="M5 12a7 7 0 0114 0"/><circle cx="9" cy="14" r="1"/><circle cx="15" cy="14" r="1"/>' },
  { name: "Config", icon: '<circle cx="12" cy="12" r="3"/><path d="M12 3v3M12 18v3M3 12h3M18 12h3"/>' },
];

function renderScreens() {
  const host = document.getElementById("screens");
  host.textContent = "";
  SCREENS.forEach((screen) => {
    const button = document.createElement("button");
    button.className = "tile";
    button.type = "button";
    button.setAttribute("aria-pressed", "false");
    button.disabled = true; // sin conexión todavía
    button.innerHTML =
      '<svg viewBox="0 0 24 24" aria-hidden="true">' +
      screen.icon +
      "</svg>" +
      '<span class="label"></span>' +
      '<span class="flag">En pantalla</span>';
    button.querySelector(".label").textContent = screen.name;
    host.appendChild(button);
  });
}

function setOnline(online, label, addr) {
  const status = document.getElementById("winStatus");
  status.dataset.online = online ? "true" : "false";
  document.getElementById("winStatusText").textContent = label;
  const pill = document.getElementById("connPill");
  pill.classList.toggle("online", online);
  pill.classList.toggle("offline", !online);
  document.getElementById("connText").textContent = label;
  document.getElementById("connAddr").textContent = addr || "—";
}

function hide() {
  win.hide();
}

function wireCaption() {
  document.getElementById("btnHide").addEventListener("click", hide);
  document.getElementById("btnClose").addEventListener("click", hide);

  const pin = document.getElementById("btnPin");
  pin.addEventListener("click", async () => {
    const pinned = pin.getAttribute("aria-pressed") !== "true";
    pin.setAttribute("aria-pressed", pinned ? "true" : "false");
    await win.setAlwaysOnTop(pinned);
  });

  // Escape esconde el popover: es la salida que espera cualquiera que abrió una
  // ventana sin decoración y no encuentra una cruz.
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") hide();
  });
}

renderScreens();
wireCaption();
setOnline(false, "Sin conectar");
