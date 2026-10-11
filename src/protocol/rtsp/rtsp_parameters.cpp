module;
#include "protocol/rtsp/rtsp_message.hpp"
#include "volume/volume_quantities.hpp"
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

module receiver.protocol.rtsp.parameters;

std::optional<AirPlayVolume> RtspParameterHandler::get(const RtspMessage &request,
                                                      RtspMessage &response) {
  std::optional<AirPlayVolume> requestedVolume;
  if (request.requestsVolume()) {
    requestedVolume = volume_.suggestedVolume();
    response.replaceBody(std::format("\r\nvolume: {:.6f}\r\n", requestedVolume->value()));
  }
  response.respondWith(200);
  return requestedVolume;
}

ParameterDiagnostics RtspParameterHandler::set(const RtspMessage &request, RtspMessage &response) {
  const auto *contentType = request.headerValue("Content-Type");
  if (!contentType) {
    response.respondWith(200);
    return {ParameterContent::missing, {}};
  }
  const std::string_view type(contentType);
  if (type.starts_with("application/x-dmap-tagged")) {
    response.respondWith(request.containsCompleteMetadata() ? 200 : 400);
    return {ParameterContent::metadata, {}};
  }
  if (type.starts_with("image/")) {
    response.respondWith(200);
    return {ParameterContent::image, {}};
  }
  if (!type.starts_with("text/parameters")) {
    response.respondWith(200);
    return {ParameterContent::unknown, {}};
  }
  ParameterDiagnostics diagnostics{ParameterContent::text, {}};
  for (const auto &parameter : request.parameterLines()) {
    if (parameter.starts_with("volume: "))
      volume_.acceptVolume(AirPlayVolume::fromWireParameter(parameter.c_str() + 8));
    else if (!parameter.starts_with("progress: "))
      diagnostics.unrecognizedParameters.push_back(parameter);
  }
  response.respondWith(200);
  return diagnostics;
}
