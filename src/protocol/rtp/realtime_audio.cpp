module;
#include <cstdint>
#include <expected>
#include <span>
#include <vector>
#include <array>
#include <algorithm>
#include <sodium.h>
module receiver.protocol.ap2.realtime_audio;

std::expected<AuthenticatedRealtimeAudio,RealtimeAudioError> RealtimeEncryptedAudio::decode(
    std::span<const std::uint8_t> wire,std::span<const std::uint8_t> key) {
  if (wire.size() < 34) return std::unexpected(RealtimeAudioError::tooShort);
  if (wire.size() > 4094) return std::unexpected(RealtimeAudioError::tooLarge);
  if (key.empty()) return std::unexpected(RealtimeAudioError::missingKey);
  if (key.size() != 32) return std::unexpected(RealtimeAudioError::invalidKeySize);
  const auto sequence = static_cast<std::uint16_t>((std::uint16_t{wire[0]} << 8) | wire[1]);
  const auto timestamp = (std::uint32_t{wire[2]} << 24) | (std::uint32_t{wire[3]} << 16) |
                         (std::uint32_t{wire[4]} << 8) | wire[5];
  std::array<std::uint8_t,12> nonce{};
  std::copy(wire.end()-8,wire.end(),nonce.begin()+4);
  std::vector<std::uint8_t> plaintext(wire.size()-34);
  unsigned long long length = 0;
  if (crypto_aead_chacha20poly1305_ietf_decrypt(plaintext.data(),&length,nullptr,
      wire.data()+10,wire.size()-18,wire.data()+2,8,nonce.data(),key.data()) != 0)
    return std::unexpected(RealtimeAudioError::authenticationFailed);
  plaintext.resize(length);
  return AuthenticatedRealtimeAudio{sequence,timestamp,std::move(plaintext)};
}
