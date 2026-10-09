#pragma once
#include "audio_decoder.hpp"
#include "resampler.hpp"
#include "pcm_encoder.hpp"

struct SessionState;
void prepareIncomingAudio(SessionState &session, ssrc_t ssrc);
OwnedAudioFrame decodeIncomingAudio(SessionState &session, std::span<const uint8_t> bytes);
ConvertedAudio convertIncomingAudio(SessionState &session, const AVFrame &frame);
EncodedPcm encodeBasicPlaybackPcm(std::span<const int32_t> samples, unsigned channels,
                                 int adjustment, PcmEncoder &encoder);
EncodedPcm encodeInterpolatedPlaybackPcm(std::span<const int32_t> samples, unsigned channels,
                                        int adjustment, PcmEncoder &encoder);
