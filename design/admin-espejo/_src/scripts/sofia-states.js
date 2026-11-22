/* Simulate a Sofía state: single choice, like a radio group made of stickers. */
(function () {
  'use strict';
  var group = document.getElementById('buddyStates');
  if (!group) return;

  function select(active) {
    Array.prototype.forEach.call(group.querySelectorAll('.chip'), function (chip) {
      chip.setAttribute('aria-pressed', String(chip === active));
    });
  }

  group.addEventListener('click', function (event) {
    var chip = event.target.closest('.chip');
    if (chip) select(chip);
  });

  var clear = document.getElementById('buddyStateClear');
  if (clear) clear.addEventListener('click', function () { select(null); });
})();
