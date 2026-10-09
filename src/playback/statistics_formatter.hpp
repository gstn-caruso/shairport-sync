#pragma once
#include "playback/playback_statistics.hpp"
#include <array>
#include <string>

enum class StatisticsStream { realtime, buffered };
struct StatisticsProfile {
  StatisticsStream stream;
  bool hasDelay, hasBackendStats, debugEnabled;
};
class StatisticsFormatter {
public:
  explicit StatisticsFormatter(StatisticsProfile profile) : profile_(profile) {}
  std::string header() const;
  std::string row(const PlaybackStatisticsSnapshot &) const;
  std::string session(int connection, const PlaybackSessionSummary &) const;
private:
  std::string selectedColumns(const std::array<std::string, 17> &, const char *separator) const;
  StatisticsProfile profile_;
};
