#pragma once

#include "fast/oot3d/grass_visibility.h"

#include <optional>
#include <utility>

namespace Fast::Oot3d {

struct GrassSelectionView {
    std::array<float, 16> PositionToClip{};
    std::array<float, 3> Eye{};
    bool operator==(const GrassSelectionView&) const = default;
};

struct GrassSelectionKey {
    // Revision covers the ordered immutable placements and their global offsets.
    uint64_t StaticRevision = 0;
    GrassLodPolicy Policy{};
    float BladeRadiusScale = 0;
    uint32_t Budget = 0;
    bool FrustumCulling = false;
    std::vector<GrassSelectionView> Views;
    bool operator==(const GrassSelectionKey&) const = default;
};

// Only selection is reusable. Wind, interactions, lighting, fog and draw pushes
// must still be refreshed every presentation. No tolerance or historical camera.
class GrassSelectionCache {
  public:
    bool Matches(const GrassSelectionKey& key) const { return mKey && *mKey == key; }
    void Store(GrassSelectionKey key) { mKey = std::move(key); }
    void Reset() { mKey.reset(); }
  private:
    std::optional<GrassSelectionKey> mKey;
};

} // namespace Fast::Oot3d
