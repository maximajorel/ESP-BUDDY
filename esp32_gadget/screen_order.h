// Validation for the configurable screen order.
//
// Pure and header-only so the same code the firmware runs is exercised by
// `tools/screen_order_test.cpp`. A corrupt order is not cosmetic: a duplicate
// would make one screen unreachable and another appear twice in the picker.
#pragma once

#include <stdint.h>

static inline bool screenOrderValid(const uint8_t *order, int count) {
  if (!order || count <= 0) return false;
  for (int i = 0; i < count; i++) {
    if (order[i] >= (uint8_t)count) return false;
    for (int j = 0; j < i; j++) {
      if (order[j] == order[i]) return false;
    }
  }
  return true;
}
