#include "utilities/string_utilities.hpp"
#include <cassert>

int main() {
  const shairport::ServiceNameFormatter formatter("receiver.local", "5.5.1", "reference");
  assert(formatter.format() == "Receiver");
}
