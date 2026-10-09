#include "utilities/string_utilities.hpp"
#include <cassert>
#include <string>

static void checkExpansion() {
  std::string hostname = "receiver.local";
  std::string packageVersion = "5.5.1";
  std::string detailedVersion = "reference";
  const shairport::ServiceNameFormatter formatter(hostname, packageVersion, detailedVersion);
  hostname = "changed.local";
  packageVersion = "changed";
  detailedVersion = "changed";
  assert(formatter.format() == "Receiver");
  assert(formatter.format("%h/%H/%h/%v/%V/%v") ==
         "receiver/Receiver/receiver/5.5.1/reference/5.5.1");
  assert(formatter.format("%unknown") == "%unknown");
  assert(formatter.format("").empty());
  assert(shairport::ServiceNameFormatter("music.room.local", "v", "V").format() == "Music.room");
  assert(shairport::ServiceNameFormatter("7receiver", "v", "V").format() == "7receiver");
  assert(shairport::ServiceNameFormatter("", "v", "V").format().empty());
  const shairport::ServiceNameFormatter sequential("%H-%v.local", "version", "detailed");
  assert(sequential.format("%h") == "%H-version-version");
}

static void checkReplacement() {
  assert(shairport::replaceOccurrences("stable", "", "") == "stable");
  assert(shairport::replaceOccurrences("stable", "", "x") == "stable");
  assert(shairport::replaceOccurrences("plain", "absent", "x") == "plain");
  assert(shairport::replaceOccurrences("ab ab ab", "ab", "longer") == "longer longer longer");
  assert(shairport::replaceOccurrences("aaaaa", "aa", "b") == "bba");
  assert(shairport::replaceOccurrences("xx", "x", "xx") == "xxxx");
  assert(shairport::replaceOccurrences("a--b--", "--", "") == "ab");
}

static void checkLimits() {
  using shairport::appendWithLimit;
  assert(appendWithLimit("base", "!", 5).value() == "base!");
  assert(appendWithLimit("abcdef", "!", 6).value() == "ab...!");
  assert(appendWithLimit("abcdef", "!", 4).value() == "...!");
  assert(appendWithLimit("abcdef", "!", 3).error() == shairport::TruncationError::limitTooSmall);
  assert(appendWithLimit("", "", 0).value().empty());
  assert(appendWithLimit("a", "", 0).error() == shairport::TruncationError::limitTooSmall);
  assert(appendWithLimit("", "suffix", 3).error() == shairport::TruncationError::limitTooSmall);
  assert(appendWithLimit("a\xc3\xa9" "bcdef", "!", 6).value() == "a...!");
  assert(appendWithLimit("a\xc3\xa9" "bcdef", "!", 7).value() == "a\xc3\xa9...!");
  assert(appendWithLimit("a\xe2\x82\xac" "bcdef", "!", 7).value() == "a...!");
  assert(appendWithLimit("a\xf0\x9f\x8e\xb5" "bcdef", "!", 8).value() == "a...!");
  const shairport::ServiceNameFormatter formatter("receiver.local", "v", "V");
  assert(formatter.format(std::string(50, 'a')) == std::string(50, 'a'));
  assert(formatter.format(std::string(51, 'a')) == std::string(47, 'a') + "...");
  assert(formatter.format(std::string(46, 'a') + "\xe2\x82\xac" + "bbbb") ==
         std::string(46, 'a') + "...");
}

int main() {
  const shairport::ServiceNameFormatter formatter("receiver.local", "5.5.1", "reference");
  assert(formatter.format() == "Receiver");
  checkExpansion();
  checkReplacement();
  checkLimits();
}
