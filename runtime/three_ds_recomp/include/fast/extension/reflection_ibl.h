#pragma once

#include <unordered_map>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace Fast::Extension {

struct ReflectionEnvironmentProfile {
    // Native ambient light of opaque world draws (fragment-lighting global
    // ambient plus the ambient of the lights they evaluate), PICA encoded.
    // Not part of the IBL signature.
    std::array<float, 3> NativeAmbient{};
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
    // `sourceKey` identifies the fragment program (0: none); the uniform
    // terms its source reads are found once per program, not per draw.
    void Observe(uint64_t renderedFrameId,
                 std::string_view fragmentShaderSource,
                 std::span<const uint8_t> fragmentUniformBytes,
                 bool eligibleWorldDraw, uint64_t sourceKey = 0U);
    [[nodiscard]] ReflectionEnvironmentProfile Resolve(
        uint64_t renderedFrameId);
    void Reset();

  private:
    // The fog and ambient terms a fragment program reads.
    struct SourceTerms {
        bool Fog = false;
        bool GlobalAmbient = false;
        uint8_t LightAmbientMask = 0;
    };
    [[nodiscard]] static SourceTerms AnalyzeSource(std::string_view source);
    std::unordered_map<uint64_t, SourceTerms> mSourceTerms;
    uint64_t mFrameId = 0;
    std::array<double, 3> mFogSum{};
    std::array<double, 3> mAmbientSum{};
    std::array<double, 3> mNativeAmbientSum{};
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

} // namespace Fast::Extension
