// Pantalla del juego Meteoritos (S3).
//
// Requisito: un juego con el giroscopio donde esquivás cosas, que se pone más
// difícil con el tiempo, guarda el récord y dispara con el botón, con estética
// ASCII.
//
// Todo se dibuja en una grilla de caracteres con la fuente 5x7: la nave es "/^\",
// los meteoritos son letras y signos, las balas son "|". Eso es lo que hace que
// la pantalla parezca una terminal y no un juego de sprites.
//
// El control se apoya en dos cosas medidas, no supuestas:
//   - el eje que lleva la gravedad se detecta al arrancar, porque según cómo
//     apoyes la placa puede ser X, Y o Z;
//   - el eje que lee la inclinación y sus sentidos se calibran con el wizard de
//     CONFIG (wizard guiado, `steer_wizard.h`) y quedan guardados en NVS.
// Sin eso, los ejes fijos hacían que la nave no se moviera. El juego ya no pide
// ni dibuja calibración: solo la usa.
//
// La inclinación se filtra de forma complementaria: el acelerómetro da la
// posición absoluta (no deriva) y el giroscopio aporta la respuesta rápida.
#include "game_difficulty.h"
#include "game_hits.h"
#include "imu.h"
#include "rgb.h"
#include "rotation.h"
#include "screens.h"
#include "settings.h"

// --- grilla -----------------------------------------------------------------

#define CELL_W 6
#define CELL_H 8
#define GRID_COLS (DISPLAY_W / CELL_W)  // 53
#define GRID_ROWS 19
#define GRID_X0 0
#define GRID_Y0 12

#define MAX_METEORS 24
#define MAX_BULLETS 6
#define MAX_ENEMIES 3
#define MAX_ENEMY_BULLETS 8
#define ENEMY_SCORE 150

#define SHIP_SPEED_COLS 28.0f   // columnas por segundo a inclinación máxima
#define SHIP_SPEED_ROWS 20.0f   // filas por segundo a inclinación máxima
#define BULLET_SPEED 46.0f      // filas por segundo hacia arriba
#define FIRE_COOLDOWN_MS 190
#define TILT_FULL_SCALE_G 0.35f // inclinación que da el 100% del control
#define TILT_DEADZONE 0.10f
// --- paleta -----------------------------------------------------------------

#define STYLE_TEXT gfxHex(0xB8E986)
#define STYLE_DIM gfxHex(0x2F4F2F)
#define STYLE_STAR gfxHex(0x4A4A4A)
#define STYLE_SHIP gfxHex(0x8CE0FF)
#define STYLE_BULLET gfxHex(0xFFF275)
#define STYLE_METEOR gfxHex(0xD0D0D0)
#define STYLE_METEOR_HOT gfxHex(0xFF9F45)
#define STYLE_WARN gfxHex(0xFF5555)
// Los cazas enemigos y sus disparos: rojo para el cuerpo, ámbar para la bala,
// para distinguirlos de los meteoritos grises y de las balas amarillas del jugador.
#define STYLE_ENEMY gfxHex(0xFF5A5A)
#define STYLE_ENEMY_BULLET gfxHex(0xFFB86B)

// --- estado -----------------------------------------------------------------

enum GameState { GAME_READY, GAME_PLAYING, GAME_OVER };

struct Meteor {
  float x;
  float y;
  float vy;
  char glyph;
  uint8_t kind;  // 0 chico, 1 mediano, 2 grande
  bool alive;
};

struct Bullet {
  float x;
  float y;
  bool alive;
};

// Un caza enemigo: se desplaza de lado a lado por la franja de arriba y dispara
// hacia abajo. Sólo lo destruye una bala del jugador; su cuerpo y sus balas
// terminan la partida.
struct Enemy {
  float x;
  float y;
  float vx;
  uint32_t nextFireAt;
  uint8_t hp;     // player shots left before it breaks
  uint8_t maxHp;  // its hp when it spawned, for the bar
  bool alive;
};

struct EnemyBullet {
  float x;
  float y;
  float vy;
  bool alive;
};

bool g_imuOk = false;

