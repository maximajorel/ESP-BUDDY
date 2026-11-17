#include "screens.h"
#include "theme.h"

#include "rgb.h"
#include "rotation.h"
#include "screen_buddy.h"
#include "screen_name.h"
#include "screen_sleep.h"
#include "screen_order.h"
#include "settings.h"
#include "text_fold.h"

static uint8_t s_current = SCREEN_CLOCK;
static uint16_t s_holdPermille = 0;

// Screen picker. A hold opens it with the names big and centred; a tap moves the
// highlight and a second hold enters. The gadget used to require a three-second
// hold to reach the next screen, with no way to jump and no feedback beyond a
// bar; this is the same one button doing more.
static bool s_menuOpen = false;
// Position in the configured order, not a screen id: a tap walks the order the
// owner chose, which is the whole point of letting them reorder.
static uint8_t s_menuPos = 0;

// Pick order, loaded from NVS. Identity unless the owner changed it.
static uint8_t s_order[SCREEN_COUNT];

// Auto-off state: the last accepted interaction and whether the glass is dark.
static bool s_asleep = false;
static uint32_t s_lastActivity = 0;
// One pushed frame after waking so the owner sees the current screen instead of
// the one that was frozen when the panel went dark.
static bool s_wakePush = false;
// One pushed frame for a rotation change or any other owner action that only
// needs the existing framebuffer re-sent.
static bool s_repaintPush = false;

// Names shipped with the firmware. The owner can rename any screen from the web
// panel; an absent NVS override falls back to these.
static const char *const SCREEN_DEFAULT_NAMES[SCREEN_COUNT] = {
    "RELOJ", "TRAGAMONEDAS", "METEORITOS", "RED", "MUSICA", "SOFIA",
    "CONFIG"};

// Names currently in use, indexed by screen id (not by pick position).
static char s_names[SCREEN_COUNT][SCREEN_NAME_MAX + 1];

const char *screenName(uint8_t index) {
  if (index >= SCREEN_COUNT) return "?";
  return s_names[index];
}

// Reads every stored override, cleaning it again on the way in: NVS survives a
// crash mid-write and a corrupt value must not reach the picker.
static void loadNames() {
  char stored[SCREEN_NAME_MAX + 1];
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    if (settingsGetScreenName(i, stored, sizeof(stored)) &&
        screenNameClean(stored, s_names[i], sizeof(s_names[i]))) {
      continue;
    }
    snprintf(s_names[i], sizeof(s_names[i]), "%s", SCREEN_DEFAULT_NAMES[i]);
  }
}

bool screensSetName(uint8_t index, const char *name) {
  if (index >= SCREEN_COUNT) return false;
  char cleaned[SCREEN_NAME_MAX + 1];
  // An empty name is the "restore the built-in one" request: the cleaner
  // rejects it, so it is routed to clearing the override instead.
  if (!name || !*name) {
    if (!settingsSetScreenName(index, "")) return false;
    snprintf(s_names[index], sizeof(s_names[index]), "%s",
             SCREEN_DEFAULT_NAMES[index]);
    return true;
  }
  if (!screenNameClean(name, cleaned, sizeof(cleaned))) return false;
  if (!settingsSetScreenName(index, cleaned)) return false;
  snprintf(s_names[index], sizeof(s_names[index]), "%s", cleaned);
  return true;
}

void screensResetNames() {
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) settingsSetScreenName(i, "");
  loadNames();
}

static void loadOrder() {
  settingsGetScreenOrder(s_order, SCREEN_COUNT);
  if (!screenOrderValid(s_order, SCREEN_COUNT)) {
    for (uint8_t i = 0; i < SCREEN_COUNT; i++) s_order[i] = i;
  }
}

void screensGetOrder(uint8_t *out) {
  if (!out) return;
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) out[i] = s_order[i];
}

bool screensSetOrder(const uint8_t *order) {
  if (!screenOrderValid(order, SCREEN_COUNT)) return false;
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) s_order[i] = order[i];
  return settingsSetScreenOrder(s_order, SCREEN_COUNT);
}

void screensBegin() {
  gameInit();
  buddyBegin();
  loadOrder();
  loadNames();
  s_lastActivity = millis();
  s_asleep = false;
  s_wakePush = false;
  // The first screen in the owner's menu order is the one the device boots on,
  // so reordering the picker also chooses the initial screen.
  screensGoTo(s_order[0]);
}

uint8_t screensCurrent() { return s_current; }

void screensRepaint() { s_repaintPush = true; }

