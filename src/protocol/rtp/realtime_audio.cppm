module;
#include <cstdint>
#include <expected>
#include <span>
#include <vector>
export module receiver.protocol.ap2.realtime_audio;

export enum class RealtimeAudioError { tooShort, tooLarge, missingKey, invalidKeySize, authenticationFailed };
export struct AuthenticatedRealtimeAudio {
  std::uint16_t sequence;
  std::uint32_t timestamp;
  std::vector<std::uint8_t> plaintext;
};
export class RealtimeEncryptedAudio {
public:
  static std::expected<AuthenticatedRealtimeAudio,RealtimeAudioError>
      decode(std::span<const std::uint8_t> stripped, std::span<const std::uint8_t> key);
};