static GameState s_state = GAME_READY;
static float s_shipX = GRID_COLS / 2.0f;
// The ship used to be pinned to the bottom row; the second tilt axis moves it.
static float s_shipRow = GRID_ROWS - 1;
static Meteor s_meteors[MAX_METEORS];
static Bullet s_bullets[MAX_BULLETS];
static Enemy s_enemies[MAX_ENEMIES];
static EnemyBullet s_enemyBullets[MAX_ENEMY_BULLETS];
// El primero aparece a los seis segundos; después manda la cadencia del nivel.
static uint32_t s_nextEnemyAt = 0;
static uint32_t s_score = 0;
static uint32_t s_shotsFired = 0;
// Survival points arrive as a fraction per frame (dt * 8). Adding a truncated
// per-frame amount would earn zero forever, so the fraction is carried over.
static float s_scoreCarry = 0.0f;
static uint32_t s_level = 1;
static uint32_t s_playStart = 0;
static uint32_t s_lastSpawn = 0;
static uint32_t s_lastFire = 0;
static uint32_t s_lastFrame = 0;
static uint32_t s_lastDraw = 0;
static bool s_needRedraw = true;
static uint32_t s_rng = 0x9E3779B9;

#define STAR_COUNT 34
static float s_starX[STAR_COUNT];
static float s_starY[STAR_COUNT];

static SteerCalibration s_cal;
static uint8_t s_rotAxis = 1;  // eje de giro, derivado de latAxis y upAxis
// Two filtered tilts: the lateral one and the fore/aft one that drives the
// vertical movement. They are independent because the board also pitches when
// the player reaches across it.
static float s_fusedLat = 0.0f;
static float s_fusedFwd = 0.0f;
static bool s_tiltPrimed = false;

// Re-measured at the start of every game: the stored calibration chooses the
// axes, but the neutral point is whatever posture the player is holding right
// now. Without this, a baseline captured days earlier pushes the ship into a
// wall for the whole game.
#define REBASELINE_MS 350
static bool s_rebaselining = false;
static uint32_t s_rebaselineStart = 0;
static float s_rebaselineSum = 0.0f;
static float s_rebaselineSumY = 0.0f;
static uint32_t s_rebaselineCount = 0;

static uint32_t nextRandom() {
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}

static float randomUnit() { return (float)(nextRandom() % 10000) / 10000.0f; }

// El eje de giro que acompaña a una inclinación sobre latAxis es el tercero.
static void deriveRotAxis() {
  for (uint8_t axis = 0; axis < 3; axis++) {
    if (axis != s_cal.latAxis && axis != s_cal.upAxis) {
      s_rotAxis = axis;
      return;
    }
  }
  s_rotAxis = 1;
}

void gameSetSteer(uint8_t latAxis, uint8_t upAxis, int8_t sign, int8_t signY,
                  uint8_t gain) {
  if (latAxis > 2 || upAxis > 2 || latAxis == upAxis) return;
  s_cal.latAxis = latAxis;
  s_cal.upAxis = upAxis;
  s_cal.sign = sign < 0 ? -1 : 1;
  s_cal.signY = signY < 0 ? -1 : 1;
  s_cal.gain = gain == 0 ? 100 : gain;
  s_cal.valid = true;
  deriveRotAxis();
  s_tiltPrimed = false;
  settingsSetSteerCalibration(s_cal);
}

void gameGetSteer(uint8_t *latAxis, uint8_t *upAxis, int8_t *sign,
                  int8_t *signY, uint8_t *gain) {
  if (latAxis) *latAxis = s_cal.latAxis;
  if (upAxis) *upAxis = s_cal.upAxis;
  if (sign) *sign = s_cal.sign;
  if (signY) *signY = s_cal.signY;
  if (gain) *gain = s_cal.gain;
}

void gameInvalidateCalibration() {
  s_cal.valid = false;
  s_cal.baseline = 0.0f;
  s_cal.baselineY = 0.0f;
  settingsSetSteerCalibration(s_cal);
}

float gameShipColumn() { return s_shipX; }

float gameShipRow() { return s_shipRow; }

bool gameWantsScreenOn() {
  // The panel may sleep on the ready/over screens, but never mid-run.
  return s_state == GAME_PLAYING;
}

const char *gameStateName() {
  switch (s_state) {
    case GAME_READY: return "READY";
    case GAME_PLAYING: return "PLAYING";
    case GAME_OVER: return "OVER";
  }
  return "?";
}

static int aliveEnemies() {
  int alive = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (s_enemies[i].alive) alive++;
  }
  return alive;
}

