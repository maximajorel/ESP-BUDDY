/* Drag to reorder the screen list: the grip is the drag source, the row is what
   moves, and the number column follows the new position so the order reads. */
(function () {
  'use strict';
  var list = document.getElementById('orderList');
  if (!list) return;
  var dragging = null;

  function renumber() {
    Array.prototype.forEach.call(list.children, function (li, i) {
      var num = li.querySelector('.num');
      if (num) num.textContent = String(i + 1);
      var input = li.querySelector('input');
      if (input) input.setAttribute('aria-label', 'Nombre de la pantalla ' + (i + 1));
    });
  }

  function clearMarks() {
    Array.prototype.forEach.call(list.children, function (li) {
      li.classList.remove('dragging', 'over');
    });
  }

  list.addEventListener('dragstart', function (event) {
    var grip = event.target.closest('.grip');
    if (!grip) { event.preventDefault(); return; }
    dragging = grip.closest('li');
    dragging.classList.add('dragging');
    if (event.dataTransfer) {
      event.dataTransfer.effectAllowed = 'move';
      event.dataTransfer.setData('text/plain', '');
    }
  });

  list.addEventListener('dragover', function (event) {
    if (!dragging) return;
    event.preventDefault();
    var row = event.target.closest('li');
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
    clearMarks();
    dragging = null;
    renumber();
  });
})();
