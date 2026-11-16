// Host-side test for the Meteoritos hit boxes.
//
//   g++ -std=c++17 -O1 -I esp32_gadget tools/game_hits_test.cpp -o /tmp/ght && /tmp/ght
//
// The owner's rule is "the fighter only dies when you shoot it". That is a
// combination of boxes: a player bullet must hit it on the same row, its shots
// must hit the ship, and its body must be solid. Each boundary is checked from
// both sides so a widened box cannot silently turn ramming into a kill.

#include <cstdio>

#include "game_hits.h"

static int g_failures = 0;

static void check(const char *what, bool got, bool want) {
  if (got != want) {
    printf("FAIL %-58s got=%d want=%d\n", what, (int)got, (int)want);
    g_failures++;
  } else {
    printf("ok   %-58s %d\n", what, (int)got);
  }
}

int main() {
  printf("-- a player bullet kills a fighter on its row\n");
  check("dead centre", gameBulletHitsEnemy(10.0f, 5.0f, 10.0f, 5.0f), true);
  check("1.3 columns off still hits", gameBulletHitsEnemy(10.0f, 5.0f, 11.3f, 5.0f),
        true);
  check("1.5 columns off misses", gameBulletHitsEnemy(10.0f, 5.0f, 11.5f, 5.0f),
        false);
  check("one row above still hits", gameBulletHitsEnemy(10.0f, 5.0f, 10.0f, 4.0f),
        true);
  check("more than a row above misses",
        gameBulletHitsEnemy(10.0f, 5.0f, 10.0f, 3.9f), false);

  printf("-- meteor and fighter boxes are different on purpose\n");
  check("a bullet passes a meteor 1.2 columns aside",
        gameBulletHitsMeteor(10.0f, 5.0f, 11.2f, 5.0f), false);
  check("a fighter is wider than a meteor",
        gameBulletHitsEnemy(10.0f, 5.0f, 11.2f, 5.0f), true);

  printf("-- the fighter's shot kills the ship\n");
  check("same cell", gameEnemyShotHitsShip(10.0f, 10.0f, 10.0f, 10.0f), true);
  check("1.25 columns off still hits",
        gameEnemyShotHitsShip(10.0f, 10.0f, 11.25f, 10.0f), true);
  check("1.4 columns off misses",
        gameEnemyShotHitsShip(10.0f, 10.0f, 11.4f, 10.0f), false);
  check("0.65 rows off still hits",
        gameEnemyShotHitsShip(10.0f, 10.0f, 10.0f, 10.65f), true);
  check("0.8 rows off misses",
        gameEnemyShotHitsShip(10.0f, 10.0f, 10.0f, 10.8f), false);

  printf("-- ramming the fighter kills the ship\n");
  check("body contact", gameEnemyBodyHitsShip(10.0f, 3.0f, 10.0f, 3.0f), true);
  check("1.7 columns off still hits",
        gameEnemyBodyHitsShip(10.0f, 3.0f, 11.7f, 3.0f), true);
  check("1.9 columns off misses",
        gameEnemyBodyHitsShip(10.0f, 3.0f, 11.9f, 3.0f), false);

  printf("-- meteor against the ship\n");
  check("same cell", gameMeteorHitsShip(10.0f, 10.0f, 10.0f, 10.0f), true);
  check("1.5 columns off still hits",
        gameMeteorHitsShip(10.0f, 10.0f, 11.5f, 10.0f), true);
  check("1.7 columns off misses",
        gameMeteorHitsShip(10.0f, 10.0f, 11.7f, 10.0f), false);

  printf("\n%s (%d failures)\n", g_failures == 0 ? "PASSED" : "FAILED",
         g_failures);
  return g_failures == 0 ? 0 : 1;
}
