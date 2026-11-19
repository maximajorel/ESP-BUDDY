// Should the access point be radiating right now?
//
// The device can run both radios at once, and for a long time it did: the AP
// stayed up for the whole life of the board even when the station link was
// already carrying the panel on the home network. An AP with zero clients still
// beacons several times a second at full power, and on a sealed S3 board that is
// heat for nothing.
//
// The rule has one job: never leave the owner without a way in. While the
// station link is down or absent the AP is the only door, so it must be up. It
// only goes quiet once the home network has been carrying traffic for a while,
// which keeps a flapping router from switching the radio back and forth, and the
// owner can always override it from the console.
//
// Pure so `tools/net_ap_policy_test.cpp` pins it without a radio.
#pragma once

#include <stdint.h>

// `auto` follows the station link. The two overrides are for the owner, not for
// the firmware: `on` gets the AP back when the router's LAN is unreachable, and
// `off` silences it while debugging. They are deliberately not persisted - a
// forced-off AP that survived a reboot would be a lockout.
#define NET_AP_AUTO 0
#define NET_AP_FORCE_ON 1
#define NET_AP_FORCE_OFF 2

// How long the station link has to stay up before the AP is allowed to go quiet.
// Long enough to cover a DHCP lease and a flap, short enough that the panel moves
// to the LAN address while the owner is still looking at the screen.
#define NET_AP_HOLD_MS 30000UL

static inline bool netApShouldRun(uint8_t mode, bool staConnected,
                                  uint32_t staConnectedMs) {
  if (mode == NET_AP_FORCE_ON) return true;
  if (mode == NET_AP_FORCE_OFF) return false;
  if (!staConnected) return true;
  // A link this young has not earned the silence yet: the AP stays up until the
  // station link is proven, not merely reported.
  return staConnectedMs < NET_AP_HOLD_MS;
}

// The console types a word, the firmware stores a number.
#define NET_AP_MODE_AUTO_NAME "auto"
#define NET_AP_MODE_ON_NAME "on"
#define NET_AP_MODE_OFF_NAME "off"

static inline const char *netApModeName(uint8_t mode) {
  if (mode == NET_AP_FORCE_ON) return NET_AP_MODE_ON_NAME;
  if (mode == NET_AP_FORCE_OFF) return NET_AP_MODE_OFF_NAME;
  return NET_AP_MODE_AUTO_NAME;
}
