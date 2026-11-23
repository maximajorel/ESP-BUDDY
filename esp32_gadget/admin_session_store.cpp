#include "admin_session_store.h"

#include <Preferences.h>

#include "admin_session_record.h"

namespace {

Preferences s_prefs;
bool s_ready = false;

// Una clave por ranura: si un registro quedó a medias, la ranura se rechaza sola
// y las otras tres siguen sirviendo.
const char *const kKeys[ADMIN_SESSION_MAX] = {"s0", "s1", "s2", "s3"};

}  // namespace

void adminSessionStoreBegin() { s_ready = s_prefs.begin("adminsess", false); }

bool adminSessionStoreLoad(AdminSessionTable *table) {
  if (!table) return false;
  adminSessionReset(table);
  if (!s_ready) return false;
  bool any = false;
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    const String value = s_prefs.getString(kKeys[i], "");
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
    char text[ADMIN_SESSION_RECORD_CAP];
    if (adminSessionRecordEncode(&table->slots[i], text, sizeof(text))) {
      s_prefs.putString(kKeys[i], text);
    } else {
      s_prefs.remove(kKeys[i]);
    }
  }
}

void adminSessionStoreClear() {
  if (!s_ready) return;
  for (int i = 0; i < ADMIN_SESSION_MAX; i++) {
    s_prefs.remove(kKeys[i]);
  }
}
