#include "player.h"
#include <gtest/gtest.h>
#include <array>
#include <cassert>
#include <cstdlib>
#include <type_traits>

static_assert(std::is_standard_layout_v<pair_cipher_bundle>);

static std::array<void *, 4> watchedResources{};
static std::array<void *, 8> releasedResources{};
static size_t releaseCount = 0;

static void recordRelease(void *resource) {
  if (!resource)
    return;
  for (auto watched : watchedResources) {
    if (resource == watched) {
      if (releaseCount < releasedResources.size())
        releasedResources[releaseCount] = resource;
      ++releaseCount;
      return;
    }
  }
}

extern "C" void __real_free(void *resource);
extern "C" void __real_pair_cipher_free(pair_cipher_context *cipher);
extern "C" void __wrap_free(void *resource) {
  recordRelease(resource);
  __real_free(resource);
}
extern "C" void __wrap_pair_cipher_free(pair_cipher_context *cipher) {
  if (cipher && cipher == watchedResources[3]) {
    recordRelease(cipher);
    __real_free(cipher);
  } else {
    __real_pair_cipher_free(cipher);
  }
}

TEST(PairCipherBundle, ReleasesOwnedBuffersAndDescriptionBeforeCipher) {
  pair_cipher_bundle bundle{};
  bundle.plaintext_read_buffer = {static_cast<uint8_t *>(std::malloc(8)), 4, 8};
  bundle.encrypted_read_buffer = {static_cast<uint8_t *>(std::malloc(16)), 8, 16};
  bundle.description = static_cast<char *>(std::malloc(12));
  bundle.cipher_ctx = static_cast<pair_cipher_context *>(std::malloc(1));
  assert(bundle.plaintext_read_buffer.data && bundle.encrypted_read_buffer.data &&
         bundle.description && bundle.cipher_ctx);
  bundle.is_encrypted = 1;
  watchedResources = {bundle.plaintext_read_buffer.data, bundle.encrypted_read_buffer.data,
                      bundle.description, bundle.cipher_ctx};
  const auto expectedOrder = watchedResources;
  releasedResources.fill(nullptr);
  releaseCount = 0;

  bundle.release();

  watchedResources.fill(nullptr);
  ASSERT_EQ(releaseCount, expectedOrder.size());
  for (size_t index = 0; index < expectedOrder.size(); ++index)
    EXPECT_EQ(releasedResources[index], expectedOrder[index]);
  EXPECT_EQ(bundle.plaintext_read_buffer.data, nullptr);
  EXPECT_EQ(bundle.plaintext_read_buffer.length, 0);
  EXPECT_EQ(bundle.plaintext_read_buffer.size, 0);
  EXPECT_EQ(bundle.encrypted_read_buffer.data, nullptr);
  EXPECT_EQ(bundle.encrypted_read_buffer.length, 0);
  EXPECT_EQ(bundle.encrypted_read_buffer.size, 0);
  EXPECT_EQ(bundle.description, nullptr);
  EXPECT_EQ(bundle.cipher_ctx, nullptr);
  EXPECT_EQ(bundle.is_encrypted, 1);
}

TEST(PairCipherBundle, ReleaseIsSafeAfterOwnershipIsEmpty) {
  pair_cipher_bundle bundle{};
  bundle.plaintext_read_buffer = {static_cast<uint8_t *>(std::malloc(8)), 4, 8};
  bundle.encrypted_read_buffer = {static_cast<uint8_t *>(std::malloc(16)), 8, 16};
  bundle.description = static_cast<char *>(std::malloc(12));
  bundle.cipher_ctx = static_cast<pair_cipher_context *>(std::malloc(1));
  assert(bundle.plaintext_read_buffer.data && bundle.encrypted_read_buffer.data &&
         bundle.description && bundle.cipher_ctx);
  bundle.is_encrypted = 1;
  watchedResources = {bundle.plaintext_read_buffer.data, bundle.encrypted_read_buffer.data,
                      bundle.description, bundle.cipher_ctx};
  releasedResources.fill(nullptr);
  releaseCount = 0;

  bundle.release();
  const auto firstReleaseCount = releaseCount;
  bundle.release();
  const auto repeatedReleaseCount = releaseCount;
  pair_cipher_bundle empty{};
  empty.release();

  watchedResources.fill(nullptr);
  EXPECT_EQ(firstReleaseCount, 4);
  EXPECT_EQ(repeatedReleaseCount, firstReleaseCount);
  EXPECT_EQ(releaseCount, firstReleaseCount);
  EXPECT_EQ(bundle.plaintext_read_buffer.data, nullptr);
  EXPECT_EQ(bundle.plaintext_read_buffer.length, 0);
  EXPECT_EQ(bundle.plaintext_read_buffer.size, 0);
  EXPECT_EQ(bundle.encrypted_read_buffer.data, nullptr);
  EXPECT_EQ(bundle.encrypted_read_buffer.length, 0);
  EXPECT_EQ(bundle.encrypted_read_buffer.size, 0);
  EXPECT_EQ(bundle.description, nullptr);
  EXPECT_EQ(bundle.cipher_ctx, nullptr);
  EXPECT_EQ(bundle.is_encrypted, 1);
  EXPECT_EQ(empty.plaintext_read_buffer.data, nullptr);
  EXPECT_EQ(empty.plaintext_read_buffer.length, 0);
  EXPECT_EQ(empty.plaintext_read_buffer.size, 0);
  EXPECT_EQ(empty.encrypted_read_buffer.data, nullptr);
  EXPECT_EQ(empty.encrypted_read_buffer.length, 0);
  EXPECT_EQ(empty.encrypted_read_buffer.size, 0);
  EXPECT_EQ(empty.description, nullptr);
  EXPECT_EQ(empty.cipher_ctx, nullptr);
  EXPECT_EQ(empty.is_encrypted, 0);
}
