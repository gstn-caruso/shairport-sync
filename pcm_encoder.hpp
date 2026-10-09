#pragma once
#include "audio_types.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

struct PcmOutputFormat { sps_format_t sampleFormat; unsigned channels; };
enum class DitherPolicy { automatic, disabled, enabled };

class EncodedPcm {
public:
  EncodedPcm() = default;
  std::span<uint8_t> bytes() { return bytes_; }
  std::span<const uint8_t> bytes() const { return bytes_; }
  size_t frames() const { return frames_; }
private:
  friend class PcmEncoder;
  EncodedPcm(std::vector<uint8_t> bytes, size_t frames)
      : bytes_(std::move(bytes)), frames_(frames) {}
  std::vector<uint8_t> bytes_;
  size_t frames_ = 0;
};

class PcmEncoder {
public:
  using RandomSource = std::function<int64_t()>;
  explicit PcmEncoder(RandomSource random);
  bool configure(PcmOutputFormat output, unsigned effectiveInputBits);
  void beginFrame(int gainFixed16, bool mono);
  void appendSample(int32_t sample);
  EncodedPcm finishFrame();
  EncodedPcm silence(size_t frames, DitherPolicy policy = DitherPolicy::automatic);
  bool dithers() const;
  void reset();
private:
  struct Encoding {
    sps_format_t format;
    unsigned bits, bytes;
    bool littleEndian, signExtend;
    unsigned bias;
  };
  static std::optional<Encoding> encodingFor(sps_format_t format);
  void append(int32_t sample, bool dither, bool advanceRandom);
  RandomSource random_;
  std::optional<Encoding> encoding_;
  unsigned channels_ = 0, effectiveInputBits_ = 0;
  int gain_ = 0x10000;
  bool mono_ = false;
  uint64_t previousRandom_ = 0;
  std::vector<uint8_t> bytes_;
};
