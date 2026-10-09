#pragma once
#include "utilities/ffmpeg_api.h"
#include <memory>
#include <optional>
#include <span>
#include <utility>

class ConvertedAudio {
public:
  ConvertedAudio() = default;
  ConvertedAudio(ConvertedAudio &&other) noexcept { *this = std::move(other); }
  ConvertedAudio &operator=(ConvertedAudio &&other) noexcept {
    if (this == &other)
      return *this;
    storage_ = std::move(other.storage_);
    byteCount_ = std::exchange(other.byteCount_, 0);
    frames_ = std::exchange(other.frames_, 0);
    retained_ = std::exchange(other.retained_, 0);
    return *this;
  }
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
