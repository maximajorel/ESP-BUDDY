// Console for the ESP32-S3-LCD-1.47B web firmware.
//
// The page is served by the board itself, so the socket host is whatever the
// browser used to reach it. Anything the board pushes is JSON with a `t` field;
// anything the page sends is a small command object.

const HOST = location.hostname || '192.168.4.1';

// `?ws=ws://host:port` overrides the socket endpoint. It lets the page be
// point at a local stand-in server while developing the UI, without needing the
// board's access point.
const WS_OVERRIDE = new URLSearchParams(location.search).get('ws');
const WS_URL = WS_OVERRIDE || `ws://${HOST}:81`;

const $ = (id) => document.getElementById(id);

// ------------------------------------------------------------------ socket --

let socket = null;
let retryDelay = 500;
let lastStateAt = 0;

function setStatus(text, online) {
  const pill = $('status');
  pill.textContent = text;
  pill.className = 'pill ' + (online ? 'online' : 'offline');
}

function connect() {
  setStatus('conectando…', false);
  socket = new WebSocket(WS_URL);

  socket.onopen = () => {
    retryDelay = 500;
    setStatus('conectado', true);
  };

  socket.onclose = () => {
    setStatus('desconectado', false);
    setTimeout(connect, retryDelay);
    retryDelay = Math.min(retryDelay * 2, 8000);
  };

  socket.onerror = () => socket.close();

  socket.onmessage = (event) => {
    let msg;
    try {
      msg = JSON.parse(event.data);
    } catch {
      return;
    }
    if (msg.t === 'state') renderState(msg);
    if (msg.t === 'hello') {
      $('preview').width = msg.w;
      $('preview').height = msg.h;
    }
  };
}

function send(command) {
  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify(command));
    return true;
  }
  return false;
}

// -------------------------------------------------------------- telemetry ---

function fmt(values, digits) {
  return values.map((v) => v.toFixed(digits)).join('  ');
}

function renderState(s) {
  lastStateAt = performance.now();
  $('accel').textContent = fmt([s.ax, s.ay, s.az], 2);
  $('gyro').textContent = fmt([s.gx, s.gy, s.gz], 1);

  const minutes = Math.floor(s.up / 60);
  const seconds = s.up % 60;
  $('uptime').textContent = `encendida ${minutes}m ${String(seconds).padStart(2, '0')}s`;
  $('heap').textContent = `${(s.heap / 1024).toFixed(0)} KB libres`;
  $('clients').textContent = `${s.clients} cliente${s.clients === 1 ? '' : 's'}`;

  if (document.activeElement !== $('bl')) {
    $('bl').value = s.bl;
    $('bl-out').textContent = s.bl;
  }
  if (document.activeElement !== $('rgb-level')) {
    $('rgb-level').value = s.level;
    $('rgb-level-out').textContent = s.level;
  }
}

// Watchdog: if the board stops pushing, say so instead of showing stale data.
setInterval(() => {
  if (lastStateAt && performance.now() - lastStateAt > 3000) {
    setStatus('sin datos', false);
  }
}, 1000);

// ---------------------------------------------------------------- display ---

$('bl').addEventListener('input', (e) => {
  const v = Number(e.target.value);
  $('bl-out').textContent = v;
  send({ t: 'bl', v });
});

$('fill-apply').addEventListener('click', () => {
  send({ t: 'fill', c: $('fill-color').value });
});

$('bars').addEventListener('click', () => {
  send({ t: 'bars' });
});

// --------------------------------------------------------------------- rgb --

function hexToRgb(hex) {
  const v = parseInt(hex.slice(1), 16);
  return { r: (v >> 16) & 255, g: (v >> 8) & 255, b: v & 255 };
}

function pushRgb() {
  const { r, g, b } = hexToRgb($('rgb-color').value);
  send({ t: 'rgb', r, g, b });
}

$('rgb-color').addEventListener('input', pushRgb);

$('rgb-level').addEventListener('input', (e) => {
  const v = Number(e.target.value);
  $('rgb-level-out').textContent = v;
  send({ t: 'level', v });
});

document.querySelectorAll('[data-effect]').forEach((button) => {
  button.addEventListener('click', () => {
    const effect = button.dataset.effect;
    if (effect === 'solid') {
      const { r, g, b } = hexToRgb($('rgb-color').value);
      send({ t: 'effect', v: 'solid', r, g, b });
    } else {
      send({ t: 'effect', v: effect });
    }
  });
});

// ------------------------------------------------------------------- image --

const canvas = $('preview');
const ctx = canvas.getContext('2d', { willReadFrequently: true });
let pendingPixels = null;

