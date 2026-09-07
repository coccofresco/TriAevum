#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fast::Oot3d {

struct ReflectionEnvironmentProfile {
    std::array<float, 3> Sky{};
    std::array<float, 3> Horizon{};
    std::array<float, 3> Ground{};
    uint64_t Signature = 0;
    uint32_t ObservationCount = 0;
    bool PicaDerived = false;
};

// Accumulates the packed fragment uniforms of opaque PICA world draws. The
// profile is low-frequency by design: original fog and global ambient colors
// drive the fallback probe without inventing a PBR sky for OoT3D.
class PicaReflectionEnvironmentAccumulator final {
  public:
    void Observe(uint64_t renderedFrameId,
                 std::string_view fragmentShaderSource,
                 std::span<const uint8_t> fragmentUniformBytes,
                 bool eligibleWorldDraw);
    [[nodiscard]] ReflectionEnvironmentProfile Resolve(
        uint64_t renderedFrameId);
    void Reset();

  private:
    uint64_t mFrameId = 0;
    std::array<double, 3> mFogSum{};
    std::array<double, 3> mAmbientSum{};
    uint32_t mFogCount = 0;
    uint32_t mAmbientCount = 0;
    ReflectionEnvironmentProfile mLastProfile{};
    bool mHasLastProfile = false;
};

struct IntegratedBrdf {
    float Scale = 0.0F;
    float Bias = 0.0F;
};

[[nodiscard]] IntegratedBrdf IntegrateReflectionBrdf(
    float nDotV, float perceptualRoughness, uint32_t sampleCount = 256U);
[[nodiscard]] std::string BuildReflectionEnvironmentComputeShader();
[[nodiscard]] std::string BuildReflectionBrdfComputeShader();
[[nodiscard]] std::string BuildReflectionMaterialResolveComputeShader();

} // namespace Fast::Oot3d
