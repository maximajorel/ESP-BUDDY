#include "admin_session_store.h"

#include <Preferences.h>

#include "admin_session_record.h"

namespace {

Preferences s_prefs;
bool s_ready = false;

// Una clave por ranura: si un registro quedó a medias, la ranura se rechaza sola
// y las otras tres siguen sirviendo. El nombre lo arma `adminSessionKeyFor`, que
// se prueba en host: una clave de más de 15 caracteres no se escribiría nunca.
char kKey[16];

void keyFor(int slot) {
  if (!adminSessionKeyFor(slot, kKey, sizeof(kKey))) kKey[0] = '\0';
}

}  // namespace

void adminSessionStoreBegin() {
  s_ready = s_prefs.begin(ADMIN_SESSION_NS, false);
}

bool adminSessionStoreLoad(AdminSessionTable *table) {
  if (!table) return false;
  adminSessionReset(table);
  if (!s_ready) return false;
  bool any = false;
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    keyFor(i);
    const String value = s_prefs.getString(kKey, "");
    AdminSession slot;
    if (!adminSessionRecordDecode(value.c_str(), &slot)) continue;
    table->slots[i] = slot;
    any = true;
  }
  return any;
}

void adminSessionStoreSave(const AdminSessionTable *table) {
  if (!table || !s_ready) return;
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    keyFor(i);
    char text[ADMIN_SESSION_RECORD_CAP];
    if (adminSessionRecordEncode(&table->slots[i], text, sizeof(text))) {
      s_prefs.putString(kKey, text);
    } else {
      s_prefs.remove(kKey);
    }
  }
}

void adminSessionStoreClear() {
  if (!s_ready) return;
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    keyFor(i);
    s_prefs.remove(kKey);
  }
}