void screensGoTo(uint8_t index) {
  if (index >= SCREEN_COUNT) index = 0;
  s_lastActivity = millis();
  if (s_asleep) {
    // A programmatic switch (panel, console) is an interaction too.
    s_asleep = false;
    displaySetAwake(true);
    rgbWake();
    s_wakePush = true;
  }
  // The music screen is the only one that has to stop background work when it
  // leaves the glass, so its exit is explicit here rather than in a hook every
  // screen would have to implement. CONFIG is the second: a brightness changed
  // a moment ago may still be waiting for its debounced NVS write.
  if (s_current == SCREEN_MUSIC && index != SCREEN_MUSIC) musicLeave();
  if (s_current == SCREEN_SETTINGS && index != SCREEN_SETTINGS) configLeave();
  s_current = index;
  s_holdPermille = 0;
  s_repaintPush = false;
  switch (s_current) {
    case SCREEN_CLOCK: clockEnter(); break;
    case SCREEN_SLOT: slotEnter(); break;
    case SCREEN_GAME: gameEnter(); break;
    case SCREEN_NETWORK: netEnter(); break;
    case SCREEN_MUSIC: musicEnter(); break;
    case SCREEN_BUDDY: buddyEnter(); break;
    case SCREEN_SETTINGS: configEnter(); break;
  }
}

// Next in the configured order, not the next index.
void screensNext() {
  for (uint8_t pos = 0; pos < SCREEN_COUNT; pos++) {
    if (s_order[pos] != s_current) continue;
    screensGoTo(s_order[(uint8_t)((pos + 1) % SCREEN_COUNT)]);
    return;
  }
  screensGoTo(s_order[0]);
}

static uint8_t orderPosOf(uint8_t screenId) {
  for (uint8_t pos = 0; pos < SCREEN_COUNT; pos++) {
    if (s_order[pos] == screenId) return pos;
  }
  return 0;
}

bool screensMenuIsOpen() { return s_menuOpen; }

const char *screensMenuName() {
  return s_menuOpen ? screenName(s_order[s_menuPos]) : "";
}

void screensMenuClose() {
  s_menuOpen = false;
  s_holdPermille = 0;
  screensGoTo(s_current);  // repaint whatever was underneath
}

// --- the screen picker ------------------------------------------------------

static void drawMenu() {
  gfxClear(GFX_BLACK);

  const char *name = screenName(s_order[s_menuPos]);
  // The 5x7 font only covers ASCII: a name saved with accents keeps them in NVS
  // and in the API, but the glass shows the folded form ("MUSICA").
  char folded[SCREEN_NAME_MAX + 1];
  textFoldAscii(name, folded, sizeof(folded));
  // Scale 3 fits every name ("TRAGAMONEDAS" is the longest at 12 characters);
  // a longer one degrades to scale 2 instead of running off the edge.
  int scale = (gfxTextWidth(3, folded) <= DISPLAY_W - 12) ? 3 : 2;
  int w = gfxTextWidth(scale, folded);
  int h = 7 * scale;
  // No header band any more, so the name sits closer to the optical centre of
  // the whole canvas instead of the old body band.
  gfxText((DISPLAY_W - w) / 2, 74 - h / 2, scale, folded, GFX_WHITE);

  // Position dots: solid for the highlighted screen, dim for the rest. Cheaper to
  // read at a glance than "3/5".
  const int dotGap = 12;
  int totalW = (SCREEN_COUNT - 1) * dotGap;
  int x = (DISPLAY_W - totalW) / 2;
  for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
    int cx = x + i * dotGap;
    if (i == s_menuPos) {
      gfxFillCircle(cx, 112, 4, GFX_WHITE);
    } else {
      gfxCircle(cx, 112, 3, CHROME_DIM);
    }
  }

  chromeFooter("TOCA: SIGUIENTE   MANTENE 1S: ENTRAR");
}

static void wakeScreen(uint32_t nowMs) {
  s_asleep = false;
  s_lastActivity = nowMs;
  s_wakePush = true;
  displaySetAwake(true);
  rgbWake();
  Serial.println("pantalla encendida");
}

static void sleepScreen(uint32_t nowMs) {
  (void)nowMs;
  s_asleep = true;
  displaySetAwake(false);
  rgbSleep();
  Serial.println("pantalla apagada");
}

// Runs after drawing: the screen the owner is looking at decides whether it may
// sleep. The picker and a busy game keep the timer at zero.
static void maybeSleep(uint32_t nowMs) {
  ScreenTimeout timeout = settingsGetScreenTimeout();
  if (timeout.seconds == 0) return;
  if (!screenSleepIncluded(timeout.mask, s_current)) return;
  if (s_current == SCREEN_GAME && gameWantsScreenOn()) {
    s_lastActivity = nowMs;
    return;
  }
  if (screenSleepDue(nowMs, s_lastActivity, timeout.seconds)) {
    sleepScreen(nowMs);
  }
}

