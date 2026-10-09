#pragma once
#include "audio_decoder.hpp"
#include "resampler.hpp"

struct SessionState;
void prepareIncomingAudio(SessionState &session, ssrc_t ssrc);
OwnedAudioFrame decodeIncomingAudio(SessionState &session, std::span<const uint8_t> bytes);
ConvertedAudio convertIncomingAudio(SessionState &session, const AVFrame &frame);
