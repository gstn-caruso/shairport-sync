#pragma once

#include "audio/format/audio_format.hpp"

class AudioInputState {
public:
  unsigned sampleRate() const { return sampleRate_; }
  unsigned framesPerPacket() const { return framesPerPacket_; }
  bool isDecodedFormatValid() const { return decodedFormatValid_; }

  void recordPacketShape(const AudioFormat &format) {
    sampleRate_ = format.sampleRate();
    framesPerPacket_ = format.framesPerPacket();
  }

  void recordDecodedFormat(const AudioFormat &format) {
    recordPacketShape(format);
    decodedFormatValid_ = true;
  }

  void setSetupSampleRate(unsigned rate) { sampleRate_ = rate; }
  void setSetupPacketFrames(unsigned frames) { framesPerPacket_ = frames; }
  void beginPlayback() { framesPerPacket_ = 352; }

private:
  unsigned sampleRate_ = 0;
  unsigned framesPerPacket_ = 0;
  bool decodedFormatValid_ = false;
};
