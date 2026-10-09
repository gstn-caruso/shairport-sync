#include "statistics_formatter.hpp"
#include <format>

std::string StatisticsFormatter::selectedColumns(const std::array<std::string, 17> &columns,
                                                const char *separator) const {
  static constexpr std::array<int, 17> realtimeDelay{2,1,2,2,0,2,1,1,2,1,1,1,0,0,1,2,2};
  static constexpr std::array<int, 17> realtimeNoDelay{0,0,0,0,0,2,1,1,2,0,1,1,0,0,1,0,0};
  static constexpr std::array<int, 17> bufferedDelay{2,2,2,1,0,0,0,0,0,1,1,0,1,0,0,2,2};
  static constexpr std::array<int, 17> bufferedNoDelay{0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0};
  const auto &selection = profile_.stream == StatisticsStream::realtime ?
      (profile_.hasDelay ? realtimeDelay : realtimeNoDelay) :
      (profile_.hasDelay ? bufferedDelay : bufferedNoDelay);
  std::string result;
  bool previous = false;
  for (size_t column = 0; column < columns.size(); ++column) {
    if (column >= 15 && !(profile_.hasDelay && profile_.hasBackendStats)) continue;
    if (selection[column] == 0 || (selection[column] == 1 && !profile_.debugEnabled)) continue;
    if (previous) result += separator;
    result += columns[column];
    previous = true;
  }
  return result;
}

std::string StatisticsFormatter::header() const {
  return selectedColumns({"Av Sync Error (ms)", "Net Sync PPM", "All Sync PPM", "Av Sync Window (ms)",
    "    Packets", "Missing", "  Late", "Too Late", "Resend Reqs", "Min DAC Queue", "Min Buffers",
    "Max Buffers", "Min Buffer Size", "Nominal FPS", "Received FPS", "Output FPS (r)", "Output FPS (c)"}, " | ");
}

std::string StatisticsFormatter::row(const PlaybackStatisticsSnapshot &s) const {
  return selectedColumns({
    std::format("{:18.2f}", s.averageSyncErrorMs), std::format("{:12.1f}", s.correctionsPpm),
    std::format("{:12.1f}", s.absoluteCorrectionsPpm), std::format("{:19.2f}", s.averageWindowMs),
    std::format("{:11}", s.playNumber), std::format("{:7}", s.missing), std::format("{:6}", s.late),
    std::format("{:8}", s.tooLate), std::format("{:11}", s.resends),
    s.minimumDacQueue == UINT64_MAX ? "          n/a" : std::format("{:13}", s.minimumDacQueue),
    std::format("{:11}", uint32_t(s.minimumBufferOccupancy)),
    std::format("{:11}", uint32_t(s.maximumBufferOccupancy)),
    s.minimumBufferedBytes > 10 * 1024 ? std::format("{:14}k", uint32_t(s.minimumBufferedBytes / 1024)) :
                                      std::format("{:15}", uint32_t(s.minimumBufferedBytes)),
    std::format("{:11.2f}", 0.0), std::format("{:12.2f}", s.inputFramesPerSecond),
    s.outputRateAvailable ? std::format("{:14.2f}", s.rawOutputFramesPerSecond) : "           N/A",
    s.outputRateAvailable ? std::format("{:14.2f}", s.correctedOutputFramesPerSecond) : "           N/A"}, "   ");
}

std::string StatisticsFormatter::session(int connection, const PlaybackSessionSummary &s) const {
  auto result = std::format("Connection {}: Playback stopped. Total playing time {:02}:{:02}:{:02}.",
      connection, s.elapsedSeconds / 3600, s.elapsedSeconds / 60 % 60, s.elapsedSeconds % 60);
  if (s.outputRateAvailable)
    result += std::format(" Output: {:.2f} (raw), {:.2f} (corrected) frames per second.",
        s.rawOutputFramesPerSecond, s.correctedOutputFramesPerSecond);
  return result;
}
