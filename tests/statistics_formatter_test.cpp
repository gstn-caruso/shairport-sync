#include <cassert>
#include <cstring>
#include <cstdint>
#include "utilities/debug.h"

extern char line_of_stats[1024];
extern int statistics_row, statistics_column, was_a_previous_column;
extern int *statistics_print_profile;
extern int ap2_buffered_nodelay_stream_statistics_print_profile[];
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
}
