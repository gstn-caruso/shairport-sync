#pragma once
#include "utilities/ffmpeg_api.h"
#include <memory>
#include <optional>
#include <span>

class ConvertedAudio {
public:
  static std::optional<ConvertedAudio> allocate(size_t bytes, size_t frames, int64_t retained) {
    ConvertedAudio audio;
    if (bytes != 0) {
      audio.storage_.reset(static_cast<uint8_t *>(av_mallocz(bytes)));
      if (!audio.storage_)
        return std::nullopt;
    }
    audio.byteCount_ = bytes;
    audio.frames_ = frames;
    audio.retained_ = retained;
    return audio;
  }
  std::span<uint8_t> bytes() { return {storage_.get(), byteCount_}; }
  std::span<const uint8_t> bytes() const { return {storage_.get(), byteCount_}; }
  size_t frames() const { return frames_; }
  int64_t retainedFrames() const { return retained_; }
  explicit operator bool() const { return storage_ != nullptr; }
  void reset() { *this = ConvertedAudio{}; }

private:
  struct Deleter { void operator()(uint8_t *bytes) const { av_free(bytes); } };
  std::unique_ptr<uint8_t, Deleter> storage_;
  size_t byteCount_ = 0, frames_ = 0;
  int64_t retained_ = 0;
};
