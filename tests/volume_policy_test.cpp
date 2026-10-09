#include "volume_policy.hpp"
#include <cassert>
#include <cmath>
#include <initializer_list>

int main() {
  VolumeSettings settings;
  settings.rangeDb = 60;
  for (auto profile : {VolumeProfile::standard, VolumeProfile::flat, VolumeProfile::dasl}) {
    settings.profile = profile;
    auto plan = VolumePolicy::plan(-15, settings, {});
    const double expected = profile == VolumeProfile::standard ? -1800 :
                            profile == VolumeProfile::flat ? -3000 : -1000;
    assert(std::abs(plan.softwareAttenuation - expected) < 1e-9);
    assert(plan.gainFixed16 == int(65536 * std::pow(10, plan.softwareAttenuation / 2000)));
    assert(VolumePolicy::plan(0, settings, {}).softwareAttenuation == 0);
    assert(VolumePolicy::plan(-30, settings, {}).softwareAttenuation == -6000);
    assert(VolumePolicy::plan(1, settings, {}).softwareAttenuation == -6000);
    assert(VolumePolicy::plan(-31, settings, {}).softwareAttenuation == -6000);
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
