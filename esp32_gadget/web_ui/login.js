/* Login for the admin panel.
 *
 * The device answers with an HttpOnly cookie, so this script never sees or
 * stores a token: it only posts the credentials and follows the redirect. The
 * only place fetch() is called is post(), so there is one error path.
 */
(function () {
  'use strict';

  function $(id) { return document.getElementById(id); }

  // Where to land after logging in. Only a same-site absolute path is accepted:
  // "/admin/pantalla" is fine, "//evil.example" is not.
  function nextTarget() {
    var raw = '';
    try {
      raw = new URLSearchParams(window.location.search).get('next') || '';
    } catch (err) {
      raw = '';
    }
    if (!raw || raw.charAt(0) !== '/' || raw.charAt(1) === '/') return '/admin';
    // Landing back on the login page would loop; the panel is the sane default.
    if (raw.indexOf('/login') === 0) return '/admin';
    return raw;
  }

  function post(path, body) {
    return fetch(path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body)
    }).then(function (response) {
      return response.text().then(function (text) {
        var parsed;
        try {
          parsed = text ? JSON.parse(text) : {};
        } catch (err) {
          throw new Error('El equipo respondió algo inesperado');
        }
        if (!response.ok || (parsed && parsed.ok === false)) {
          throw new Error(parsed && parsed.error ? parsed.error : 'Error ' + response.status);
        }
        return parsed;
      });
    });
  }

  function showError(message) {
    var el = $('loginError');
    el.textContent = message;
    el.hidden = false;
  }

  function clearError() {
    var el = $('loginError');
    el.textContent = '';
    el.hidden = true;
  }

  // Already logged in: the login page is not a place to stay.
  function skipIfLoggedIn() {
    fetch('/api/session', { headers: { Accept: 'application/json' } })
      .then(function (response) { return response.json(); })
      .then(function (state) {
        if (state && state.authenticated) window.location.replace(nextTarget());
      })
      .catch(function () { /* not fatal: the form still works */ });
  }

  function init() {
    var form = $('loginForm');
    if (!form) return;

    skipIfLoggedIn();

    form.addEventListener('submit', function (event) {
      event.preventDefault();
      clearError();
      var button = $('loginBtn');
      var original = button.textContent;
      button.disabled = true;
      button.textContent = 'Entrando…';

      post('/api/session', {
        user: $('loginUser').value.trim(),
        pass: $('loginPass').value
      }).then(
        function (result) {
          var target = nextTarget();
          // A default pair is not a usable pair: send the owner straight to the
          // card that changes it, with the banner already waiting.
          if (result && result.mustChange) target = '/admin#cuenta';
          window.location.replace(target);
        },
        function (error) {
          button.disabled = false;
          button.textContent = original;
          $('loginPass').value = '';
          $('loginPass').focus();
          showError(error.message || 'No se pudo entrar');
        }
      );
    });
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
