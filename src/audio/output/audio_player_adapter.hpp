#pragma once
#include "audio/decoding/audio_decoder.hpp"
#include "audio/resampling/resampler.hpp"
#include "audio/pcm/pcm_encoder.hpp"
#include <functional>

struct SessionState;
void applySessionVolume(double level, SessionState &session);
void applySessionVolumeEffects(double level, SessionState &session,
                               const std::function<void()> &publish = {});
void prepareIncomingAudio(SessionState &session, ssrc_t ssrc);
OwnedAudioFrame decodeIncomingAudio(SessionState &session, std::span<const uint8_t> bytes);
ConvertedAudio convertIncomingAudio(SessionState &session, const AVFrame &frame);
