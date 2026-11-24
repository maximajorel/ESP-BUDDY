/* ESP32 Gadget admin panel.
 *
 * Talks to the firmware's JSON API. The only place fetch() is called is api(),
 * so every failure goes through one error path and one toast.
 */
(function () {
  'use strict';

  var POLL_MS = 2000;
  // Shipped names, used only while the device has not answered yet. The device
  // is the source of truth after that.
  var DEFAULT_NAMES = ['RELOJ', 'TRAGAMONEDAS', 'METEORITOS', 'RED', 'MUSICA', 'SOFIA', 'CONFIG'];
  // Must match SCREEN_NAME_MAX in the firmware: longer names come back cut.
  var SCREEN_NAME_MAX = 12;
  // The moods POST /api/face accepts (buddyGameMoodFromName in buddy_game.h). The
  // panel never invents a state: the buttons carry one of these wire names.
  // 'dance' is the one entry that is not a game mood: POST /api/face pins the
  // dance on a synthetic beat, so the music reaction can be proven without a
  // track playing on Spotify.
  var SOFIA_MOODS = ['focus', 'thinking', 'alert', 'happy', 'anger', 'sleepy', 'sad', 'dance'];
  // How long the face holds the simulated mood before falling back on its own.
  var SOFIA_HOLD_MS = 8000;
  // Tiles and toasts follow the names the device reports, so a rename shows up
  // everywhere in the panel and not only in the order list.
  var screenNames = DEFAULT_NAMES.slice();
  // Order and names being edited in the Pantalla card; null means "take it from
  // the device".
  var orderDraft = null;
  // Keyed by screen id, not by position: moving a row must move its name too.
  var namesDraft = null;
  // True while the owner has unsaved changes. Without it, the once-a-second
  // status refresh reset the draft and the arrows looked like they did nothing.
  var draftDirty = false;

  function $(id) { return document.getElementById(id); }

  // Attach a listener only when the element is in the page. The markup changes
  // more often than this file, and one missing id used to throw inside wire()
  // and take every listener after it down with the error.
  function on(id, type, handler, options) {
    var el = $(id);
    if (el) el.addEventListener(type, handler, options);
    return el;
  }

  // Any control the owner changes stays theirs until the card is applied.
  ['input', 'change'].forEach(function (type) {
    document.addEventListener(type, function (event) {
      var el = event.target;
      if (el && el.id) markDirty(el.id);
    }, true);
  });

  // --- transport ------------------------------------------------------------

  function api(method, path, body) {
    var options = { method: method, headers: {} };
    if (body !== undefined) {
      options.headers['Content-Type'] = 'application/json';
      options.body = JSON.stringify(body);
    }
    return fetch(path, options).then(function (response) {
      return response.text().then(function (text) {
        var parsed;
        try {
          parsed = text ? JSON.parse(text) : {};
        } catch (err) {
          throw new Error('El equipo respondió algo inesperado');
        }
        if (response.status === 401) {
          // The session expired or was closed from another device. Send the
          // owner to the door and remember which page they wanted, so logging in
          // lands them back here instead of at the top of the panel.
          var back = window.location.pathname + window.location.hash;
          window.location.replace('/login?next=' + encodeURIComponent(back));
          throw new Error('La sesión venció');
        }
        if (!response.ok) {
          throw new Error(parsed && parsed.error ? parsed.error : 'Error ' + response.status);
        }
        if (parsed && parsed.ok === false) {
          throw new Error(parsed.error || 'El equipo rechazó el cambio');
        }
        return parsed;
      });
    });
  }

  // --- feedback -------------------------------------------------------------

  var toastTimer = 0;

  function toast(message, kind) {
    var el = $('toast');
    el.textContent = message;
    el.className = 'toast' + (kind ? ' ' + kind : '');
    el.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { el.hidden = true; }, 3200);
  }

  function busy(button, label, work) {
    var original = button.textContent;
    // Buttons with markup (the app tiles) keep their icon and badge: writing
    // textContent would flatten them and the tile could not be restored.
    var rich = button.children.length > 0;
    button.disabled = true;
    button.setAttribute('aria-busy', 'true');
    if (label && !rich) button.textContent = label;
    function done() {
      button.disabled = false;
      button.removeAttribute('aria-busy');
      if (label && !rich) button.textContent = original;
    }
    return work().then(
      function (value) { done(); return value; },
      function (error) { done(); throw error; }
    );
  }

  // Runs a mutating request with a busy button and a toast on the outcome.
  // The pick order and the name of every screen, edited together: the row shows
  // the screen where the menu reaches it, with the name it will show. The rows
  // are reordered by dragging the grip; the pointer moves the real <li>.
  // A drag in progress must not be rebuilt by the status poll, so the flag below
  // makes renderOrder() a no-op until dragend writes the new order into the
  // draft.
  var orderDragging = false;

  function renderOrder(status) {
    if (orderDragging) return;
    if (status && Array.isArray(status.order) && Array.isArray(status.names) &&
        !draftDirty) {
      orderDraft = status.order.slice();
      namesDraft = status.names.slice();
    }
    var list = $('orderList');
    if (!list || !orderDraft || !namesDraft) return;
    // Rebuild only when the visible order or saved names changed, so the
    // once-a-second status poll does not churn the DOM (or the caret) under the
    // owner's finger. The dirty flag is not part of the signature on purpose:
    // it becomes true on the first keystroke and would rebuild the list under
    // the caret while the typed value is not in namesDraft yet.
    var sig = orderDraft.join(',') + '|' + namesDraft.join('\u0001');
    if (list.getAttribute('data-sig') === sig) return;
    list.setAttribute('data-sig', sig);
    list.innerHTML = '';
    orderDraft.forEach(function (id, pos) {
      var li = document.createElement('li');
      // The id travels with the row: after a drag the DOM order is read back and
      // becomes the new orderDraft, so a row cannot lose its screen.
      li.setAttribute('data-id', String(id));
      var grip = document.createElement('span');
      grip.className = 'grip';
      grip.draggable = true;
      grip.textContent = '\u283f';
      grip.setAttribute('aria-label', 'Arrastrar para reordenar');
      grip.title = 'Arrastrar para reordenar';
      var num = document.createElement('span');
      num.className = 'num';
      num.textContent = String(pos + 1);
      var name = document.createElement('input');
      name.type = 'text';
      name.value = namesDraft[id] || '';
      name.maxLength = SCREEN_NAME_MAX;
      name.autocomplete = 'off';
      name.spellcheck = false;
      name.setAttribute('aria-label', 'Nombre de la pantalla ' + (pos + 1));
      // Typing marks the draft dirty but does not touch the stored value until
      // blur, so the poll never rebuilds the list and steals the focus.
      name.addEventListener('input', function () { draftDirty = true; });
      name.addEventListener('change', function () {
        namesDraft[id] = name.value;
        draftDirty = true;
      });
      li.appendChild(grip);
      li.appendChild(num);
      li.appendChild(name);
      list.appendChild(li);
    });
  }

  // Drag to reorder: the grip is the drag source, the row is what moves, and the
  // new position is read from the DOM on dragend. The save path is untouched:
  // orderDraft is all /api/screens/order ever receives.
  function wireOrderDrag() {
    var list = $('orderList');
    if (!list) return;
    var dragging = null;

    list.addEventListener('dragstart', function (event) {
      var grip = event.target.closest ? event.target.closest('.grip') : null;
      if (!grip) { event.preventDefault(); return; }
      dragging = grip.closest('li');
      if (!dragging) return;
      orderDragging = true;
      dragging.classList.add('dragging');
      if (event.dataTransfer) {
        event.dataTransfer.effectAllowed = 'move';
        event.dataTransfer.setData('text/plain', '');
      }
    });

    list.addEventListener('dragover', function (event) {
      if (!dragging) return;
      event.preventDefault();
      var row = event.target.closest ? event.target.closest('li') : null;
      if (!row || row === dragging) return;
      var box = row.getBoundingClientRect();
      var after = event.clientY > box.top + box.height / 2;
      list.insertBefore(dragging, after ? row.nextSibling : row);
      Array.prototype.forEach.call(list.children, function (li) {
        li.classList.toggle('over', li === row);
      });
    });

    list.addEventListener('drop', function (event) { event.preventDefault(); });

    list.addEventListener('dragend', function () {
      if (!dragging) return;
      Array.prototype.forEach.call(list.children, function (li) {
        li.classList.remove('dragging', 'over');
      });
      // The list's order is the order the owner just dropped.
      var next = [];
      Array.prototype.forEach.call(list.children, function (li) {
        var id = Number(li.getAttribute('data-id'));
        if (!isNaN(id)) next.push(id);
      });
      dragging = null;
      orderDragging = false;
      if (orderDraft && next.length === orderDraft.length) orderDraft = next;
      draftDirty = true;
      renderOrder(null);
    });
  }

  function submit(button, path, body, okMessage, busyLabel) {
    return busy(button, busyLabel || 'Guardando…', function () {
      return api('POST', path, body);
    }).then(
      function () {
        toast(okMessage, 'ok');
        // Applied: the card follows the device again from the next poll.
        clearDirtyIn(button && button.closest ? button.closest('section') : null);
        return true;
      },
      function (error) { toast(error.message, 'err'); return false; }
    );
  }

  // --- formatting -----------------------------------------------------------

  function humanUptime(ms) {
    var total = Math.floor((ms || 0) / 1000);
    var days = Math.floor(total / 86400);
    var hours = Math.floor((total % 86400) / 3600);
    var minutes = Math.floor((total % 3600) / 60);
    var seconds = total % 60;
    if (days > 0) return days + ' d ' + hours + ' h ' + minutes + ' min';
    if (hours > 0) return hours + ' h ' + minutes + ' min';
    if (minutes > 0) return minutes + ' min ' + seconds + ' s';
    return seconds + ' s';
  }

  function humanBytes(bytes) {
    var value = bytes || 0;
    if (value >= 1048576) return (value / 1048576).toFixed(1) + ' MB';
    if (value >= 1024) return Math.round(value / 1024) + ' KB';
    return value + ' B';
  }

  function describeSta(sta) {
    if (!sta) return '—';
    if (sta.connected) {
      return 'Conectado a ' + sta.ssid + ' · ' + sta.rssi + ' dBm';
    }
    if (!sta.configured) return 'Sin configurar';
    if (sta.state === 'failed') {
      // The disconnect reason is the real cause; WiFi.status() alone cannot
      // tell a rejected key exchange from a router refusing the device.
      switch (sta.disconnectReason) {
        case 2: case 15: case 202: case 204:
          return 'No se pudo conectar a ' + sta.ssid + ' · contraseña incorrecta';
        case 8: case 203:
          return 'El router rechazó al equipo · revisá el filtro MAC';
        case 201: case 210: case 211:
          return 'No se encontró la red ' + sta.ssid + ' · revisá el nombre';
        case 200:
          return 'Se perdió la señal de ' + sta.ssid + ' · acercate al router';
        default:
          return 'No se pudo conectar a ' + sta.ssid +
                 (sta.disconnectReason ? ' · motivo ' + sta.disconnectReason : '');
      }
    }
    return 'Conectando a ' + (sta.ssid || '…');
  }

  function hexToRgb(hex) {
    var match = /^#?([0-9a-f]{6})$/i.exec(hex || '');
    if (!match) return null;
    var value = parseInt(match[1], 16);
    return { r: (value >> 16) & 255, g: (value >> 8) & 255, b: value & 255 };
  }

  function rgbToHex(rgb) {
    function part(value) {
      var clamped = Math.max(0, Math.min(255, Math.round(value || 0)));
      return ('0' + clamped.toString(16)).slice(-2);
    }
    return '#' + part(rgb.r) + part(rgb.g) + part(rgb.b);
  }

  // The bead stores 0..255 and the panel speaks percent, the same scale the
  // CONFIG screen's second row shows. One place does the conversion so the slider,
  // its label and the value sent to the device cannot disagree.
  function levelToPercent(level) {
    var clamped = Math.max(0, Math.min(255, Math.round(level || 0)));
    return Math.round(clamped * 100 / 255);
  }

  function percentToLevel(percent) {
    var clamped = Math.max(0, Math.min(100, Math.round(percent || 0)));
    return Math.round(clamped * 255 / 100);
  }

  // A poll must not stomp on a field the user is typing into.
  // Controls the owner has touched but not applied yet. The status poll runs every
  // second and used to write straight into them, so a picked colour or dragged
  // slider snapped back to the device value before the owner could press Aplicar.
  // Screens the auto-off applies to: a checkbox per screen, rendered from the
  // names the device reports so a rename shows up here too.
  // Which controls the bead's real owner allows. Redrawn only when the answer
  // changes, so toggling it does not fight the panel's own focus.
  var beadOwnedByBuddy = null;
  function renderBeadOwnership(configured) {
    if (beadOwnedByBuddy === configured) return;
    beadOwnedByBuddy = configured;
    var note = $('rgbBuddyNote');
    if (note) note.hidden = !configured;
    ['rgbColorRow', 'rgbEffectRow'].forEach(function (id) {
      var el = $(id);
      if (el) el.hidden = configured;
    });
  }

  function renderTimeoutScreens(status) {
    var box = $('timeoutScreens');
    if (!box) return;
    var names = Array.isArray(status.names) ? status.names : DEFAULT_NAMES;
    var mask = (status.display && typeof status.display.timeoutMask === 'number')
      ? status.display.timeoutMask : 0x09;
    var namesKey = names.join('\u0001');
    var sig = namesKey + '|' + mask;
    if (box.getAttribute('data-sig') === sig) return;
    box.setAttribute('data-sig', sig);
    box.innerHTML = '';
    names.forEach(function (name, index) {
      var label = document.createElement('label');
      label.className = 'switch';
      var input = document.createElement('input');
      input.type = 'checkbox';
      input.setAttribute('data-sleep-screen', String(index));
      input.checked = ((mask >> index) & 1) !== 0;
      var text = document.createElement('span');
      text.textContent = name || ('Pantalla ' + index);
      // Text first, checkbox last: the row reads as a setting, not a form.
      label.appendChild(text);
      label.appendChild(input);
      box.appendChild(label);
    });
  }

  function currentTimeoutMask() {
    var mask = 0;
    var inputs = document.querySelectorAll('[data-sleep-screen]');
    for (var i = 0; i < inputs.length; i++) {
      if (inputs[i].checked) {
        mask |= 1 << Number(inputs[i].getAttribute('data-sleep-screen'));
      }
    }
    return mask;
  }

  var dirtyFields = {};

  function markDirty(id) {
    if (id) dirtyFields[id] = true;
  }

  // --- theme -----------------------------------------------------------------
  // The accent comes from the device (NVS) so the panel and the glass agree.
  function mixHex(hex, base, weight) {
    var a = parseInt(hex.slice(1), 16);
    var b = parseInt(base.slice(1), 16);
    function mix(shift) {
      return Math.round(((a >> shift) & 255) * (1 - weight) +
                        ((b >> shift) & 255) * weight);
    }
    return '#' + ((1 << 24) | (mix(16) << 16) | (mix(8) << 8) | mix(0))
      .toString(16).slice(1);
  }

  function applyAccent(hex) {
    if (!/^#[0-9a-f]{6}$/i.test(hex || '')) return;
    var root = document.documentElement.style;
    root.setProperty('--gold', hex);
    root.setProperty('--gold-soft', mixHex(hex, '#0e1015', 0.82));
  }

  // --- dark theme -----------------------------------------------------------
  // Light paper is the default; the dark theme is a per-browser choice and the
  // device stores nothing about it. localStorage can throw (private mode, a
  // locked profile), so every access is guarded and the panel still toggles for
  // the session.
  var THEME_KEY = 'espbuddy-theme';

  function paintTheme(dark) {
    document.documentElement.setAttribute('data-theme', dark ? 'dark' : 'light');
    var btn = $('themeBtn');
    if (!btn) return;
    btn.setAttribute('aria-pressed', String(dark));
    btn.setAttribute('aria-label', dark ? 'Cambiar a tema claro' : 'Cambiar a tema oscuro');
    btn.title = dark ? 'Tema claro' : 'Tema oscuro';
    var use = btn.querySelector('use');
    if (use) use.setAttribute('href', dark ? '#i-sun' : '#i-moon');
  }

  function wireTheme() {
    var saved = null;
    try { saved = localStorage.getItem(THEME_KEY); } catch (error) { saved = null; }
    paintTheme(saved === 'dark');

    var btn = $('themeBtn');
    if (!btn) return;
    btn.addEventListener('click', function () {
      var dark = document.documentElement.getAttribute('data-theme') !== 'dark';
      paintTheme(dark);
      try { localStorage.setItem(THEME_KEY, dark ? 'dark' : 'light'); } catch (error) { /* private mode */ }
    });
  }

  function isEditing(el) {
    if (!el) return false;
    if (document.activeElement === el) return true;
    return !!(el.id && dirtyFields[el.id]);
  }

  function clearDirtyIn(node) {
    if (!node) return;
    var fields = node.querySelectorAll('[id]');
    for (var i = 0; i < fields.length; i++) delete dirtyFields[fields[i].id];
    if (node.id) delete dirtyFields[node.id];
  }

  function setText(id, text) {
    var el = $(id);
    if (el) el.textContent = text;
  }

  function setValue(id, value, force) {
    var el = $(id);
    if (el && (force || !isEditing(el))) el.value = value;
  }

  // --- rendering ------------------------------------------------------------

  function renderStatus(status) {
    var pill = $('connPill');
    var online = !!(status.sta && status.sta.connected) || !!(status.ap && status.ap.ssid);
    // The pill describes the phone <-> device link, which is up by definition if
    // this code is running. A failed home-WiFi join is a different thing and is
    // reported on its own row; using this pill for both produced a red dot next
    // to the words "En línea".
    if (pill) pill.className = 'pill ' + (online ? 'online' : 'offline');
    setText('connText', online ? 'En línea' : 'Sin conexión');

    var ap = status.ap || {};
    setText('stApSsid', ap.ssid || '—');
    setText('stApPass', ap.password || '(red abierta)');
    setText('stApIp', ap.ip || '—');
    setText('stSta', describeSta(status.sta));
    setText('stTime', (status.time && status.time.local) || '—');
    setText('stUptime', humanUptime(status.uptimeMs));
    setText('stHeap', humanBytes(status.heapFree));
    if (Array.isArray(status.names) && status.names.length) {
      screenNames = status.names.slice();
    }
    setText('stScreen', screenNames[status.screen] || status.screenName || '—');
    setText('nowScreen', 'Ahora: ' + (screenNames[status.screen] || status.screenName || '—'));
    renderOrder(status);
    renderTimeoutScreens(status);
    var timeoutSec = status.display && status.display.timeoutSec;
    if (timeoutSec !== undefined && $('screenTimeout') &&
        !isEditing($('screenTimeout'))) {
      var value = String(timeoutSec);
      var select = $('screenTimeout');
      if (!select.querySelector('option[value="' + value + '"]')) {
        var extra = document.createElement('option');
        extra.value = value;
        extra.textContent = timeoutSec + ' segundos';
        select.appendChild(extra);
      }
      select.value = value;
    }

    var scores = status.scores || {};
    setText('scoreGame', String(scores.game || 0));
    setText('scoreSlot', String(scores.slot || 0));

    setValue('apSsid', ap.ssid || '');
    setValue('apPass', ap.password || '');

    var display = status.display || {};
    if (display.brightness !== undefined) {
      if (!isEditing($('brightness'))) {
        setValue('brightness', display.brightness);
        setText('brightnessOut', display.brightness + ' %');
      }
    }

    if (display.uiColor) {
      if (!isEditing($('uiColor'))) applyAccent(display.uiColor);
      setValue('uiColor', display.uiColor);
      setText('uiColorHex', display.uiColor.toUpperCase());
    }

    var rotationMode = $('rotationMode');
    if (rotationMode && !isEditing(rotationMode)) {
      // Only the three modes the firmware offers: automatic and the two
      // unmirrored images. A mirrored image is never what a user wants, it only
      // appears when firmware is wrong.
      var mode = display.rotationMode || 'auto';
      rotationMode.value = (mode === 'horizontal' || mode === 'inverted') ? mode : 'auto';
    }

    var rotationLock = $('rotationLock');
    if (rotationLock && !isEditing(rotationLock)) {
      rotationLock.checked = !!display.rotationLocked;
    }

    var steer = status.steer || {};
    var invert = $('steerInvert');
    if (invert && !isEditing(invert)) invert.checked = steer.sign === -1;
    var invertY = $('steerInvertY');
    if (invertY && !isEditing(invertY)) invertY.checked = steer.signY === -1;

    var buddy = status.buddy || {};
    // The bridge state is one pill at the right end of the Sofía section head;
    // the sentence that explains the cause rides on it as a tooltip.
    // One sentence per cause the firmware reports. "Sin respuesta" was the only
    // thing the panel could say when the transport hid the reason; now a rejected
    // certificate, a missing clock and a Cloudflare block read differently.
    var buddyLinks = {
      ok: 'Conectado', unconfigured: 'Falta configurar', 'bad-url': 'URL inválida',
      'no-time': 'Sin hora sincronizada', tls: 'Certificado rechazado',
      blocked: 'Bloqueado por Cloudflare', unauthorized: 'Token rechazado',
      http: 'El bridge respondió un error', offline: 'Sin respuesta'
    };
    var bridgePill = $('bridgePill');
    if (bridgePill) {
      bridgePill.className = 'pill ' + (buddy.online ? 'online' : 'offline');
      setText('bridgeState', buddy.online ? 'En línea'
        : (buddy.configured ? 'Sin respuesta' : 'Sin configurar'));
      bridgePill.title = buddyLinks[buddy.link || ''] || '';
    }
    if (buddy.url) setValue('buddyUrl', buddy.url);
    if (buddy.eyeColor) setValue('buddyEyeColor', buddy.eyeColor);

    // Sofía owns the bead while her bridge is configured: buddyLedTick repaints her
    // phase colour every loop, so a colour or mode picked here would be overwritten
    // one loop later. The card says so instead of offering a control that lies; the
    // intensity still multiplies over her colour.
    renderBeadOwnership(!!buddy.configured);

    var rgb = status.rgb || {};
    if (rgb.r !== undefined) {
      var rgbHex = rgbToHex(rgb);
      if (!isEditing($('rgbColor'))) {
        setValue('rgbColor', rgbHex);
        setText('rgbColorHex', rgbHex.toUpperCase());
      }
      var levelPercent = levelToPercent(rgb.level);
      if (!isEditing($('rgbLevel'))) {
        setValue('rgbLevel', levelPercent);
        setText('rgbLevelOut', levelPercent + ' %');
      }
      if (!isEditing($('rgbEffect'))) setValue('rgbEffect', String(rgb.effect));
    }

    var picker = $('screenPicker');
    if (!picker) return;
    var tiles = picker.querySelectorAll('.tile');
    for (var i = 0; i < tiles.length; i++) {
      var index = Number(tiles[i].getAttribute('data-screen'));
      var active = index === status.screen;
      tiles[i].classList.toggle('active', active);
      if (active) tiles[i].setAttribute('aria-current', 'true');
      else tiles[i].removeAttribute('aria-current');
      var label = tiles[i].querySelector('.tile-name');
      if (label) label.textContent = screenNames[index] || '—';
    }
  }

  // --- polling --------------------------------------------------------------

  var pollInFlight = false;
  var lastStatus = null;

  function refresh() {
    if (pollInFlight) return;
    pollInFlight = true;
    api('GET', '/api/status').then(
      function (status) {
        pollInFlight = false;
        lastStatus = status;
        renderStatus(status);
      },
      function () {
        pollInFlight = false;
        var pill = $('connPill');
        if (pill) pill.className = 'pill offline';
        setText('connText', 'Sin conexión');
      }
    );
  }

  // --- network scan ---------------------------------------------------------

  function strengthLabel(rssi) {
    if (rssi >= -55) return 'Excelente';
    if (rssi >= -67) return 'Buena';
    if (rssi >= -78) return 'Floja';
    return 'Muy floja';
  }

  function runScan() {
    var button = $('scanBtn');
    var hint = $('scanHint');
    var list = $('netlist');
    list.hidden = true;
    list.textContent = '';
    hint.textContent = 'Buscando redes…';

    // The device sweeps asynchronously so it never blocks its own web server (and
    // never drops this very request). Poll until it says it is done.
    function poll(triesLeft) {
      return api('GET', '/api/scan').then(function (result) {
        if (result && result.ok !== false && result.scanning && triesLeft > 0) {
          return new Promise(function (resolve) {
            setTimeout(function () { resolve(poll(triesLeft - 1)); }, 700);
          });
        }
        return result;
      });
    }

    busy(button, 'Buscando…', function () {
      // POST starts the sweep, GET only reads its state: the two are separate so a
      // second reader can never consume the list of the first.
      return api('POST', '/api/scan', {}).then(function () { return poll(15); });
    }).then(
      function (result) {
        if (result && result.ok === false) {
          hint.textContent = result.error || 'No se pudo buscar redes';
          return;
        }
        var networks = (result && result.networks) || [];
        hint.textContent = networks.length
          ? 'Elegí tu red para completar el nombre'
          : 'No se encontró ninguna red. Acercate al router e intentá de nuevo.';
        if (!networks.length) return;

        networks.forEach(function (network) {
          var item = document.createElement('button');
          item.type = 'button';

          var name = document.createElement('span');
          name.textContent = network.ssid || '(oculta)';

          var meta = document.createElement('span');
          meta.className = 'rssi';
          meta.textContent = network.secure
            ? strengthLabel(network.rssi) + ' · con clave'
            : strengthLabel(network.rssi) + ' · abierta';

          item.appendChild(name);
          item.appendChild(meta);
          item.addEventListener('click', function () {
            $('wifiSsid').value = network.ssid || '';
            $('wifiPass').value = '';
            $('wifiPass').focus();
            toast('Red elegida: escribí la contraseña y guardá', 'ok');
          });
          list.appendChild(item);
        });
        list.hidden = false;
      },
      function (error) {
        hint.textContent = 'No se pudo buscar redes';
        toast(error.message, 'err');
      }
    );
  }

  // Explains a failed join by comparing what the device can see against what it
  // is configured to join. A wrong password and an unreachable network look
  // identical from the outside, and the fix for each is different.
  var AUTH_LABELS = {
    0: 'abierta', 1: 'WEP', 2: 'WPA', 3: 'WPA2',
    4: 'WPA/WPA2', 5: 'WPA2-Enterprise', 6: 'WPA3', 7: 'WPA2/WPA3'
  };

  function diagnoseWifi(button) {
    var diag = $('wifiDiag');
    var wanted = ($('wifiSsid').value || '').trim();
    busy(button, 'Analizando…', function () { return api('GET', '/api/scan'); }).then(
      function (result) {
        var networks = (result && result.networks) || [];
        var found = null;
        for (var i = 0; i < networks.length; i++) {
          if (networks[i].ssid === wanted) { found = networks[i]; break; }
        }
        var text;
        if (!wanted) {
          text = 'Escribí el nombre de tu red arriba para poder diagnosticar.';
        } else if (!found) {
          text = 'El equipo no ve "' + wanted + '" en este momento. Revisá que el ' +
                 'nombre esté bien escrito y que la red no sea solo de 5 GHz ' +
                 '(el equipo usa 2,4 GHz).';
        } else if (found.auth === 6) {
          text = '"' + wanted + '" usa WPA3 exclusivo y este chip no puede unirse. ' +
                 'En el router, cambiá la seguridad a WPA2/WPA3 mixto o WPA2.';
        } else {
          var st = lastStatus && lastStatus.sta ? lastStatus.sta : {};
          var reason = st.disconnectReason;
          var head = 'El equipo ve "' + wanted + '" (' + found.rssi + ' dBm, ' +
                     (AUTH_LABELS[found.auth] || 'seguridad ' + found.auth) + '), ' +
                     'así que la red existe y es compatible. ';
          if (reason === 2 || reason === 15 || reason === 202 || reason === 204) {
            text = head + 'El router aceptó la conexión pero el intercambio de ' +
                   'claves WPA2 falló, y eso significa una sola cosa: la contraseña ' +
                   'es incorrecta. Ojo, distingue mayúsculas de minúsculas y el ' +
                   'autocompletado suele meter otra.';
          } else if (reason === 8 || reason === 203) {
            text = head + 'El router rechazó al equipo. Casi siempre es filtrado por ' +
                   'MAC: agregá ' + (st.mac || 'la MAC del equipo') +
                   ' a la lista blanca del router.';
          } else {
            text = head + 'Revisá la contraseña (distingue mayúsculas) y que el ' +
                   'router no filtre por MAC. La MAC del equipo es ' +
                   (st.mac || 'la que muestra Estado') + '.';
          }
        }
        diag.textContent = text;
        diag.hidden = false;
      },
      function (error) { toast(error.message, 'err'); }
    );
  }

  // --- spotify --------------------------------------------------------------

  // The device's Spotify link is explained in the panel as three steps, in the
  // same words a person would use. Every state gets a sentence plus what to do
  // next, never just a number.
  var SPOTIFY_POLL_MS = 1000;
  var SPOTIFY_REDIRECT = 'http://127.0.0.1:8888/callback';

  var SPOTIFY_AUTH = {
    0: ['Falta el Client ID',
        'Pegá el Client ID de tu app en el campo de arriba y tocá Guardar Client ID.'],
    1: ['El equipo no está en tu WiFi de casa',
        'Spotify necesita internet. Conectá el equipo a tu red en la tarjeta WiFi de casa y volvé a esta tarjeta.'],
    2: ['Falta autorizar',
        'Ya hay Client ID, pero el equipo todavía no tiene permiso. Tocá Autorizar y seguí los pasos del código.'],
    3: ['Autorizado y listo',
        'El equipo ya tiene permiso para pedir lo que está sonando.']
  };

  var spotifyTimer = 0;
  var spotifyInFlight = false;
  var spotifyPrefilled = false;

  // The Client ID counter: the field is capped at 63 and the number in the box
  // says how much of that is used, the same way a native field shows its limit.
  var SPOTIFY_CLIENT_ID_MAX = 63;

  function renderClientCount(value) {
    setText('spotifyCount', (value || '').length + '/' + SPOTIFY_CLIENT_ID_MAX);
  }

  function setSpotifyState(kind, title, hint) {
    var box = $('spotifyStateBox');
    if (box) box.className = 'sp-state' + (kind ? ' ' + kind : '');
    setText('spotifyState', title);
    setText('spotifyStateHint', hint);
  }

  function spotifyClock(ms) {
    var total = Math.max(0, Math.floor((ms || 0) / 1000));
    var hours = Math.floor(total / 3600);
    var minutes = Math.floor((total % 3600) / 60);
    var seconds = total % 60;
    var tail = ('0' + seconds).slice(-2);
    if (hours > 0) {
      return hours + ':' + ('0' + minutes).slice(-2) + ':' + tail;
    }
    return minutes + ':' + tail;
  }

  function renderSpotify(data) {
    var auth = Number(data.auth);
    var state = SPOTIFY_AUTH[auth] ||
      ['No se pudo leer el estado', 'El equipo no informó en qué paso está la autorización.'];
    setSpotifyState(auth === 3 ? 'ok' : 'warn', state[0], state[1]);

    // The stored Client ID is shown once; after that the field belongs to the
    // person typing, so a poll must not put back what they just erased.
    var clientField = $('spotifyClientId');
    if (!spotifyPrefilled && data.configured && data.clientId && clientField) {
      clientField.value = data.clientId;
      spotifyPrefilled = true;
    }
    if (clientField) renderClientCount(clientField.value);
    setText('spotifyRedirect', data.redirectUri || SPOTIFY_REDIRECT);

    var feed = Number(data.feed);
    var error = $('spotifyError');
    var trackBox = $('spotifyTrack');
    var note = $('spotifyFeedNote');

    if (feed === 4) {
      var detail = data.error || 'El equipo no pudo hablar con Spotify';
      if (error) {
        error.textContent = 'El último intento falló: ' + detail + '.' +
          (data.httpStatus ? ' El servidor de Spotify respondió ' + data.httpStatus + '.' : '') +
          ' Revisá que el equipo tenga internet y que tu app siga existiendo en el panel de Spotify.';
        error.hidden = false;
      }
    } else if (error) {
      error.hidden = true;
    }

    if (feed === 2) {
      var track = data.track || {};
      setText('spotifyTitle', track.title || 'Sin título');
      setText('spotifyArtist', track.artist || 'Artista desconocido');
      // The album gets its own line, the one the cover was designed around.
      var album = $('spotifyAlbum');
      if (album) {
        album.textContent = track.album || '';
        album.hidden = !track.album;
      }
      setText('spotifyPlay', track.isPlaying ? 'Reproduciendo' : 'En pausa');

      var duration = Number(track.durationMs) || 0;
      var progress = Number(track.progressMs) || 0;
      var percent = duration > 0
        ? Math.max(0, Math.min(100, (progress / duration) * 100))
        : 0;
      var bar = $('spotifyBar');
      if (bar) bar.style.width = percent.toFixed(1) + '%';
      setText('spotifyTime', spotifyClock(progress) + ' de ' + spotifyClock(duration));
      if (trackBox) trackBox.hidden = false;
      if (note) note.hidden = true;
      return;
    }

    if (trackBox) trackBox.hidden = true;
    var message;
    if (feed === 1) {
      message = 'Buscando qué está sonando…';
    } else if (feed === 3) {
      message = 'No hay nada sonando en Spotify ahora. Dale play en tu teléfono y esto se actualiza solo.';
    } else if (feed === 4) {
      message = 'Sin canción: mirá el error de arriba.';
    } else {
      message = 'Esperando el primer dato del equipo…';
    }
    if (!note) return;
    note.textContent = message;
    note.hidden = false;
  }

  function refreshSpotify() {
    if (spotifyInFlight) return;
    spotifyInFlight = true;
    api('GET', '/api/spotify').then(
      function (data) {
        spotifyInFlight = false;
        renderSpotify(data);
      },
      function (error) {
        spotifyInFlight = false;
        setSpotifyState('bad', 'Sin conexión con el equipo',
          'No se pudo leer el estado de Spotify: ' + error.message);
      }
    );
  }

  function startSpotify() {
    if (spotifyTimer) return;
    refreshSpotify();
    spotifyTimer = setInterval(refreshSpotify, SPOTIFY_POLL_MS);
  }

  function stopSpotify() {
    if (!spotifyTimer) return;
    clearInterval(spotifyTimer);
    spotifyTimer = 0;
  }

  // Refresh once a second only while the card is on screen: the board is a small
  // web server and a hidden card has no reason to keep asking it for data.
  function watchSpotify() {
    var card = $('spotify');
    if (!card) return;
    if (typeof IntersectionObserver === 'undefined') {
      startSpotify();
      return;
    }
    new IntersectionObserver(function (entries) {
      if (entries[0].isIntersecting) startSpotify();
      else stopSpotify();
    }, { threshold: 0.05 }).observe(card);
  }

  // --- calendario -----------------------------------------------------------
  //
  // La placa habla con Google por su cuenta: esta tarjeta sólo muestra su estado
  // y le pasa las dos credenciales. Nada de esto viaja al bridge, y los títulos
  // de los eventos no salen del equipo salvo a este panel con sesión.
  var CALENDAR_POLL_MS = 5000;
  var calTimer = 0;
  var calInFlight = false;
  var calListed = false;
  var calListCount = -1;
  // Ultimo estado recibido. Lo usa el formulario para avisar antes de romper una
  // conexion que ya funciona: cambiar las credenciales invalida el permiso.
  var calLast = null;

  var CAL_PHASE = {
    early: 'se viene', near: 'falta poco', starting: 'ya casi', ongoing: 'empezó'
  };

  function calendarState(cal) {
    if (cal.kind === 'ready') {
      return cal.lastError
        ? { cls: 'warn', text: 'Conectado con un aviso' }
        : { cls: 'ok', text: 'Conectado' };
    }
    if (cal.kind === 'auth_pending') {
      return { cls: 'warn', text: 'Esperando que apruebes en Google' };
    }
    if (cal.kind === 'needs_auth') {
      return { cls: 'warn', text: 'Falta conectar Google' };
    }
    return { cls: 'warn', text: 'Falta configurar' };
  }

  function calendarWhen(cal) {
    if (cal.reaction && cal.reaction.active) {
      return CAL_PHASE[cal.reaction.phase] || 'cerca';
    }
    if (!cal.hasNext) return 'sin eventos a la vista';
    if (cal.nextStarted) return 'empezó hace ' + Math.abs(cal.nextInMin) + ' min';
    if (cal.nextInMin > 0) return 'en ' + cal.nextInMin + ' min';
    return 'en menos de un minuto';
  }

  function calendarHint(cal) {
    if (cal.kind === 'unconfigured') {
      return 'Seguí los pasos de arriba, guardá el Client ID y el secret, y tocá ' +
        'Conectar con Google.';
    }
    if (cal.kind === 'needs_auth') {
      return 'Las credenciales están guardadas. Tocá Conectar con Google y aprobá ' +
        'el permiso: el equipo se entera solo y no hay que copiar ningún token.';
    }
    if (cal.kind === 'auth_pending') {
      return 'Abrí el enlace, escribí el código y aprobá. Cuando termines, esta ' +
        'tarjeta cambia sola.';
    }
    if (cal.lastError) return 'Aviso del equipo: ' + cal.lastError;
    if (!cal.hasNext) return 'Conectado. No hay eventos a la vista.';
    var lead = cal.nextLeadMin < 0
      ? 'ese evento no tiene recordatorio, así que no mueve la cara'
      : 'recordatorio ' + cal.nextLeadMin + ' min antes';
    return 'Próximo: ' + cal.nextSummary + ', ' + calendarWhen(cal) + ' · ' + lead + '.';
  }

  function renderCalendar(cal) {
    calLast = cal;
    var state = calendarState(cal);
    var box = $('calStateBox');
    if (box) box.className = 'sp-state ' + state.cls;
    setText('calState', state.text);
    setText('calStateHint', calendarHint(cal));

    setText('calCreds', cal.configured
      ? (cal.authorized ? 'Autorizadas' : 'Sin autorizar')
      : 'Sin configurar');
    setText('calId', cal.selectedName || cal.selectedId || '—');
    setText('calNext', cal.hasNext
      ? (cal.nextStarted ? 'empezó · ' : (cal.nextInMin > 0 ? 'en ' + cal.nextInMin + ' min · ' : '')) +
        cal.nextSummary
      : '—');
    setText('calLead', cal.reaction && cal.reaction.active
      ? 'La cara está reaccionando (' + (CAL_PHASE[cal.reaction.phase] || '') + ')'
      : 'Por defecto ' + cal.fallbackLeadMin + ' min · gracia ' + cal.graceMin + ' min');

    if (!isEditing($('calFallbackLead'))) {
      setValue('calFallbackLead', String(cal.fallbackLeadMin));
    }
    if (!isEditing($('calGrace'))) setValue('calGrace', String(cal.graceMin));

    var connect = $('calConnect');
    if (connect) connect.disabled = !cal.configured || cal.pending;

    var auth = $('calAuthBox');
    if (auth) auth.hidden = !cal.pending;
    if (cal.pending) {
      setText('calUserCode', cal.userCode || '—');
      var link = $('calAuthLink');
      if (link && cal.verificationUrl) link.href = cal.verificationUrl;
      setText('calAuthTimer', cal.pendingSecondsLeft > 0
        ? 'El código vence en ' + cal.pendingSecondsLeft + ' s.'
        : 'El código venció: pedí uno nuevo.');
    }

    var picker = $('calPicker');
    if (picker) picker.hidden = !cal.authorized;
    // La lista se pide cuando cambia la cantidad de calendarios (o la primera
    // vez): no tiene sentido bajarla cada cinco segundos.
    if (cal.authorized && (!calListed || calListCount !== cal.calendarCount)) {
      calListed = true;
      calListCount = cal.calendarCount;
      loadCalendars(cal.selectedId);
    }
  }

  function loadCalendars(selectedId) {
    api('GET', '/api/calendar/calendars').then(function (answer) {
      var select = $('calSelect');
      if (!select) return;
      var items = (answer.items || []).slice();
      items.sort(function (a, b) {
        return (b.primary ? 1 : 0) - (a.primary ? 1 : 0);
      });
      select.innerHTML = '';
      items.forEach(function (item) {
        var option = document.createElement('option');
        option.value = item.id;
        option.textContent = item.name + (item.primary ? ' (principal)' : '');
        if (item.id === selectedId) option.selected = true;
        select.appendChild(option);
      });
      if (!items.length) {
        var empty = document.createElement('option');
        empty.value = '';
        empty.textContent = 'Todavía no hay calendarios';
        select.appendChild(empty);
      }
    }, function () { /* la próxima vuelta del poll lo reintenta */ });
  }

  function refreshCalendar() {
    if (calInFlight) return;
    calInFlight = true;
    api('GET', '/api/calendar').then(
      function (cal) { calInFlight = false; renderCalendar(cal); },
      function (error) {
        calInFlight = false;
        var box = $('calStateBox');
        if (box) box.className = 'sp-state bad';
        setText('calState', 'Sin respuesta');
        setText('calStateHint', error.message);
      }
    );
  }

  function startCalendar() {
    if (calTimer) return;
    refreshCalendar();
    calTimer = setInterval(refreshCalendar, CALENDAR_POLL_MS);
  }

  function stopCalendar() {
    if (!calTimer) return;
    clearInterval(calTimer);
    calTimer = 0;
  }

  // Same rule as the Spotify card: a section nobody is looking at does not keep
  // asking the board for numbers.
  function watchCalendar() {
    var card = $('integraciones');
    if (!card) return;
    if (typeof IntersectionObserver === 'undefined') {
      startCalendar();
      return;
    }
    new IntersectionObserver(function (entries) {
      if (entries[0].isIntersecting) startCalendar();
      else stopCalendar();
    }, { threshold: 0.05 }).observe(card);
  }

  // --- clima ----------------------------------------------------------------
  //
  // Open-Meteo no pide clave: la tarjeta sólo guarda el lugar y cada cuánto se
  // refresca. La placa es la que habla con el servicio; este panel sólo muestra
  // lo que el equipo ya sabe y le pasa la ciudad elegida.
  var weatherResults = [];
  var weatherSelected = -1;

  var WEATHER_HINT = {
    unconfigured: 'Todavía no hay un lugar guardado. Buscá tu ciudad o usá ' +
      'Detectar mi ubicación, y la pantalla RELOJ empieza a mostrar el clima.',
    no_data: 'El lugar está guardado y el equipo todavía no bajó el clima. ' +
      'Suele tardar menos de un minuto: podés apurar con Actualizar ahora.',
    error: 'El último intento falló. Revisá que el equipo tenga internet y ' +
      'volvé a intentar con Actualizar ahora.',
    ready: 'El equipo muestra este clima en la pantalla RELOJ.'
  };

  function weatherState(wx) {
    if (wx.kind === 'unconfigured' || !wx.configured) {
      return { cls: 'warn', text: 'Sin ubicación' };
    }
    if (wx.kind === 'error') return { cls: 'bad', text: 'Error' };
    if (wx.kind === 'no_data' || !wx.hasData) {
      return { cls: 'warn', text: 'Sin lectura todavía' };
    }
    return { cls: 'ok', text: 'Al día' };
  }

  // "hace 3 min" reads better than an epoch: the owner wants to know how stale
  // the reading is, not when it happened.
  function weatherAgo(ms) {
    if (!ms) return '—';
    var secs = Math.max(0, Math.round((Date.now() - Number(ms)) / 1000));
    if (secs < 60) return 'hace ' + secs + ' s';
    var mins = Math.floor(secs / 60);
    if (mins < 60) return 'hace ' + mins + ' min';
    var hours = Math.floor(mins / 60);
    if (hours < 24) return 'hace ' + hours + ' h';
    return 'hace ' + Math.floor(hours / 24) + ' días';
  }

  // Spanish decimal comma, the same number the glass shows.
  function weatherNumber(value, digits) {
    if (typeof value !== 'number' || !isFinite(value)) return '—';
    return value.toFixed(digits).replace('.', ',');
  }

  function renderWeather(wx) {
    var state = weatherState(wx);
    var box = $('wxStateBox');
    if (box) box.className = 'sp-state ' + state.cls;
    setText('wxState', state.text);
    setText('wxStateHint', wx.syncing
      ? 'El equipo está consultando el clima ahora mismo…'
      : (WEATHER_HINT[wx.kind] || WEATHER_HINT.unconfigured));

    // The device reports the last failure on its own line: a state of "Al día"
    // with an old error still needs to be readable.
    var error = $('wxError');
    if (error) {
      error.textContent = wx.lastError ? 'Último error: ' + wx.lastError : '';
      error.hidden = !wx.lastError;
    }

    var place = wx.place || {};
    var now = wx.now || {};
    setText('wxPlace', place.valid ? (place.name || 'Ubicación guardada') : '—');
    setText('wxNow', now.valid && typeof now.tempC === 'number'
      ? weatherNumber(now.tempC, 1) + ' °C · ' + (now.label || 'sin descripción')
      : '—');
    setText('wxUpdated', weatherAgo(wx.lastSuccessMs));
    setText('wxRefresh', wx.refreshMin
      ? 'Cada ' + wx.refreshMin + ' min'
      : '—');
    setText('wxLat', place.valid ? weatherNumber(Number(place.lat), 4) : '—');
    setText('wxLon', place.valid ? weatherNumber(Number(place.lon), 4) : '—');
    if (wx.refreshMin) setValue('wxRefreshMin', String(wx.refreshMin));

    var configured = !!wx.configured && !!place.valid;
    var detect = $('wxDetect');
    if (detect) detect.disabled = !!wx.syncing;
    var refreshNow = $('wxRefreshNow');
    if (refreshNow) refreshNow.disabled = !configured || !!wx.syncing;
    var forget = $('wxForget');
    if (forget) forget.disabled = !configured;
    // "Usar esta ciudad" only makes sense with a result picked from the list.
    var use = $('wxUse');
    if (use) use.disabled = weatherSelected < 0;
  }

  function loadWeather() {
    return api('GET', '/api/weather').then(
      function (wx) { renderWeather(wx); },
      function (error) {
        var box = $('wxStateBox');
        if (box) box.className = 'sp-state bad';
        setText('wxState', 'Error');
        setText('wxStateHint', 'No se pudo leer el clima del equipo: ' + error.message);
      }
    );
  }

  function renderWeatherResults(results) {
    weatherResults = results || [];
    weatherSelected = -1;
    var use = $('wxUse');
    if (use) use.disabled = true;
    var list = $('wxResults');
    if (!list) return;
    list.textContent = '';
    list.hidden = !weatherResults.length;
    weatherResults.forEach(function (item, index) {
      var chip = document.createElement('button');
      chip.type = 'button';
      chip.className = 'chip';
      chip.setAttribute('aria-pressed', 'false');
      var where = [item.region, item.country].filter(Boolean).join(', ');
      chip.textContent = item.name + (where ? ' — ' + where : '');
      chip.addEventListener('click', function () {
        weatherSelected = index;
        Array.prototype.forEach.call(list.querySelectorAll('.chip'), function (other) {
          other.setAttribute('aria-pressed', String(other === chip));
        });
        if (use) use.disabled = false;
      });
      list.appendChild(chip);
    });
  }

  // Every weather action answers with the same body as GET /api/weather, so the
  // card is repainted from the response instead of waiting for another round.
  function weatherAction(button, path, body, okMessage, busyLabel) {
    return busy(button, busyLabel || 'Guardando…', function () {
      return api('POST', path, body);
    }).then(
      function (wx) {
        renderWeather(wx);
        toast(okMessage, 'ok');
        return wx;
      },
      function (error) { toast(error.message, 'err'); return null; }
    );
  }

  // --- tabs -----------------------------------------------------------------

  // The panel is ten sections behind one tab strip. The tablist owns the roving
  // tabindex and the hash is the source of truth, so /admin#spotify, an in-page
  // link and the browser's back button all land on the same section.
  function wireTabs() {
    var nav = $('nav');
    // Only the panel's own navigation: the integration and network sub-tabs are
    // also role="tab" but they live inside a section and switch cards there.
    var tabs = nav
      ? Array.prototype.slice.call(nav.querySelectorAll('[role="tab"]'))
      : [];
    if (!nav || !tabs.length) return;

    function panelOf(tab) {
      return document.getElementById(tab.getAttribute('aria-controls'));
    }

    function keepVisible(tab) {
      // On a phone the strip scrolls sideways; on a laptop every tab is shown.
      if (nav.scrollWidth <= nav.clientWidth + 4) return;
      var left = tab.offsetLeft;
      var right = left + tab.offsetWidth;
      if (left < nav.scrollLeft + 8) nav.scrollLeft = Math.max(0, left - 12);
      else if (right > nav.scrollLeft + nav.clientWidth - 8) {
        nav.scrollLeft = right - nav.clientWidth + 12;
      }
    }

    function select(tab, focus) {
      tabs.forEach(function (item) {
        var on = item === tab;
        item.setAttribute('aria-selected', on ? 'true' : 'false');
        item.tabIndex = on ? 0 : -1;
        item.classList.toggle('active', on);
        var panel = panelOf(item);
        if (panel) panel.hidden = !on;
      });
      keepVisible(tab);
      if (focus) tab.focus();
    }

    function tabFor(id) {
      for (var i = 0; i < tabs.length; i++) {
        if (tabs[i].getAttribute('aria-controls') === id) return tabs[i];
      }
      return null;
    }

    function go(tab) {
      select(tab, false);
      // One history entry per section, so Back and Forward walk the tabs the way
      // the hash already addresses them (/admin#spotify). Re-selecting the same
      // tab must not pile up identical entries.
      var id = tab.getAttribute('aria-controls');
      if (window.history && window.history.pushState &&
          (window.location.hash || '') !== '#' + id) {
        window.history.pushState(null, '', '#' + id);
      }
    }

    tabs.forEach(function (tab) {
      tab.addEventListener('click', function () { go(tab); });
      tab.addEventListener('keydown', function (event) {
        var index = tabs.indexOf(tab);
        var next = null;
        // The column reads top to bottom, and below 760px the same strip scrolls
        // sideways: both pairs are live so the keys match what the eye sees.
        if (event.key === 'ArrowRight' || event.key === 'ArrowDown') {
          next = tabs[(index + 1) % tabs.length];
        } else if (event.key === 'ArrowLeft' || event.key === 'ArrowUp') {
          next = tabs[(index - 1 + tabs.length) % tabs.length];
        } else if (event.key === 'Home') next = tabs[0];
        else if (event.key === 'End') next = tabs[tabs.length - 1];
        if (!next) return;
        event.preventDefault();
        go(next);
        next.focus();
      });
    });

    // The default-credentials banner links to #cuenta, and back/forward change
    // the hash without firing a click.
    window.addEventListener('hashchange', function () {
      var id = (window.location.hash || '').replace(/^#/, '');
      var tab = id ? tabFor(id) : null;
      if (tab) select(tab, false);
    });

    var id = (window.location.hash || '').replace(/^#/, '');
    select((id && tabFor(id)) || tabs[0], false);
  }

  // --- wiring ---------------------------------------------------------------

  // Sub-tabs inside one section: the strip shows one card at a time. The cards
  // carry data-subpanel and the button's aria-controls names the one to show.
  function wireSubtabs() {
    Array.prototype.forEach.call(document.querySelectorAll('.subtabs'), function (strip) {
      strip.addEventListener('click', function (event) {
        var btn = event.target.closest ? event.target.closest('.subtab') : null;
        if (!btn) return;
        var region = strip.parentElement;
        if (!region) return;
        Array.prototype.forEach.call(strip.querySelectorAll('.subtab'), function (item) {
          item.setAttribute('aria-selected', String(item === btn));
        });
        Array.prototype.forEach.call(region.querySelectorAll('[data-subpanel]'), function (panel) {
          panel.hidden = panel.id !== btn.getAttribute('aria-controls');
        });
      });
    });
  }

  function wire() {
    on('scanBtn', 'click', runScan);

    on('wifiForm', 'submit', function (event) {
      event.preventDefault();
      var ssid = $('wifiSsid').value.trim();
      if (!ssid) {
        toast('Escribí el nombre de la red', 'err');
        return;
      }
      submit(
        event.target.querySelector('button[type="submit"]'),
        '/api/wifi',
        { ssid: ssid, pass: $('wifiPass').value },
        'Listo: el equipo va a conectarse a ' + ssid
      ).then(function (ok) { if (ok) refresh(); });
    });

    on('wifiDiagBtn', 'click', function (event) {
      diagnoseWifi(event.target);
    });

    on('wifiForget', 'click', function (event) {
      if (!confirm('¿Olvidar la red de casa? El equipo deja de intentar conectarse.')) {
        return;
      }
      submit(event.target, '/api/wifi/forget', undefined, 'Red de casa olvidada')
        .then(function (ok) {
          if (!ok) return;
          $('wifiSsid').value = '';
          $('wifiPass').value = '';
          refresh();
        });
    });

    // The pick menu's names and order. Inside init() because it needs api(),
    // toast and the draft state that live in this closure. Two requests, order
    // first: a name-only save leaves a working menu, while an order-only save
    // with renamed screens would look half applied.
    wireOrderDrag();
    if ($('orderSave')) {
      on('orderSave', 'click', function (event) {
        if (!orderDraft || !namesDraft) return;
        // Read the live inputs as well: if the owner typed and hit Save with the
        // keyboard, the blur-triggered change event may not have run yet.
        var list = $('orderList');
        var inputs = list ? list.querySelectorAll('input') : [];
        if (inputs.length === orderDraft.length) {
          for (var pos = 0; pos < inputs.length; pos++) {
            namesDraft[orderDraft[pos]] = inputs[pos].value;
          }
        }
        var names = namesDraft.map(function (value) { return (value || '').trim(); });
        if (names.some(function (value) { return !value; })) {
          toast('Cada pantalla necesita un nombre', 'err');
          return;
        }
        busy(event.target, 'Guardando…', function () {
          return api('POST', '/api/screens/order', { order: orderDraft })
            .then(function () {
              return api('POST', '/api/screens/names', { names: names });
            });
        }).then(
          function () {
            draftDirty = false;  // back to following the device
            toast('Nombres y orden guardados', 'ok');
            refresh();
          },
          function (error) { toast(error.message, 'err'); }
        );
      });
    }

    on('apForm', 'submit', function (event) {
      event.preventDefault();
      var ssid = $('apSsid').value.trim();
      var pass = $('apPass').value;

      if (!ssid) { toast('El nombre de la red no puede estar vacío', 'err'); return; }
      if (ssid.length > 32) { toast('El nombre admite hasta 32 caracteres', 'err'); return; }
      if (pass.length > 0 && pass.length < 8) {
        toast('La contraseña necesita al menos 8 caracteres', 'err');
        return;
      }
      if (pass.length > 64) { toast('La contraseña admite hasta 64 caracteres', 'err'); return; }
      if (!confirm('Al cambiar estos datos vas a tener que reconectarte a la nueva red. ¿Seguir?')) {
        return;
      }

      submit(
        event.target.querySelector('button[type="submit"]'),
        '/api/ap',
        { ssid: ssid, pass: pass },
        'Punto de acceso actualizado. Reconectate a "' + ssid + '"'
      );
    });

    ['apSsid', 'apPass'].forEach(function (id) {
      on(id, 'input', function () { $('apWarn').hidden = false; });
    });

    on('screenPicker', 'click', function (event) {
      var tile = event.target.closest('.tile');
      if (!tile) return;
      var index = Number(tile.getAttribute('data-screen'));
      submit(tile, '/api/screen', { index: index }, 'Pantalla: ' + (screenNames[index] || index),
        'Cambiando…').then(function (ok) { if (ok) refresh(); });
    });

    on('brightness', 'input', function (event) {
      setText('brightnessOut', event.target.value + ' %');
    });
    on('brightness', 'change', function (event) {
      submit(event.target, '/api/settings',
        { brightness: Number(event.target.value) }, 'Brillo actualizado');
    });

    if ($('uiColor')) {
      on('uiColor', 'input', function (event) {
        setText('uiColorHex', event.target.value.toUpperCase());
        applyAccent(event.target.value);
      });
      on('uiColor', 'change', function (event) {
        submit(event.target, '/api/settings', { uiColor: event.target.value },
          'Color de interfaz guardado');
      });
    }

    if ($('panelColors')) {
      on('panelColors', 'click', function (event) {
        // The board paints four full-screen colours before answering, so the
        // button stays busy for a couple of seconds on purpose.
        submit(event.target, '/api/panel/colors', {},
          'Colores probados en la pantalla', 'Probando…');
      });
    }

    if ($('buddyForm')) {
      on('buddyForm', 'submit', function (event) {
        event.preventDefault();
        var url = $('buddyUrl').value.trim();
        var token = $('buddyToken').value.trim();
        if (!url) { toast('La URL del bridge no puede estar vacía', 'err'); return; }
        // The device validates the whole URL and answers with the exact reason;
        // catching the missing scheme here saves a round trip for the one typo
        // that is easy to make by hand.
        if (!/^https?:\/\//i.test(url)) {
          toast('La URL tiene que empezar con http:// o https://', 'err');
          return;
        }
        var body = { buddyUrl: url };
        if (token) body.buddyToken = token;
        var askToken = $('buddyAskToken') ? $('buddyAskToken').value.trim() : '';
        if (askToken) body.buddyAskToken = askToken;
        submit(event.target.querySelector('button[type="submit"]'),
          '/api/settings', body, 'Buddy guardado').then(function (ok) {
          if (ok && $('buddyAskToken')) $('buddyAskToken').value = '';
          if (ok) $('buddyToken').value = '';
        });
      });
    }
    if ($('buddyEyeColor')) {
      // No live preview: the panel is not the glass. The colour takes effect on
      // the next face frame and is stored in NVS by the device.
      on('buddyEyeColor', 'change', function (event) {
        submit(event.target, '/api/settings',
          { buddyEyeColor: event.target.value }, 'Color de ojos guardado');
      });
    }
    if ($('buddyEyeSwatches')) {
      on('buddyEyeSwatches', 'click', function (event) {
        var swatch = event.target.closest('button[data-color]');
        if (!swatch) return;
        var color = swatch.getAttribute('data-color');
        $('buddyEyeColor').value = color;
        markDirty('buddyEyeColor');
        // The busy state goes on the input, not the swatch: replacing a colour
        // chip's text with "Guardando…" would blank its label.
        submit($('buddyEyeColor'), '/api/settings', { buddyEyeColor: color },
          'Color de ojos guardado');
      });
    }
    if ($('buddyEyeReset')) {
      on('buddyEyeReset', 'click', function (event) {
        $('buddyEyeColor').value = '#00b4ff';
        markDirty('buddyEyeColor');
        submit(event.target, '/api/settings', { buddyEyeColor: '' },
          'Color de ojos restaurado');
      });
    }

    if ($('buddyForget')) {
      on('buddyForget', 'click', function (event) {
        submit(event.target, '/api/settings', { buddyToken: '' },
          'Token del buddy olvidado');
      });
    }
    // Simular un estado: los chips son de a uno y mueven la cara de verdad.
    // El equipo sostiene el gesto y después vuelve solo a su estado real; el
    // endpoint es el que ya usa el juego, /api/face.
    function selectMood(active) {
      var group = $('buddyStates');
      if (!group) return;
      Array.prototype.forEach.call(group.querySelectorAll('.chip'), function (chip) {
        chip.setAttribute('aria-pressed', String(chip === active));
      });
    }

    on('buddyStates', 'click', function (event) {
      var chip = event.target.closest ? event.target.closest('.chip') : null;
      if (!chip) return;
      var mood = chip.getAttribute('data-mood');
      if (SOFIA_MOODS.indexOf(mood) < 0) return;
      var label = (chip.textContent || mood).trim();
      busy(chip, null, function () {
        return api('POST', '/api/face', { mood: mood, ms: SOFIA_HOLD_MS });
      }).then(
        function () {
          selectMood(chip);
          toast('Sofía: ' + label + ' en el equipo', 'ok');
        },
        function (error) { toast(error.message, 'err'); }
      );
    });

    on('buddyStateClear', 'click', function (event) {
      busy(event.target, 'Volviendo…', function () {
        return api('POST', '/api/face', { mood: '' });
      }).then(
        function () {
          selectMood(null);
          toast('Sofía vuelve al estado real', 'ok');
        },
        function (error) { toast(error.message, 'err'); }
      );
    });

    // --- integraciones: Google Calendar -------------------------------------
    //
    // Todo esto es de la placa: guarda las credenciales, pide el código del
    // device flow y elige el calendario. El bridge no participa.
    if ($('calForm')) {
      on('calForm', 'submit', function (event) {
        event.preventDefault();
        var body = {};
        [['calClientId', 'clientId'], ['calClientSecret', 'clientSecret']]
          .forEach(function (pair) {
            var field = $(pair[0]);
            var value = field ? field.value.trim() : '';
            if (value) body[pair[1]] = value;
          });
        var lead = $('calFallbackLead') ? $('calFallbackLead').value.trim() : '';
        if (lead !== '') body.fallbackLeadMin = Number(lead);
        var grace = $('calGrace') ? $('calGrace').value.trim() : '';
        if (grace !== '') body.graceMin = Number(grace);
        if (!Object.keys(body).length) {
          toast('No hay nada nuevo para guardar', 'err');
          return;
        }
        // Cambiar el par de credenciales invalida el permiso que ya existe (un
        // refresh token pertenece al cliente que lo emitio). El aviso evita
        // desconectar una cuenta sin querer; el formulario de al lado no lo dice.
        if (calLast && calLast.authorized &&
            (body.clientId || body.clientSecret) &&
            !confirm('Si cambiás el Client ID o el secreto, la cuenta de Google ' +
                     'conectada se desconecta y hay que aprobar el permiso otra ' +
                     'vez. ¿Seguir?')) {
          return;
        }
        submit(event.target.querySelector('button[type="submit"]'),
          '/api/calendar/config', body, 'Credenciales guardadas').then(function (ok) {
          if (!ok) return;
          if ($('calClientSecret')) $('calClientSecret').value = '';
          calListed = false;
          refreshCalendar();
        });
      });
    }
    if ($('calConnect')) {
      on('calConnect', 'click', function (event) {
        submit(event.target, '/api/calendar/connect', {},
          'Código pedido: aprobalo en Google').then(function (ok) {
          if (ok) refreshCalendar();
        });
      });
    }
    if ($('calCancel')) {
      on('calCancel', 'click', function (event) {
        submit(event.target, '/api/calendar/cancel', {},
          'Conexión cancelada').then(function (ok) {
          if (ok) refreshCalendar();
        });
      });
    }
    if ($('calSelectSave')) {
      on('calSelectSave', 'click', function (event) {
        var select = $('calSelect');
        if (!select || !select.value) {
          toast('Elegí un calendario', 'err');
          return;
        }
        var label = select.options[select.selectedIndex].textContent || '';
        // El sufijo es de la UI, no del nombre real del calendario de Google.
        label = label.replace(/ \(principal\)$/, '');
        submit(event.target, '/api/calendar/select',
          { calendarId: select.value, name: label },
          'Calendario guardado').then(function (ok) {
          if (ok) refreshCalendar();
        });
      });
    }
    if ($('calRefresh')) {
      on('calRefresh', 'click', function (event) {
        submit(event.target, '/api/calendar/refresh', {},
          'Actualizando el calendario').then(function (ok) {
          if (ok) refreshCalendar();
        });
      });
    }
    if ($('calForget')) {
      on('calForget', 'click', function (event) {
        if (!confirm('Se borran el Client ID, el secreto, el permiso y el ' +
                     'calendario elegido. Para volver a usarlo hay que configurar ' +
                     'todo otra vez. ¿Seguir?')) {
          return;
        }
        submit(event.target, '/api/calendar/forget', {},
          'Credenciales de Google olvidadas').then(function (ok) {
          if (!ok) return;
          calListed = false;
          refreshCalendar();
        });
      });
    }

    // --- integraciones: clima -----------------------------------------------
    //
    // Todo pasa por la placa: ella busca la ciudad, guarda el lugar y habla con
    // Open-Meteo. La tarjeta sólo lleva el texto que escribió el dueño.
    if ($('wxSearchForm')) {
      on('wxSearchForm', 'submit', function (event) {
        event.preventDefault();
        var query = $('wxQuery').value.trim();
        if (!query) {
          toast('Escribí el nombre de una ciudad', 'err');
          return;
        }
        busy(event.target.querySelector('button[type="submit"]'), 'Buscando…', function () {
          return api('POST', '/api/weather/search', { q: query });
        }).then(
          function (answer) {
            renderWeatherResults(answer.results || []);
            if (!weatherResults.length) {
              toast('No encontré esa ciudad: probá con otro nombre', 'err');
            }
          },
          function (error) {
            // A 404 is "no encontré esa ciudad", not a broken card: the list
            // goes away and the toast says what to try instead.
            renderWeatherResults([]);
            toast(error.message, 'err');
          }
        );
      });
    }

    if ($('wxUse')) {
      on('wxUse', 'click', function (event) {
        var item = weatherResults[weatherSelected];
        if (!item) {
          toast('Elegí una ciudad de la lista', 'err');
          return;
        }
        weatherAction(event.target, '/api/weather/location',
          { lat: Number(item.lat), lon: Number(item.lon), name: item.name },
          'Ciudad guardada: ' + item.name).then(function (wx) {
          if (wx) renderWeatherResults([]);
        });
      });
    }

    if ($('wxDetect')) {
      on('wxDetect', 'click', function (event) {
        weatherAction(event.target, '/api/weather/detect', undefined,
          'Ubicación detectada', 'Detectando…');
      });
    }

    if ($('wxConfigForm')) {
      on('wxConfigForm', 'submit', function (event) {
        event.preventDefault();
        var field = $('wxRefreshMin');
        var minutes = field ? Number(field.value) : NaN;
        // The firmware rejects anything outside 5..180; catching it here avoids a
        // round trip and keeps the reason next to the field.
        if (!isFinite(minutes) || minutes < 5 || minutes > 180) {
          toast('El refresco va de 5 a 180 minutos', 'err');
          return;
        }
        weatherAction(event.target.querySelector('button[type="submit"]'),
          '/api/weather/config', { refreshMin: Math.round(minutes) },
          'Refresco cada ' + Math.round(minutes) + ' min').then(function (wx) {
          if (!wx) return;
          // Applied: the field follows the device again, so the stored value is
          // what shows even if the typed one had an odd format.
          clearDirtyIn($('wxConfigForm'));
          renderWeather(wx);
        });
      });
    }

    if ($('wxRefreshNow')) {
      on('wxRefreshNow', 'click', function (event) {
        weatherAction(event.target, '/api/weather/refresh', undefined,
          'Pidiendo el clima al equipo', 'Actualizando…');
      });
    }

    if ($('wxForget')) {
      on('wxForget', 'click', function (event) {
        if (!confirm('Se borra la ubicación guardada y la pantalla RELOJ deja de ' +
                     'mostrar el clima hasta que elijas otra. ¿Seguir?')) {
          return;
        }
        weatherAction(event.target, '/api/weather/forget', undefined,
          'Ubicación olvidada').then(function (wx) {
          if (wx) renderWeatherResults([]);
        });
      });
    }

    if ($('timeoutScreens')) {
      on('timeoutScreens', 'change', function (event) {
        if (!event.target.hasAttribute('data-sleep-screen')) return;
        submit(event.target, '/api/settings',
          { screenTimeoutMask: currentTimeoutMask() },
          'Pantallas del apagado actualizadas');
      });
    }

    if ($('screenTimeout')) {
      on('screenTimeout', 'change', function (event) {
        submit(event.target, '/api/settings',
          { screenTimeoutSec: Number(event.target.value) },
          Number(event.target.value) === 0
            ? 'La pantalla no se apaga sola' : 'Apagado automático actualizado');
      });
    }

    on('steerInvert', 'change', function (event) {
      submit(event.target, '/api/settings',
        { steerSign: event.target.checked ? -1 : 1 },
        event.target.checked ? 'Izquierda/derecha invertidos'
                             : 'Izquierda/derecha normales');
    });

    on('steerInvertY', 'change', function (event) {
      submit(event.target, '/api/settings',
        { steerSignY: event.target.checked ? -1 : 1 },
        event.target.checked ? 'Arriba/abajo invertidos' : 'Arriba/abajo normales');
    });

    on('rotationMode', 'change', function (event) {
      submit(event.target, '/api/settings',
        { rotationMode: event.target.value }, 'Rotación actualizada');
    });

    on('rotationLock', 'change', function (event) {
      submit(event.target, '/api/settings',
        { rotationLocked: event.target.checked },
        event.target.checked ? 'Rotación bloqueada' : 'Rotación libre');
    });

    on('rgbLevel', 'input', function (event) {
      setText('rgbLevelOut', event.target.value + ' %');
    });

    on('rgbColor', 'input', function (event) {
      setText('rgbColorHex', event.target.value.toUpperCase());
    });

    on('rgbApply', 'click', function (event) {
      // Only the intensity when Sofía owns the bead: sending a colour would be a
      // request the firmware immediately undoes, and the card would report a value
      // the light never shows.
      var body = { level: percentToLevel(Number($('rgbLevel').value)) };
      if (!beadOwnedByBuddy) {
        var color = hexToRgb($('rgbColor').value) || { r: 255, g: 215, b: 0 };
        body.r = color.r;
        body.g = color.g;
        body.b = color.b;
        body.effect = Number($('rgbEffect').value);
      }
      submit(event.target, '/api/settings', { rgb: body }, 'Luz actualizada');
    });

    on('setTime', 'click', function (event) {
      // The browser's clock is a UTC epoch, which is exactly what the device
      // needs; it applies its own UTC offset on top.
      submit(event.target, '/api/time',
        { epoch: Math.floor(Date.now() / 1000) },
        'Hora del equipo actualizada').then(function (ok) { if (ok) refresh(); });
    });

    on('pressShort', 'click', function (event) {
      submit(event.target, '/api/press', { long: false }, 'Toque enviado');
    });

    on('pressLong', 'click', function (event) {
      submit(event.target, '/api/press', { long: true }, 'Mantenido enviado: cambia de pantalla');
    });

    on('steerRecal', 'click', function (event) {
      submit(event.target, '/api/steer/reset', undefined,
        'Listo: te llevé a CONFIG y abrí la calibración guiada').then(
        function (ok) { if (ok) refresh(); });
    });

    on('steerSwap', 'click', function (event) {
      submit(event.target, '/api/steer/swap', undefined,
        'Eje del control cambiado').then(function (ok) { if (ok) refresh(); });
    });

    on('spotifyClientId', 'input', function (event) {
      renderClientCount(event.target.value);
    });

    on('spotifyForm', 'submit', function (event) {
      event.preventDefault();
      var clientId = $('spotifyClientId').value.trim();
      if (!clientId) {
        toast('Pegá el Client ID de tu app', 'err');
        return;
      }
      submit(
        event.target.querySelector('button[type="submit"]'),
        '/api/spotify/config',
        { clientId: clientId },
        'Client ID guardado'
      ).then(function (ok) {
        if (!ok) return;
        spotifyPrefilled = true;
        refreshSpotify();
      });
    });

    on('spotifyAuthBtn', 'click', function (event) {
      busy(event.target, 'Preparando…', function () {
        return api('POST', '/api/spotify/url');
      }).then(
        function (result) {
          var link = $('spotifyAuthLink');
          link.href = result.url;
          link.textContent = result.url;
          setText('spotifyRedirect', result.redirectUri || SPOTIFY_REDIRECT);
          $('spotifyAuthBox').hidden = false;
          toast('Enlace listo: abrilo y aprobá el acceso', 'ok');
        },
        function (error) { toast(error.message, 'err'); }
      );
    });

    on('spotifyCodeForm', 'submit', function (event) {
      event.preventDefault();
      // Send exactly what was pasted. The firmware parses the whole callback URL,
      // the `code=...` fragment or a bare code (spotifyExtractCode), and doing the
      // trimming here as well meant two parsers that could disagree: a mistake in
      // this regex would have posted a wrong code the device could not detect.
      var code = $('spotifyCode').value.trim();
      if (!code) {
        toast('Pegá la dirección completa o el código de la barra de direcciones', 'err');
        return;
      }
      submit(
        event.target.querySelector('button[type="submit"]'),
        '/api/spotify/code',
        { code: code },
        'Autorizado: el equipo ya puede mostrar lo que suena',
        'Verificando…'
      ).then(function (ok) {
        if (!ok) return;
        $('spotifyCode').value = '';
        refreshSpotify();
      });
    });

    on('spotifyForget', 'click', function (event) {
      if (!confirm('¿Olvidar la autorización? El Client ID queda guardado y hay que autorizar de nuevo.')) {
        return;
      }
      submit(event.target, '/api/spotify/forget', undefined, 'Autorización olvidada')
        .then(function (ok) { if (ok) refreshSpotify(); });
    });

    on('resetScores', 'click', function (event) {      if (!confirm('¿Borrar los récords de ambos juegos?')) return;
      submit(event.target, '/api/scores/reset', undefined, 'Récords borrados')
        .then(function (ok) { if (ok) refresh(); });
    });

    if ($('accountForm')) {
      on('accountForm', 'submit', function (event) {
        event.preventDefault();
        var pass = $('acctPass').value;
        if (pass !== $('acctRepeat').value) {
          toast('Las contraseñas nuevas no coinciden', 'err');
          $('acctRepeat').focus();
          return;
        }
        var button = event.target.querySelector('button[type="submit"]');
        busy(button, 'Guardando…', function () {
          return api('POST', '/api/admin/credentials', {
            current: $('acctCurrent').value,
            user: $('acctUser').value.trim(),
            pass: pass
          });
        }).then(
          function () {
            toast('Credenciales guardadas', 'ok');
            session.user = $('acctUser').value.trim();
            session.mustChange = false;
            renderSession();
            $('acctCurrent').value = '';
            $('acctPass').value = '';
            $('acctRepeat').value = '';
          },
          function (error) { toast(error.message, 'err'); }
        );
      });
    }

    if ($('logoutBtn')) {
      on('logoutBtn', 'click', function (event) { logout(event.target); });
    }
    if ($('logoutBtnCard')) {
      on('logoutBtnCard', 'click', function (event) { logout(event.target); });
    }

    on('restartBtn', 'click', function (event) {
      if (!confirm('¿Reiniciar el equipo? No se borra nada.')) return;
      submit(event.target, '/api/restart', undefined, 'Reiniciando… recargá en unos 10 segundos');
    });

    on('factoryBtn', 'click', function (event) {
      if (!confirm('Esto borra el WiFi de casa y los récords. ¿Restablecer de fábrica?')) return;
      submit(event.target, '/api/factory', undefined,
        'Restableciendo… reconectate a la red ESP32-GADGET');
    });
  }

  // --- sesión y cuenta ------------------------------------------------------

  // What the device says about the browser holding this page. The cookie itself
  // is HttpOnly, so this cache is the only copy the panel has.
  var session = { user: '', mustChange: false };

  function loadSession() {
    return fetch('/api/session', { headers: { Accept: 'application/json' } })
      .then(function (response) { return response.json(); })
      .then(function (state) {
        if (!state) return;
        if (!state.authenticated) {
          // Not logged in (a stale tab, or the session expired between two
          // requests): the panel has nothing to show.
          window.location.replace('/login?next=' +
            encodeURIComponent(window.location.pathname));
          return;
        }
        session.user = state.user || '';
        session.mustChange = !!state.mustChange;
        renderSession();
      })
      .catch(function () {
        // A dead link already has its own message through the status poll.
      });
  }

  function renderSession() {
    var chip = $('whoChip');
    if (chip && $('whoUser')) {
      setText('whoUser', session.user || '—');
      chip.hidden = false;
    }
    var who = $('accountWho');
    if (who) who.textContent = 'Sesión iniciada como ' + (session.user || '—') + '.';
    var userField = $('acctUser');
    if (userField && !userField.value) userField.value = session.user;
    // The default pair is a working password, which is exactly why the warning
    // stays until it is replaced. No close button on purpose.
    var banner = $('mustChangeBanner');
    if (banner) banner.hidden = !session.mustChange;
  }

  function logout(button) {
    return busy(button, 'Saliendo…', function () {
      return fetch('/api/session/logout', { method: 'POST' }).then(function (response) {
        return response.text();
      });
    }).then(
      function () { window.location.replace('/login'); },
      // Even if the device did not answer, the local cookie is useless now; the
      // login page is the right place to be either way.
      function () { window.location.replace('/login'); }
    );
  }

  function init() {
    if (!$('toast')) return;
    wireTheme();
    wireTabs();
    wireSubtabs();
    wire();
    loadSession();
    refresh();
    setInterval(refresh, POLL_MS);
    watchSpotify();
    watchCalendar();
    loadWeather();
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
