#pragma once
#include "fast/oot3d/grass_scene_bridge.h"
#include "fast/oot3d/graphics_settings.h"
#include "fast/renderer3ds/pica_render_backend.h"
#include <optional>
#include <unordered_map>

namespace Fast::Oot3d {
struct GrassDrawMemoKey {
    uint64_t Geometry = 0, Content = 0, Texture = 0, Material = 0, Uniforms = 0, Rules = 0, Textures = 0;
    bool MasksOnly = false;
    bool operator==(const GrassDrawMemoKey&) const = default;
};
struct GrassDrawMemoKeyHash {
    size_t operator()(const GrassDrawMemoKey& key) const noexcept;
};
[[nodiscard]] uint64_t GrassDrawRulesVersion(const InteractiveGrassSettings& settings);
// No vertex/index/texture payload reads. Missing producer identities bypass
// memoization; an address alone never proves immutable content.
[[nodiscard]] std::optional<GrassDrawMemoKey> BuildGrassDrawMemoKey(const Renderer3ds::PicaDrawView& draw,
                                                                    uint64_t rules, uint64_t textures, bool world,
                                                                    bool texture0, bool perspective, bool masksOnly);
class GrassDrawMemo {
  public:
    struct Entry {
        std::optional<GrassSceneMesh> Mesh;
        uint64_t Frame = 0;
    };
    [[nodiscard]] Entry* Find(const GrassDrawMemoKey& key, uint64_t frame);
    void Store(const GrassDrawMemoKey& key, std::optional<GrassSceneMesh> mesh, uint64_t frame);
    [[nodiscard]] const std::array<float, 16>* CameraInverse(const Renderer3ds::PicaDrawView& draw);
    void Clear() {
        mEntries.clear();
        mCameras.clear();
        mRulesFrame = UINT64_MAX;
    }
    uint64_t Rules(const InteractiveGrassSettings& settings, uint64_t frame) {
        if (mRulesFrame != frame) {
            mRulesFrame = frame;
            mRules = GrassDrawRulesVersion(settings);
        }
        return mRules;
    }

  private:
    struct Camera {
        std::array<float, 16> View, Inverse;
    };
    std::vector<Camera> mCameras;
    uint64_t mRulesFrame = UINT64_MAX, mRules = 0;
    std::unordered_map<GrassDrawMemoKey, Entry, GrassDrawMemoKeyHash> mEntries;
};
// Refresh exact clip transform and dynamic source lighting. World geometry is
// retained only when the current captured camera predicts the same rigid draw.
[[nodiscard]] bool RefreshGrassMemoDraw(GrassSceneMesh& mesh, const Renderer3ds::PicaDrawView& draw,
                                        const std::array<float, 16>* cameraInverse = nullptr);
} // namespace Fast::Oot3d