function packRgb565(imageData) {
  const { data } = imageData;
  const out = new Uint8Array((data.length / 4) * 2);
  for (let i = 0, j = 0; i < data.length; i += 4) {
    // 5 bits red, 6 green, 5 blue, emitted high byte first: the order the
    // ST7789 expects on the wire.
    const value = ((data[i] & 0xf8) << 8) | ((data[i + 1] & 0xfc) << 3) | (data[i + 2] >> 3);
    out[j++] = value >> 8;
    out[j++] = value & 0xff;
  }
  return out;
}

$('image-file').addEventListener('change', async (e) => {
  const file = e.target.files[0];
  if (!file) return;

  const hint = $('preview-hint');
  $('image-send').disabled = true;
  hint.textContent = `Procesando ${file.name}…`;

  let source = null;
  let release = () => {};

  // createImageBitmap is the fast path, but it rejects formats the browser can
  // still render through an <img> - HEIC from a phone camera is the usual case.
  // Try both before declaring the file unreadable, and always say something.
  try {
    source = await createImageBitmap(file);
    release = () => source.close();
  } catch (err) {
    console.warn('createImageBitmap falló, probando con <img>', err);
    try {
      const url = URL.createObjectURL(file);
      const img = new Image();
      img.src = url;
      await img.decode();
      source = img;
      release = () => URL.revokeObjectURL(url);
    } catch (err2) {
      console.error(err2);
      hint.textContent =
        `No pude decodificar "${file.name}". Probá con JPG o PNG; si sale del celu, ` +
        `activá "Más compatible" en los ajustes de la cámara.`;
      return;
    }
  }

  try {
    const W = canvas.width;
    const H = canvas.height;
    const sw = source.width || source.naturalWidth;
    const sh = source.height || source.naturalHeight;
    if (!sw || !sh) throw new Error('la imagen no reporta dimensiones');

    // Cover fit: fill the panel, crop the overflow, never letterbox.
    const scale = Math.max(W / sw, H / sh);
    const w = sw * scale;
    const h = sh * scale;

    ctx.fillStyle = '#000';
    ctx.fillRect(0, 0, W, H);
    ctx.drawImage(source, (W - w) / 2, (H - h) / 2, w, h);

    pendingPixels = packRgb565(ctx.getImageData(0, 0, W, H));
    $('image-send').disabled = false;
    hint.textContent =
      `${file.name} — ${W}×${H}, ${(pendingPixels.length / 1024).toFixed(0)} KB de píxeles listos.`;
  } catch (err) {
    console.error(err);
    hint.textContent = `No pude preparar la imagen: ${err.message}`;
  } finally {
    release();
  }
});

$('image-send').addEventListener('click', () => {
  if (!pendingPixels) return;

  const progress = $('image-progress');
  progress.hidden = false;
  progress.value = 0;
  $('image-send').disabled = true;

  const request = new XMLHttpRequest();
  request.open('POST', '/api/image');
  // Must be multipart/form-data: the ESP32 WebServer only routes the body to
  // its upload callback for multipart parts. A raw octet-stream body goes to
  // the argument parser instead and the upload callback never runs, which the
  // board reports as "rejected: got 0".
  const form = new FormData();
  form.append('image', new Blob([pendingPixels], { type: 'application/octet-stream' }), 'frame.bin');

  request.upload.onprogress = (e) => {
    if (e.lengthComputable) progress.value = (e.loaded / e.total) * 100;
  };

  request.onload = () => {
    progress.value = 100;
    $('image-send').disabled = false;
    let body = {};
    try {
      body = JSON.parse(request.responseText);
    } catch {
      body = {};
    }
    $('preview-hint').textContent = body.ok
      ? 'Enviada. Debería estar en el LCD ahora mismo.'
      : `La placa rechazó la imagen (HTTP ${request.status}): ${request.responseText}`;
  };

  request.onerror = () => {
    $('image-send').disabled = false;
    $('preview-hint').textContent = 'Falló el envío: revisá que sigas conectado al AP de la placa.';
  };

  request.send(form);
});

// -------------------------------------------------------------------- wifi --

$('wifi-save').addEventListener('click', async () => {
  const ssid = $('wifi-ssid').value.trim();
  const pass = $('wifi-pass').value;
  if (!ssid) {
    $('wifi-msg').textContent = 'Necesito el SSID de tu red.';
    return;
  }

  $('wifi-msg').textContent = 'Guardando…';
  try {
    const response = await fetch('/api/wifi', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, pass }),
    });
    const body = await response.json();
    $('wifi-msg').textContent = body.ok
      ? 'Guardado. La placa está reiniciando y va a intentar conectarse a tu red…'
      : `Error: ${body.error || 'desconocido'}`;
  } catch {
    $('wifi-msg').textContent =
      'Se guardó y la placa se reinició (la conexión se corta por eso).';
  }
});

// -------------------------------------------------------------------- boot --

$('endpoint').textContent = `${HOST} · WebSocket en el puerto 81`;
$('ws-endpoint').textContent = WS_URL;
connect();
