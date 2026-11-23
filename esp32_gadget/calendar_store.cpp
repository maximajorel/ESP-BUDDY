#include "calendar_store.h"

#include <Preferences.h>
#include <string.h>

namespace {

Preferences s_prefs;
bool s_ready = false;
char s_clientId[CALENDAR_CLIENT_ID_MAX + 1];
char s_clientSecret[CALENDAR_CLIENT_SECRET_MAX + 1];
char s_refresh[CALENDAR_TOKEN_MAX + 1];
char s_selectedId[CALENDAR_ID_MAX + 1];
char s_selectedName[CALENDAR_NAME_MAX + 1];
int s_fallbackLead = CALENDAR_FALLBACK_LEAD_DEFAULT;
int s_grace = CALENDAR_GRACE_DEFAULT;
// Whether the grant saved in flash was asked for with the write scope. A grant
// made by a firmware that only knew `calendar.readonly` is still a valid refresh
// token, but Google answers 403 to every write: the flag is how the panel can say
// "reconectá" before the owner tries to save and loses the edit.
bool s_writeScope = false;

// Printable ASCII with no spaces: every value here either travels in a form body
// or is compared against an id Google sent. A newline pasted with the secret is
// the failure that looks like "invalid_client" and is impossible to diagnose
// from the panel.
bool printable(const char *value, size_t minLen, size_t maxLen) {
  if (!value) return false;
  const size_t len = strlen(value);
  if (len < minLen || len > maxLen) return false;
  for (size_t i = 0; i < len; i++) {
    const unsigned char c = (unsigned char)value[i];
    if (c <= 0x20 || c >= 0x7F) return false;
  }
  return true;
}

void loadString(Preferences &prefs, const char *key, char *out, size_t cap) {
  out[0] = '\0';
  String value = prefs.getString(key, "");
  if (value.length() < cap) {
    snprintf(out, cap, "%s", value.c_str());
  }
}

}  // namespace

void calendarStoreBegin() {
  s_clientId[0] = '\0';
  s_clientSecret[0] = '\0';
  s_refresh[0] = '\0';
  s_selectedId[0] = '\0';
  s_selectedName[0] = '\0';
  s_fallbackLead = CALENDAR_FALLBACK_LEAD_DEFAULT;
  s_grace = CALENDAR_GRACE_DEFAULT;
  s_writeScope = false;
  s_ready = s_prefs.begin("calendar", false);
  if (!s_ready) return;

  loadString(s_prefs, "cid", s_clientId, sizeof(s_clientId));
  loadString(s_prefs, "csec", s_clientSecret, sizeof(s_clientSecret));
  loadString(s_prefs, "rt", s_refresh, sizeof(s_refresh));
  loadString(s_prefs, "sel", s_selectedId, sizeof(s_selectedId));
  loadString(s_prefs, "selName", s_selectedName, sizeof(s_selectedName));
  int lead = s_prefs.getInt("fbLead", CALENDAR_FALLBACK_LEAD_DEFAULT);
  if (lead >= CALENDAR_FALLBACK_LEAD_MIN && lead <= CALENDAR_FALLBACK_LEAD_MAX) {
    s_fallbackLead = lead;
  }
  int grace = s_prefs.getInt("grace", CALENDAR_GRACE_DEFAULT);
  if (grace >= CALENDAR_GRACE_MIN && grace <= CALENDAR_GRACE_MAX) {
    s_grace = grace;
  }
  s_writeScope = s_prefs.getUChar("wrScope", 0) != 0;
}

const char *calendarStoreClientId() { return s_clientId; }
const char *calendarStoreClientSecret() { return s_clientSecret; }
const char *calendarStoreRefreshToken() { return s_refresh; }
const char *calendarStoreSelectedId() { return s_selectedId; }
const char *calendarStoreSelectedName() { return s_selectedName; }

bool calendarStoreSetClientId(const char *clientId) {
  if (!printable(clientId, 8, CALENDAR_CLIENT_ID_MAX)) return false;
  snprintf(s_clientId, sizeof(s_clientId), "%s", clientId);
  if (s_ready) s_prefs.putString("cid", s_clientId);
  return true;
}

bool calendarStoreSetClientSecret(const char *clientSecret) {
  if (!printable(clientSecret, 8, CALENDAR_CLIENT_SECRET_MAX)) return false;
  snprintf(s_clientSecret, sizeof(s_clientSecret), "%s", clientSecret);
  if (s_ready) s_prefs.putString("csec", s_clientSecret);
  return true;
}

bool calendarStoreSetRefreshToken(const char *token) {
  if (!printable(token, 8, CALENDAR_TOKEN_MAX)) return false;
  snprintf(s_refresh, sizeof(s_refresh), "%s", token);
  if (s_ready) s_prefs.putString("rt", s_refresh);
  return true;
}

bool calendarStoreWriteScope() { return s_writeScope; }

void calendarStoreSetWriteScope(bool granted) {
  s_writeScope = granted;
  if (s_ready) s_prefs.putUChar("wrScope", granted ? 1 : 0);
}

bool calendarStoreSetSelected(const char *id, const char *name) {
  if (!printable(id, 1, CALENDAR_ID_MAX)) return false;
  snprintf(s_selectedId, sizeof(s_selectedId), "%s", id);
  snprintf(s_selectedName, sizeof(s_selectedName), "%s", name ? name : id);
  if (s_ready) {
    s_prefs.putString("sel", s_selectedId);
    s_prefs.putString("selName", s_selectedName);
  }
  return true;
}

int calendarStoreFallbackLeadMin() { return s_fallbackLead; }

bool calendarStoreSetFallbackLeadMin(int minutes) {
  if (minutes < CALENDAR_FALLBACK_LEAD_MIN ||
      minutes > CALENDAR_FALLBACK_LEAD_MAX) {
    return false;
  }
  s_fallbackLead = minutes;
  if (s_ready) s_prefs.putInt("fbLead", minutes);
  return true;
}

int calendarStoreGraceMin() { return s_grace; }

bool calendarStoreSetGraceMin(int minutes) {
  if (minutes < CALENDAR_GRACE_MIN || minutes > CALENDAR_GRACE_MAX) return false;
  s_grace = minutes;
  if (s_ready) s_prefs.putInt("grace", minutes);
  return true;
}

bool calendarStoreConfigured() {
  return s_clientId[0] && s_clientSecret[0];
}

bool calendarStoreAuthorized() { return s_refresh[0] != '\0'; }

void calendarStoreForgetToken() {
  s_refresh[0] = '\0';
  // A token is gone, so is the grant that carried the scope.
  s_writeScope = false;
  if (!s_ready) return;
  s_prefs.remove("rt");
  s_prefs.remove("wrScope");
}

void calendarStoreForgetAll() {
  s_clientId[0] = '\0';
  s_clientSecret[0] = '\0';
  s_refresh[0] = '\0';
  s_selectedId[0] = '\0';
  s_selectedName[0] = '\0';
  s_fallbackLead = CALENDAR_FALLBACK_LEAD_DEFAULT;
  s_grace = CALENDAR_GRACE_DEFAULT;
  s_writeScope = false;
  if (!s_ready) return;
  s_prefs.remove("cid");
  s_prefs.remove("csec");
  s_prefs.remove("rt");
  s_prefs.remove("sel");
  s_prefs.remove("selName");
  s_prefs.remove("fbLead");
  s_prefs.remove("grace");
  s_prefs.remove("wrScope");
}
