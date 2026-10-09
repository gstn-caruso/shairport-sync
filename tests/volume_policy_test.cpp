#include "common.h"
#include <cassert>
#include <cmath>
#include <initializer_list>

int main() {
  for (auto curve : {vol2attn, flat_vol2attn, dasl_tapered_vol2attn}) {
    assert(curve(0, 0, -6000) == 0);
    assert(curve(-30, 0, -6000) == -6000);
    assert(curve(-144, 0, -6000) == -6000);
    assert(curve(1, 0, -6000) == -6000);
    assert(curve(-31, 0, -6000) == -6000);
  }
  assert(vol2attn(-15, 0, -6000) == -1800);
  assert(flat_vol2attn(-15, 0, -6000) == -3000);
  assert(std::abs(dasl_tapered_vol2attn(-15, 0, -6000) + 1000) < 1e-9);
}
