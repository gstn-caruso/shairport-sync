#include "platform/utilities/string_utilities.hpp"
#include <gtest/gtest.h>
#include <string>

static void expectLimitTooSmall(const std::expected<std::string, shairport::TruncationError> &result) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), shairport::TruncationError::limitTooSmall);
}

TEST(StringUtilities, DefaultServiceNameCapitalizesHostname) {
  const shairport::ServiceNameFormatter formatter("receiver.local", "5.5.1", "reference");
  EXPECT_EQ(formatter.format(), "Receiver");
}

TEST(StringUtilities, FormatterCapturesHostnameAndVersionValues) {
  std::string hostname = "receiver.local";
  std::string packageVersion = "5.5.1";
  std::string detailedVersion = "reference";
  const shairport::ServiceNameFormatter formatter(hostname, packageVersion, detailedVersion);
  hostname = "changed.local";
  packageVersion = "changed";
  detailedVersion = "changed";
  EXPECT_EQ(formatter.format(), "Receiver");
  EXPECT_EQ(formatter.format("%h/%H/%h/%v/%V/%v"),
            "receiver/Receiver/receiver/5.5.1/reference/5.5.1");
}

TEST(StringUtilities, UnknownServiceNameTokenIsPreserved) {
  const shairport::ServiceNameFormatter formatter("receiver.local", "5.5.1", "reference");
  EXPECT_EQ(formatter.format("%unknown"), "%unknown");
}

TEST(StringUtilities, EmptyServiceNameFormatStaysEmpty) {
  const shairport::ServiceNameFormatter formatter("receiver.local", "5.5.1", "reference");
  EXPECT_TRUE(formatter.format("").empty());
}

TEST(StringUtilities, DefaultServiceNameRemovesOnlyLocalSuffix) {
  EXPECT_EQ(shairport::ServiceNameFormatter("music.room.local", "v", "V").format(), "Music.room");
}

TEST(StringUtilities, DefaultServiceNamePreservesLeadingDigit) {
  EXPECT_EQ(shairport::ServiceNameFormatter("7receiver", "v", "V").format(), "7receiver");
}

TEST(StringUtilities, EmptyHostnameProducesEmptyServiceName) {
  EXPECT_TRUE(shairport::ServiceNameFormatter("", "v", "V").format().empty());
}

TEST(StringUtilities, ServiceNameExpansionProcessesTokensSequentially) {
  const shairport::ServiceNameFormatter sequential("%H-%v.local", "version", "detailed");
  EXPECT_EQ(sequential.format("%h"), "%H-version-version");
}

TEST(StringUtilities, EmptyReplacementPatternLeavesInputUnchanged) {
  EXPECT_EQ(shairport::replaceOccurrences("stable", "", ""), "stable");
  EXPECT_EQ(shairport::replaceOccurrences("stable", "", "x"), "stable");
}

TEST(StringUtilities, AbsentReplacementPatternLeavesInputUnchanged) {
  EXPECT_EQ(shairport::replaceOccurrences("plain", "absent", "x"), "plain");
}

TEST(StringUtilities, ReplacementExpandsEveryOccurrence) {
  EXPECT_EQ(shairport::replaceOccurrences("ab ab ab", "ab", "longer"), "longer longer longer");
}

TEST(StringUtilities, ReplacementUsesNonoverlappingOccurrences) {
  EXPECT_EQ(shairport::replaceOccurrences("aaaaa", "aa", "b"), "bba");
}

TEST(StringUtilities, ReplacementDoesNotReprocessInsertedText) {
  EXPECT_EQ(shairport::replaceOccurrences("xx", "x", "xx"), "xxxx");
}

TEST(StringUtilities, EmptyReplacementRemovesEveryOccurrence) {
  EXPECT_EQ(shairport::replaceOccurrences("a--b--", "--", ""), "ab");
}

TEST(StringUtilities, AppendingWithinExactByteLimitPreservesInput) {
  EXPECT_EQ(shairport::appendWithLimit("base", "!", 5).value(), "base!");
}

TEST(StringUtilities, TruncationReservesEllipsisAndSuffixBytes) {
  EXPECT_EQ(shairport::appendWithLimit("abcdef", "!", 6).value(), "ab...!");
  EXPECT_EQ(shairport::appendWithLimit("abcdef", "!", 4).value(), "...!");
}

TEST(StringUtilities, LimitTooSmallForEllipsisAndSuffixReturnsError) {
  expectLimitTooSmall(shairport::appendWithLimit("abcdef", "!", 3));
}

TEST(StringUtilities, EmptyInputAndSuffixFitZeroLimit) {
  EXPECT_TRUE(shairport::appendWithLimit("", "", 0).value().empty());
}

TEST(StringUtilities, NonemptyInputCannotFitZeroLimit) {
  expectLimitTooSmall(shairport::appendWithLimit("a", "", 0));
}

TEST(StringUtilities, SuffixLargerThanLimitReturnsError) {
  expectLimitTooSmall(shairport::appendWithLimit("", "suffix", 3));
}

TEST(StringUtilities, TruncationKeepsTwoByteUtf8CharacterOnlyWhenItFits) {
  EXPECT_EQ(shairport::appendWithLimit("a\xc3\xa9" "bcdef", "!", 6).value(), "a...!");
  EXPECT_EQ(shairport::appendWithLimit("a\xc3\xa9" "bcdef", "!", 7).value(), "a\xc3\xa9...!");
}

TEST(StringUtilities, TruncationDoesNotSplitThreeByteUtf8Character) {
  EXPECT_EQ(shairport::appendWithLimit("a\xe2\x82\xac" "bcdef", "!", 7).value(), "a...!");
}

TEST(StringUtilities, TruncationDoesNotSplitFourByteUtf8Character) {
  EXPECT_EQ(shairport::appendWithLimit("a\xf0\x9f\x8e\xb5" "bcdef", "!", 8).value(), "a...!");
}

TEST(StringUtilities, ServiceNameTruncatesOnlyAboveFiftyBytes) {
  const shairport::ServiceNameFormatter formatter("receiver.local", "v", "V");
  EXPECT_EQ(formatter.format(std::string(50, 'a')), std::string(50, 'a'));
  EXPECT_EQ(formatter.format(std::string(51, 'a')), std::string(47, 'a') + "...");
}

TEST(StringUtilities, ServiceNameTruncationPreservesUtf8Boundary) {
  const shairport::ServiceNameFormatter formatter("receiver.local", "v", "V");
  EXPECT_EQ(formatter.format(std::string(46, 'a') + "\xe2\x82\xac" + "bbbb"),
            std::string(46, 'a') + "...");
}
