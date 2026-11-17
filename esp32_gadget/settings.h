// Persistent device settings in NVS.
//
// Everything the user can change survives a power cycle: credentials, the access
// point name and password, brightness, timezone and the game high score. NVS is
// the only storage the firmware needs, so there is no filesystem to mount.
#pragma once

#include <Arduino.h>

#include "screen_name.h"

#define SETTINGS_SSID_MAX 32
#define SETTINGS_PASS_MAX 64
#define SETTINGS_AP_SSID_MAX 32
#define SETTINGS_AP_PASS_MAX 64

// Factory access point password: eight digits are far easier to type on a phone
// than a word, and WPA2 needs at least eight characters anyway.
#define SETTINGS_AP_PASS_DEFAULT "12345678"
// The first builds shipped this one. It is migrated away on boot so an existing
// owner is not left typing a word-shaped secret.
#define SETTINGS_AP_PASS_LEGACY "luckyseven"

struct Settings {
  char staSsid[SETTINGS_SSID_MAX + 1];
  char staPass[SETTINGS_PASS_MAX + 1];
  char apSsid[SETTINGS_AP_SSID_MAX + 1];
  char apPass[SETTINGS_AP_PASS_MAX + 1];
  int8_t tzOffsetHours;
  uint8_t brightness;   // 0..100
  uint16_t slotHighScore;
  uint32_t gameHighScore;
  bool clock24h;
};

void settingsBegin();
const Settings &settingsGet();
Settings &settingsMutable();

// Writes the whole struct back to NVS. Returns false on a storage error.
bool settingsSave();

// Restores factory defaults; used by the "reset" button in the web panel.
void settingsResetDefaults();

// True when station credentials are present.
bool settingsHasSta();

// How the landscape image is rotated onto the portrait panel. Stored under its
// own NVS key rather than inside the settings blob, so changing the layout of
// the blob never invalidates a user's credentials.
uint8_t settingsGetOrientation();
void settingsSetOrientation(uint8_t value);

// Automatic rotation (follow the accelerometer) or one pinned image, and the
// owner's lock. Each has its own key for the same reason as the orientation, and
// both are read once at boot because the rotation module asks for them every
// tick.
//   mode: ROT_MODE_AUTO, or an ORIENT_* value pinning exactly that image.
uint8_t settingsGetRotationMode();
void settingsSetRotationMode(uint8_t mode);
bool settingsGetRotationLocked();
void settingsSetRotationLocked(bool locked);

// Which sensor axes steer the dodge game, learned from a one-off gesture so it
// works no matter how the board is held. Stored under its own keys for the same
// reason as the orientation.
struct SteerCalibration {
  uint8_t latAxis;   // accelerometer axis that reads the left/right tilt
  uint8_t upAxis;    // accelerometer axis that carries gravity
  int8_t sign;       // -1 inverts the left/right controls
  int8_t signY;      // -1 inverts the up/down controls
  uint8_t gain;      // sensitivity percentage
  bool valid;        // false until a calibration gesture has been recorded
  float baseline;    // neutral reading on latAxis
  float baselineY;   // neutral reading on the axis that reads up/down tilt
};

// The owner's UI accent (gold by default), its own key so the settings blob
// layout never changes. Cached in RAM: the screens read it every frame.
#define SETTINGS_UI_COLOR_DEFAULT 0xFFD700u
uint32_t settingsGetUiColor();
bool settingsSetUiColor(uint32_t rgb);

// Sofía's eye colour (S2): its own NVS key so the settings blob layout never
// changes, cached in RAM because the face asks for it every frame. Only the eyes
// use it; the LED bead stays the bridge's phase light.
uint32_t settingsGetBuddyEyeColor();
bool settingsSetBuddyEyeColor(uint32_t rgb);

// Order the screen picker walks through. Its own NVS key, and generic over the
// count, so neither the settings blob layout nor this file needs to know how many
// screens the UI has.
void settingsGetScreenOrder(uint8_t *out, uint8_t count);
bool settingsSetScreenOrder(const uint8_t *order, uint8_t count);

// Buddy bridge (Sofía): URL and token, under their own keys. The Hermes API key
// never reaches the device; this token only opens the local bridge.
#define SETTINGS_BUDDY_URL_MAX 96
#define SETTINGS_BUDDY_TOKEN_MAX 64
void settingsGetBuddyUrl(char *out, size_t cap);
void settingsGetBuddyToken(char *out, size_t cap);
bool settingsSetBuddyUrl(const char *url);
bool settingsSetBuddyToken(const char *token);

// Owner-chosen name for one screen. Each name lives under its own NVS key so the
// settings blob layout stays untouched; an absent key means "use the built-in
// name". A get that finds nothing returns false and leaves `out` empty, and a
// set with an empty name removes the override instead of storing a blank.
bool settingsGetScreenName(uint8_t index, char *out, size_t cap);
bool settingsSetScreenName(uint8_t index, const char *name);

// Auto-off for the glass and the LED bead. Separate keys, same reason as the
// orientation: the settings blob is versioned by size.
struct ScreenTimeout {
  uint16_t seconds;  // 0 = never
  uint8_t mask;      // bit per screen id; see screen_sleep.h
};
ScreenTimeout settingsGetScreenTimeout();
void settingsSetScreenTimeout(const ScreenTimeout &t);

SteerCalibration settingsGetSteerCalibration();
void settingsSetSteerCalibration(const SteerCalibration &cal);

// Rear LED bead intensity, 0..255. Its own key for the same reason as the
// orientation: the settings blob layout never changes. The default is the level
// the firmware already booted with before the key existed, so an upgraded board
// looks the same until the owner touches it.
#define SETTINGS_RGB_LEVEL_DEFAULT 64
uint8_t settingsGetRgbLevel();
bool settingsSetRgbLevel(uint8_t level);

// LCD backlight, on the same 5..100 scale the web panel edits. Out-of-range
// input is clamped here so neither the panel nor the CONFIG screen duplicates
// the limits, and an in-range value costs no NVS write.
bool settingsSetLcdBrightness(uint8_t percent);
