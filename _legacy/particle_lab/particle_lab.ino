/*
 * particle_lab - gravity-driven particle simulator for the
 * Waveshare ESP32-S3-LCD-1.47B.
 *
 * Two views, switched with the BOOT button (GPIO0):
 *
 *   1. SAND  - thousands of grains fall and pile up. The gravity vector comes
 *              from the onboard QMI8658, so tilting the board makes the pile
 *              avalanche in that direction.
 *   2. WATER - a tank of water. Same sensor, different physics: water levels
 *              out into a flat surface and sloshes when the board is tilted.
 *
 * Serial (115200) also accepts commands, which is what makes the physics
 * testable without a camera pointed at the panel:
 *
 *   m            toggle sand <-> water
 *   r            reset the current world
 *   s            print mode, fps, particle count, centroid, gravity, pin state
 *   d            dump a coarse ASCII map of the world
 *   t            run the BOOT press path with a synthetic contact
 *   g <x> <y>    force a gravity direction in screen space, e.g. "g -1 0"
 *   g auto       go back to the accelerometer
 *   axis <sx> <sy>  flip sensor-to-screen axis signs (-1 or 1)
 */

#include <esp32-hal-psram.h>

#include "Display_ST7789.h"
#include "button_debounce.h"
#include "imu.h"
#include "sim.h"

#define BOOT_PIN 0

// Frames per second, refreshed once per second by loop().
uint32_t g_fps = 0;

// Sensor axes do not necessarily line up with screen axes, and the sign of
// "down" depends on how the board is held. These are runtime-tweakable with
// the `axis` serial command so the mapping can be corrected without a
// recompile.
static float s_axisX = 1.0f;
static float s_axisY = 1.0f;

static uint16_t *s_fb = nullptr;

// Low-pass filtered acceleration, in g.
static float s_fx = 0.0f, s_fy = 0.0f, s_fz = 0.0f;

static bool s_override = false;
static float s_gx = 0.0f, s_gy = 1.0f;

// Debounce state for the BOOT button, shared by the real pin poll and the
// `t` self-test.
static ButtonDebounce s_boot;

static void drawModeStrip() {
  uint16_t color = (simMode() == SIM_MODE_SAND) ? LCD_Swap16(0xFB40)   // amber
                                               : LCD_Swap16(0x2D7F);  // cyan
  for (int i = 0; i < LCD_WIDTH * 2; i++) s_fb[i] = color;
}

static void printStats() {
  Serial.printf("[mode=%s fps=%lu particles=%d centroid=(%.2f, %.2f) gravity=(%.2f, %.2f)%s accel=(%.2f, %.2f, %.2f) boot=%d]\n",
                simMode() == SIM_MODE_SAND ? "sand" : "water",
                (unsigned long)g_fps, simParticles(), simCentroidX(), simCentroidY(),
                s_gx, s_gy, s_override ? " override" : "", s_fx, s_fy, s_fz,
                digitalRead(BOOT_PIN) == HIGH ? 1 : 0);
}

static void switchMode() {
  simSetMode(simMode() == SIM_MODE_SAND ? SIM_MODE_WATER : SIM_MODE_SAND);
  Serial.printf("mode -> %s\n", simMode() == SIM_MODE_SAND ? "sand" : "water");
}

// ------------------------------------------------------------ serial input ---

static char s_line[96];
static uint8_t s_lineLen = 0;

static void handleLine(char *line) {
  // Trim leading spaces.
  while (*line == ' ') line++;
  if (!*line) return;

  if (!strcmp(line, "m")) {
    switchMode();
  } else if (!strcmp(line, "r")) {
    simReset();
    Serial.println("world reset");
  } else if (!strcmp(line, "s")) {
    printStats();
  } else if (!strcmp(line, "d")) {
    simDumpAscii();
  } else if (!strncmp(line, "g ", 2)) {
    if (!strcmp(line + 2, "auto")) {
      s_override = false;
      Serial.println("gravity -> sensor");
    } else {
      float x, y;
      if (sscanf(line + 2, "%f %f", &x, &y) == 2) {
        float mag = sqrtf(x * x + y * y);
        if (mag < 0.0001f) {
          Serial.println("gravity vector must not be zero");
          return;
        }
        s_override = true;
        s_gx = x / mag;
        s_gy = y / mag;
        Serial.printf("gravity -> (%.2f, %.2f) override\n", s_gx, s_gy);
      } else {
        Serial.println("usage: g <x> <y> | g auto");
      }
    }
  } else if (!strncmp(line, "axis ", 5)) {
    float sx, sy;
    if (sscanf(line + 5, "%f %f", &sx, &sy) == 2) {
      s_axisX = (sx < 0) ? -1.0f : 1.0f;
      s_axisY = (sy < 0) ? -1.0f : 1.0f;
      Serial.printf("axis -> (%+.0f, %+.0f)\n", s_axisX, s_axisY);
    } else {
      Serial.println("usage: axis <sx> <sy>");
    }
  } else if (!strcmp(line, "t")) {
    // Runs the real press path - debounce state machine, then switchMode -
    // with a synthetic contact instead of the mechanical switch. A serial
    // command cannot prove the switch itself, but it does prove that every
    // line of firmware between the pin and the view change works.
    uint32_t now = millis();
    s_boot.update(false, now);              // contact closes
    bool fired = s_boot.update(false, now + 40);  // still down 40 ms later
    s_boot = ButtonDebounce();              // the pin is high again: reset
    Serial.printf("button self-test: %s\n", fired ? "press accepted" : "NO EVENT");
    if (fired) switchMode();
  } else if (!strcmp(line, "p")) {
    startPinDriveTest();
  } else if (!strcmp(line, "?")) {
    Serial.println("commands: m r s d t p g <x> <y> | g auto | axis <sx> <sy>");
  } else {
    Serial.println("unknown command, try ?");
  }
}

