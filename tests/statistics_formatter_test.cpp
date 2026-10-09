#include <cassert>
#include <cstring>
#include <cstdint>
#include "utilities/debug.h"
#include "statistics_formatter.hpp"

extern char line_of_stats[1024];
extern int statistics_row, statistics_column, was_a_previous_column;
extern int *statistics_print_profile;
extern int ap2_buffered_nodelay_stream_statistics_print_profile[];
extern int ap2_buffered_synced_stream_statistics_print_profile[];
extern int ap2_realtime_synced_stream_statistics_print_profile[];
extern int ap2_realtime_nodelay_stream_statistics_print_profile[];
void statistics_item(const char *, const char *, ...);

int main() {
  set_debug_level(1);
  statistics_print_profile = ap2_buffered_nodelay_stream_statistics_print_profile;
  for (int row = 0; row < 2; ++row) {
    statistics_row = row;
    statistics_column = was_a_previous_column = 0;
    line_of_stats[0] = 0;
    for (int column = 0; column < 12; ++column)
      statistics_item("hidden", "unused");
    statistics_item("Min Buffer Size", "%*u", 15, uint32_t(-1));
    assert(std::strcmp(line_of_stats, row == 0 ? "Min Buffer Size" : "     4294967295") == 0);
  }
  PlaybackStatisticsSnapshot snapshot;
  StatisticsFormatter formatter({StatisticsStream::buffered, false, false, true});
  assert(formatter.header() == "Min Buffer Size");
  assert(formatter.row(snapshot) == "     4294967295");
  for (auto stream : {StatisticsStream::realtime, StatisticsStream::buffered})
    for (bool delay : {false, true})
      for (bool backend : {false, true})
        for (bool debugging : {false, true}) {
          StatisticsFormatter candidate({stream, delay, backend, debugging});
          set_debug_level(debugging);
          statistics_print_profile = stream == StatisticsStream::realtime ?
            (delay ? ap2_realtime_synced_stream_statistics_print_profile : ap2_realtime_nodelay_stream_statistics_print_profile) :
            (delay ? ap2_buffered_synced_stream_statistics_print_profile : ap2_buffered_nodelay_stream_statistics_print_profile);
          snapshot.minimumBufferOccupancy = 3;
          snapshot.maximumBufferOccupancy = 9;
          for (int row = 0; row < 2; ++row) {
            statistics_row = row;
            statistics_column = was_a_previous_column = 0;
            line_of_stats[0] = 0;
            statistics_item("Av Sync Error (ms)", "%18.2f", 0.0);
            statistics_item("Net Sync PPM", "%12.1f", 0.0);
            statistics_item("All Sync PPM", "%12.1f", 0.0);
            statistics_item("Av Sync Window (ms)", "%19.2f", 0.0);
            statistics_item("    Packets", "%11d", 0);
            statistics_item("Missing", "%7u", 0U);
            statistics_item("  Late", "%6u", 0U);
            statistics_item("Too Late", "%8u", 0U);
            statistics_item("Resend Reqs", "%11u", 0U);
            statistics_item("Min DAC Queue", "          n/a");
            statistics_item("Min Buffers", "%11u", 3U);
            statistics_item("Max Buffers", "%11u", 9U);
            statistics_item("Min Buffer Size", "%15u", uint32_t(-1));
            statistics_item("Nominal FPS", "%11.2f", 0.0);
            statistics_item("Received FPS", "%12.2f", 0.0);
            if (backend) {
              statistics_item("Output FPS (r)", "           N/A");
              statistics_item("Output FPS (c)", "           N/A");
            }
            assert((row == 0 ? candidate.header() : candidate.row(snapshot)) == line_of_stats);
          }
        }
}
