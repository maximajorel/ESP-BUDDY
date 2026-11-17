// Network screen (S4).
//
// Requirement: a screen with the connection data - the WiFi name and password -
// plus a web admin panel to change that data and manage the device.
//
// This screen is the only way to find the panel, so it leads with the access
// point name and password in large type rather than burying them in a list.
#include "net.h"
#include "screens.h"
#include "theme.h"
#include "settings.h"

#define LABEL_COLOR gfxHex(0x7A8CA0)
#define VALUE_COLOR GFX_WHITE
#define PASS_COLOR uiAccent()
#define URL_COLOR gfxHex(0x66D9EF)
#define OK_COLOR gfxHex(0x69DB7C)
#define BAD_COLOR gfxHex(0xFF6B6B)

static uint32_t s_lastDraw = 0;
static uint32_t s_redrawAt = 0;
static bool s_needRedraw = true;

// Values can be up to 32 characters; scale 2 only fits 26, so long names drop
// to scale 1 instead of running off the panel.
static void drawValue(int y, const char *text, uint16_t color) {
  int scale = gfxTextWidth(2, text) <= DISPLAY_W - 24 ? 2 : 1;
  gfxText(12, scale == 2 ? y : y + 4, scale, text, color);
}

void netEnter() {
  s_needRedraw = true;
}

void netShortPress() {
  // A dead station link is the one thing a user can reasonably retry from the
  // device itself without a phone.
  if (settingsHasSta()) {
    netSetSta(settingsGet().staSsid, settingsGet().staPass);
  }
  s_needRedraw = true;
}

bool netTick(uint32_t nowMs) {
  // Refresh once a second so the "connected" state, client count and signal
  // strength stay honest without repainting an idle screen.
  bool due = (uint32_t)(nowMs - s_redrawAt) >= 1000;
  if (!due && !s_needRedraw) return false;

  s_redrawAt = nowMs;
  s_needRedraw = false;
  s_lastDraw = nowMs;
  (void)s_lastDraw;

  const NetStatus &st = netStatus();
  const Settings &cfg = settingsGet();

  gfxClear(GFX_BLACK);

  char line[96];

  // Both radios run at once, so once the home network is up there are two ways
  // into the panel. The one the owner actually wants is the address on their own
  // network: reaching the panel should never require leaving the WiFi they are
  // already on. That URL is therefore the headline, not a footnote.
  if (st.staConnected) {
    gfxText(12, 4, 1, "PANEL DESDE TU WIFI", LABEL_COLOR);
    snprintf(line, sizeof(line), "http://%s", st.staIp);
    gfxText(12, 14, 2, line, uiAccent());

    gfxText(12, 38, 1, "SI PREFERIS EL AP DEL EQUIPO", LABEL_COLOR);
    gfxText(12, 48, 1, "http://" NET_AP_IP, URL_COLOR);

    gfxText(12, 66, 1, "RED DEL EQUIPO / CLAVE", LABEL_COLOR);
    snprintf(line, sizeof(line), "%s / %s", cfg.apSsid,
             cfg.apPass[0] ? cfg.apPass : "(abierta)");
    gfxText(12, 76, 1, line, PASS_COLOR);

    gfxHLine(12, 90, DISPLAY_W - 24, CHROME_LINE);

    snprintf(line, sizeof(line), "%s   %d dBm   %u cliente(s)", st.staSsid,
             (int)st.staRssi, (unsigned)st.apClients);
    gfxText(12, 98, 1, line, OK_COLOR);
    snprintf(line, sizeof(line), "HORA: %s",
             st.timeSynced ? "SINCRONIZADA" : "SIN SINCRONIZAR");
    gfxText(12, 112, 1, line, st.timeSynced ? OK_COLOR : LABEL_COLOR);
    chromeFooter("TOCA REINTENTAR   MANTENE: MENU");
    return true;
  }

  gfxText(12, 4, 1, "CONECTATE A ESTA RED", LABEL_COLOR);
  drawValue(15, cfg.apSsid, VALUE_COLOR);

  gfxText(12, 38, 1, "CLAVE", LABEL_COLOR);
  drawValue(49, cfg.apPass[0] ? cfg.apPass : "(red abierta)", PASS_COLOR);

  gfxText(12, 72, 1, "PANEL WEB", LABEL_COLOR);
  drawValue(83, "http://" NET_AP_IP, URL_COLOR);

  gfxHLine(12, 108, DISPLAY_W - 24, CHROME_LINE);

  if (!st.staConfigured) {
    snprintf(line, sizeof(line), "WIFI DE CASA: SIN CONFIGURAR");
    gfxText(12, 114, 1, line, BAD_COLOR);
    gfxText(12, 126, 1, "Configurala en el panel web", LABEL_COLOR);
  } else if (st.staState == NET_STA_FAILED) {
    gfxText(12, 114, 1, "NO SE PUDO CONECTAR", BAD_COLOR);
    gfxText(12, 126, 1, "Revisa la clave en el panel web", VALUE_COLOR);
  } else {
    snprintf(line, sizeof(line), "CONECTANDO A %s", st.staSsid);
    gfxText(12, 114, 1, line, PASS_COLOR);
    gfxText(12, 126, 1, "Toca para reintentar", LABEL_COLOR);
  }

  snprintf(line, sizeof(line), "HORA: %s", st.timeSynced ? "SINCRONIZADA" : "SIN SINCRONIZAR");
  gfxText(12, 138, 1, line, st.timeSynced ? OK_COLOR : LABEL_COLOR);

  chromeFooter(st.staConfigured ? "TOCA REINTENTAR   MANTENE 3S"
                                : "MANTENE 3S PARA SEGUIR");
  return true;
}
