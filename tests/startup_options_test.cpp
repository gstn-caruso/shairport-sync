#include "app/startup_options.hpp"
#include <gtest/gtest.h>
#include <initializer_list>
#include <vector>

namespace {
auto parse(std::initializer_list<std::string_view> arguments) {
  return StartupOptions::parse({arguments.begin(), arguments.size()});
}

TEST(StartupOptions, NoArgumentsStartsWithDefaultConfiguration) {
  auto options = parse({});
  ASSERT_TRUE(options.has_value());
  EXPECT_EQ(options->operation(), StartupOptions::Operation::receive);
  EXPECT_FALSE(options->configurationPath().has_value());
}

TEST(StartupOptions, ExplicitConfigurationPathIsOwnedWithoutChangingIt) {
  auto options = parse({"--config", "/tmp/receiver settings.conf"});
  ASSERT_TRUE(options.has_value());
  EXPECT_EQ(options->operation(), StartupOptions::Operation::receive);
  EXPECT_EQ(options->configurationPath(), "/tmp/receiver settings.conf");
}

TEST(StartupOptions, VersionDoesNotRequireConfiguration) {
  auto options = parse({"--version"});
  ASSERT_TRUE(options.has_value());
  EXPECT_EQ(options->operation(), StartupOptions::Operation::version);
}

TEST(StartupOptions, CheckUsesDefaultConfigurationWhenNoPathIsGiven) {
  auto options = parse({"--check-config"});
  ASSERT_TRUE(options.has_value());
  EXPECT_EQ(options->operation(), StartupOptions::Operation::checkConfiguration);
  EXPECT_FALSE(options->configurationPath().has_value());
}

TEST(StartupOptions, CheckAcceptsConfigurationBeforeOrAfterOperation) {
  for (const auto &options : {parse({"--check-config", "--config", "receiver.conf"}),
                              parse({"--config", "receiver.conf", "--check-config"})}) {
    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->operation(), StartupOptions::Operation::checkConfiguration);
    EXPECT_EQ(options->configurationPath(), "receiver.conf");
  }
}

struct InvalidArguments {
  const char *name;
  std::vector<std::string_view> arguments;
};

class InvalidStartupOptions : public testing::TestWithParam<InvalidArguments> {};

TEST_P(InvalidStartupOptions, RejectsArgumentsWithAnExplanation) {
  auto options = StartupOptions::parse(GetParam().arguments);
  ASSERT_FALSE(options.has_value());
  EXPECT_FALSE(options.error().empty());
}

INSTANTIATE_TEST_SUITE_P(OperationalInterface, InvalidStartupOptions, testing::Values(
  InvalidArguments{"MissingPath", {"--config"}},
  InvalidArguments{"EmptyPath", {"--config", ""}},
  InvalidArguments{"OptionInsteadOfPath", {"--config", "--version"}},
  InvalidArguments{"RepeatedPath", {"--config", "a", "--config", "b"}},
  InvalidArguments{"RepeatedCheck", {"--check-config", "--check-config"}},
  InvalidArguments{"RepeatedVersion", {"--version", "--version"}},
  InvalidArguments{"VersionAndCheck", {"--version", "--check-config"}},
  InvalidArguments{"VersionAndPath", {"--version", "--config", "a"}},
  InvalidArguments{"PathAndVersion", {"--config", "a", "--version"}},
  InvalidArguments{"PositionalPath", {"receiver.conf"}},
  InvalidArguments{"LegacyConfig", {"-c", "receiver.conf"}},
  InvalidArguments{"LegacyVersion", {"-V"}},
  InvalidArguments{"LegacyDaemon", {"--daemon"}},
  InvalidArguments{"LegacyVerbose", {"-v"}},
  InvalidArguments{"BackendArguments", {"--", "pulse"}},
  InvalidArguments{"UnknownOption", {"--unknown"}}
), [](const auto &info) { return info.param.name; });
}
