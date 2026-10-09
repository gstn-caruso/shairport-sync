#include "playback_sync.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <optional>

namespace {
constexpr uint64_t second = 1000000000;
std::optional<uint64_t> frameDuration(uint64_t frames, unsigned rate) {
  if (!rate || frames / rate > UINT64_MAX / second)
    return {};
  const auto whole = frames / rate * second;
  const auto fraction = frames % rate * second / rate;
  if (whole > UINT64_MAX - fraction)
    return {};
  return whole + fraction;
}
std::optional<uint64_t> framesDuring(uint64_t nanoseconds, unsigned rate, bool rounded = false) {
  if (!rate || nanoseconds / second > UINT64_MAX / rate)
    return {};
  const auto whole = nanoseconds / second * rate;
  const auto fraction = (nanoseconds % second * rate + (rounded ? second / 2 : 0)) / second;
  if (whole > UINT64_MAX - fraction)
    return {};
  return whole + fraction;
}
std::optional<int64_t> signedFramesDuring(int64_t nanoseconds, unsigned rate) {
  const bool negative = nanoseconds < 0;
  const uint64_t magnitude = negative ? uint64_t{0} - uint64_t(nanoseconds) : uint64_t(nanoseconds);
  const auto frames = framesDuring(magnitude, rate);
  if (!frames || *frames > uint64_t(INT64_MAX) + negative)
    return {};
  return std::bit_cast<int64_t>(negative ? uint64_t{0} - *frames : *frames);
}
int64_t halfSum(int64_t left, int64_t right) {
  const auto whole = left / 2 + right / 2;
  const auto remainder = left % 2 + right % 2;
  if (whole > 0 && remainder < 0)
    return whole - (-remainder + 1) / 2;
  if (whole < 0 && remainder > 0)
    return whole + (remainder + 1) / 2;
  return whole + remainder / 2;
}
}
void PlaybackSync::Window::add(int64_t error) {
  values[next] = error;
  next = (next + 1) % values.size();
  count = std::min(count + 1, values.size());
  highest = lowest = secondHighest = secondLowest = values[0];
  for (size_t index = 0; index < count; ++index) {
    const auto value = values[index];
    if (value > highest) { secondHighest = highest; highest = value; }
    else if (value > secondHighest) secondHighest = value;
    else if (value < lowest) { secondLowest = lowest; lowest = value; }
    else if (value < secondLowest) secondLowest = value;
  }
}
int64_t PlaybackSync::Window::center() const { return halfSum(secondHighest, secondLowest); }
int64_t PlaybackSync::Window::spread() const {
  const auto difference = uint64_t(secondHighest) - uint64_t(secondLowest);
  return static_cast<int64_t>(std::min(difference, uint64_t(INT64_MAX)));
}
SyncDecision PlaybackSync::observe(const SyncObservation &observation, SyncPolicy policy) {
  SyncDecision decision;
  if (!observation.measured || !observation.inputRate || !observation.outputRate ||
      observation.framesInDac > UINT64_MAX - previousRetained_)
    return decision;
  const auto delay = frameDuration(observation.framesInDac + previousRetained_, observation.outputRate);
  previousRetained_ = observation.retainedFrames;
  if (!delay || observation.measurementDuration < 0 || observation.measurementDuration >= 2000000)
    return decision;
  decision.errorNs = std::bit_cast<int64_t>(
      observation.dacMeasurementTime + *delay - observation.expectedFrameTime);
  const auto errorFrames = signedFramesDuring(decision.errorNs, observation.outputRate);
  if (!errorFrames)
    return {};
  decision.errorFrames = *errorFrames;
  if (skippingInitial_) {
    pendingSkip_ = static_cast<uint64_t>(std::max(decision.errorFrames, int64_t{0}));
    if (!pendingSkip_)
      skippingInitial_ = false;
  }
  if (observation.firstFrame || observation.timestampGap != 0) {
    auto gap = decision.errorFrames;
    if (observation.firstFrame)
      skippingInitial_ = true;
    else if (observation.timestampGap < 0) {
      const auto duration = frameDuration(uint64_t(-int64_t(observation.timestampGap)), observation.inputRate);
      const auto count = duration ? framesDuring(*duration, observation.outputRate, true) : std::nullopt;
      if (!count || *count > uint64_t(INT64_MAX))
        return {};
      gap = static_cast<int64_t>(*count);
    }
    if (gap > 0) {
      if (uint64_t(gap) > UINT64_MAX - pendingSkip_)
        return {};
      pendingSkip_ += gap;
      decision.errorNs = 0;
    } else if (gap < 0) {
      pendingSkip_ = 0;
      skippingInitial_ = false;
      decision.silenceFrames = uint64_t{0} - uint64_t(gap);
      decision.errorNs = decision.errorFrames = 0;
    }
  }
  int64_t centered = 0;
  if (!pendingSkip_) {
    window_.add(decision.errorNs);
    decision.windowSpreadNs = window_.spread();
    centered = window_.center();
    const bool settled = observation.blockFrames &&
        observation.playNumber >= 10240 / observation.blockFrames +
            (10240 % observation.blockFrames != 0);
    if (policy.enabled && settled) {
      const auto correction = static_cast<int>(std::max(size_t{1}, observation.blockFrames / 350));
      if (centered > policy.toleranceNs) decision.correctionFrames = -correction;
      else if (centered < -policy.toleranceNs) decision.correctionFrames = correction;
    }
  }
  const double centeredSeconds = centered * 0.000000001;
  if (policy.enabled && observation.timestamp && policy.resyncThresholdSeconds > 0 &&
      window_.full() && !window_.crossesZero() &&
      (centeredSeconds > policy.resyncThresholdSeconds ||
       centeredSeconds < -policy.resyncThresholdSeconds)) {
    decision.dropPacket = true;
    decision.resyncNext = centered < 0;
  }
  return decision;
}
size_t PlaybackSync::skipFrom(size_t encodedFrames) {
  if (!pendingSkip_)
    return 0;
  const auto skipped = static_cast<size_t>(std::min(pendingSkip_, uint64_t(encodedFrames)));
  pendingSkip_ -= skipped;
  if (!pendingSkip_)
    skippingInitial_ = false;
  return skipped;
}
void PlaybackSync::resetForPlay() { *this = PlaybackSync{}; }
