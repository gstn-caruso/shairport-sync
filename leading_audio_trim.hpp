#pragma once
#include "utilities/ffmpeg_api.h"
#include <algorithm>

class LeadingAudioTrim {
public:
  void add(size_t frames) { frames_ += frames; }
  bool applyTo(AVFrame &frame) {
    const size_t removed = std::min(frames_, static_cast<size_t>(std::max(frame.nb_samples, 0)));
    if (removed != 0) {
      if (av_frame_make_writable(&frame) < 0)
        return false;
      const auto format = static_cast<AVSampleFormat>(frame.format);
      const bool planar = av_sample_fmt_is_planar(format);
      const unsigned channels =
#if LIBAVUTIL_VERSION_MAJOR >= 57
          frame.ch_layout.nb_channels;
#else
          frame.channels;
#endif
      const unsigned planes = planar ? channels : 1;
      const size_t stride = av_get_bytes_per_sample(format) * (planar ? 1 : channels);
      for (unsigned index = 0; index < planes; ++index) {
        auto *start = frame.extended_data[index] + removed * stride;
        frame.extended_data[index] = start;
        if (index < AV_NUM_DATA_POINTERS)
          frame.data[index] = start;
      }
      frame.nb_samples -= removed;
    }
    frames_ = 0;
    return true;
  }
private:
  size_t frames_ = 0;
};
