module;
#include "protocol/rtsp/rtsp_message.hpp"
#include <memory>
#include <span>
module receiver.protocol.rtsp.request;

RtspRequestResult RtspRequestReader::read() {
  if (input_.stopped()) {
    effects_.diagnostic(RtspRequestDiagnostic::shutdown, RtspRequestPhase::headers, 0);
    return {RtspRequestStatus::shutdown, {}};
  }
  return {RtspRequestStatus::allocationFailure, {}};
}
