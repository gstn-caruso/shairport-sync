#include "volume/volume_policy.hpp"
#include <algorithm>
#include <array>
#include <cmath>

double VolumePolicy::attenuation(double level, int32_t maximum, int32_t minimum, VolumeProfile profile) {
  if (level < -30 || level > 0 || !std::isfinite(level)) return minimum;
  if (profile == VolumeProfile::flat) return ((maximum - minimum) * (30.0 + level) / 30) + minimum;
  if (profile == VolumeProfile::dasl) {
    const double fraction = 1 - level / -30.0;
    const double flat = minimum + (maximum - minimum) * fraction;
    if (fraction <= 0) return minimum;
    return std::min(double(maximum), std::max(flat, maximum + 1000 * std::log10(fraction) / std::log10(2)));
  }
  const int32_t range = maximum - minimum;
  const double firstSlope = -range / 2;
  const std::array<std::array<double, 2>, 3> lines{{{0, firstSlope},
      {-5, firstSlope - (range + firstSlope) / 2}, {-17, double(-range)}}};
  double result = 0;
  for (const auto &line : lines)
    if (level <= line[0]) result = std::min(result, line[1] * (level - line[0]) / (-30 - line[0]));
  return result + maximum;
}

VolumePlan VolumePolicy::plan(double level, VolumeSettings settings, OutputVolumeCapabilities output) {
  VolumePlan result;
  int32_t hardwareMaximum = 0, hardwareMinimum = 0;
  int32_t softwareMaximum = 0, softwareMinimum = -9630;
  bool hardware = output.range.has_value(), software = !hardware;
  if (hardware) {
    hardwareMaximum = output.range->maximum;
    hardwareMinimum = output.range->minimum;
    if (settings.maximumDb) {
      const double requested = *settings.maximumDb * 100;
      if (requested <= hardwareMaximum && requested >= hardwareMinimum)
        hardwareMaximum = int32_t(*settings.maximumDb) * 100;
      else if (settings.rangeDb != 0) {
        hardwareMaximum = hardwareMinimum;
        softwareMaximum = requested - hardwareMinimum;
      } else result.maximumIgnored = true;
    }
    if (settings.rangeDb != 0) {
      const int32_t desired = std::trunc(settings.rangeDb * 100);
      if (desired > hardwareMaximum - hardwareMinimum) {
        software = true;
        const int32_t remaining = desired - (hardwareMaximum - hardwareMinimum);
        if (softwareMaximum - remaining < softwareMinimum) result.rangeIgnored = true;
        else softwareMinimum = softwareMaximum - remaining;
      } else hardwareMinimum = hardwareMaximum - desired;
    }
  } else {
    if (settings.maximumDb && *settings.maximumDb * 100 <= softwareMaximum &&
        *settings.maximumDb * 100 >= softwareMinimum)
      softwareMaximum = int32_t(*settings.maximumDb) * 100;
    if (settings.rangeDb != 0) {
      const int32_t desired = std::trunc(settings.rangeDb * 100);
      if (desired > softwareMaximum - softwareMinimum) result.rangeIgnored = true;
      else softwareMinimum = softwareMaximum - desired;
    }
  }
  if (level == -144) {
    result.requestMute = !settings.ignoreControl;
    return result;
  }
  const int32_t maximum = hardware && software ? hardwareMaximum - hardwareMinimum + softwareMaximum - softwareMinimum :
      hardware ? hardwareMaximum : softwareMaximum;
  const int32_t minimum = hardware && software ? 0 : hardware ? hardwareMinimum : softwareMinimum;
  result.scaledAttenuation = settings.ignoreControl ? maximum : attenuation(level, maximum, minimum, settings.profile);
  double hardwareAttenuation = 0;
  if (hardware && software) {
    if (settings.hardwarePriority) {
      if (softwareMaximum - softwareMinimum > result.scaledAttenuation) {
        result.softwareAttenuation = softwareMinimum + result.scaledAttenuation;
        hardwareAttenuation = hardwareMinimum;
      } else {
        result.softwareAttenuation = softwareMaximum;
        hardwareAttenuation = hardwareMinimum + result.scaledAttenuation - (softwareMaximum - softwareMinimum);
      }
    } else {
      if (hardwareMaximum - hardwareMinimum > result.scaledAttenuation) {
        hardwareAttenuation = hardwareMinimum + result.scaledAttenuation;
        result.softwareAttenuation = softwareMinimum;
      } else {
        hardwareAttenuation = hardwareMaximum;
        result.softwareAttenuation = softwareMinimum + result.scaledAttenuation - (hardwareMaximum - hardwareMinimum);
      }
    }
  } else if (hardware) hardwareAttenuation = result.scaledAttenuation;
  else result.softwareAttenuation = result.scaledAttenuation;
  if (hardware && output.canSetHardwareVolume) {
    result.hardwareAttenuation = hardwareAttenuation;
    if (!software) result.gainFixed16 = 65536;
  }
  if (software) result.gainFixed16 = 65536.0 * std::pow(10, result.softwareAttenuation / 2000);
  result.unmute = true;
  return result;
}