void gameGetStats(uint32_t *score, uint32_t *level, int *meteorsAlive,
                  int *bulletsAlive, int *enemiesAlive,
                  int *enemyBulletsAlive) {
  if (score) *score = s_score;
  if (level) *level = s_level;
  if (meteorsAlive) {
    int alive = 0;
    for (int i = 0; i < MAX_METEORS; i++) {
      if (s_meteors[i].alive) alive++;
    }
    *meteorsAlive = alive;
  }
  if (bulletsAlive) {
    // Reported as "shots ever fired" so the console can prove the button fires
    // even though an individual bullet is gone in less than half a second.
    *bulletsAlive = (int)s_shotsFired;
  }
  if (enemiesAlive) *enemiesAlive = aliveEnemies();
  if (enemyBulletsAlive) {
    int alive = 0;
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
      if (s_enemyBullets[i].alive) alive++;
    }
    *enemyBulletsAlive = alive;
  }
}

int gameAliveEnemies() { return aliveEnemies(); }

float gameEnemyColumn(int slot) {
  int seen = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!s_enemies[i].alive) continue;
    if (seen++ == slot) return s_enemies[i].x;
  }
  return -1.0f;
}

float gameEnemyDrift(int slot) {
  int seen = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!s_enemies[i].alive) continue;
    if (seen++ == slot) return s_enemies[i].vx;
  }
  return 0.0f;
}

// Defined further down with the rest of the spawning code; the console test hook
// above it needs the declaration.
static void spawnEnemy(uint32_t nowMs, const GameDifficulty &d);

int gameEnemyHp(int slot) {
  int seen = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!s_enemies[i].alive) continue;
    if (seen++ == slot) return (int)s_enemies[i].hp;
  }
  return 0;
}

int gameEnemyMaxHp(int slot) {
  int seen = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!s_enemies[i].alive) continue;
    if (seen++ == slot) return (int)s_enemies[i].maxHp;
  }
  return 0;
}

void gameSetAgeSeconds(uint32_t seconds) {
  if (s_state != GAME_PLAYING) return;
  s_playStart = millis() - seconds * 1000u;
  s_level = 0;  // the next step sees a new level and pulses it
}

bool gameSpawnEnemyForTest() {
  if (s_state != GAME_PLAYING) return false;
  int before = aliveEnemies();
  uint32_t elapsed = millis() - s_playStart;
  spawnEnemy(millis(), gameDifficultyAt(elapsed));
  return aliveEnemies() > before;
}

void gameSetShip(float column, float row) {
  if (column < 1.0f) column = 1.0f;
  if (column > (float)(GRID_COLS - 2)) column = (float)(GRID_COLS - 2);
  if (row < 1.0f) row = 1.0f;
  if (row > (float)(GRID_ROWS - 1)) row = (float)(GRID_ROWS - 1);
  s_shipX = column;
  s_shipRow = row;
  s_needRedraw = true;
}

// Swaps the learned tilt axis for the other in-plane one. The calibration
// gesture is easy to perform in the wrong direction ("one side to the other"
// can mean front to back), so the user needs a correction that does not depend
// on repeating it correctly.
void gameSwapSteerAxis() {
  uint8_t other = (uint8_t)(3 - s_cal.latAxis - s_cal.upAxis);
  if (other > 2 || other == s_cal.upAxis) return;
  s_cal.latAxis = other;
  // Both baselines belong to the axes of the previous calibration.
  s_cal.baseline = 0.0f;
  s_cal.baselineY = 0.0f;
  s_cal.valid = true;
  deriveRotAxis();
  s_tiltPrimed = false;
  settingsSetSteerCalibration(s_cal);
}

// --- dibujo -----------------------------------------------------------------

static int cellX(float col) { return GRID_X0 + (int)(col * CELL_W + 0.5f); }
static int cellY(float row) { return GRID_Y0 + (int)(row * CELL_H); }

static void drawCharAt(float col, float row, int scale, char glyph,
                       uint16_t color) {
  gfxChar(cellX(col), cellY(row), scale, glyph, color);
}

