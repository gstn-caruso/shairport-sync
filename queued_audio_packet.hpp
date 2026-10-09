#pragma once
#include "audio_decoder.hpp"
#include "converted_audio.hpp"

struct AudioPacketMetadata {
  uint16_t sequence;
  uint32_t timestamp;
  size_t frames;
  int32_t timestampGap;
  ssrc_t encoding;
  bool ready;
};

class QueuedAudioPacket {
public:
  static QueuedAudioPacket decoded(AudioFormat format, uint16_t sequence, uint32_t timestamp,
                                   int32_t gap, OwnedAudioFrame frame) {
    return QueuedAudioPacket(format, sequence, timestamp, gap, std::move(frame));
  }
  AudioPacketMetadata metadata() const { return metadata_; }
  AudioFormat format() const { return format_; }
  size_t samplesDecoded() const { return decodedCount_; }
  std::optional<AVSampleFormat> decodedSampleFormat() const {
    return frame_ ? std::optional(static_cast<AVSampleFormat>(frame_->format)) : std::nullopt;
  }

private:
  QueuedAudioPacket(AudioFormat format, uint16_t sequence, uint32_t timestamp, int32_t gap,
                    OwnedAudioFrame frame)
      : metadata_{sequence, timestamp, frame ? static_cast<size_t>(frame->nb_samples) : 0,
                  gap, format.ssrc(), true}, format_(format), frame_(std::move(frame)),
        decodedCount_(metadata_.frames) {}
  AudioPacketMetadata metadata_;
  AudioFormat format_;
  OwnedAudioFrame frame_;
  ConvertedAudio audio_;
  size_t decodedCount_;
};
