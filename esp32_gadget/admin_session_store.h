// La sesión del panel guardada en NVS, para que el popover de la bandeja no pida
// usuario y clave después de cada reboot del equipo.
//
// Sólo se guardan las sesiones con hora absoluta (`openedAtEpoch`): una abierta
// sin NTP no tiene con qué demostrar que sigue viva, así que vale dentro de este
// arranque y nada más. El formato y su parser son puros
// (`admin_session_record.h`) y se prueban en host; acá queda sólo el NVS.
#pragma once

#include "admin_auth.h"

void adminSessionStoreBegin();

// Llena `table` con las sesiones guardadas. Devuelve true si encontró alguna.
// Una ranura ilegible queda vacía: un blob de una versión anterior no puede
// dejar al dueño afuera del panel, lo deja entrando una vez.
bool adminSessionStoreLoad(AdminSessionTable *table);

// Escribe las sesiones que tengan hora absoluta y borra las demás ranuras.
void adminSessionStoreSave(const AdminSessionTable *table);

// Olvida todo (reset de fábrica, "salir de todos los dispositivos").
void adminSessionStoreClear();
