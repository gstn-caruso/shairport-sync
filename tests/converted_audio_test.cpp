#include "converted_audio.hpp"
#include <cassert>

int main() {
  auto original = *ConvertedAudio::allocate(8, 2, 3);
  original.bytes()[0] = 17;
  auto transferred = std::move(original);
  assert(original.frames() == 0 && original.retainedFrames() == 0);
  assert(original.bytes().empty() && !original);
  assert(transferred.frames() == 2 && transferred.retainedFrames() == 3);
  assert(transferred.bytes().size() == 8 && transferred.bytes()[0] == 17);
  ConvertedAudio replaced;
  replaced = std::move(transferred);
  assert(transferred.bytes().empty());
  assert(replaced.bytes()[0] == 17);
  replaced.reset();
  replaced.reset();
  assert(replaced.bytes().empty() && replaced.frames() == 0);
}
