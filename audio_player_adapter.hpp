#pragma once
#include "audio_decoder.hpp"
#include "resampler.hpp"
#include "pcm_encoder.hpp"

struct SessionState;
void applySessionVolume(double level, SessionState &session);
void prepareIncomingAudio(SessionState &session, ssrc_t ssrc);
OwnedAudioFrame decodeIncomingAudio(SessionState &session, std::span<const uint8_t> bytes);
ConvertedAudio convertIncomingAudio(SessionState &session, const AVFrame &frame);
