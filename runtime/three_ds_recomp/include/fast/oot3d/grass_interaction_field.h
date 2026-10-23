#pragma once

#include "fast/oot3d/grass_types.h"
#include "fast/oot3d/graphics_settings.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Fast::Oot3d {

class GrassInteractionField final {
  public:
    explicit GrassInteractionField(uint32_t resolution = 64,
                                   float worldExtent = 1200.0F);
    void Configure(uint32_t resolution, float worldExtent);
    void Update(float deltaSeconds, const InteractiveGrassSettings& settings,
                const std::optional<GrassInteractor>& link);
    void UpdateActors(float deltaSeconds, const InteractiveGrassSettings& settings,
                      std::span<const GrassInteractor> actors);
    [[nodiscard]] std::array<float, 2> Sample(float worldX,
                                               float worldZ) const;
    [[nodiscard]] std::array<float, 2> Sample(float worldX, float worldZ,
                                               float worldY,
                                               float verticalTolerance) const;
    [[nodiscard]] bool Contains(float worldX, float worldZ) const noexcept;
    [[nodiscard]] uint32_t Resolution() const { return mResolution; }
    [[nodiscard]] float WorldExtent() const { return mWorldExtent; }
    [[nodiscard]] std::array<float, 2> Center() const { return mCenter; }
    [[nodiscard]] std::span<const std::array<float, 2>> Displacements() const {
        return mDisplacement;
    }
    [[nodiscard]] std::span<const float> InteractionHeights() const {
        return mInteractionHeight;
    }
    [[nodiscard]] bool Initialized() const noexcept {
        return mInitialized;
    }
    [[nodiscard]] bool AtRest() const {
        return std::all_of(mActiveColumns.begin(), mActiveColumns.end(),
                           [](const auto& range) { return range[0] >= range[1]; });
    }
    void Reset();

  private:
    struct CellValue {
        std::array<float, 2> Displacement;
        std::array<float, 2> Velocity;
        float Height;
    };
    void ShiftTo(const std::array<float, 2>& center);
    void ClearValues();
    void ClearActiveColumns();
    void Stamp(const InteractiveGrassSettings& settings, const GrassInteractor& actor);
    // Zero (+0) displacement and velocity and no interaction height.
    [[nodiscard]] bool AtRest(size_t index) const noexcept;
    [[nodiscard]] std::array<float, 2> SampleImpl(
        float worldX, float worldZ, const float* worldY,
        float verticalTolerance) const;

    uint32_t mResolution;
    float mWorldExtent;
    std::array<float, 2> mCenter{};
    bool mInitialized = false;
    std::vector<GrassInteractor> mPreviousActors;
    std::vector<std::array<float, 2>> mDisplacement;
    std::vector<std::array<float, 2>> mVelocity;
    std::vector<float> mInteractionHeight;
    // Cells evolve independently and a cell at rest stays at rest, so only
    // the columns [first, end) of each row may differ from rest.
    std::vector<std::array<uint32_t, 2>> mActiveColumns;
    std::vector<std::pair<size_t, CellValue>> mShiftScratch;
};

} // namespace Fast::Oot3d
