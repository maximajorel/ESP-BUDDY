/* Sub-tabs inside one section: the strip shows one card at a time. */
(function () {
  'use strict';
  Array.prototype.forEach.call(document.querySelectorAll('.subtabs'), function (strip) {
    strip.addEventListener('click', function (event) {
      var btn = event.target.closest('.subtab');
      if (!btn) return;
      var region = strip.parentElement;
      Array.prototype.forEach.call(strip.querySelectorAll('.subtab'), function (b) {
        b.setAttribute('aria-selected', String(b === btn));
      });
      Array.prototype.forEach.call(region.querySelectorAll('[data-subpanel]'), function (panel) {
        panel.hidden = panel.id !== btn.getAttribute('aria-controls');
      });
    });
  });
})();