static void resetField() {
  for (int i = 0; i < MAX_METEORS; i++) s_meteors[i].alive = false;
  for (int i = 0; i < MAX_BULLETS; i++) s_bullets[i].alive = false;
  for (int i = 0; i < MAX_ENEMIES; i++) s_enemies[i].alive = false;
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    s_enemyBullets[i].alive = false;
  }
  s_nextEnemyAt = 0;
  for (int i = 0; i < STAR_COUNT; i++) {
    s_starX[i] = randomUnit() * (GRID_COLS - 1);
    s_starY[i] = randomUnit() * GRID_ROWS;
  }
  s_shipX = GRID_COLS / 2.0f;
  s_shipRow = GRID_ROWS - 1;
}

static void startGame(uint32_t nowMs) {
  resetField();
  s_state = GAME_PLAYING;
  s_score = 0;
  s_level = 1;
  s_playStart = nowMs;
  s_lastSpawn = nowMs;
  s_nextEnemyAt = nowMs + GAME_ENEMY_FIRST_MS;
  s_lastFire = 0;
  s_shotsFired = 0;
  s_lastFrame = nowMs;
  s_fusedLat = 0.0f;
  s_fusedFwd = 0.0f;
  s_tiltPrimed = false;
  s_scoreCarry = 0.0f;
  s_rebaselining = g_imuOk;
  s_rebaselineStart = nowMs;
  s_rebaselineSum = 0.0f;
  s_rebaselineSumY = 0.0f;
  s_rebaselineCount = 0;
  s_needRedraw = true;
  rgbSetColor(20, 60, 120);
}

static void gameOver(uint32_t nowMs) {
  (void)nowMs;
  s_state = GAME_OVER;
  s_needRedraw = true;
  if (s_score > settingsGet().gameHighScore) {
    settingsMutable().gameHighScore = s_score;
    settingsSave();
  }
  rgbPulse(200, 20, 20, 900);
}

// La curva de dificultad vive en game_difficulty.h, que tiene test en host.
static void spawnMeteor(float speed) {
  for (int i = 0; i < MAX_METEORS; i++) {
    if (s_meteors[i].alive) continue;

    Meteor &m = s_meteors[i];
    m.x = 0.5f + randomUnit() * (GRID_COLS - 1.5f);
    m.y = 0.0f;
    uint32_t r = nextRandom() % 100;
    if (r < 55) {
      m.kind = 0;
      m.glyph = '*';
      m.vy = speed * 1.25f;
    } else if (r < 85) {
      m.kind = 1;
      m.glyph = 'O';
      m.vy = speed;
    } else {
      m.kind = 2;
      m.glyph = '@';
      m.vy = speed * 0.8f;
    }
    m.alive = true;
    return;
  }
}

static void spawnEnemy(uint32_t nowMs, const GameDifficulty &d) {
  if (aliveEnemies() >= MAX_ENEMIES) return;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (s_enemies[i].alive) continue;
    Enemy &e = s_enemies[i];
    e.x = 3.0f + randomUnit() * (float)(GRID_COLS - 6);
    e.y = 1.0f + randomUnit() * 1.5f;
    e.vx = (randomUnit() < 0.5f ? -1.0f : 1.0f) * d.enemyDriftCols;
    e.nextFireAt = nowMs + 900 + (uint32_t)(randomUnit() * 900.0f);
    e.hp = d.enemyHp;
    e.maxHp = d.enemyHp;
    e.alive = true;
    return;
  }
}

static void enemyFire(const Enemy &e, const GameDifficulty &d) {
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (s_enemyBullets[i].alive) continue;
    EnemyBullet &b = s_enemyBullets[i];
    b.x = e.x;
    b.y = e.y + 1.0f;
    b.vy = d.enemyBulletSpeed;
    b.alive = true;
    return;
  }
}

static void fireBullet(uint32_t nowMs) {
  if ((uint32_t)(nowMs - s_lastFire) < FIRE_COOLDOWN_MS) return;
  for (int i = 0; i < MAX_BULLETS; i++) {
    if (s_bullets[i].alive) continue;
    s_bullets[i].x = s_shipX;
    s_bullets[i].y = s_shipRow - 1;
    s_bullets[i].alive = true;
    s_lastFire = nowMs;
    s_shotsFired++;
    return;
  }
}

// --- física ----------------------------------------------------------------

