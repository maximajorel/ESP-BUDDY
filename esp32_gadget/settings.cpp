#include "settings.h"

#include <Preferences.h>
#include <esp_mac.h>

#include "buddy_face.h"
#include "config_value.h"
#include "display.h"
#include "rotation_policy.h"
#include "screen_sleep.h"

static Preferences s_prefs;
// Cached UI accent: read once at boot, updated by the panel, never from NVS on
// a draw (the screens ask for it every frame).
static uint32_t s_uiColor = SETTINGS_UI_COLOR_DEFAULT;
// Cached the same way: the face reads it on every draw.
static uint32_t s_buddyEyeColor = BUDDY_EYE_COLOR_DEFAULT;
static Settings s_settings;
static bool s_loaded = false;

static void copyInto(char *dst, size_t cap, const char *src) {
  if (!src) src = "";
  size_t n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

static void deriveApSsid(char *out, size_t cap) {
  // Last three bytes of the factory MAC keep the default name unique between
  // boards, which matters as soon as two of them sit on the same table.
  // esp_read_mac reads the eFuse directly: WiFi.macAddress() returns zeros until
  // the radio has actually started, which produced a useless -000000 default.
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  snprintf(out, cap, "ESP32-GADGET-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

void settingsResetDefaults() {
  Settings &s = s_settings;
  memset(&s, 0, sizeof(s));
  s.staSsid[0] = '\0';
  s.staPass[0] = '\0';
  deriveApSsid(s.apSsid, sizeof(s.apSsid));
  copyInto(s.apPass, sizeof(s.apPass), SETTINGS_AP_PASS_DEFAULT);
  s.tzOffsetHours = -3;  // Tucuman, Argentina: no daylight saving
  s.brightness = 90;
  s.slotHighScore = 0;
  s.gameHighScore = 0;
  s.clock24h = true;
}

void settingsBegin() {
  settingsResetDefaults();

  if (!s_prefs.begin("gadget", false)) return;

  s_uiColor = s_prefs.getUInt("uiClr", SETTINGS_UI_COLOR_DEFAULT) & 0xFFFFFFu;
  s_buddyEyeColor =
      s_prefs.getUInt("bdEye", BUDDY_EYE_COLOR_DEFAULT) & 0xFFFFFFu;

  size_t got = s_prefs.getBytesLength("cfg");
  if (got == sizeof(Settings)) {
    s_prefs.getBytes("cfg", &s_settings, sizeof(Settings));
    // A short read or an older layout leaves a zeroed struct; fall back to the
    // defaults rather than booting with an empty name. The -000000 suffix is
    // also repaired, because earlier builds stored it before the MAC was
    // readable.
    if (s_settings.apSsid[0] == '\0' ||
        strcmp(s_settings.apSsid + strlen(s_settings.apSsid) - 7, "-000000") == 0) {
      deriveApSsid(s_settings.apSsid, sizeof(s_settings.apSsid));
    }
    if (s_settings.brightness > 100) s_settings.brightness = 90;
    // Migration: everything up to this build used a word-like password.
    if (strcmp(s_settings.apPass, SETTINGS_AP_PASS_LEGACY) == 0) {
      copyInto(s_settings.apPass, sizeof(s_settings.apPass),
               SETTINGS_AP_PASS_DEFAULT);
      settingsSave();
    }
    if (s_settings.tzOffsetHours < -12 || s_settings.tzOffsetHours > 14) {
      s_settings.tzOffsetHours = -3;
    }
  } else {
    settingsSave();
  }
  s_loaded = true;
}

bool settingsSave() {
  // The struct is stored whole, so the string fields are re-terminated first in
  // case a caller filled them without the room for a terminator.
  s_settings.staSsid[SETTINGS_SSID_MAX] = '\0';
  s_settings.staPass[SETTINGS_PASS_MAX] = '\0';
  s_settings.apSsid[SETTINGS_AP_SSID_MAX] = '\0';
  s_settings.apPass[SETTINGS_AP_PASS_MAX] = '\0';
  return s_prefs.putBytes("cfg", &s_settings, sizeof(Settings)) ==
         sizeof(Settings);
}

const Settings &settingsGet() { return s_settings; }
Settings &settingsMutable() { return s_settings; }

bool settingsHasSta() { return s_settings.staSsid[0] != '\0'; }

// Kept out of the settings blob on purpose: the blob is versioned only by its
// size, so adding a field would make every stored credential unreadable.
uint8_t settingsGetOrientation() {
  uint8_t value = s_prefs.getUChar("orient", ORIENT_DEFAULT);
  return value > 3 ? ORIENT_DEFAULT : value;
}

void settingsSetOrientation(uint8_t value) {
  if (value > 3) value = ORIENT_DEFAULT;
  s_prefs.putUChar("orient", value);
}

// Automatic rotation. The mode byte is the same numbering `orient` always used
// plus ROT_MODE_AUTO, so a board upgraded from the build that only had the
// orientation keeps working: with no `rotMode` key the image follows the sensor
// and the first usable sample anchors it to the rotation already stored.
uint8_t settingsGetRotationMode() {
  if (!s_loaded && !s_prefs.begin("gadget", true)) return ROT_MODE_AUTO;
  uint8_t value = s_prefs.getUChar("rotMode", ROT_MODE_AUTO);
  return value <= ROT_MODE_AUTO ? value : ROT_MODE_AUTO;
}

void settingsSetRotationMode(uint8_t mode) {
  if (!s_loaded && !s_prefs.begin("gadget", false)) return;
  if (mode > ROT_MODE_AUTO) mode = ROT_MODE_AUTO;
  s_prefs.putUChar("rotMode", mode);
}

bool settingsGetRotationLocked() {
  if (!s_loaded && !s_prefs.begin("gadget", true)) return false;
  return s_prefs.getBool("rotLock", false);
}

void settingsSetRotationLocked(bool locked) {
  if (!s_loaded && !s_prefs.begin("gadget", false)) return;
  s_prefs.putBool("rotLock", locked);
}

uint32_t settingsGetUiColor() { return s_uiColor; }

bool settingsSetUiColor(uint32_t rgb) {
  s_uiColor = rgb & 0xFFFFFFu;
  if (!s_loaded && !s_prefs.begin("gadget", false)) return false;
  return s_prefs.putUInt("uiClr", s_uiColor);
}

uint32_t settingsGetBuddyEyeColor() { return s_buddyEyeColor; }

bool settingsSetBuddyEyeColor(uint32_t rgb) {
  s_buddyEyeColor = rgb & 0xFFFFFFu;
  if (!s_loaded && !s_prefs.begin("gadget", false)) return false;
  return s_prefs.putUInt("bdEye", s_buddyEyeColor);
}

// The bead's intensity used to live only in RAM, so every reboot dropped it back
// to the firmware default. It now has its own key, read once at boot and written
// by both the CONFIG screen and the panel.
uint8_t settingsGetRgbLevel() {
  if (!s_loaded && !s_prefs.begin("gadget", true)) {
    return SETTINGS_RGB_LEVEL_DEFAULT;
  }
  return s_prefs.getUChar("rgbLvl", SETTINGS_RGB_LEVEL_DEFAULT);
}

bool settingsSetRgbLevel(uint8_t level) {
  if (!s_loaded && !s_prefs.begin("gadget", false)) return false;
  return s_prefs.putUChar("rgbLvl", level) == 1;
}

bool settingsSetLcdBrightness(uint8_t percent) {
  uint8_t value = cfgClamp((int)percent, CFG_LCD_MIN, CFG_LCD_MAX);
  if (s_settings.brightness == value) return true;
  s_settings.brightness = value;
  return settingsSave();
}

// Clock (0) and network (3) are the screens that are just ambient information;
// slot, game and music stay dark-free because they are being used.
#define SCREEN_TIMEOUT_DEFAULT_SEC 120
#define SCREEN_TIMEOUT_DEFAULT_MASK 0x09

ScreenTimeout settingsGetScreenTimeout() {
  ScreenTimeout t;
  t.seconds = s_prefs.getUShort("slpSec", SCREEN_TIMEOUT_DEFAULT_SEC);
  t.mask = s_prefs.getUChar("slpMask", SCREEN_TIMEOUT_DEFAULT_MASK);
  if (t.seconds > SCREEN_SLEEP_MAX_SEC) t.seconds = SCREEN_TIMEOUT_DEFAULT_SEC;
  return t;
}

void settingsSetScreenTimeout(const ScreenTimeout &t) {
  uint16_t seconds = t.seconds > SCREEN_SLEEP_MAX_SEC ? SCREEN_SLEEP_MAX_SEC
                                                      : t.seconds;
  s_prefs.putUShort("slpSec", seconds);
  s_prefs.putUChar("slpMask", t.mask);
}

SteerCalibration settingsGetSteerCalibration() {
  SteerCalibration cal;
  cal.latAxis = s_prefs.getUChar("stLat", 0);
  cal.upAxis = s_prefs.getUChar("stUp", 2);
  cal.sign = (int8_t)s_prefs.getChar("stSign", 1);
  cal.signY = (int8_t)s_prefs.getChar("stSignY", 1);
  cal.gain = s_prefs.getUChar("stGain", 100);
  cal.valid = s_prefs.getBool("stValid", false);
  cal.baseline = s_prefs.getFloat("stBase", 0.0f);
  cal.baselineY = s_prefs.getFloat("stBaseY", 0.0f);
  if (cal.latAxis > 2) cal.latAxis = 0;
  if (cal.upAxis > 2) cal.upAxis = 2;
  if (cal.gain == 0) cal.gain = 100;
  if (cal.sign == 0) cal.sign = 1;
  if (cal.signY == 0) cal.signY = 1;
  return cal;
}

void settingsSetSteerCalibration(const SteerCalibration &cal) {
  s_prefs.putUChar("stLat", cal.latAxis);
  s_prefs.putUChar("stUp", cal.upAxis);
  s_prefs.putChar("stSign", cal.sign);
  s_prefs.putChar("stSignY", cal.signY);
  s_prefs.putUChar("stGain", cal.gain);
  s_prefs.putBool("stValid", cal.valid);
  s_prefs.putFloat("stBase", cal.baseline);
  s_prefs.putFloat("stBaseY", cal.baselineY);
}

// --- screen order -----------------------------------------------------------

void settingsGetScreenOrder(uint8_t *out, uint8_t count) {
  if (!out || count == 0) return;
  for (uint8_t i = 0; i < count; i++) out[i] = i;  // default: natural order
  if (!s_loaded && !s_prefs.begin("gadget", true)) return;
  uint8_t storage[8];
  if (count > sizeof(storage)) return;
  size_t got = s_prefs.getBytesLength("order");
  if (got == (size_t)count) {
    if (s_prefs.getBytes("order", storage, count) != count) return;
  } else if (count > 1 && got == (size_t)count - 1) {
    // Upgrade from the firmware that had one screen fewer: the new screen goes
    // last, the owner's order survives, and the migrated order is persisted so
    // the next boot does not migrate again.
    if (s_prefs.getBytes("order", storage, got) != got) return;
    storage[count - 1] = (uint8_t)(count - 1);
    s_prefs.putBytes("order", storage, count);
  } else {
    return;
  }
  for (uint8_t i = 0; i < count; i++) out[i] = storage[i];
}

bool settingsSetScreenOrder(const uint8_t *order, uint8_t count) {
  if (!order || count == 0 || count > 8) return false;
  if (!s_loaded && !s_prefs.begin("gadget", false)) return false;
  return s_prefs.putBytes("order", order, count) == count;
}

// --- buddy bridge -----------------------------------------------------------

// Where the local bridge for Sofía listens when the owner has not changed it.
#define BUDDY_URL_DEFAULT "http://192.168.100.64:8787"

void settingsGetBuddyUrl(char *out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (!s_loaded && !s_prefs.begin("gadget", true)) return;
  String value = s_prefs.getString("bdUrl", BUDDY_URL_DEFAULT);
  snprintf(out, cap, "%s", value.c_str());
}

void settingsGetBuddyToken(char *out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = '\0';
  if (!s_loaded && !s_prefs.begin("gadget", true)) return;
  String value = s_prefs.getString("bdTok", "");
  snprintf(out, cap, "%s", value.c_str());
}

bool settingsSetBuddyUrl(const char *url) {
  if (!url) return false;
  if (!s_loaded && !s_prefs.begin("gadget", false)) return false;
  if (strlen(url) > SETTINGS_BUDDY_URL_MAX) return false;
  return s_prefs.putString("bdUrl", url) > 0;
}

bool settingsSetBuddyToken(const char *token) {
  if (!s_loaded && !s_prefs.begin("gadget", false)) return false;
  if (!token || !*token) {
    // Clearing the token is how the owner unpairs the face.
    s_prefs.remove("bdTok");
    return true;
  }
  if (strlen(token) > SETTINGS_BUDDY_TOKEN_MAX) return false;
  return s_prefs.putString("bdTok", token) > 0;
}

// --- screen names -----------------------------------------------------------

// "sn0".."sn9": short enough for the 15-byte NVS key limit and independent per
// screen, so renaming one never rewrites the others.
static void screenNameKey(uint8_t index, char *out, size_t cap) {
  snprintf(out, cap, "sn%u", (unsigned)index);
}

bool settingsGetScreenName(uint8_t index, char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = '\0';
  if (!s_loaded && !s_prefs.begin("gadget", true)) return false;
  char key[8];
  screenNameKey(index, key, sizeof(key));
  // Stored as raw bytes, not as a String: the value is at most
  // SCREEN_NAME_MAX bytes by construction and this path runs on every boot.
  size_t got = s_prefs.getBytesLength(key);
  if (got == 0 || got >= cap) return false;
  if (s_prefs.getBytes(key, out, got) != got) return false;
  out[got] = '\0';
  return out[0] != '\0';
}

bool settingsSetScreenName(uint8_t index, const char *name) {
  if (!s_loaded && !s_prefs.begin("gadget", false)) return false;
  char key[8];
  screenNameKey(index, key, sizeof(key));
  if (!name || !*name) {
    s_prefs.remove(key);
    return true;
  }
  size_t n = strlen(name);
  if (n > SCREEN_NAME_MAX) return false;
  return s_prefs.putBytes(key, name, n) == n;
}
