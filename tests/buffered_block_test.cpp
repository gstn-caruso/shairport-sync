#include "buffered_block_fixture.hpp"
#include <gtest/gtest.h>
import receiver.protocol.ap2.buffered_block;

TEST(BufferedAudioBlock, CopiesBigEndianMetadataAndMasksSequenceToTwentyThreeBits) {
  auto parsed = BufferedAudioBlock::parse(buffered_block_fixture::golden);
  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed->sequence(), 0x2bcdefu);
  EXPECT_EQ(parsed->timestamp(), 0x01020304u);
  EXPECT_EQ(parsed->ssrc(), 0x16000000u);
}

TEST(BufferedAudioBlock, GoldenAlacPayloadAuthenticatesAndOwnsPreparedBytes) {
  auto wire = buffered_block_fixture::encrypt();
  auto block = BufferedAudioBlock::parse(wire);
  ASSERT_TRUE(block);
  auto payload = block->prepare({BufferedBlockCodec::alac}, buffered_block_fixture::key, 44100);
  ASSERT_TRUE(payload);
  EXPECT_EQ(*payload, std::vector<uint8_t>(buffered_block_fixture::plaintext.begin(), buffered_block_fixture::plaintext.end()));
  std::fill(wire.begin(), wire.end(), 0);
  EXPECT_EQ(block->timestamp(), 0x01020304u);
  EXPECT_EQ((*payload)[0], 0x10);
  EXPECT_EQ((*payload)[15], 0x0f);
}

struct AacCase { std::string name; unsigned rate, channels; std::array<uint8_t,7> header; };
void PrintTo(const AacCase &scenario, std::ostream *output) { *output << scenario.name; }
class AacPreparation : public testing::TestWithParam<AacCase> {};
TEST_P(AacPreparation, PrependsFrozenAdtsBytesWithoutChangingPlaintext) {
  auto block = BufferedAudioBlock::parse(buffered_block_fixture::golden);
  ASSERT_TRUE(block);
  const auto &scenario = GetParam();
  auto prepared = block->prepare({BufferedBlockCodec::aac, scenario.channels}, buffered_block_fixture::key, scenario.rate);
  ASSERT_TRUE(prepared);
  std::vector<uint8_t> expected(scenario.header.begin(), scenario.header.end());
  expected.insert(expected.end(), buffered_block_fixture::plaintext.begin(), buffered_block_fixture::plaintext.end());
  EXPECT_EQ(*prepared, expected);
}
INSTANTIATE_TEST_SUITE_P(Rates, AacPreparation, testing::Values(
  AacCase{"Stereo44100",44100,2,{0xff,0xf9,0x50,0x80,0x02,0xff,0xfc}},
  AacCase{"Stereo48000",48000,2,{0xff,0xf9,0x4c,0x80,0x02,0xff,0xfc}},
  AacCase{"EightChannels48000",48000,7,{0xff,0xf9,0x4d,0xc0,0x02,0xff,0xfc}},
  AacCase{"UnsupportedRateFallback",32000,2,{0xff,0xf9,0x50,0x80,0x02,0xff,0xfc}}
), [](const auto &scenario) { return scenario.param.name; });

struct TamperCase { std::string name; std::size_t offset; };
void PrintTo(const TamperCase &scenario, std::ostream *output) { *output << scenario.name; }
class BlockAuthentication : public testing::TestWithParam<TamperCase> {};
TEST_P(BlockAuthentication, RejectsTamperingWithoutReturningPayload) {
  auto wire = buffered_block_fixture::encrypt();
  wire[GetParam().offset] ^= 1;
  auto block = BufferedAudioBlock::parse(wire);
  ASSERT_TRUE(block);
  auto payload = block->prepare({BufferedBlockCodec::alac}, buffered_block_fixture::key, 44100);
  ASSERT_FALSE(payload);
  EXPECT_EQ(payload.error(), BufferedBlockError::authenticationFailed);
}
INSTANTIATE_TEST_SUITE_P(AuthenticatedBytes, BlockAuthentication, testing::Values(
  TamperCase{"TimestampAad",4}, TamperCase{"SsrcAad",8}, TamperCase{"Ciphertext",12},
  TamperCase{"Tag",43}, TamperCase{"Nonce",51}
), [](const auto &scenario) { return scenario.param.name; });

TEST(BufferedAudioBlock, MissingAndShortKeysAreRejectedBeforeAuthentication) {
  auto block = BufferedAudioBlock::parse(buffered_block_fixture::golden);
  ASSERT_TRUE(block);
  auto missing = block->prepare({BufferedBlockCodec::alac}, {}, 44100);
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error(), BufferedBlockError::missingKey);
  const std::array<uint8_t,1> shortKey{};
  auto invalid = block->prepare({BufferedBlockCodec::alac}, shortKey, 44100);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error(), BufferedBlockError::invalidKeySize);
  auto wrongKey = buffered_block_fixture::key;
  wrongKey[0] ^= 1;
  auto unauthenticated = block->prepare({BufferedBlockCodec::alac}, wrongKey, 44100);
  ASSERT_FALSE(unauthenticated);
  EXPECT_EQ(unauthenticated.error(), BufferedBlockError::authenticationFailed);
}

TEST(BufferedAudioBlock, AuthenticatedEmptyPlaintextReturnsNoPayload) {
  auto wire = buffered_block_fixture::encrypt({});
  auto block = BufferedAudioBlock::parse(wire);
  ASSERT_TRUE(block);
  for (auto codec : {BufferedBlockCodec::alac, BufferedBlockCodec::aac}) {
    auto payload = block->prepare({codec}, buffered_block_fixture::key, 44100);
    ASSERT_FALSE(payload);
    EXPECT_EQ(payload.error(), BufferedBlockError::emptyPlaintext);
  }
}

TEST(BufferedAudioBlock, SequenceBytesAreNotAuthenticatedAdditionalData) {
  auto wire = buffered_block_fixture::encrypt();
  wire[0] ^= 0x40;
  wire[3] ^= 1;
  auto block = BufferedAudioBlock::parse(wire);
  ASSERT_TRUE(block);
  EXPECT_EQ(block->sequence(), 0x2bcdeeu);
  EXPECT_TRUE(block->prepare({BufferedBlockCodec::alac}, buffered_block_fixture::key, 44100));
}