static void readTilt(float dt) {
  if (!g_imuOk) {
    s_fusedLat *= 0.9f;
    s_fusedFwd *= 0.9f;
    return;
  }
  float a[3], g[3];
  imuReadAll(a, g);

  float sign = (float)s_cal.sign;
  // The owner's image may be the other one of the pair: then the two in-plane
  // readings point the other way and both steering axes turn over with them. The
  // rotation is held for as long as a game runs, so this cannot change mid-run.
  float mirror = (float)rotationInPlaneSign();
  sign *= mirror;
  // En esta placa, sostenida con la pantalla hacia el dueño y el borde de arriba
  // lejos, un valor positivo del eje de adelante/atrás corresponde a inclinar el
  // borde de arriba hacia uno. Se verificó en hardware: sin este menos la nave
  // subía al inclinar "hacia adelante". El interruptor del panel sigue
  // invirtiendo respecto de este default.
  float signY = -(float)s_cal.signY;
  signY *= mirror;
  float accelLat = (a[s_cal.latAxis] - s_cal.baseline) * sign;
  float accelFwd = (a[s_rotAxis] - s_cal.baselineY) * signY;
  float gyroLat = g[s_rotAxis] * 0.0174533f * sign;   // dps -> rad/s
  // Inclinar el equipo adelante/atrás es un giro sobre el eje lateral, así que
  // la velocidad angular vertical sale del eje lateral. El signo es el opuesto
  // al horizontal: con un eje x/y/z derecho, la misma vuelta que suma en un eje
  // de aceleración resta en el otro. Copiar el signo del eje horizontal hacía
  // que el filtro peleara contra el acelerómetro.
  float gyroFwd = -g[s_cal.latAxis] * 0.0174533f * signY;

  if (!s_tiltPrimed) {
    s_fusedLat = accelLat;
    s_fusedFwd = accelFwd;
    s_tiltPrimed = true;
  }
  // Ambas magnitudes están en la misma escala para ángulos chicos (seno en g y
  // radianes integrados), así que se pueden mezclar directamente.
  s_fusedLat = 0.85f * (s_fusedLat + gyroLat * dt) + 0.15f * accelLat;
  s_fusedFwd = 0.85f * (s_fusedFwd + gyroFwd * dt) + 0.15f * accelFwd;
}

static float tiltToSteering(float fused) {
  float gain = (float)s_cal.gain / 100.0f;
  float u = (fused * gain) / TILT_FULL_SCALE_G;
  if (u > 1.0f) u = 1.0f;
  if (u < -1.0f) u = -1.0f;
  if (fabsf(u) < TILT_DEADZONE) return 0.0f;
  return u > 0 ? (u - TILT_DEADZONE) / (1.0f - TILT_DEADZONE)
               : (u + TILT_DEADZONE) / (1.0f - TILT_DEADZONE);
}

