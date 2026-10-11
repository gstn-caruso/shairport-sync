#include "protocol/ap2/buffered_flush_policy.hpp"

void BufferedFlushPolicy::requestImmediate(std::uint32_t, std::uint32_t) {}
bool BufferedFlushPolicy::requestDeferred(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) {
  return false;
}
BufferedFlushPolicy::Decision BufferedFlushPolicy::evaluate(bool, std::uint32_t, std::uint32_t) {
  return {};
}
void BufferedFlushPolicy::resetForBufferedReceiver() {}
void BufferedFlushPolicy::clearDeferredForPlayback() {}
