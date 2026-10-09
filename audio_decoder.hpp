#pragma once

#include "audio_format.hpp"
#include "utilities/ffmpeg_api.h"
#include <expected>
#include <memory>
#include <mutex>
#include <span>

struct AudioFrameDeleter {
  void operator()(AVFrame *frame) const { av_frame_free(&frame); }
};
using OwnedAudioFrame = std::unique_ptr<AVFrame, AudioFrameDeleter>;
enum class Preparation { changed, unchanged };
struct DecoderFailure {
  enum class Kind { notPrepared, packetTooShort, packetTooLarge, codecUnavailable,
                    allocationFailed, openFailed, sendFailed, receiveFailed };
  Kind kind;
  int nativeCode = 0;
};

class AudioDecoder {
public:
  std::expected<Preparation, DecoderFailure> prepare(AudioFormat format);
  std::expected<OwnedAudioFrame, DecoderFailure> decode(std::span<const uint8_t> bytes);
  void reset();
  std::optional<AudioFormat> currentFormat() const;
  std::optional<AVSampleFormat> decodedSampleFormat() const;

private:
  struct ContextDeleter {
    void operator()(AVCodecContext *context) const { avcodec_free_context(&context); }
  };
  using Context = std::unique_ptr<AVCodecContext, ContextDeleter>;
  mutable std::mutex mutex_;
  Context context_;
  std::optional<AudioFormat> format_;
};
