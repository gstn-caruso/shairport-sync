#include <cstddef>
extern "C" {
#include "pair_ap/pair-tlv.h"
}
#include <gtest/gtest.h>
#include <array>
#include <memory>

TEST(PairTlv, EmptyCollectionNeedsNoOutputStorage) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  size_t size = 0;

  EXPECT_EQ(pair_tlv_format(values.get(), nullptr, &size), 0);
  EXPECT_EQ(size, 0);
}

TEST(PairTlv, EmptyValueRequiresHeaderBeforeWritingOutput) {
  std::unique_ptr<pair_tlv_values_t, decltype(&pair_tlv_free)> values(pair_tlv_new(), pair_tlv_free);
  ASSERT_TRUE(values);
  ASSERT_EQ(pair_tlv_add_value(values.get(), TLVType_Separator, nullptr, 0), 0);
  std::array<uint8_t, 2> output{0x5a, 0xa5};
  size_t capacity = 0;

  EXPECT_EQ(pair_tlv_format(values.get(), output.data(), &capacity), PAIR_TLV_ERROR_INSUFFICIENT_SIZE);
  EXPECT_EQ(capacity, 2);
  EXPECT_EQ(output, (std::array<uint8_t, 2>{0x5a, 0xa5}));
}