static void step(uint32_t nowMs) {
  float dt = (float)(nowMs - s_lastFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  s_lastFrame = nowMs;
  if (dt <= 0.0f) return;

  // Give the player a moment to settle before the field starts moving.
  if (s_rebaselining) {
    float a[3], g[3];
    imuReadAll(a, g);
    s_rebaselineSum += a[s_cal.latAxis];
    s_rebaselineSumY += a[s_rotAxis];
    s_rebaselineCount++;
    if ((uint32_t)(nowMs - s_rebaselineStart) >= REBASELINE_MS &&
        s_rebaselineCount > 0) {
      s_cal.baseline = s_rebaselineSum / (float)s_rebaselineCount;
      s_cal.baselineY = s_rebaselineSumY / (float)s_rebaselineCount;
      s_rebaselining = false;
      s_tiltPrimed = false;
    }
    return;
  }

  uint32_t elapsed = nowMs - s_playStart;
  // Toda la curva (nivel, cadencia y velocidades) sale de una función pura.
  GameDifficulty diff = gameDifficultyAt(elapsed);
  if (diff.level != s_level) {
    s_level = diff.level;
    rgbPulse(255, 160, 0, 300);
  }
  s_scoreCarry += dt * 8.0f;
  if (s_scoreCarry >= 1.0f) {
    uint32_t whole = (uint32_t)s_scoreCarry;
    s_score += whole;
    s_scoreCarry -= (float)whole;
  }

  readTilt(dt);
  s_shipX += tiltToSteering(s_fusedLat) * SHIP_SPEED_COLS * dt;
  s_shipRow += tiltToSteering(s_fusedFwd) * SHIP_SPEED_ROWS * dt;
  if (s_shipX < 1.0f) s_shipX = 1.0f;
  if (s_shipX > (float)(GRID_COLS - 2)) s_shipX = (float)(GRID_COLS - 2);
  // La fila 0 queda fuera del alcance para que la nave no se pegue a la línea
  // de aparición ni dispare desde arriba del HUD.
  if (s_shipRow < 1.0f) s_shipRow = 1.0f;
  if (s_shipRow > (float)(GRID_ROWS - 1)) s_shipRow = (float)(GRID_ROWS - 1);

  if ((uint32_t)(nowMs - s_lastSpawn) >= diff.meteorIntervalMs) {
    s_lastSpawn = nowMs;
    spawnMeteor(diff.meteorSpeed);
  }

  if ((int32_t)(nowMs - s_nextEnemyAt) >= 0) {
    s_nextEnemyAt = nowMs + diff.enemyIntervalMs;
    spawnEnemy(nowMs, diff);
  }

  for (int i = 0; i < MAX_METEORS; i++) {
    if (!s_meteors[i].alive) continue;
    s_meteors[i].y += s_meteors[i].vy * dt;
    if (s_meteors[i].y > GRID_ROWS + 1) s_meteors[i].alive = false;
  }

  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy &e = s_enemies[i];
    if (!e.alive) continue;
    e.x += e.vx * dt;
    if (e.x < 2.0f) {
      e.x = 2.0f;
      e.vx = fabsf(e.vx);
    } else if (e.x > (float)(GRID_COLS - 3)) {
      e.x = (float)(GRID_COLS - 3);
      e.vx = -fabsf(e.vx);
    }
    if ((int32_t)(nowMs - e.nextFireAt) >= 0) {
      enemyFire(e, diff);
      e.nextFireAt = nowMs + diff.enemyFireMs + (uint32_t)(randomUnit() * 400.0f);
    }
  }

  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    EnemyBullet &b = s_enemyBullets[i];
    if (!b.alive) continue;
    b.y += b.vy * dt;
    if (b.y > GRID_ROWS + 1) b.alive = false;
  }

  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!s_bullets[i].alive) continue;
    s_bullets[i].y -= BULLET_SPEED * dt;
    if (s_bullets[i].y < -1.0f) s_bullets[i].alive = false;
  }

  // Las balas se resuelven antes que las colisiones con la nave, así una piedra
  // destruida en el mismo cuadro no puede además matarte.
  for (int b = 0; b < MAX_BULLETS; b++) {
    if (!s_bullets[b].alive) continue;
    for (int m = 0; m < MAX_METEORS; m++) {
      if (!s_meteors[m].alive) continue;
      if (!gameBulletHitsMeteor(s_bullets[b].x, s_bullets[b].y,
                                s_meteors[m].x, s_meteors[m].y)) {
        continue;
      }
      s_bullets[b].alive = false;
      s_meteors[m].alive = false;
      s_score += (uint32_t)(20 + s_meteors[m].kind * 15);
      rgbPulse(120, 220, 255, 130);
      break;
    }
  }

  // Un caza sólo cae por una bala del jugador; no lo destruyen los meteoritos.
  for (int b = 0; b < MAX_BULLETS; b++) {
    if (!s_bullets[b].alive) continue;
    for (int e = 0; e < MAX_ENEMIES; e++) {
      if (!s_enemies[e].alive) continue;
      if (!gameBulletHitsEnemy(s_bullets[b].x, s_bullets[b].y,
                               s_enemies[e].x, s_enemies[e].y)) {
        continue;
      }
      s_bullets[b].alive = false;
      if (s_enemies[e].hp > 0) s_enemies[e].hp--;
      if (s_enemies[e].hp == 0) {
        s_enemies[e].alive = false;
        s_score += ENEMY_SCORE;
        rgbPulse(255, 120, 40, 220);
      } else {
        // A hit that does not break it still flashes, so the owner sees the bar
        // drop and knows the shot landed.
        rgbPulse(255, 210, 210, 90);
      }
      break;
    }
  }

  // Las balas enemigas bajan más rápido que los meteoritos y también matan.
  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!s_enemyBullets[i].alive) continue;
    if (!gameEnemyShotHitsShip(s_enemyBullets[i].x, s_enemyBullets[i].y,
                               s_shipX, s_shipRow)) {
      continue;
    }
    gameOver(nowMs);
    return;
  }

  // El cuerpo del caza también es sólido: embestirlo termina la partida.
  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!s_enemies[i].alive) continue;
    if (!gameEnemyBodyHitsShip(s_enemies[i].x, s_enemies[i].y, s_shipX,
                               s_shipRow)) {
      continue;
    }
    gameOver(nowMs);
    return;
  }

  for (int m = 0; m < MAX_METEORS; m++) {
    if (!s_meteors[m].alive) continue;
    // The ship is no longer pinned to one row, so the hit box is a band around
    // its current position instead of "below the last row".
    if (!gameMeteorHitsShip(s_meteors[m].x, s_meteors[m].y, s_shipX,
                            s_shipRow)) {
      continue;
    }
    gameOver(nowMs);
    return;
  }

  for (int i = 0; i < STAR_COUNT; i++) {
    s_starY[i] += dt * 1.6f;
    if (s_starY[i] > GRID_ROWS) s_starY[i] -= GRID_ROWS;
  }
}

