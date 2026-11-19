// ESP32 Gadget - seven screens on a Waveshare ESP32-S3-LCD-1.47B.
//
// Screens: CLOCK, SLOT MACHINE, DODGE (ASCII game), NETWORK, MUSIC, BUDDY and
// CONFIG (LCD and rear-LED brightness). One button drives everything: a short
// press acts on the current screen and a one-second hold opens the picker, where
// the owner's order and names apply.
//
// The serial console is a first-class part of the firmware, not debug leftovers.
// The panel has no readable bus, so `dump` (an ASCII render of the framebuffer)
// and `press`/`hold` (synthetic gestures) are how the display and the game can be
// verified without a finger on the board. See INSTALL.md.
#include <Arduino.h>
#include <WiFi.h>

#include "button.h"
#include "console.h"
#include "display.h"
#include "gfx.h"
#include "imu.h"
#include "net.h"
#include "rgb.h"
#include "rotation.h"
#include "buddy_led.h"
#include "screens.h"
#include "settings.h"
#include "spotify.h"

static ButtonFsm s_button;

static void drawSplash() {
  gfxClear(GFX_BLACK);
  gfxTextCentered(26, 3, "ESP32", GFX_WHITE);
  gfxTextCentered(56, 3, "GADGET", gfxHex(0xFFD700));
  gfxHLine(40, 86, DISPLAY_W - 80, gfxHex(0x333333));
  gfxTextCentered(96, 1, "RELOJ TRAGAMONEDAS METEORITOS RED MUSICA SOFIA CONFIG",
                  gfxHex(0x8A8A8A));
  gfxTextCentered(112, 1, "CONECTATE A ESTA RED:", gfxHex(0x7A8CA0));
  gfxTextCentered(124, 2, settingsGet().apSsid, GFX_WHITE);
  gfxTextCentered(146, 1, "MANTENE 1S: MENU DE PANTALLAS", gfxHex(0xFFD700));
  displayPush(gfxBuffer());
}

// --- lifecycle --------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  // A host that closes the port must never wedge the firmware: drop the write
  // instead of blocking on it.
  Serial.setTxTimeoutMs(0);

  // Reading the MAC for the default access point name needs the radio up.
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);

  settingsBegin();

  if (!gfxBegin()) {
    Serial.println("FATAL: no framebuffer (PSRAM missing?)");
  }
  if (!displayBegin()) {
    Serial.println("FATAL: no panel buffer");
  }
  // The mode, the lock and the stored image decide the first frame; the sensor
  // takes over from the first tick on if the owner left it in automatic.
  rotationBegin();
  displaySetBacklight(settingsGet().brightness);

  rgbBegin();
  // The bead level is persisted now, so it is restored before the first frame
  // instead of flashing the firmware default on every boot.
  rgbSetLevel(settingsGetRgbLevel());
  g_imuOk = imuBegin();
  pinMode(BTN_PIN, INPUT_PULLUP);

  drawSplash();

  netBegin();
  spotifyBegin();
  screensBegin();

  Serial.printf("\n%s %s ready\n", "esp32-gadget", "1.0.0");
  Serial.printf("ap=%s  panel=http://%s\n", settingsGet().apSsid, NET_AP_IP);
  Serial.printf("imu=%s  heap=%lu\n", g_imuOk ? "ok" : "missing",
                (unsigned long)ESP.getFreeHeap());
  Serial.println("escribi 'help' para la consola");
}

void loop() {
  uint32_t now = millis();

  bool rawDown = digitalRead(BTN_PIN) == LOW;
  if (consoleSyntheticPressActive(now)) rawDown = true;

  ButtonEvent ev = s_button.update(rawDown, now);
  if (ev.longPress) {
    screensLongPress();
    // Report what actually happened: the picker opening is not a screen change,
    // and printing the old screen name there made the log read like a bug.
    if (screensMenuIsOpen()) {
      Serial.printf("hold -> menu en %s\n", screensMenuName());
    } else {
      Serial.printf("hold -> %s\n", screenName(screensCurrent()));
    }
  }
  if (ev.shortPress) {
    bool inMenu = screensMenuIsOpen();
    screensShortPress();
    if (inMenu) {
      Serial.printf("tap -> menu en %s\n", screensMenuName());
    } else {
      Serial.printf("tap en %s\n", screenName(screensCurrent()));
    }
  }

  consolePoll();
  netLoop();
  spotifyLoop();
  buddyLedTick(now);
  rgbTick();
  // After the network so a repaint requested by the panel and one requested by
  // the sensor land in the same frame.
  rotationTick(now);

  if (screensTick(now, s_button.holdPermille(now))) {
    displayPush(gfxBuffer());
  }

  // One millisecond of sleep per pass. This loop has no blocking call of its
  // own, so without this the task spins at 240 MHz for the whole life of the
  // board and the idle task never runs; 1 ms is far below what the button, the
  // sensor or a 20 fps face need, and it hands the core back between frames.
  vTaskDelay(1);
}
