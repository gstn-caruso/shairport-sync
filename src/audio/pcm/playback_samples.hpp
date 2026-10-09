#pragma once
#include "audio/pcm/converted_audio.hpp"
#include "audio/pcm/pcm_encoder.hpp"
#include <functional>
#include <vector>

enum class PlaybackMode { stereo, mono, reverse, left, right };
enum class CorrectionStyle { basic, vernier };
struct Correction {
  CorrectionStyle style;
  int delta;
  int effectiveFor(size_t frames) const;
};

class PlaybackSamples {
public:
  using IndexChooser = std::function<size_t(size_t)>;
  explicit PlaybackSamples(IndexChooser choose);
  bool prepare(const ConvertedAudio &audio, PlaybackMode mode);
  EncodedPcm encode(PcmEncoder &encoder, Correction correction);
private:
  void appendFrame(PcmEncoder &encoder, size_t frame) const;
  void encodeBasic(PcmEncoder &encoder, int delta) const;
  void encodeVernier(PcmEncoder &encoder, int delta) const;
  IndexChooser choose_;
  NativePcmShape shape_;
  std::vector<int32_t> samples_;
};