// --- pantallas --------------------------------------------------------------

// Barra de vida del caza: sólo cuando necesita más de un disparo. Tres celdas de
// ancho, 2 px de alto, arriba del glifo; el relleno dice cuántos disparos faltan.
static void drawEnemyHealth(const Enemy &e) {
  if (e.maxHp <= 1) return;
  int x = cellX(e.x - 1.5f);
  int y = cellY(e.y) - 5;
  int w = 3 * CELL_W;
  gfxFillRect(x, y, w, 2, STYLE_DIM);
  int fill = w * (int)e.hp / (int)e.maxHp;
  if (fill > 0) gfxFillRect(x, y, fill, 2, STYLE_ENEMY);
}

static void drawField(uint32_t nowMs) {
  gfxClear(GFX_BLACK);

  for (int i = 0; i < STAR_COUNT; i++) {
    drawCharAt(s_starX[i], s_starY[i], 1, '.', STYLE_STAR);
  }

  for (int i = 0; i < MAX_METEORS; i++) {
    if (!s_meteors[i].alive) continue;
    uint16_t color = s_meteors[i].kind == 2 ? STYLE_METEOR_HOT : STYLE_METEOR;
    drawCharAt(s_meteors[i].x, s_meteors[i].y - 0.75f, 1, '\'', STYLE_DIM);
    drawCharAt(s_meteors[i].x, s_meteors[i].y, 1, s_meteors[i].glyph, color);
  }

  for (int i = 0; i < MAX_BULLETS; i++) {
    if (!s_bullets[i].alive) continue;
    drawCharAt(s_bullets[i].x, s_bullets[i].y, 1, '|', STYLE_BULLET);
  }

  for (int i = 0; i < MAX_ENEMIES; i++) {
    if (!s_enemies[i].alive) continue;
    drawEnemyHealth(s_enemies[i]);
    drawCharAt(s_enemies[i].x - 1.0f, s_enemies[i].y, 1, '/', STYLE_ENEMY);
    drawCharAt(s_enemies[i].x, s_enemies[i].y, 1, 'V', STYLE_ENEMY);
    drawCharAt(s_enemies[i].x + 1.0f, s_enemies[i].y, 1, '\\', STYLE_ENEMY);
  }

  for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
    if (!s_enemyBullets[i].alive) continue;
    drawCharAt(s_enemyBullets[i].x, s_enemyBullets[i].y, 1, '!',
               STYLE_ENEMY_BULLET);
  }

  bool blink = ((nowMs / 120) % 2) == 0;
  drawCharAt(s_shipX - 1.0f, s_shipRow, 1, '/', STYLE_SHIP);
  drawCharAt(s_shipX, s_shipRow, 1, blink ? '^' : 'A', STYLE_SHIP);
  drawCharAt(s_shipX + 1.0f, s_shipRow, 1, '\\', STYLE_SHIP);

  gfxHLine(0, GRID_Y0 + GRID_ROWS * CELL_H + 1, DISPLAY_W, STYLE_DIM);
}

