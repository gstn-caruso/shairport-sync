#include "audio/pcm/playback_samples.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

int Correction::effectiveFor(size_t frames) const {
  if (frames < 100)
    return 0;
  return style == CorrectionStyle::basic ? std::clamp(delta, -1, 1) :
         delta >= -20 && delta <= 20 ? delta : 0;
}
PlaybackSamples::PlaybackSamples(IndexChooser choose) : choose_(std::move(choose)) {
  if (!choose_)
    throw std::invalid_argument("Basic correction requires an index chooser");
}
bool PlaybackSamples::prepare(const ConvertedAudio &audio, PlaybackMode mode) {
  samples_.clear();
  shape_ = {};
  if (audio.frames() == 0)
    return audio.bytes().empty();
  const auto shape = audio.shape();
  if (shape.channels() == 0 || (shape.sampleBits() != 16 && shape.sampleBits() != 32) ||
      audio.frames() > std::numeric_limits<size_t>::max() / shape.bytesPerFrame() ||
      audio.bytes().size() != audio.frames() * shape.bytesPerFrame())
    return false;
  samples_.resize(audio.frames() * shape.channels());
  for (size_t sample = 0; sample < samples_.size(); ++sample) {
    if (shape.sampleBits() == 16) {
      int16_t value;
      std::memcpy(&value, audio.bytes().data() + sample * 2, 2);
      samples_[sample] = int32_t(value) * 65536;
    } else {
      std::memcpy(&samples_[sample], audio.bytes().data() + sample * 4, 4);
    }
  }
  shape_ = shape;
  if (shape.channels() == 2)
    for (size_t frame = 0; frame < audio.frames(); ++frame) {
      auto &left = samples_[frame * 2];
      auto &right = samples_[frame * 2 + 1];
      switch (mode) {
      case PlaybackMode::mono:
        left = right = static_cast<int32_t>((int64_t(left) + right) >> 1);
        break;
      case PlaybackMode::reverse: std::swap(left, right); break;
      case PlaybackMode::left: right = left; break;
      case PlaybackMode::right: left = right; break;
      case PlaybackMode::stereo: break;
      }
    }
  return true;
}
void PlaybackSamples::appendFrame(PcmEncoder &encoder, size_t frame) const {
  for (unsigned channel = 0; channel < shape_.channels(); ++channel)
    encoder.appendSample(samples_[frame * shape_.channels() + channel]);
}
void PlaybackSamples::encodeBasic(PcmEncoder &encoder, int delta) const {
  const size_t frames = samples_.size() / shape_.channels();
  const size_t correctedAt = delta == 0 ? frames : choose_(frames);
  if (delta != 0 && (correctedAt == 0 || correctedAt >= frames - 1))
    throw std::out_of_range("Basic correction requires an interior frame");
  for (size_t frame = 0; frame < correctedAt; ++frame)
    appendFrame(encoder, frame);
  if (delta > 0)
    for (unsigned channel = 0; channel < shape_.channels(); ++channel) {
      const int64_t before = samples_[(correctedAt - 1) * shape_.channels() + channel];
      const int64_t after = samples_[correctedAt * shape_.channels() + channel];
      encoder.appendSample(static_cast<int32_t>((before + after) / 2));
    }
  for (size_t frame = correctedAt + (delta < 0 ? 1 : 0); frame < frames; ++frame)
    appendFrame(encoder, frame);
}
void PlaybackSamples::encodeVernier(PcmEncoder &encoder, int delta) const {
  const size_t frames = samples_.size() / shape_.channels();
  if (delta == 0) {
    for (size_t frame = 0; frame < frames; ++frame)
      appendFrame(encoder, frame);
    return;
  }
  const size_t outputFrames = static_cast<size_t>(int64_t(frames) + delta);
  constexpr int64_t one = int64_t{1} << 32;
  const int64_t step = one * (frames - 1) / (outputFrames - 1);
  int64_t position = 0;
  for (size_t frame = 0; frame < outputFrames; ++frame) {
    const size_t before = std::min(static_cast<size_t>(position >> 32), frames - 1);
    const size_t after = std::min(before + 1, frames - 1);
    const int64_t afterWeight = position & 0xffffffff;
    for (unsigned channel = 0; channel < shape_.channels(); ++channel) {
      const int64_t left = samples_[before * shape_.channels() + channel];
      const int64_t right = samples_[after * shape_.channels() + channel];
      encoder.appendSample(static_cast<int32_t>(
          (left * (one - afterWeight) + right * afterWeight) / one));
    }
    position += step;
  }
}
EncodedPcm PlaybackSamples::encode(PcmEncoder &encoder, Correction correction) {
  if (samples_.empty() || samples_.size() / shape_.channels() < 3)
    return {};
  const int delta = correction.effectiveFor(samples_.size() / shape_.channels());
  if (correction.style == CorrectionStyle::basic)
    encodeBasic(encoder, delta);
  else
    encodeVernier(encoder, delta);
  return encoder.finishFrame();
}
