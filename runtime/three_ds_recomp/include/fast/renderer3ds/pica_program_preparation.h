#pragma once
#include "pica_vertex_artifact.h"

namespace Fast::Renderer3ds {
// Explicit loading boundary. Calls complete before gameplay; no draw packet or
// borrowed asynchronous work survives the call. Other backends may omit it.
class PicaProgramPreparationBackend {
  public:
    virtual ~PicaProgramPreparationBackend() = default;
    virtual void PrepareNativePicaPrograms(std::span<const PicaVertexArtifact> vertices) = 0;
};
}
