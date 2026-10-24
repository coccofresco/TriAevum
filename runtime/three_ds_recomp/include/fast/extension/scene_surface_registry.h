#pragma once

#include "fast/extension/linear_scene_color.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

namespace Fast::Extension {

enum class SceneSurfaceKind : uint8_t {
    Color,
    Depth,
    NormalGuide,
    MaterialGuide,
    RigidMotionGuide,
    AmbientGuide,
    Reflection,
    Motion,
    LinearWorkingColor,
    Composite,
    Output,
    FogGuide,
    OutlineGeometryGuide,
    DiffuseAlbedoGuide,
    // Scene colour produced at a declared native boundary rather than at a
    // presentation. A boundary composes the source's own domain, so its result
    // is not the presentation's composite and must never be reused as one.
    BoundaryComposition,
    BoundaryWorkingColor,
};

struct SceneSurfaceKey {
    uint64_t TargetNamespace = 0;
    uint32_t PhysicalAddress = 0;
    SceneSurfaceKind Kind = SceneSurfaceKind::Color;
    bool operator==(const SceneSurfaceKey&) const = default;
};

struct SceneSurfaceKeyHash {
    size_t operator()(const SceneSurfaceKey& key) const noexcept;
};

struct SceneSurface {
    SceneSurfaceKey Key{};
    uintptr_t NativeImage = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Format = 0;
    uint64_t Generation = 0;
    bool Sampleable = false;
    SceneColorEncoding ColorEncoding = SceneColorEncoding::Unknown;
};

class SceneSurfaceRegistry final {
  public:
    using RetireCallback = std::function<void(const SceneSurface&)>;

    explicit SceneSurfaceRegistry(RetireCallback retire = {});
    const SceneSurface& Publish(SceneSurface surface);
    [[nodiscard]] std::optional<SceneSurface> Find(const SceneSurfaceKey& key) const;
    bool Retire(const SceneSurfaceKey& key);
    void RetireNamespace(uint64_t targetNamespace);
    void Clear();
    [[nodiscard]] size_t Size() const { return mSurfaces.size(); }

  private:
    void NotifyRetired(const SceneSurface& surface);
    std::unordered_map<SceneSurfaceKey, SceneSurface, SceneSurfaceKeyHash> mSurfaces;
    RetireCallback mRetire;
    uint64_t mNextGeneration = 1;
};

} // namespace Fast::Extension
