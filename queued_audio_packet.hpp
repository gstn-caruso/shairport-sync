#pragma once
#include "audio_decoder.hpp"
#include "converted_audio.hpp"
#include "leading_audio_trim.hpp"
#include "resampler.hpp"
#include <bit>

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
    return sampleFormat_;
  }
  AVSampleFormat sampleFormatForConversion() const {
    return sampleFormat_.value_or(format_.isAac() ? AV_SAMPLE_FMT_FLTP :
        format_.sampleBits() == 16 ? AV_SAMPLE_FMT_S16P : AV_SAMPLE_FMT_S32P);
  }
  void mute() {
    if (metadata_.frames == 0)
      metadata_.frames = format_.framesPerPacket();
    frame_.reset();
    audio_.reset();
    trim_ = LeadingAudioTrim{};
  }
  bool trimBefore(uint32_t timestamp) {
    const auto removed = std::bit_cast<int32_t>(timestamp - metadata_.timestamp);
    if (removed <= 0)
      return true;
    if (static_cast<size_t>(removed) >= metadata_.frames)
      return false;
    trim_.add(removed);
    metadata_.timestamp = timestamp;
    metadata_.frames -= removed;
    return true;
  }
  bool startsBefore(uint32_t timestamp) const {
    return std::bit_cast<int32_t>(metadata_.timestamp - timestamp) < 0;
  }
  std::expected<void, ResamplerFailure> convertWith(Resampler &resampler) {
    if (frame_ && !trim_.applyTo(*frame_))
      return std::unexpected(ResamplerFailure{ResamplerFailure::Kind::allocationFailed});
    auto converted = frame_ ? resampler.convert(*frame_) : resampler.silence(metadata_.frames);
    if (!converted)
      return std::unexpected(converted.error());
    audio_ = std::move(*converted);
    metadata_.frames = audio_.frames();
    frame_.reset();
    return {};
  }
  std::span<const uint8_t> audioBytes() const { return audio_.bytes(); }

private:
  QueuedAudioPacket(AudioFormat format, uint16_t sequence, uint32_t timestamp, int32_t gap,
                    OwnedAudioFrame frame)
      : metadata_{sequence, timestamp, frame ? static_cast<size_t>(frame->nb_samples) : 0,
                  gap, format.ssrc(), true}, format_(format), frame_(std::move(frame)),
        decodedCount_(metadata_.frames), sampleFormat_(frame_ ?
            std::optional(static_cast<AVSampleFormat>(frame_->format)) : std::nullopt) {}
  AudioPacketMetadata metadata_;
  AudioFormat format_;
  OwnedAudioFrame frame_;
  ConvertedAudio audio_;
  size_t decodedCount_;
  std::optional<AVSampleFormat> sampleFormat_;
  LeadingAudioTrim trim_;
};
