#pragma once

#include "audio_format.hpp"

class AudioInputState {
public:
  unsigned sampleRate() const { return sampleRate_; }
  unsigned framesPerPacket() const { return framesPerPacket_; }
  bool isDecodedFormatValid() const { return decodedFormatValid_; }

  void recordPacketShape(const AudioFormat &format) {
    sampleRate_ = format.sampleRate();
    framesPerPacket_ = format.framesPerPacket();
  }

private:
  unsigned sampleRate_ = 0;
  unsigned framesPerPacket_ = 0;
  bool decodedFormatValid_ = false;
};
