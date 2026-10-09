#include "channel_mapping.hpp"
#include <array>
#include <cassert>

static void checkMapping(ChannelMapping::Specification specification, int16_t left, int16_t right) {
  auto mapping = ChannelMapping::from({"FL", "FR"}, 2, specification);
  const std::array<int16_t, 4> source{5, 9, -5, -9};
  std::array<int16_t, 4> output{};
  assert(mapping.map(source, output));
  assert(output[0] == left && output[1] == right);
}
int main() {
  checkMapping({}, 5, 9);
  checkMapping({true, {"FR", "FL"}, "FL FR"}, 9, 5);
  checkMapping({false, {"FR", "FL"}, ""}, 5, 9);
  checkMapping({true, {}, "FR FL"}, 9, 5);
  checkMapping({true, {"UNKNOWN", "FL"}, ""}, 9, 5);
  checkMapping({true, {"FM", "--"}, ""}, 6, 0);
  auto mono = ChannelMapping::from({"FL", "FR"}, 1, {true, {"FM"}, ""});
  const std::array<int32_t, 2> oddSigned{-5, 9};
  std::array<int32_t, 1> mixed{};
  assert(mono.map(oddSigned, mixed) && mixed[0] == 2);
  auto incomplete = ChannelMapping::from({"FL", "FR"}, 2, {true, {}, "UNKNOWN FR"});
  assert(incomplete.isIncomplete());
  const std::array<int16_t, 1> shortInput{7};
  std::array<int16_t, 2> untouched{11, 13};
  assert(!incomplete.map(shortInput, untouched));
  assert(untouched[0] == 11 && untouched[1] == 13);
}
