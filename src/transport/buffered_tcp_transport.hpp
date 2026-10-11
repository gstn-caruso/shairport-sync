#pragma once
#include "transport/bounded_byte_queue.hpp"
#include <atomic>
#include <expected>
#include <functional>
#include <pthread.h>
#include <string>

class BufferedTcpTransport : public ExactByteInput {
public:
  using Starter = std::function<int(pthread_t *, void *(*)(void *), void *)>;
  // The listener is borrowed, nonblocking, and has this worker as its sole acceptor.
  BufferedTcpTransport(int listener, std::size_t capacity, std::string name = {}, Starter starter = createThread);
  ~BufferedTcpTransport() override;
  std::expected<void, int> start();
  std::expected<void, int> requestStop();
  void join();
  ByteQueueResult readExact(std::span<std::uint8_t> destination) override { return queue_.readExact(destination); }
private:
  static int createThread(pthread_t *, void *(*)(void *), void *);
  static void *worker(void *);
  bool readable(int descriptor);
  void run();
  void joinWorker();
  int listener_, wake_ = -1;
  BoundedByteQueue queue_;
  std::string name_;
  Starter starter_;
  std::atomic<bool> stopping_ = false;
  pthread_t thread_{};
  bool attempted_ = false, started_ = false, joined_ = false;
  std::mutex lifecycle_, joining_;
};
