#pragma once
#include <cstdint>
#include <optional>

enum class VolumeProfile { standard, flat, dasl };
struct VolumeSettings {
  VolumeProfile profile = VolumeProfile::standard;
  std::optional<double> maximumDb;
  double rangeDb = 0;
  bool hardwarePriority = true, ignoreControl = false;
};
struct VolumeRange { int32_t minimum, maximum; };
struct OutputVolumeCapabilities {
  std::optional<VolumeRange> range;
  bool canSetHardwareVolume = false;
};
struct VolumePlan {
  std::optional<double> hardwareAttenuation;
  double softwareAttenuation = 0, scaledAttenuation = 0;
  std::optional<int> gainFixed16;
  bool requestMute = false, unmute = false;
  bool maximumIgnored = false, rangeIgnored = false;
};
class VolumePolicy {
public:
  static VolumePlan plan(double level, VolumeSettings settings, OutputVolumeCapabilities output);
private:
  static double attenuation(double level, int32_t maximum, int32_t minimum, VolumeProfile);
};
