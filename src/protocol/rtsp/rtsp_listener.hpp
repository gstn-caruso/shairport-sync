#pragma once

#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>
#include <sys/socket.h>

class RtspListener {
public:
  RtspListener();
  ~RtspListener();
  void start(std::function<void(std::stop_token)> routine);
  void requestStop();
  void stop();
  bool addSocket(int descriptor);
  int accept(std::stop_token stop, sockaddr *remote, socklen_t *size);
  bool hasSockets() const;

private:
  void releaseSockets();
  int wake_;
  std::stop_source stop_;
  std::mutex lifecycle_;
  std::jthread worker_;
  std::vector<int> sockets_;
};
