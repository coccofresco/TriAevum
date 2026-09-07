#pragma once

#include "oot3d/renderer/pica_shader_hooks.h"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Fast::Oot3d {

struct RigidMotionSample {
    std::array<float, 2> MotionUv{};
    bool Valid = false;
};

[[nodiscard]] bool DecodeCommonRigidOriginClip(
    std::span<const uint8_t> vertexUniformBytes, bool framebufferFlipped,
    std::array<float, 4>& originClip);

class PicaRigidMotionTracker final {
  public:
    [[nodiscard]] RigidMotionSample Track(
        uint64_t instanceId, uint64_t frameId,
        const std::array<float, 4>& originClip,
        const std::array<float, 2>& jitterUv);
    void Reset();
    void PruneBeforeFrame(uint64_t frameId);

  private:
    struct Record {
        uint64_t FrameId = 0;
        std::array<float, 2> ScreenUv{};
        bool Valid = false;
    };
    std::unordered_map<uint64_t, Record> mRecords;
};

struct PicaRigidMotionShaderVariant {
    std::string Source;
    uint64_t FragmentKey = 0;
    bool Applied = false;
    bool UsedProvidedProgram = false;
};

[[nodiscard]] PicaRigidMotionShaderVariant
BuildPicaTemporalVertexInstrumentation(
    std::string_view source, uint64_t vertexKey,
    const ::Oot3d::Renderer::PicaTemporalVertexProgramView& program);

// Legacy compatibility/test API. Production instrumentation must use the
// typed frontend program overload above and may not analyze generated GLSL.
[[nodiscard]] PicaRigidMotionShaderVariant BuildPicaTemporalVertexVariant(
    std::string_view source, uint64_t vertexKey,
    const ::Oot3d::Renderer::PicaTemporalVertexProgramView* program =
        nullptr);

// Legacy compatibility/test API retained for source-equivalence coverage.
[[nodiscard]] PicaRigidMotionShaderVariant BuildPicaRigidMotionShaderVariant(
    std::string_view source, uint64_t fragmentKey);

} // namespace Fast::Oot3d
