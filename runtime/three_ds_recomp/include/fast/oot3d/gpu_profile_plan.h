#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Fast::Oot3d {

enum class GpuProfileScope : uint8_t {
    Frame = 0,
    NativePica,
    ToonRaster,
    Grass,
    Cacao,
    DepthPreparation,
    Reflection,
    MotionVectors,
    SceneComposite,
    AntiAliasing,
    Upscaler,
    DisplayTransfer,
    Scanout,
    Overlay,
    Count,
};

inline constexpr size_t kGpuProfileScopeCount =
    static_cast<size_t>(GpuProfileScope::Count);
inline constexpr uint32_t kGpuProfileQueriesPerFrame =
    static_cast<uint32_t>(kGpuProfileScopeCount * 2U);

struct GpuProfileQueryRange {
    uint32_t Begin = 0;
    uint32_t End = 0;
};

[[nodiscard]] GpuProfileQueryRange GpuProfileQueries(
    GpuProfileScope scope);

class GpuProfileFramePlan {
  public:
    bool Begin(GpuProfileScope scope);
    bool End(GpuProfileScope scope);
    void Reset();

    [[nodiscard]] bool Open(GpuProfileScope scope) const;
    [[nodiscard]] bool Written(GpuProfileScope scope) const;

  private:
    [[nodiscard]] static size_t Index(GpuProfileScope scope);

    std::array<bool, kGpuProfileScopeCount> mOpen{};
    std::array<bool, kGpuProfileScopeCount> mWritten{};
};

} // namespace Fast::Oot3d
