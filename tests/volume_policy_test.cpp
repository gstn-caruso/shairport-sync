#include "common.h"
#include "volume_policy.hpp"
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
  VolumeSettings settings;
  settings.rangeDb = 60;
  for (auto profile : {VolumeProfile::standard, VolumeProfile::flat, VolumeProfile::dasl}) {
    settings.profile = profile;
    auto plan = VolumePolicy::plan(-15, settings, {});
    const auto curve = profile == VolumeProfile::standard ? vol2attn :
                       profile == VolumeProfile::flat ? flat_vol2attn : dasl_tapered_vol2attn;
    assert(plan.softwareAttenuation == curve(-15, 0, -6000));
    assert(plan.gainFixed16 == int(65536 * std::pow(10, plan.softwareAttenuation / 2000)));
  }
  settings.profile = VolumeProfile::flat;
  OutputVolumeCapabilities hardware{{VolumeRange{-4000, 0}}, true};
  auto mixed = VolumePolicy::plan(-15, settings, hardware);
  assert(mixed.hardwareAttenuation == -3000 && mixed.softwareAttenuation == 0);
  settings.hardwarePriority = false;
  mixed = VolumePolicy::plan(-15, settings, hardware);
  assert(mixed.hardwareAttenuation == -1000 && mixed.softwareAttenuation == -2000);
  settings.rangeDb = 20;
  auto hardwareOnly = VolumePolicy::plan(-15, settings, hardware);
  assert(hardwareOnly.hardwareAttenuation == -1000 && hardwareOnly.gainFixed16 == 65536);
  assert(VolumePolicy::plan(-144, settings, hardware).requestMute);
  settings.ignoreControl = true;
  auto ignored = VolumePolicy::plan(-144, settings, hardware);
  assert(!ignored.requestMute && !ignored.gainFixed16 && !ignored.unmute);
  assert(VolumePolicy::plan(-15, settings, hardware).hardwareAttenuation == 0);
}
