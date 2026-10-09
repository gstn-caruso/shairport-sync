#pragma once
#include "audio_decoder.hpp"
#include "resampler.hpp"
#include "audio/pcm/pcm_encoder.hpp"
#include <functional>

struct SessionState;
void applySessionVolume(double level, SessionState &session);
void applySessionVolumeEffects(double level, SessionState &session,
                               const std::function<void()> &publish = {});
void prepareIncomingAudio(SessionState &session, ssrc_t ssrc);
OwnedAudioFrame decodeIncomingAudio(SessionState &session, std::span<const uint8_t> bytes);
ConvertedAudio convertIncomingAudio(SessionState &session, const AVFrame &frame);
