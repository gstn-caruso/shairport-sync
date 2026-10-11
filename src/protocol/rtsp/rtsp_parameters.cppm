module;
#include "protocol/rtsp/rtsp_message.hpp"
#include "volume/volume_quantities.hpp"
#include <optional>
#include <string>
#include <vector>

export module receiver.protocol.rtsp.parameters;

export class ParameterVolumePort {
public:
  virtual ~ParameterVolumePort() = default;
  virtual AirPlayVolume suggestedVolume() = 0;
  virtual void acceptVolume(AirPlayVolume volume) = 0;
};

export enum class ParameterContent { metadata, image, text, unknown, missing };

export struct ParameterDiagnostics {
  ParameterContent content;
  std::vector<std::string> unrecognizedParameters;
};

export class RtspParameterHandler {
public:
  explicit RtspParameterHandler(ParameterVolumePort &volume) : volume_(volume) {}
  std::optional<AirPlayVolume> get(const RtspMessage &request, RtspMessage &response);
  ParameterDiagnostics set(const RtspMessage &request, RtspMessage &response);

private:
  ParameterVolumePort &volume_;
};
