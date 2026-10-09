#pragma once
#include "converted_audio.hpp"
#include <cstring>

struct audio_buffer_entry { // decoded audio packets
  void trimBefore(uint32_t target, unsigned bytesPerFrame) {
    const int32_t remove = target - timestamp;
    if (remove <= 0)
      return;
    void *destination = data.bytes().data();
    auto *source = static_cast<char *>(destination) + bytesPerFrame * remove;
    const auto remaining = length - remove;
    std::memmove(destination, source, remaining * bytesPerFrame);
    timestamp = target;
    length = remaining;
  }
  uint8_t ready;
  uint8_t status; // flags
  uint16_t resend_request_number;
  ConvertedAudio data;
  uint16_t sequence_number;
  uint64_t initialisation_time; // the time the packet was added or the time it was noticed the
                                // packet was missing
  uint64_t resend_time;         // time of last resend request or zero
  uint32_t timestamp;           // for timing
  int32_t timestamp_gap;        // the difference between the timestamp and the expected timestamp.
  size_t length; // the length of the decoded data (or silence requested) in input frames
  ssrc_t ssrc;      // this is the type of this specific frame.
  AVFrame *avframe; // Decoded audio carried by FFmpeg before output conversion.
};
using abuf_t = audio_buffer_entry;
