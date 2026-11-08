#pragma once
#include "pica_vertex_artifact.h"
#include "fast/renderer/pass_shader_artifact.h"

#include <cstddef>
#include <span>

namespace Fast::Renderer3ds {
// Work a renderer completes before gameplay so that no first use pays for it:
// the startup program parts and resources whose creation stalls a frame.
struct PicaPreparationStatus {
    size_t Parts = 0;
    size_t PreparedParts = 0;
    // Every startup part exists (or none were requested).
    bool ProgramsReady = true;
    // The upscaler the settings use exists for the parameters it last ran with
    // (or there is nothing to create ahead of its first dispatch).
    bool UpscalerReady = true;

    [[nodiscard]] bool Ready() const noexcept { return ProgramsReady && UpscalerReady; }
};

// Explicit loading boundary. Calls complete before gameplay; no draw packet or
// borrowed asynchronous work survives the call. Other backends may omit it.
class PicaProgramPreparationBackend {
  public:
    virtual ~PicaProgramPreparationBackend() = default;
    virtual void PrepareNativePicaPrograms(std::span<const PicaVertexArtifact> vertices) = 0;
    // True when draws should carry their always-available (parametric)
    // fragment program next to the specialized one (hybrid preparation).
    [[nodiscard]] virtual bool UsesAlwaysAvailablePrograms() const { return false; }
    // A frame-reproducible session (fixed steps): a draw whose specialized
    // program is not ready completes it instead of rendering the
    // always-available one (title_shader_preparation.h). Startup preparation
    // is unchanged. Set before the first draw.
    virtual void SetFrameReproducibleProgramSelection(bool reproducible) { (void)reproducible; }
    // Pass programs a title derives from its own programs (shadow casters,
    // surface lighting), built with its vertex family at development time.
    virtual void RegisterTitlePassPrograms(std::span<const Renderer::PassShaderArtifact> programs) { (void)programs; }
    // Thread-safe. Progress of the work above; the host keeps the guest waiting
    // (and shows it) until Ready().
    [[nodiscard]] virtual PicaPreparationStatus PreparationStatus() const { return {}; }
};
}
