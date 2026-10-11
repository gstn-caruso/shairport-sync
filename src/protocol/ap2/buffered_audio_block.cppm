module;
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

export module receiver.protocol.ap2.buffered_block;

export enum class BufferedBlockError { invalidSize, missingKey, invalidKeySize, authenticationFailed, emptyPlaintext };
export enum class BufferedBlockCodec { alac, aac };
export struct BufferedBlockFormat {
  BufferedBlockCodec codec;
  unsigned aacChannelConfiguration = 2;
};
export class BufferedAudioBlock {
public:
  static constexpr std::size_t minimumSize = 36;
  static constexpr std::size_t maximumSize = 16384;
  // The wire storage is borrowed through preparation. Metadata and prepared bytes are owned.
  static std::expected<BufferedAudioBlock, BufferedBlockError> parse(std::span<const std::uint8_t> wire);
  std::uint32_t sequence() const { return sequence_; }
  std::uint32_t timestamp() const { return timestamp_; }
  std::uint32_t ssrc() const { return ssrc_; }
  std::expected<std::vector<std::uint8_t>, BufferedBlockError>
      prepare(BufferedBlockFormat format, std::span<const std::uint8_t> key, unsigned inputRate) const;
private:
  BufferedAudioBlock(std::span<const std::uint8_t> wire,
      std::uint32_t sequence, std::uint32_t timestamp, std::uint32_t ssrc)
      : wire_(wire), sequence_(sequence), timestamp_(timestamp), ssrc_(ssrc) {}
  std::span<const std::uint8_t> wire_;
  std::uint32_t sequence_, timestamp_, ssrc_;
};
