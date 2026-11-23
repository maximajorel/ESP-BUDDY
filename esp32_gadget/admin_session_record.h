// El formato con el que una sesión del panel se guarda y se vuelve a leer.
//
// Puro y header-only: lo que se escribe en NVS es un token bearer, así que el
// parser es la parte que hay que probar en host (`tools/admin_auth_test.cpp`) y
// no descubrir con una sesión que de golpe vale o no vale después de un reboot.
//
// El registro es `token,hora`:
//   - `token`: exactamente ADMIN_TOKEN_HEX caracteres hexadecimales en
//     minúscula, que es lo que produce `adminHexEncode`.
//   - `hora`: epoch en segundos del momento en que se abrió. Cero no se guarda:
//     una sesión abierta sin reloj no tiene con qué demostrar que sigue viva.
//
// Cualquier otra cosa que aparezca en NVS (un blob de una versión anterior, una
// escritura a medias) se rechaza, que es lo mismo que decir "no hay sesión": el
// dueño vuelve a entrar al panel y listo.
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "admin_auth.h"

// Token (64) + coma + epoch de 19 dígitos + terminador, con aire.
#define ADMIN_SESSION_RECORD_CAP (ADMIN_TOKEN_HEX + 1 + 20 + 8)

// El namespace y las claves de NVS. Los dos límites son del sistema de archivos
// interno de NVS: un nombre de más de 15 caracteres o un valor de más de 1984
// bytes se rechazan al escribir y la sesión simplemente no se guardaría, sin
// error visible. Por eso se fijan acá, con test, y no se escriben a mano en el
// módulo que toca NVS.
#define ADMIN_SESSION_NS "adminsess"
#define ADMIN_SESSION_NS_MAX 15
#define ADMIN_SESSION_VALUE_MAX 1984

// La clave de una ranura: "s0".."s3". Devolver false para una ranura que no
// existe es más útil que generar una clave inventada.
static inline bool adminSessionKeyFor(int slot, char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  if (slot < 0 || slot >= ADMIN_SESSION_MAX) return false;
  const int n = snprintf(out, cap, "s%d", slot);
  return n > 0 && (size_t)n < cap;
}

static inline bool adminSessionTokenValid(const char *token) {
  if (!token) return false;
  if (strnlen(token, ADMIN_TOKEN_CAP) != ADMIN_TOKEN_HEX) return false;
  for (int i = 0; i < ADMIN_TOKEN_HEX; i++) {
    const char c = token[i];
    const bool digit = c >= '0' && c <= '9';
    const bool lower = c >= 'a' && c <= 'f';
    const bool upper = c >= 'A' && c <= 'F';
    if (!digit && !lower && !upper) return false;
  }
  return true;
}

// Escribe `token,hora` de una sesión que tenga hora absoluta.
static inline bool adminSessionRecordEncode(const AdminSession *session,
                                            char *out, size_t cap) {
  if (!session || !out || cap == 0) return false;
  out[0] = '\0';
  if (!session->openedAtEpoch || session->openedAtEpoch < ADMIN_CLOCK_MIN_EPOCH) {
    return false;
  }
  if (!adminSessionTokenValid(session->token)) return false;
  const int n = snprintf(out, cap, "%s,%lld", session->token,
                         (long long)session->openedAtEpoch);
  return n > 0 && (size_t)n < cap;
}

// Lee un registro y lo deja como una ranura usada. `out` queda en cero si el
// texto no es un registro válido.
static inline bool adminSessionRecordDecode(const char *text, AdminSession *out) {
  if (!text || !out) return false;
  memset(out, 0, sizeof(*out));

  const char *comma = strchr(text, ',');
  if (!comma) return false;
  const size_t tokenLen = (size_t)(comma - text);
  if (tokenLen != ADMIN_TOKEN_HEX) return false;

  char token[ADMIN_TOKEN_CAP];
  memcpy(token, text, tokenLen);
  token[tokenLen] = '\0';
  if (!adminSessionTokenValid(token)) return false;

  const char *digits = comma + 1;
  if (!*digits) return false;
  int64_t epoch = 0;
  for (const char *p = digits; *p; p++) {
    if (*p < '0' || *p > '9') return false;
    if (epoch > 100000000000000LL) return false;  // absurdo: no es un epoch
    epoch = epoch * 10 + (*p - '0');
  }
  if (epoch < ADMIN_CLOCK_MIN_EPOCH) return false;

  // Se normaliza a minúsculas: es lo que produce `adminHexEncode` y lo que manda
  // la cookie. Un registro en mayúsculas se leería, se guardaría y después nunca
  // coincidiría con el token del navegador.
  for (size_t i = 0; i < tokenLen; i++) {
    if (token[i] >= 'A' && token[i] <= 'F') token[i] = (char)(token[i] - 'A' + 'a');
  }
  snprintf(out->token, sizeof(out->token), "%s", token);
  out->openedAtEpoch = epoch;
  out->openedMs = 0;
  out->used = true;
  return true;
}
