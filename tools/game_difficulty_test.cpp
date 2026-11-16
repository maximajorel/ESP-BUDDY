// Host-side test for the difficulty curve.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/game_difficulty_test.cpp -o /tmp/gdt && /tmp/gdt
//
// "Harder and faster" is a promise about numbers. Keeping every ramp in one pure
// function makes it checkable: a level every ten seconds, everything that hurts
// the player strictly worse while above its floor or below its cap, and no
// overflow after a game left running for weeks.

#include <cstdio>
#include <cstdint>
#include <cmath>

#include "game_difficulty.h"

static int g_failures = 0;

static void check(const char *what, bool got) {
  if (!got) {
    printf("FAIL %s\n", what);
    g_failures++;
  } else {
    printf("ok   %s\n", what);
  }
}

static void checkInt(const char *what, uint32_t got, uint32_t want) {
  if (got != want) {
    printf("FAIL %-50s got=%lu want=%lu\n", what, (unsigned long)got,
           (unsigned long)want);
    g_failures++;
  } else {
    printf("ok   %-50s %lu\n", what, (unsigned long)got);
  }
}

static bool near(float a, float b) { return fabsf(a - b) < 0.001f; }

int main() {
  printf("-- level boundaries\n");
  checkInt("t=0 is level 1", gameLevelAt(0), 1);
  checkInt("just before ten seconds is still level 1", gameLevelAt(9999), 1);
  checkInt("ten seconds is level 2", gameLevelAt(10000), 2);
  checkInt("125 s is level 13", gameLevelAt(125000), 13);

  printf("-- level 1 is the documented starting point\n");
  {
    GameDifficulty d = gameDifficultyAt(0);
    checkInt("level", d.level, 1);
    checkInt("meteor interval", d.meteorIntervalMs, 750);
    check("meteor speed 4.0", near(d.meteorSpeed, 4.0f));
    checkInt("fighter interval", d.enemyIntervalMs, 12000);
    checkInt("fighter fire period", d.enemyFireMs, 2200);
    check("fighter shot speed 11.0", near(d.enemyBulletSpeed, 11.0f));
    check("fighter drift 4.0", near(d.enemyDriftCols, 4.0f));
    checkInt("a level 1 fighter dies in one shot", d.enemyHp, 1);
    checkInt("first fighter at six seconds", GAME_ENEMY_FIRST_MS, 6000);
  }

  printf("-- every ramp moves in the hard direction, then clamps\n");
  {
    uint32_t prevMeteor = 99999;
    uint32_t prevEnemy = 99999;
    uint32_t prevFire = 99999;
    float prevSpeed = 0.0f;
    float prevShot = 0.0f;
    float prevDrift = 0.0f;
    uint32_t prevHp = 0;
    bool hpGrows = true;
    bool meteorsTighten = true;
    bool fightersMoreOften = true;
    bool fightersFireFaster = true;
    bool meteorsFaster = true;
    bool shotsFaster = true;
    bool driftFaster = true;
    for (uint32_t lv = 1; lv <= 12; lv++) {
      GameDifficulty d = gameDifficultyAt((lv - 1) * 10000);
      if (d.level != lv) {
        printf("FAIL level mismatch at %lu\n", (unsigned long)lv);
        g_failures++;
      }
      if (d.meteorIntervalMs > prevMeteor) meteorsTighten = false;
      if (d.enemyIntervalMs > prevEnemy) fightersMoreOften = false;
      if (d.enemyFireMs > prevFire) fightersFireFaster = false;
      if (d.meteorSpeed < prevSpeed) meteorsFaster = false;
      if (d.enemyBulletSpeed < prevShot) shotsFaster = false;
      if (d.enemyDriftCols < prevDrift) driftFaster = false;
      if (d.enemyHp < prevHp) hpGrows = false;
      prevMeteor = d.meteorIntervalMs;
      prevEnemy = d.enemyIntervalMs;
      prevFire = d.enemyFireMs;
      prevSpeed = d.meteorSpeed;
      prevShot = d.enemyBulletSpeed;
      prevDrift = d.enemyDriftCols;
      prevHp = d.enemyHp;

      check("meteor interval stays 160..750",
            d.meteorIntervalMs >= 160 && d.meteorIntervalMs <= 750);
      check("meteor speed stays 4..18",
            d.meteorSpeed >= 4.0f && d.meteorSpeed <= 18.0f);
      check("fighter interval stays 4500..12000",
            d.enemyIntervalMs >= 4500 && d.enemyIntervalMs <= 12000);
      check("fire period stays 700..2200",
            d.enemyFireMs >= 700 && d.enemyFireMs <= 2200);
      check("shot speed stays 11..20",
            d.enemyBulletSpeed >= 11.0f && d.enemyBulletSpeed <= 20.0f);
      check("drift stays 4..9",
            d.enemyDriftCols >= 4.0f && d.enemyDriftCols <= 9.0f);
      check("fighter HP stays 1..6", d.enemyHp >= 1 && d.enemyHp <= 6);
    }
    check("meteors come more often", meteorsTighten);
    check("fighters come more often", fightersMoreOften);
    check("fighters fire more often", fightersFireFaster);
    check("meteors fall faster", meteorsFaster);
    check("fighter shots fall faster", shotsFaster);
    check("fighters drift faster", driftFaster);
    check("fighters need more shots", hpGrows);
  }

  printf("-- fighters take more shots as the levels pass\n");
  {
    checkInt("level 1: one shot", gameDifficultyAt(0).enemyHp, 1);
    checkInt("level 2: one shot", gameDifficultyAt(10000).enemyHp, 1);
    checkInt("level 3: two shots", gameDifficultyAt(20000).enemyHp, 2);
    checkInt("level 5: three shots", gameDifficultyAt(40000).enemyHp, 3);
    checkInt("level 7: four shots", gameDifficultyAt(60000).enemyHp, 4);
    checkInt("level 9: five shots", gameDifficultyAt(80000).enemyHp, 5);
    checkInt("level 11: six shots", gameDifficultyAt(100000).enemyHp, 6);
  }

  printf("-- a game left running for weeks does not overflow\n");
  {
    GameDifficulty d = gameDifficultyAt(0xFFFFFFFFu);
    check("meteor interval at its floor", d.meteorIntervalMs == 160);
    check("meteor speed at its cap", near(d.meteorSpeed, 18.0f));
    check("fighter interval at its floor", d.enemyIntervalMs == 4500);
    check("fire period at its floor", d.enemyFireMs == 700);
    check("shot speed at its cap", near(d.enemyBulletSpeed, 20.0f));
    checkInt("fighter HP at its cap", d.enemyHp, 6);
    check("drift at its cap", near(d.enemyDriftCols, 9.0f));
    check("level is still a sane number", d.level > 1000);
  }

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
