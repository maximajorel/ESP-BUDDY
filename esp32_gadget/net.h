// WiFi, NTP and the web admin panel.
//
// The device runs both radios at once: an access point so a phone or laptop can
// always reach the admin panel and read the connection data off the screen, and
// an optional station link to the home network for NTP and internet time.
#pragma once

#include <Arduino.h>

#define NET_AP_IP "192.168.4.1"

// How the attempt to join the home network is going. "Connecting forever" gives
// the owner nothing to act on, which is the worst possible answer for a device
// whose whole point is being set up from a phone.
#define NET_STA_IDLE 0
#define NET_STA_CONNECTING 1
#define NET_STA_OK 2
#define NET_STA_FAILED 3

struct NetStatus {
  bool apActive;
  uint8_t staState;
  int wifiStatusCode;
  // Reason code from the last STA disconnect. This is what separates a wrong
  // password (handshake timeout) from a router refusing the association
  // (for example a MAC allow-list), which look identical from WiFi.status().
  int disconnectReason;
  bool staConfigured;
  bool staConnected;
  char staSsid[33];
  int32_t staRssi;
  char staIp[16];
  char apSsid[33];
  uint8_t apClients;
  bool timeSynced;
};

void netBegin();

// Services HTTP clients and retries the station link. Call every loop.
void netLoop();

const NetStatus &netStatus();

// Applies the configured UTC offset and (re)starts SNTP.
void netApplyTimeZone();

// Persists station credentials and reconnects. Returns false when the
// credentials cannot be used.
bool netSetSta(const char *ssid, const char *pass);

// Restarts the access point. Returns false on invalid input.
bool netSetAp(const char *ssid, const char *pass);

// Forgets the home network and drops the station link. Without this, a typo in
// the password would be unrecoverable from the panel: the only remaining way
// out would be a factory reset, which also erases the game records.
void netClearSta();

// JSON array of nearby networks into `out`.
void netScanJson(String &out);

void netRestart();

// Why the last credential change failed, for the panel to display.
const char *netLastError();

// One HTTP API result: status code, JSON body, and whether the device has to
// restart after the response is sent.
struct ApiResult {
  int code = 200;
  String body;
  bool restart = false;
};

// Pure request handler: every endpoint is a function of method, path and body.
// The HTTP server and the serial console both call this, so the API can be
// verified without a network path to the device's access point.
ApiResult netHandleApi(const char *method, const char *path, const String &body);

// The web panel is baked into the firmware as byte arrays. These accessors let
// the serial console prove the shipped copy matches the files on disk; a hash
// mismatch is the difference between "I edited the HTML" and "the device serves
// the edited HTML".
uint32_t netAssetCount();
const char *netAssetPath(uint32_t index);
uint32_t netAssetLength(uint32_t index);
uint32_t netAssetHash(uint32_t index);
