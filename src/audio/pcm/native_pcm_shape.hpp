#pragma once
#include <cstddef>

class NativePcmShape {
public:
  NativePcmShape() = default;
  NativePcmShape(unsigned channels, unsigned sampleBits, unsigned effectiveBits)
      : channels_(channels), sampleBits_(sampleBits), effectiveBits_(effectiveBits) {}
  unsigned channels() const { return channels_; }
  unsigned sampleBits() const { return sampleBits_; }
  unsigned effectiveBits() const { return effectiveBits_; }
  size_t bytesPerFrame() const { return size_t(channels_) * sampleBits_ / 8; }
  bool operator==(const NativePcmShape &) const = default;
private:
  unsigned channels_ = 0, sampleBits_ = 0, effectiveBits_ = 0;
};
