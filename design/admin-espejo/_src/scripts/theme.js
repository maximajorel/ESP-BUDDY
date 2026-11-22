/* Theme switch: light paper by default, dark on demand. The choice is remembered
   in the browser; the device does not store it. */
(function () {
  'use strict';
  var root = document.documentElement;
  var btn = document.getElementById('themeBtn');

  function paint(dark) {
    root.setAttribute('data-theme', dark ? 'dark' : 'light');
    if (!btn) return;
    btn.setAttribute('aria-pressed', String(dark));
    btn.setAttribute('aria-label', dark ? 'Cambiar a tema claro' : 'Cambiar a tema oscuro');
    btn.title = dark ? 'Tema claro' : 'Tema oscuro';
    var use = btn.querySelector('use');
    if (use) use.setAttribute('href', dark ? '#i-sun' : '#i-moon');
  }

  var saved = null;
  try { saved = localStorage.getItem('espbuddy-theme'); } catch (error) { saved = null; }
  paint(saved === 'dark');

  if (!btn) return;
  btn.addEventListener('click', function () {
    var dark = root.getAttribute('data-theme') !== 'dark';
    paint(dark);
    try { localStorage.setItem('espbuddy-theme', dark ? 'dark' : 'light'); } catch (error) { /* private mode */ }
  });
})();