bool screensTick(uint32_t nowMs, uint16_t holdPermille) {
  bool changed = false;

  // A game in progress owns the board's tilt: the player is steering with it, so
  // the image must not follow the sensor while that is true. Same predicate the
  // sleep timer uses, raised and cleared here because this is where the current
  // screen is known.
  rotationSetHeld(s_current == SCREEN_GAME && gameWantsScreenOn());

  if (s_asleep) return false;
  if (s_wakePush) {
    s_wakePush = false;
    s_lastActivity = nowMs;
    return true;
  }
  if (s_repaintPush) {
    s_repaintPush = false;
    s_lastActivity = nowMs;
    return true;
  }

  if (s_menuOpen) {
    if (holdPermille != s_holdPermille) changed = true;
    s_holdPermille = holdPermille;
    drawMenu();
    if (changed) chromeHoldBar(s_holdPermille);
    // The picker is not a screen of its own: it sleeps with the screen it was
    // opened from. Leaving it open used to keep the glass on forever.
    maybeSleep(nowMs);
    return true;
  }
  switch (s_current) {
    case SCREEN_CLOCK: changed = clockTick(nowMs); break;
    case SCREEN_SLOT: changed = slotTick(nowMs); break;
    case SCREEN_GAME: changed = gameTick(nowMs); break;
    case SCREEN_NETWORK: changed = netTick(nowMs); break;
    case SCREEN_MUSIC: changed = musicTick(nowMs); break;
    case SCREEN_BUDDY: changed = buddyTick(nowMs); break;
    case SCREEN_SETTINGS: changed = configTick(nowMs); break;
  }
  if (holdPermille != s_holdPermille) changed = true;
  s_holdPermille = holdPermille;
  // The hold bar lives below the footer band, so it is painted on top of
  // whatever the screen drew rather than by the screen itself.
  if (changed) chromeHoldBar(s_holdPermille);
  maybeSleep(nowMs);
  return changed;
}

void screensShortPress() {
  if (s_asleep) {
    // The tap that lights the glass must not also shoot, spin or switch screens.
    wakeScreen(millis());
    return;
  }
  s_lastActivity = millis();
  // While the picker is open the tap belongs to it: it moves the highlight.
  if (s_menuOpen) {
    s_menuPos = (uint8_t)((s_menuPos + 1) % SCREEN_COUNT);
    return;
  }
  switch (s_current) {
    case SCREEN_CLOCK: clockShortPress(); break;
    case SCREEN_SLOT: slotShortPress(); break;
    case SCREEN_GAME: gameShortPress(); break;
    case SCREEN_NETWORK: netShortPress(); break;
    case SCREEN_MUSIC: musicShortPress(); break;
    case SCREEN_BUDDY: buddyShortPress(); break;
    case SCREEN_SETTINGS: configShortPress(); break;
  }
}

void screensLongPress() {
  if (s_asleep) {
    // A hold only wakes; it must not open the picker while the owner is just
    // trying to see the screen again.
    wakeScreen(millis());
    return;
  }
  s_lastActivity = millis();
  // The hold indicator is cleared by screensGoTo; nothing screen-specific has
  // to happen on the way out.
  s_holdPermille = 0;
  // A screen may claim the hold for its own confirmation: the game uses it to
  // accept "iniciar calibracion", and the picker must not open on top of that
  // prompt. Every other screen leaves the hold to the menu.
  if (!s_menuOpen && s_current == SCREEN_GAME && gameLongPress()) return;
  if (s_menuOpen) {
    // Second hold: enter the highlighted screen.
    s_menuOpen = false;
    screensGoTo(s_order[s_menuPos]);
    Serial.printf("menu -> %s\n", screenName(screensCurrent()));
    return;
  }
  s_menuOpen = true;
  s_menuPos = orderPosOf(s_current);
  Serial.printf("menu abierto en %s\n", screenName(s_order[s_menuPos]));
}

// --- chrome -----------------------------------------------------------------

void chromeFooter(const char *hint) {
  if (!hint || !*hint) return;
  gfxFillRect(0, CHROME_FOOTER_Y, DISPLAY_W, CHROME_HOLD_BAR_Y - CHROME_FOOTER_Y,
              GFX_BLACK);
  int w = gfxTextWidth(1, hint);
  gfxText((DISPLAY_W - w) / 2, CHROME_FOOTER_Y, 1, hint, CHROME_DIM);
}

void chromeHoldBar(uint16_t permille) {
  gfxFillRect(0, CHROME_HOLD_BAR_Y, DISPLAY_W, DISPLAY_H - CHROME_HOLD_BAR_Y,
              GFX_BLACK);
  if (permille == 0) return;
  if (permille > 1000) permille = 1000;
  int w = (int)((long)DISPLAY_W * permille / 1000);
  // The bar turns gold as it fills, so the gesture has obvious feedback even
  // before the screen switches.
  gfxFillRect(0, CHROME_HOLD_BAR_Y, w, DISPLAY_H - CHROME_HOLD_BAR_Y,
              uiAccent());
}

int gfxTextRight(int rightX, int y, int scale, const char *text, uint16_t color) {
  int w = gfxTextWidth(scale, text);
  int x = rightX - w;
  gfxText(x, y, scale, text, color);
  return x;
}
