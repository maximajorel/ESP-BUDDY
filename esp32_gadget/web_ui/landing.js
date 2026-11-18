// The public entrance: the device face, the screen picker and the web games.
//
// Everything here runs in the browser on the device's own access point, so it
// talks to the same origin and holds no credentials. Reading `/api/status` is
// public; changing the screen is the one public write (`POST /api/screen`), and
// nothing else leaves this page.
//
// The face reproduces the gesture vocabulary the firmware draws (gaze drift,
// breathing, lids, hot anger). It does NOT read the device pose: the firmware
// does not publish it yet, and claiming otherwise would be a lie in the UI.
(function () {
  "use strict";

  var reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  // ---------------------------------------------------------------------------
  // The face
  // ---------------------------------------------------------------------------
  function faceMarkup() {
    function eye(side) {
      return '<g class="eye-group eye-' + side + '">' +
        '<rect class="eye-glow" x="-56" y="-62" width="112" height="124" rx="30"/>' +
        '<rect class="eye-core" x="-40" y="-48" width="80" height="96" rx="30"/>' +
        '<g class="lid-top"><rect class="lid" x="-40" y="-48" width="80" height="96" rx="30"/></g>' +
        '<g class="lid-bottom"><rect class="lid" x="-40" y="-48" width="80" height="96" rx="30"/></g>' +
        '<path class="mark xmark" d="M-24 -24 L24 24 M24 -24 L-24 24"/>' +
      '</g>';
    }
    return '<svg class="face" viewBox="0 0 320 172" role="img" aria-label="Cara de Sofía">' +
      '<g class="face-root">' +
        '<g class="breathe">' + eye('l') + eye('r') + '</g>' +
        '<text class="zzz" x="86" y="-46" font-size="14" aria-hidden="true">z</text>' +
        '<text class="zzz" x="104" y="-64" font-size="18" aria-hidden="true">z</text>' +
        '<text class="zzz" x="124" y="-84" font-size="23" aria-hidden="true">z</text>' +
      '</g></svg>';
  }

  // Only the entrance has a face. The game is a minimal board: the eyes that
  // react to it are the device's, and this page does not draw them.
  var heroHost = document.querySelector('[data-face="hero"]');
  heroHost.innerHTML = faceMarkup();
  var hero = heroHost.firstElementChild;

  function mood(name) { hero.dataset.mood = name; }

  // The whole eye group drifts, exactly like the firmware's gaze channel.
  function gaze(x, y) {
    hero.style.setProperty("--gaze-x", x + "px");
    hero.style.setProperty("--gaze-y", y + "px");
  }

  function blink() {
    var m = hero.dataset.mood;
    if (m === "sleepy" || m === "blink") return;
    hero.dataset.mood = "blink";
    setTimeout(function () {
      if (hero.dataset.mood === "blink") hero.dataset.mood = m;
    }, 120);
  }

  function drift() {
    var m = hero.dataset.mood;
    if (m === "calm" || m === "focus" || m === "bored" || m === "thinking") {
      gaze(Math.round(Math.random() * 6 - 3), Math.round(Math.random() * 4 - 2));
    } else if (m === "sleepy" || m === "dead") {
      gaze(0, 0);
    }
    setTimeout(drift, 2400 + Math.random() * 2200);
  }

  if (!reduceMotion) {
    setInterval(blink, 5200);
    drift();
  }

  // ---------------------------------------------------------------------------
  // Device state: what the glass shows and which screens exist
  // ---------------------------------------------------------------------------
  // Screen ids are part of the HTTP API and never move (screens.h). Only the
  // names and the pick order are the owner's.
  var SCREEN_ICONS = ["i-clock", "i-slot", "i-rocket", "i-wifi", "i-music", "i-smile", "i-settings"];

  var ui = {
    nowName: document.getElementById("nowName"),
    dockNow: document.getElementById("dockNow"),
    carousel: document.getElementById("carousel"),
    grid: document.getElementById("screenGrid"),
    screenCount: document.getElementById("screenCount"),
    pill: document.getElementById("connPill"),
    pillText: document.getElementById("connText"),
    dockMsg: document.getElementById("dockMsg"),
    retry: document.getElementById("retryBtn")
  };

  var device = { order: [], names: [], current: 0, loaded: false };

  function api(method, path, body) {
    var options = { method: method, headers: {} };
    if (body !== undefined) {
      options.headers["Content-Type"] = "application/json";
      options.body = JSON.stringify(body);
    }
    return fetch(path, options).then(function (response) {
      return response.text().then(function (text) {
        var parsed;
        try {
          parsed = text ? JSON.parse(text) : {};
        } catch (err) {
          throw new Error("El equipo respondió algo inesperado");
        }
        if (!response.ok) {
          throw new Error(parsed.error || ("Error " + response.status));
        }
        return parsed;
      });
    });
  }

  function setConnected(ok, detail) {
    ui.pill.classList.toggle("offline", !ok);
    ui.pillText.textContent = ok ? "En línea" : "Sin conexión";
    ui.dockMsg.hidden = ok;
    if (!ok && detail) ui.dockMsg.firstChild.nodeValue = detail + " ";
  }

  function screenNameAt(index) {
    var name = device.names[index];
    return name ? name : "Pantalla " + (index + 1);
  }

  function renderScreens() {
    var current = device.current;
    ui.carousel.innerHTML = device.order.map(function (id, position) {
      var on = id === current;
      return '<button class="slide" type="button" role="option" data-screen="' + id + '" ' +
        'aria-selected="' + on + '" aria-pressed="' + on + '">' +
        '<svg aria-hidden="true"><use href="#' + (SCREEN_ICONS[id] || "i-display") + '"/></svg>' +
        '<span class="num">' + String(position + 1).padStart(2, "0") + '</span>' +
        '<b>' + screenNameAt(id).toUpperCase() + '</b></button>';
    }).join("");

    ui.grid.innerHTML = device.order.map(function (id) {
      var on = id === current;
      return '<button class="tile" type="button" data-screen="' + id + '" aria-pressed="' + on + '">' +
        '<svg aria-hidden="true"><use href="#' + (SCREEN_ICONS[id] || "i-display") + '"/></svg>' +
        '<b>' + screenNameAt(id).toUpperCase() + '</b></button>';
    }).join("");

    ui.screenCount.textContent = device.order.length + " pantallas";
    ui.nowName.textContent = screenNameAt(current);
    ui.dockNow.textContent = "Ahora: " + screenNameAt(current);

    // Bring the current screen into view without moving the page: the dock is
    // its own scroll container.
    var active = ui.carousel.querySelector('[aria-selected="true"]');
    if (active) {
      var offset = active.offsetLeft - (ui.carousel.clientWidth - active.offsetWidth) / 2;
      ui.carousel.scrollLeft = Math.max(0, offset);
    }
  }

  function markCurrent() {
    document.querySelectorAll("[data-screen]").forEach(function (button) {
      var on = Number(button.dataset.screen) === device.current;
      button.setAttribute("aria-pressed", on);
      if (button.getAttribute("role") === "option") button.setAttribute("aria-selected", on);
    });
    ui.nowName.textContent = screenNameAt(device.current);
    ui.dockNow.textContent = "Ahora: " + screenNameAt(device.current);
  }

  function loadStatus() {
    return api("GET", "/api/status").then(function (status) {
      device.order = (status.order || []).slice();
      device.names = (status.names || []).slice();
      device.current = typeof status.screen === "number" ? status.screen : 0;
      device.loaded = true;
      setConnected(true);
      renderScreens();
    }).catch(function () {
      device.loaded = false;
      setConnected(false, "No pude leer el equipo.");
    });
  }

  var busy = false;
  function chooseScreen(id) {
    if (busy || !device.loaded) return;
    if (id === device.current) {
      closeMenu();
      return;
    }
    busy = true;
    document.querySelectorAll('[data-screen="' + id + '"]').forEach(function (button) {
      button.setAttribute("aria-busy", "true");
    });
    api("POST", "/api/screen", { index: id }).then(function () {
      device.current = id;
      markCurrent();
      mood("focus");
      setTimeout(function () { mood("calm"); }, 900);
      setConnected(true);
    }).catch(function (error) {
      setConnected(false, "No pude cambiar la pantalla: " + error.message);
    }).then(function () {
      busy = false;
      document.querySelectorAll("[data-screen]").forEach(function (button) {
        button.removeAttribute("aria-busy");
      });
      closeMenu();
    });
  }

  ui.carousel.addEventListener("click", function (event) {
    var button = event.target.closest("[data-screen]");
    if (button) chooseScreen(Number(button.dataset.screen));
  });

  ui.grid.addEventListener("click", function (event) {
    var button = event.target.closest("[data-screen]");
    if (button) chooseScreen(Number(button.dataset.screen));
  });

  ui.retry.addEventListener("click", function () { loadStatus(); });

  // The glass can change with the button, so the dock follows the device. It is
  // a read; a poll never writes and never fights a selection in flight.
  setInterval(function () {
    if (document.hidden || busy || document.body.classList.contains("game-open")) return;
    if (!device.loaded) { loadStatus(); return; }
    api("GET", "/api/status").then(function (status) {
      if (typeof status.screen !== "number" || status.screen === device.current) return;
      device.current = status.screen;
      markCurrent();
    }).catch(function () { setConnected(false, "No pude leer el equipo."); });
  }, 10000);

  // ---------------------------------------------------------------------------
  // The menu
  // ---------------------------------------------------------------------------
  var menuBtn = document.getElementById("menuBtn");

  function openMenu() {
    document.body.classList.add("menu-open");
    menuBtn.setAttribute("aria-expanded", "true");
    setTimeout(function () { document.querySelector("[data-close-menu]").focus(); }, 60);
  }

  function closeMenu() {
    if (!document.body.classList.contains("menu-open")) return;
    document.body.classList.remove("menu-open");
    menuBtn.setAttribute("aria-expanded", "false");
    if (!document.body.classList.contains("game-open")) menuBtn.focus();
  }

  menuBtn.addEventListener("click", function () {
    if (document.body.classList.contains("menu-open")) closeMenu(); else openMenu();
  });
  document.querySelectorAll("[data-close-menu]").forEach(function (button) {
    button.addEventListener("click", closeMenu);
  });

  // ---------------------------------------------------------------------------
  // Ta-te-ti
  // ---------------------------------------------------------------------------
  var LINES = [
    [0, 1, 2], [3, 4, 5], [6, 7, 8],
    [0, 3, 6], [1, 4, 7], [2, 5, 8],
    [0, 4, 8], [2, 4, 6]
  ];
  // How often the buddy plays a random move instead of its best one. That single
  // number is the whole difference between the three levels.
  var SLIP = { easy: 0.6, normal: 0.18, hard: 0 };

  var boardEl = document.getElementById("board");
  var sayEl = document.getElementById("say");
  var recordEl = document.getElementById("record");

  var game = { cells: [], over: false, locked: false, diff: "normal" };
  var score = loadScore();

  function loadScore() {
    try {
      var stored = JSON.parse(window.localStorage.getItem("gadget.tateti") || "{}");
      if (stored && typeof stored.you === "number" && typeof stored.buddy === "number") {
        return { you: stored.you, buddy: stored.buddy, draws: stored.draws || 0 };
      }
    } catch (err) {
      // A blocked or corrupt store is not worth failing a game over.
    }
    return { you: 0, buddy: 0, draws: 0 };
  }

  function saveScore() {
    try {
      window.localStorage.setItem("gadget.tateti", JSON.stringify(score));
    } catch (err) {
      // Private mode: the record lives only for this visit.
    }
  }

  function renderBoard() {
    boardEl.innerHTML = "";
    game.cells.forEach(function (mark, index) {
      var cell = document.createElement("button");
      cell.type = "button";
      cell.className = "cell";
      cell.setAttribute("role", "gridcell");
      cell.setAttribute("aria-label", "Casilla " + (index + 1) +
        (mark ? (mark === "x" ? ", tuya" : ", de Sofía") : ", vacía"));
      if (mark) {
        cell.innerHTML = '<span class="mark ' + mark + '">' + (mark === "x" ? "✕" : "◯") + "</span>";
      } else {
        cell.disabled = game.over || game.locked;
      }
      cell.addEventListener("click", function () { play(index); });
      boardEl.appendChild(cell);
    });
  }

  function winnerOf(cells) {
    for (var i = 0; i < LINES.length; i++) {
      var line = LINES[i];
      if (cells[line[0]] && cells[line[0]] === cells[line[1]] && cells[line[1]] === cells[line[2]]) {
        return { mark: cells[line[0]], line: line };
      }
    }
    return cells.indexOf(null) === -1 ? { mark: "draw", line: [] } : null;
  }

  function openCells(cells) {
    var open = [];
    cells.forEach(function (mark, index) { if (!mark) open.push(index); });
    return open;
  }

  function say(text, who, dim) {
    sayEl.className = "say" + (who === "buddy" ? " buddy" : "") + (dim ? " dim" : "");
    sayEl.innerHTML = '<span class="dot" aria-hidden="true"></span><span>' + text + "</span>";
  }

  function refreshRecord() {
    recordEl.textContent = "Récord " + score.you + "–" + score.buddy + " · empates " + score.draws;
  }

  function buddyPick() {
    var cells = game.cells.slice();
    var open = openCells(cells);
    if (Math.random() < SLIP[game.diff]) {
      return open[Math.floor(Math.random() * open.length)];
    }
    if (game.diff === "hard") {
      // Own best move under perfect play. Anything less would make "no perdona"
      // a promise the code does not keep.
      return bestMove(cells, "o");
    }
    for (var i = 0; i < open.length; i++) {
      var win = cells.slice();
      win[open[i]] = "o";
      var won = winnerOf(win);
      if (won && won.mark === "o") return open[i];
    }
    for (var j = 0; j < open.length; j++) {
      var block = cells.slice();
      block[open[j]] = "x";
      var blocked = winnerOf(block);
      if (blocked && blocked.mark === "x") return open[j];
    }
    if (cells[4] === null) return 4;
    var corners = [0, 2, 6, 8].filter(function (k) { return cells[k] === null; });
    if (corners.length) return corners[Math.floor(Math.random() * corners.length)];
    return open[Math.floor(Math.random() * open.length)];
  }

  // Depth is at most nine plies on a 3x3 board, so a plain minimax is cheap and
  // exact. Score from Sofia's point of view: a win sooner is better than a win
  // later, and a loss later is better than a loss now.
  function minimax(cells, turn, depth) {
    var result = winnerOf(cells);
    if (result) {
      if (result.mark === "o") return 10 - depth;
      if (result.mark === "x") return depth - 10;
      return 0;
    }
    var open = openCells(cells);
    var best = turn === "o" ? -Infinity : Infinity;
    for (var i = 0; i < open.length; i++) {
      cells[open[i]] = turn;
      var value = minimax(cells, turn === "o" ? "x" : "o", depth + 1);
      cells[open[i]] = null;
      best = turn === "o" ? Math.max(best, value) : Math.min(best, value);
    }
    return best;
  }

  function bestMove(cells, turn) {
    var open = openCells(cells);
    var best = -Infinity;
    var choice = open[0];
    for (var i = 0; i < open.length; i++) {
      cells[open[i]] = turn;
      var value = minimax(cells, "x", 0);
      cells[open[i]] = null;
      if (value > best) {
        best = value;
        choice = open[i];
      }
    }
    return choice;
  }

  function reset() {
    game = { cells: [null, null, null, null, null, null, null, null, null], over: false, locked: false, diff: game.diff };
    renderBoard();
    say("Tu turno. Elegí una casilla.", "you");
    refreshRecord();
  }

  function finish(result) {
    game.over = true;
    renderBoard();
    if (result.mark === "draw") {
      score.draws++;
      say("Empate. Otra, dale.", "buddy", true);
    } else if (result.mark === "x") {
      score.you++;
      say("Me ganaste… otra vez no, ¿eh?", "buddy", true);
    } else {
      score.buddy++;
      say("¡Gané! Te leí la jugada.", "buddy", true);
    }
    saveScore();
    refreshRecord();
    var cells = boardEl.children;
    result.line.forEach(function (index) {
      cells[index].classList.add("win");
      if (result.mark === "o") cells[index].classList.add("o-win");
    });
  }

  function place(index, mark) {
    game.cells[index] = mark;
    renderBoard();
    var result = winnerOf(game.cells);
    if (result) {
      finish(result);
      return true;
    }
    return false;
  }

  function play(index) {
    if (game.over || game.locked || game.cells[index]) return;
    game.locked = true;
    say("Mm…", "you");
    if (place(index, "x")) {
      game.locked = false;
      return;
    }
    // Sofia answers after a beat, and the device is the one that shows her
    // thinking.
    say("Dejame pensar…", "buddy", true);
    setTimeout(function () {
      var pick = buddyPick();
      if (!place(pick, "o")) {
        game.locked = false;
        var threats = LINES.filter(function (line) {
          var mine = line.filter(function (k) { return game.cells[k] === "x"; }).length;
          var hers = line.filter(function (k) { return game.cells[k] === "o"; }).length;
          return mine === 2 && hers === 0;
        }).length;
        say(threats ? "Te vi venir. No tan rápido." : "Tu turno. Sorprendeme.", "you");
        // The cells were rebuilt while the board was still locked; without this
        // repaint every empty cell stays disabled and the game dead-ends after
        // one exchange.
        renderBoard();
      }
    }, 800);
  }

  document.querySelectorAll("[data-diff]").forEach(function (button) {
    button.addEventListener("click", function () {
      document.querySelectorAll("[data-diff]").forEach(function (other) {
        other.setAttribute("aria-pressed", other === button);
      });
      game.diff = button.dataset.diff;
      say("Dificultad " + button.firstChild.textContent + ". Nueva partida.", "buddy");
      setTimeout(reset, 700);
    });
  });

  document.getElementById("againBtn").addEventListener("click", reset);

  var gameView = document.getElementById("gameView");
  var gameOpener = null;

  function openGame(opener) {
    gameOpener = opener || document.activeElement;
    closeMenu();
    document.body.classList.add("game-open");
    reset();
    document.querySelector("[data-close-game]").focus();
  }

  function closeGame() {
    if (!document.body.classList.contains("game-open")) return;
    document.body.classList.remove("game-open");
    if (gameOpener && gameOpener.focus) gameOpener.focus();
  }

  document.querySelectorAll("[data-open-game]").forEach(function (button) {
    button.addEventListener("click", function () { openGame(button); });
  });
  document.querySelectorAll("[data-close-game]").forEach(function (button) {
    button.addEventListener("click", closeGame);
  });

  document.addEventListener("keydown", function (event) {
    if (event.key !== "Escape") return;
    if (document.body.classList.contains("game-open")) closeGame();
    else if (document.body.classList.contains("menu-open")) closeMenu();
  });

  // ---------------------------------------------------------------------------
  // Boot
  // ---------------------------------------------------------------------------
  renderBoard();
  refreshRecord();
  loadStatus();
})();