static void drawHud() {
  char left[32];
  snprintf(left, sizeof(left), "PUNTOS %lu", (unsigned long)s_score);
  gfxText(6, 1, 1, left, STYLE_TEXT);

  char right[44];
  if (g_imuOk) {
    snprintf(right, sizeof(right), "NIV %lu  RECORD %lu",
             (unsigned long)s_level,
             (unsigned long)settingsGet().gameHighScore);
  } else {
    snprintf(right, sizeof(right), "NIV %lu  SIN SENSOR",
             (unsigned long)s_level);
  }
  gfxTextRight(DISPLAY_W - 6, 1, 1, right, g_imuOk ? STYLE_TEXT : STYLE_WARN);
  // The header band is gone, so the HUD keeps its own one-pixel rule to
  // separate the score from the playfield.
  gfxHLine(0, 10, DISPLAY_W, STYLE_DIM);
}

static void drawShipBanner(int cx, int top) {
  static const char *const ART[4] = {
      "   /\\   ",
      "  /  \\  ",
      " / /\\ \\ ",
      "/_/  \\_\\",
  };
  for (int i = 0; i < 4; i++) {
    int w = gfxTextWidth(2, ART[i]);
    gfxText(cx - w / 2, top + i * 16, 2, ART[i], STYLE_SHIP);
  }
}

static void drawReady() {
  gfxClear(GFX_BLACK);
  drawShipBanner(DISPLAY_W / 2, 12);
  gfxTextCentered(84, 2, "ESQUIVA METEORITOS", STYLE_TEXT);
  gfxTextCentered(104, 1, "INCLINA PARA MOVER   TOCA PARA DISPARAR",
                 GFX_WHITE);
  char best[40];
  snprintf(best, sizeof(best), "RECORD %lu",
           (unsigned long)settingsGet().gameHighScore);
  gfxTextCentered(116, 1, best, STYLE_BULLET);
  if (!s_cal.valid) {
    gfxTextCentered(132, 1, "CALIBRA LA INCLINACION EN CONFIG", CHROME_DIM);
  }
  chromeFooter("TOCA: EMPEZAR   MANTENE: MENU");
}

static void drawOver() {
  gfxClear(GFX_BLACK);
  gfxTextCentered(16, 3, "FIN DEL", STYLE_WARN);
  gfxTextCentered(42, 3, "JUEGO", STYLE_WARN);

  char line[48];
  snprintf(line, sizeof(line), "PUNTOS %lu", (unsigned long)s_score);
  gfxTextCentered(88, 2, line, GFX_WHITE);

  bool record = s_score >= settingsGet().gameHighScore && s_score > 0;
  snprintf(line, sizeof(line), "%s %lu", record ? "NUEVO RECORD" : "RECORD",
           (unsigned long)settingsGet().gameHighScore);
  gfxTextCentered(108, 2, line, record ? STYLE_BULLET : CHROME_DIM);
  if (!s_cal.valid) {
    gfxTextCentered(132, 1, "CALIBRA LA INCLINACION EN CONFIG", CHROME_DIM);
  }

  chromeFooter("TOCA: OTRA VEZ   MANTENE: MENU");
}

// --- interfaz de pantalla ---------------------------------------------------

void gameInit() {
  s_cal = settingsGetSteerCalibration();
  if (s_cal.gain == 0) s_cal.gain = 100;
  if (s_cal.sign == 0) s_cal.sign = 1;
  if (s_cal.signY == 0) s_cal.signY = 1;
  deriveRotAxis();
}

void gameEnter() {
  gameInit();
  s_state = GAME_READY;
  s_needRedraw = true;
  resetField();
  rgbSetColor(20, 60, 120);
}

void gameShortPress() {
  uint32_t now = millis();
  switch (s_state) {
    case GAME_READY:
    case GAME_OVER:
      // There is no calibration gesture on the glass anymore: a tap starts or
      // restarts the run at once.
      startGame(now);
      break;
    case GAME_PLAYING:
      fireBullet(now);
      break;
  }
}

bool gameTick(uint32_t nowMs) {
  if (s_state == GAME_PLAYING) {
    step(nowMs);
    if ((uint32_t)(nowMs - s_lastDraw) < 25 && !s_needRedraw) return false;
    s_lastDraw = nowMs;
    s_needRedraw = false;
    drawField(nowMs);
    drawHud();
    return true;
  }

  if (!s_needRedraw) return false;
  s_needRedraw = false;
  if (s_state == GAME_READY) {
    drawReady();
  } else {
    drawOver();
  }
  return true;
}
