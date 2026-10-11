module;
#include <cstdint>
#include <expected>
#include <span>
#include <vector>
#include <array>
#include <optional>
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
export struct DatagramRead { std::size_t count; int errorCode = 0; };
export class DatagramInput {
public:
  virtual ~DatagramInput() = default;
  // Successful reads initialize count bytes and never report more than destination.size().
  virtual DatagramRead read(std::span<std::uint8_t> destination) = 0;
};
export class RandomSource {
public:
  virtual ~RandomSource() = default;
  virtual double draw() = 0;
};
export enum class RealtimeReceiveKind { readError, shortPacket, dropped, rejected, audio };
export struct RealtimeReceiveOutcome {
  RealtimeReceiveKind kind;
  int errorCode = 0;
  RealtimeAudioError rejection = RealtimeAudioError::tooShort;
  std::optional<AuthenticatedRealtimeAudio> audio{};
};
export class RealUDPIngress {
public:
  RealUDPIngress(DatagramInput &input, RandomSource &random, double dropFraction)
      : input_(input), random_(random), dropFraction_(dropFraction) {}
  RealtimeReceiveOutcome one(std::span<const std::uint8_t> key);
private:
  DatagramInput &input_;
  RandomSource &random_;
  const double dropFraction_;
  std::array<std::uint8_t,4096> buffer_;
};
