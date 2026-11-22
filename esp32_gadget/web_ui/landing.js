// The public entrance: the screen picker and the web games.
//
// Everything here runs in the browser on the device's own access point, so it
// talks to the same origin and holds no credentials. Reading `/api/status` is
// public; changing the screen is the one public write (`POST /api/screen`), and
// nothing else leaves this page.
//
// This page does not draw Sofía's face. The eyes live on the glass and only
// there: the browser can name a mood the firmware knows (`POST /api/face`), and
// that is the whole relationship between this surface and her face.
(function () {
  "use strict";

  // ---------------------------------------------------------------------------
  // Device state: what the glass shows and which screens exist
  // ---------------------------------------------------------------------------
  // Screen ids are part of the HTTP API and never move (screens.h). Only the
  // names and the pick order are the owner's.
  var SCREEN_ICONS = ["i-clock", "i-slot", "i-rocket", "i-wifi", "i-music", "i-smile", "i-settings"];

  var ui = {
    dockNow: document.getElementById("dockNow"),
    carousel: document.getElementById("carousel"),
    grid: document.getElementById("screenGrid"),
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

  // A failed read is reported in the dock message, which is also the only
  // control that can retry: the status bar carries just the menu button. Every
  // node here is optional, so a trimmed page can never throw its way out of the
  // wiring.
  function setConnected(ok, detail) {
    if (!ui.dockMsg) return;
    ui.dockMsg.hidden = ok;
    if (!ok && detail && ui.dockMsg.firstChild) ui.dockMsg.firstChild.nodeValue = detail + " ";
  }

  function screenNameAt(index) {
    var name = device.names[index];
    return name ? name : "Pantalla " + (index + 1);
  }

  function renderScreens() {
    if (!ui.carousel || !ui.grid) return;
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

    if (ui.dockNow) ui.dockNow.textContent = "Ahora: " + screenNameAt(current);

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
    if (ui.dockNow) ui.dockNow.textContent = "Ahora: " + screenNameAt(device.current);
  }

  // ---------------------------------------------------------------------------
  // Concentración: start the countdown and read its state back
  // ---------------------------------------------------------------------------
  // The timer lives on the glass and counts from the board's clock. This page
  // starts it, cancels it and reports what the device says; it never counts on
  // its own, because a second clock in the browser would drift and lie about the
  // number the owner is actually watching.
  var focusUi = {
    minutes: document.getElementById("focusMinutes"),
    seconds: document.getElementById("focusSeconds"),
    start: document.getElementById("focusStart"),
    stop: document.getElementById("focusStop"),
    now: document.getElementById("focusNow")
  };
  var focusBusy = false;
  // The last status the device gave us, so a failed or slow send re-renders the
  // truth instead of guessing that the session is over.
  var focusLast = null;

  // Minutes and seconds are two fields but one number: the device counts
  // seconds, and it is the only clock. Above 99:59 the big digits cannot show the
  // number, so the panel never asks for more; zero means "the usual session"
  // (cancel has its own button).
  function focusRequestedSeconds() {
    var m = parseInt(focusUi.minutes.value, 10);
    var s = parseInt(focusUi.seconds.value, 10);
    if (!isFinite(m) || m < 0) m = 0;
    if (!isFinite(s) || s < 0) s = 0;
    m = Math.min(m, 99);
    s = Math.min(s, 59);
    var total = m * 60 + s;
    if (total < 1) total = 25 * 60;
    return Math.min(total, 5999);
  }

  // mm:ss with the same rounding the glass uses: a partial second is shown, never
  // a 00:00 that is still running.
  function focusClock(ms) {
    var total = Math.ceil(ms / 1000);
    var m = Math.floor(total / 60);
    var s = total % 60;
    return (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s;
  }

  function renderFocus(status) {
    if (status) focusLast = status;
    var f = status && status.focus;
    var active = !!(f && f.active);
    var finished = !!(f && f.finished);
    if (active && f.remainingMs > 0) {
      focusUi.now.textContent = "Sofía está concentrada: quedan " +
        focusClock(f.remainingMs) + " en su pantalla.";
      focusUi.now.hidden = false;
    } else if (finished) {
      focusUi.now.textContent = "Sesión completa. Sofía te espera despierta.";
      focusUi.now.hidden = false;
    } else {
      focusUi.now.hidden = true;
    }
    if (focusUi.stop) focusUi.stop.hidden = !active;
    if (focusUi.start) focusUi.start.disabled = active || focusBusy;
  }

  function sendFocus(seconds) {
    if (focusBusy) return;
    focusBusy = true;
    if (focusUi.start) focusUi.start.disabled = true;
    api("POST", "/api/focus", { seconds: seconds })
      .then(function () { return api("GET", "/api/status"); })
      .then(renderFocus)
      .catch(function (error) {
        setConnected(false, "No pude arrancar la concentración: " + error.message);
      })
      .then(function () {
        focusBusy = false;
        // The button state comes from the device, never from "the request came
        // back": a Start that re-enables itself over a live session invites a
        // second click that would replace it.
        renderFocus(focusLast);
      });
  }

  if (focusUi.start) {
    focusUi.start.addEventListener("click", function () {
      sendFocus(focusRequestedSeconds());
    });
  }
  if (focusUi.stop) {
    focusUi.stop.addEventListener("click", function () { sendFocus(0); });
  }
  document.querySelectorAll("[data-focus-min]").forEach(function (chip) {
    chip.addEventListener("click", function () {
      focusUi.minutes.value = chip.dataset.focusMin;
      focusUi.seconds.value = 0;
      document.querySelectorAll("[data-focus-min]").forEach(function (other) {
        other.setAttribute("aria-pressed", other === chip ? "true" : "false");
      });
    });
  });

  function loadStatus() {
    return api("GET", "/api/status").then(function (status) {
      device.order = (status.order || []).slice();
      device.names = (status.names || []).slice();
      device.current = typeof status.screen === "number" ? status.screen : 0;
      device.loaded = true;
      setConnected(true);
      renderFocus(status);
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

  // The face lives on the device; this page only names a mood from the closed
  // vocabulary the firmware knows. An older build without the endpoint must not
  // break the game, so failures are swallowed.
  var BUDDY_SCREEN = 5;  // SCREEN_BUDDY in screens.h
  var screenBeforeGame = null;

  function face(mood, ms) {
    api("POST", "/api/face", { mood: mood, ms: ms || 0 }).catch(function () {});
  }

  function setDeviceScreen(id) {
    if (!device.loaded || device.current === id) return Promise.resolve();
    return api("POST", "/api/screen", { index: id }).then(function () {
      device.current = id;
      markCurrent();
    }).catch(function () {});
  }

  if (ui.carousel) {
    ui.carousel.addEventListener("click", function (event) {
      var button = event.target.closest("[data-screen]");
      if (button) chooseScreen(Number(button.dataset.screen));
    });
  }

  if (ui.grid) {
    ui.grid.addEventListener("click", function (event) {
      var button = event.target.closest("[data-screen]");
      if (button) chooseScreen(Number(button.dataset.screen));
    });
  }

  if (ui.retry) ui.retry.addEventListener("click", function () { loadStatus(); });

  // The glass can change with the button, so the dock follows the device. It is
  // a read; a poll never writes and never fights a selection in flight.
  setInterval(function () {
    if (document.hidden || busy || document.body.classList.contains("game-open")) return;
    if (!device.loaded) { loadStatus(); return; }
    api("GET", "/api/status").then(function (status) {
      renderFocus(status);
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
    if (menuBtn) menuBtn.setAttribute("aria-expanded", "true");
    setTimeout(function () {
      var closer = document.querySelector("[data-close-menu]");
      if (closer) closer.focus();
    }, 60);
  }

  function closeMenu() {
    if (!document.body.classList.contains("menu-open")) return;
    document.body.classList.remove("menu-open");
    if (menuBtn) menuBtn.setAttribute("aria-expanded", "false");
    if (menuBtn && !document.body.classList.contains("game-open")) menuBtn.focus();
  }

  if (menuBtn) {
    menuBtn.addEventListener("click", function () {
      if (document.body.classList.contains("menu-open")) closeMenu(); else openMenu();
    });
  }
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
  var recordEl = document.getElementById("record");

  var game = { cells: [], over: false, locked: false, diff: "normal" };
  var score = loadScore();
  // A finished round holds its result long enough to be read, then the board
  // clears itself and the next round starts at the same difficulty: there is no
  // restart button, so a dead board never needs one.
  var RESULT_HOLD_MS = 2200;
  var restartTimer = null;

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
    if (!boardEl) return;
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

  function refreshRecord() {
    if (recordEl) {
      recordEl.textContent = "Récord " + score.you + "–" + score.buddy + " · empates " + score.draws;
    }
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
    clearTimeout(restartTimer);
    restartTimer = null;
    game = { cells: [null, null, null, null, null, null, null, null, null], over: false, locked: false, diff: game.diff };
    renderBoard();
    refreshRecord();
  }

  function finish(result) {
    game.over = true;
    renderBoard();
    if (result.mark === "draw") {
      score.draws++;
      face("sleepy", 5000);
    } else if (result.mark === "x") {
      score.you++;
      face("happy", 5000);
    } else {
      score.buddy++;
      face("anger", 6000);
    }
    saveScore();
    refreshRecord();
    var cells = boardEl ? boardEl.children : [];
    result.line.forEach(function (index) {
      cells[index].classList.add("win");
      if (result.mark === "o") cells[index].classList.add("o-win");
    });
    // The result stays on screen while the device plays it, then the next round
    // starts on its own.
    restartTimer = setTimeout(function () {
      restartTimer = null;
      reset();
    }, RESULT_HOLD_MS);
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
    face("focus", 1500);
    if (place(index, "x")) {
      game.locked = false;
      return;
    }
    // Sofia answers after a beat, and the device is the one that shows her
    // thinking.
    face("thinking", 1500);
    setTimeout(function () {
      var pick = buddyPick();
      face("alert", 1200);
      if (!place(pick, "o")) {
        game.locked = false;
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
      reset();
    });
  });

  var gameView = document.getElementById("gameView");
  var gameOpener = null;

  function openGame(opener) {
    gameOpener = opener || document.activeElement;
    closeMenu();
    document.body.classList.add("game-open");
    // The eyes that react are the device's, so the game asks for the face and
    // gives the owner's screen back when it closes.
    screenBeforeGame = device.loaded ? device.current : null;
    setDeviceScreen(BUDDY_SCREEN);
    face("focus", 4000);
    reset();
    var closer = document.querySelector("[data-close-game]");
    if (closer) closer.focus();
  }

  function closeGame() {
    if (!document.body.classList.contains("game-open")) return;
    document.body.classList.remove("game-open");
    face("", 0);
    if (screenBeforeGame !== null) {
      setDeviceScreen(screenBeforeGame);
      screenBeforeGame = null;
    }
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
