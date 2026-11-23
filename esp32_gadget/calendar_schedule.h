// Cuándo toca el próximo intento de sincronizar el calendario.
//
// Puro y header-only: la política decide si el dueño ve la agenda dos segundos
// después de arrancar o dos minutos después, así que se prueba en host
// (`tools/calendar_core_test.cpp`) en vez de descubrirla mirando el reloj.
#pragma once

#include <stdint.h>

// Con un sync bueno: quince minutos alcanzan para un calendario personal.
#define CALENDAR_SYNC_REFRESH_MS (15u * 60u * 1000u)
// Sin un sync bueno todavía, el primer intento puede fallar por cosas del
// arranque (el WiFi asentándose, el primer intercambio TLS). Tres intentos
// seguidos y recién ahí el ritmo lento: no se golpea a Google ni se deja al
// dueño dos minutos sin agenda por un tropiezo del primer segundo.
#define CALENDAR_SYNC_BOOT_RETRY_MS (15u * 1000u)
#define CALENDAR_SYNC_BOOT_RETRIES 3
// Sin un sync bueno y con los intentos rápidos agotados: dos minutos, como
// antes, para que una credencial mal escrita no cueste un cuarto de hora.
#define CALENDAR_SYNC_FAILED_MS (2u * 60u * 1000u)

// `everSynced`: alguna vez trajo la agenda. `consecutiveFailures`: intentos
// fallidos desde el último bueno.
static inline uint32_t calendarSyncDelayMs(bool everSynced,
                                           int consecutiveFailures) {
  if (everSynced) return CALENDAR_SYNC_REFRESH_MS;
  if (consecutiveFailures < 0) consecutiveFailures = 0;
  if (consecutiveFailures < CALENDAR_SYNC_BOOT_RETRIES) {
    return CALENDAR_SYNC_BOOT_RETRY_MS;
  }
  return CALENDAR_SYNC_FAILED_MS;
}
