#include "volume/volume_policy.hpp"
#include <algorithm>
#include <array>
#include <cmath>

CentibelAttenuation VolumePolicy::attenuation(AirPlayVolume requested, CentibelAttenuation upper,
                                             CentibelAttenuation lower, VolumeProfile profile) {
  if (!requested.isPlayable()) return lower;
  const double level = requested.value();
  const int32_t maximum = static_cast<int32_t>(upper.value());
  const int32_t minimum = static_cast<int32_t>(lower.value());
  if (profile == VolumeProfile::flat)
    return CentibelAttenuation{((maximum - minimum) * (30.0 + level) / 30) + minimum};
  if (profile == VolumeProfile::dasl) {
    const double fraction = 1 - level / -30.0;
    const double flat = minimum + (maximum - minimum) * fraction;
    if (fraction <= 0) return lower;
    return CentibelAttenuation{std::min(double(maximum), std::max(flat, maximum + 1000 * std::log10(fraction) / std::log10(2)))};
  }
  const int32_t range = maximum - minimum;
  const double firstSlope = -range / 2;
  const std::array<std::array<double, 2>, 3> lines{{{0, firstSlope},
      {-5, firstSlope - (range + firstSlope) / 2}, {-17, double(-range)}}};
  double result = 0;
  for (const auto &line : lines)
    if (level <= line[0]) result = std::min(result, line[1] * (level - line[0]) / (-30 - line[0]));
  return CentibelAttenuation{result + maximum};
}

namespace {
class EffectiveVolumeRanges {
  CentibelAttenuation hardwareMaximum, hardwareMinimum;
  CentibelAttenuation softwareMaximum, softwareMinimum{-9630};
  bool hardware, software;
  bool maximumIgnored = false, rangeIgnored = false;

  void negotiateHardware(VolumeSettings settings, VolumeRange range) {
    hardwareMaximum = range.maximum;
    hardwareMinimum = range.minimum;
    if (settings.maximumDb) {
      const auto requested = settings.maximumDb->inCentibels();
      if (requested <= hardwareMaximum && requested >= hardwareMinimum)
        hardwareMaximum = settings.maximumDb->maximumAttenuation();
      else if (settings.rangeDb != Decibels{0}) {
        hardwareMaximum = hardwareMinimum;
        softwareMaximum = (requested - hardwareMinimum).wholeCentibels();
      } else maximumIgnored = true;
    }
    if (settings.rangeDb != Decibels{0}) {
      const auto desired = settings.rangeDb.inCentibels().wholeCentibels();
      if (desired > hardwareMaximum - hardwareMinimum) {
        software = true;
        const auto remaining = desired - (hardwareMaximum - hardwareMinimum);
        if (softwareMaximum - remaining < softwareMinimum) rangeIgnored = true;
        else softwareMinimum = softwareMaximum - remaining;
      } else hardwareMinimum = hardwareMaximum - desired;
    }
  }

  void negotiateSoftware(VolumeSettings settings) {
    if (settings.maximumDb && settings.maximumDb->inCentibels() <= softwareMaximum &&
        settings.maximumDb->inCentibels() >= softwareMinimum)
      softwareMaximum = settings.maximumDb->maximumAttenuation();
    if (settings.rangeDb != Decibels{0}) {
      const auto desired = settings.rangeDb.inCentibels().wholeCentibels();
      if (desired > softwareMaximum - softwareMinimum) rangeIgnored = true;
      else softwareMinimum = softwareMaximum - desired;
    }
  }

public:
  EffectiveVolumeRanges(VolumeSettings settings, OutputVolumeCapabilities output)
      : hardware(output.range.has_value()), software(!hardware) {
    if (hardware) negotiateHardware(settings, *output.range);
    else negotiateSoftware(settings);
  }

  CentibelAttenuation maximum() const {
    return hardware && software ? hardwareMaximum - hardwareMinimum + softwareMaximum - softwareMinimum :
        hardware ? hardwareMaximum : softwareMaximum;
  }

  CentibelAttenuation minimum() const {
    return hardware && software ? CentibelAttenuation{0} : hardware ? hardwareMinimum : softwareMinimum;
  }

  VolumePlan initialPlan() const {
    VolumePlan result;
    result.maximumIgnored = maximumIgnored;
    result.rangeIgnored = rangeIgnored;
    return result;
  }

  void allocateAttenuation(VolumePlan &result, bool hardwarePriority, bool canSetHardwareVolume) const {
    CentibelAttenuation hardwareAttenuation;
    if (hardware && software) {
      if (hardwarePriority) {
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
    if (hardware && canSetHardwareVolume) {
      result.hardwareAttenuation = hardwareAttenuation;
      if (!software) result.gainFixed16 = FixedGain16::unity();
    }
    if (software) result.gainFixed16 = result.softwareAttenuation.fixedGain();
  }
};
}

VolumePlan VolumePolicy::plan(AirPlayVolume level, VolumeSettings settings, OutputVolumeCapabilities output) {
  const EffectiveVolumeRanges ranges(settings, output);
  VolumePlan result = ranges.initialPlan();
  if (level.isMute()) {
    result.requestMute = !settings.ignoreControl;
    return result;
  }
  result.scaledAttenuation = settings.ignoreControl ? ranges.maximum() :
      attenuation(level, ranges.maximum(), ranges.minimum(), settings.profile);
  ranges.allocateAttenuation(result, settings.hardwarePriority, output.canSetHardwareVolume);
  result.unmute = true;
  return result;
}
