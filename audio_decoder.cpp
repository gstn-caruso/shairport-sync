#include "audio_decoder.hpp"
#include <climits>
#include <cstring>
#include <libavutil/intreadwrite.h>

namespace {
struct PacketDeleter {
  void operator()(AVPacket *packet) const { av_packet_free(&packet); }
};
using Packet = std::unique_ptr<AVPacket, PacketDeleter>;

bool supplyAlacCookie(AVCodecContext &context, const AudioFormat &format) {
  constexpr int cookieSize = 36;
  context.extradata = static_cast<uint8_t *>(av_mallocz(cookieSize + AV_INPUT_BUFFER_PADDING_SIZE));
  if (!context.extradata)
    return false;
  context.extradata_size = cookieSize;
  // The ALAC atom contains a 12-byte header followed by its network-order ALACSpecificConfig.
  auto *cookie = context.extradata;
  AV_WB32(cookie, cookieSize);
  std::memcpy(cookie + 4, "alac", 4);
  AV_WB32(cookie + 12, format.framesPerPacket());
  cookie[17] = format.sampleBits();
  cookie[18] = 40;
  cookie[19] = 10;
  cookie[20] = 14;
  cookie[21] = format.channels();
  AV_WB16(cookie + 22, 255);
  AV_WB32(cookie + 32, format.sampleRate());
  return true;
}
}

std::expected<Preparation, DecoderFailure> AudioDecoder::prepare(AudioFormat format) {
  std::lock_guard lock(mutex_);
  if (format_ == format)
    return Preparation::unchanged;
  auto *codec = avcodec_find_decoder(format.isAac() ? AV_CODEC_ID_AAC : AV_CODEC_ID_ALAC);
  if (!codec)
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::codecUnavailable});
  Context replacement(avcodec_alloc_context3(codec));
  if (!replacement || (!format.isAac() && !supplyAlacCookie(*replacement, format)))
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::allocationFailed});
  const int opened = avcodec_open2(replacement.get(), codec, nullptr);
  if (opened < 0)
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::openFailed, opened});
  replacement->sample_rate = format.sampleRate();
  context_ = std::move(replacement);
  format_ = format;
  return Preparation::changed;
}

std::expected<OwnedAudioFrame, DecoderFailure> AudioDecoder::decode(std::span<const uint8_t> bytes) {
  std::lock_guard lock(mutex_);
  if (bytes.size() <= 8)
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::packetTooShort});
  if (!context_)
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::notPrepared});
  if (bytes.size() > INT_MAX)
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::packetTooLarge});
  Packet packet(av_packet_alloc());
  if (!packet)
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::allocationFailed});
  const int allocated = av_new_packet(packet.get(), static_cast<int>(bytes.size()));
  if (allocated < 0)
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::allocationFailed, allocated});
  std::memcpy(packet->data, bytes.data(), bytes.size());
  const int sent = avcodec_send_packet(context_.get(), packet.get());
  if (sent < 0)
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::sendFailed, sent});
  OwnedAudioFrame frame(av_frame_alloc());
  if (!frame)
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::allocationFailed});
  const int received = avcodec_receive_frame(context_.get(), frame.get());
  if (received < 0)
    return std::unexpected(DecoderFailure{DecoderFailure::Kind::receiveFailed, received});
  return frame;
}

void AudioDecoder::reset() {
  std::lock_guard lock(mutex_);
  context_.reset();
  format_.reset();
}

std::optional<AudioFormat> AudioDecoder::currentFormat() const {
  std::lock_guard lock(mutex_);
  return format_;
}

std::optional<AVSampleFormat> AudioDecoder::decodedSampleFormat() const {
  std::lock_guard lock(mutex_);
  return context_ ? std::optional(context_->sample_fmt) : std::nullopt;
}
