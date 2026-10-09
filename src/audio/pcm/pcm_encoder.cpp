#include "audio/pcm/pcm_encoder.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>

PcmEncoder::PcmEncoder(RandomSource random) : random_(std::move(random)) {
  if (!random_)
    throw std::invalid_argument("PCM encoding requires a random source");
}

std::optional<PcmEncoder::Encoding> PcmEncoder::encodingFor(sps_format_t format) {
  constexpr bool nativeLittle = std::endian::native == std::endian::little;
  static constexpr std::array encodings{
    Encoding{SPS_FORMAT_S8, 8, 1, true, false, 0},
    Encoding{SPS_FORMAT_U8, 8, 1, true, false, 128},
    Encoding{SPS_FORMAT_S16_LE, 16, 2, true, false, 0},
    Encoding{SPS_FORMAT_S16_BE, 16, 2, false, false, 0},
    Encoding{SPS_FORMAT_S24_LE, 24, 4, true, false, 0},
    Encoding{SPS_FORMAT_S24_BE, 24, 4, false, false, 0},
    Encoding{SPS_FORMAT_S24_3LE, 24, 3, true, false, 0},
    Encoding{SPS_FORMAT_S24_3BE, 24, 3, false, false, 0},
    Encoding{SPS_FORMAT_S32_LE, 32, 4, true, false, 0},
    Encoding{SPS_FORMAT_S32_BE, 32, 4, false, false, 0},
    Encoding{SPS_FORMAT_S16, 16, 2, nativeLittle, true, 0},
    Encoding{SPS_FORMAT_S24, 24, 4, nativeLittle, true, 0},
    Encoding{SPS_FORMAT_S32, 32, 4, nativeLittle, true, 0}
  };
  const auto found = std::ranges::find(encodings, format, &Encoding::format);
  return found == encodings.end() ? std::nullopt : std::optional(*found);
}

bool PcmEncoder::configure(PcmOutputFormat output, unsigned effectiveInputBits) {
  auto encoding = encodingFor(output.sampleFormat);
  if (!encoding || output.channels == 0)
    return false;
  encoding_ = encoding;
  channels_ = output.channels;
  effectiveInputBits_ = effectiveInputBits;
  return true;
}
void PcmEncoder::beginFrame(int gainFixed16, bool mono) {
  gain_ = gainFixed16;
  mono_ = mono;
  bytes_.clear();
}
bool PcmEncoder::dithers() const {
  return encoding_ && (gain_ != 0x10000 || mono_ || effectiveInputBits_ > encoding_->bits);
}
void PcmEncoder::appendSample(int32_t sample) { append(sample, dithers(), false); }

void PcmEncoder::append(int32_t sample, bool dither, bool advanceRandom) {
  if (!encoding_)
    throw std::logic_error("PCM output is not configured");
  constexpr auto maximum = std::numeric_limits<int64_t>::max();
  constexpr auto minimum = std::numeric_limits<int64_t>::min();
  const int64_t product = int64_t(sample) * gain_;
  int64_t scaled = product > maximum / 65536 ? maximum :
                   product < minimum / 65536 ? minimum : product * 65536;
  if (dither || advanceRandom) {
    const uint64_t mask = (uint64_t{1} << (64 - encoding_->bits)) - 1;
    const uint64_t random = static_cast<uint64_t>(random_());
    const int64_t noise = int64_t(random & mask) - int64_t(previousRandom_ & mask);
    previousRandom_ = random;
    if (dither)
      scaled = noise >= 0 ? (scaled > maximum - noise ? maximum : scaled + noise) :
                            (scaled < minimum - noise ? minimum : scaled + noise);
  }
  uint32_t value = static_cast<uint32_t>(scaled >> (64 - encoding_->bits));
  value += encoding_->bias;
  if (!encoding_->signExtend && encoding_->bits < 32)
    value &= (uint32_t{1} << encoding_->bits) - 1;
  for (unsigned byte = 0; byte < encoding_->bytes; ++byte) {
    const unsigned shift = encoding_->littleEndian ? byte * 8 :
                            (encoding_->bytes - byte - 1) * 8;
    bytes_.push_back(static_cast<uint8_t>(value >> shift));
  }
}
EncodedPcm PcmEncoder::finishFrame() {
  if (!encoding_)
    throw std::logic_error("PCM output is not configured");
  const size_t bytesPerFrame = encoding_->bytes * channels_;
  if (bytes_.size() % bytesPerFrame != 0)
    throw std::logic_error("PCM samples do not form complete channel frames");
  const auto frames = bytes_.size() / bytesPerFrame;
  auto output = EncodedPcm(std::move(bytes_), frames);
  bytes_.clear();
  return output;
}
EncodedPcm PcmEncoder::silence(size_t frames, DitherPolicy policy) {
  bytes_.clear();
  const bool dither = policy == DitherPolicy::enabled ||
                      (policy == DitherPolicy::automatic && dithers());
  for (size_t sample = 0; sample < frames * channels_; ++sample)
    append(0, dither, true);
  return finishFrame();
}
void PcmEncoder::reset() {
  bytes_.clear();
  previousRandom_ = 0;
  gain_ = 0x10000;
  mono_ = false;
}