static void pollSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      s_line[s_lineLen] = '\0';
      handleLine(s_line);
      s_lineLen = 0;
      continue;
    }
    if (s_lineLen < sizeof(s_line) - 1) s_line[s_lineLen++] = c;
  }
}

// ---------------------------------------------------------------- button ---

static void pollButton() {
  if (s_boot.update(digitalRead(BOOT_PIN) == HIGH, millis())) switchMode();
}

// Electrical self-test of the BOOT input path.
//
// The `t` command feeds the debounce a synthetic level, which proves the logic
// but never touches the pin. This instead drives GPIO0 itself low, so the real
// pollButton() above sees a real low on the real pad and switches the view the
// normal way. The only thing it cannot cover is the switch's own contact.
//
// Safe: GPIO0 only matters as a strap pin at boot, and the board's 10K pull-up
// means driving it low sinks about 0.3 mA.
static bool s_pinDriveActive = false;
static uint32_t s_pinDriveStarted = 0;

static void startPinDriveTest() {
  pinMode(BOOT_PIN, OUTPUT);
  digitalWrite(BOOT_PIN, LOW);
  s_pinDriveActive = true;
  s_pinDriveStarted = millis();
  Serial.println("pin test: driving GPIO0 low through the normal press path");
}

static void servicePinDriveTest() {
  if (!s_pinDriveActive) return;
  if (millis() - s_pinDriveStarted < 150) return;
  pinMode(BOOT_PIN, INPUT_PULLUP);
  s_pinDriveActive = false;
  Serial.printf("pin test done, boot=%d\n", digitalRead(BOOT_PIN) == HIGH ? 1 : 0);
}

// ------------------------------------------------------------------ setup ---

void setup() {
  Serial.begin(115200);
  // Never block the main loop waiting for a host to drain the port. Without
  // this, killing the terminal that had the port open leaves the firmware
  // wedged inside the next Serial.print and the board stops answering
  // altogether, USB included. Observed exactly that during development.
  Serial.setTxTimeoutMs(0);
  delay(200);
  Serial.println("\nparticle_lab: starting");

  Serial.printf("PSRAM: %s (%u bytes free)\n", psramFound() ? "yes" : "no",
                (unsigned)ESP.getFreePsram());

  s_fb = (uint16_t *)ps_malloc(LCD_PIXELS * sizeof(uint16_t));
  if (!s_fb) {
    Serial.println("FATAL: could not allocate the framebuffer in PSRAM");
    while (true) delay(1000);
  }
  if (!simBegin()) {
    Serial.println("FATAL: could not allocate the simulation grids in PSRAM");
    while (true) delay(1000);
  }

  LCD_Init();
  Set_Backlight(90);
  Serial.println("display ready");

  if (imuBegin()) {
    Serial.println("QMI8658 ready");
  } else {
    Serial.println("WARN: QMI8658 did not answer, falling back to screen-down gravity");
  }

  pinMode(BOOT_PIN, INPUT_PULLUP);

  Serial.println("mode -> sand");
  Serial.println("commands: m r s d g <x> <y> | g auto | axis <sx> <sy>");
}

// ------------------------------------------------------------------- loop ---

static uint32_t s_frameCount = 0;
static uint32_t s_fpsWindowStart = 0;
static uint32_t s_lastHeartbeat = 0;

void loop() {
  pollSerial();
  pollButton();
  servicePinDriveTest();

  float ax, ay, az;
  imuReadAccel(ax, ay, az);
  s_fx = s_fx * 0.7f + ax * 0.3f;
  s_fy = s_fy * 0.7f + ay * 0.3f;
  s_fz = s_fz * 0.7f + az * 0.3f;

  if (s_override) {
    // direction already stored
  } else {
    // A stationary accelerometer reports the reaction force, so gravity points
    // the opposite way in the device frame.
    float gx = -s_fx * s_axisX;
    float gy = -s_fy * s_axisY;
    float mag = sqrtf(gx * gx + gy * gy);

    if (mag < 0.20f) {
      // Board lying flat: there is no in-plane gravity to read. Falling to the
      // bottom of the screen keeps the scene readable instead of frozen.
      gx = 0.0f;
      gy = 1.0f;
    } else {
      gx /= mag;
      gy /= mag;
    }
    s_gx = gx;
    s_gy = gy;
  }

  simStep(s_gx, s_gy);
  simRender(s_fb);
  drawModeStrip();
  LCD_PushFullFrame(s_fb);

  s_frameCount++;
  uint32_t now = millis();
  if (now - s_fpsWindowStart >= 1000) {
    g_fps = s_frameCount * 1000UL / (now - s_fpsWindowStart);
    s_frameCount = 0;
    s_fpsWindowStart = now;
  }

  if (now - s_lastHeartbeat >= 4000) {
    s_lastHeartbeat = now;
    printStats();
  }
}
