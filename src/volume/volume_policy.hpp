#pragma once
#include "volume/volume_quantities.hpp"
#include <optional>

enum class VolumeProfile { standard, flat, dasl };
struct VolumeSettings {
  VolumeProfile profile = VolumeProfile::standard;
  std::optional<Decibels> maximumDb;
  Decibels rangeDb;
  bool hardwarePriority = true, ignoreControl = false;
};
struct VolumeRange { CentibelAttenuation minimum, maximum; };
struct OutputVolumeCapabilities {
  std::optional<VolumeRange> range;
  bool canSetHardwareVolume = false;
};
struct VolumePlan {
  std::optional<CentibelAttenuation> hardwareAttenuation;
  CentibelAttenuation softwareAttenuation, scaledAttenuation;
  std::optional<FixedGain16> gainFixed16;
  bool requestMute = false, unmute = false;
  bool maximumIgnored = false, rangeIgnored = false;
};
class VolumePolicy {
public:
  static VolumePlan plan(AirPlayVolume level, VolumeSettings settings, OutputVolumeCapabilities output);
private:
  static CentibelAttenuation attenuation(AirPlayVolume level, CentibelAttenuation maximum,
                                         CentibelAttenuation minimum, VolumeProfile);
};
