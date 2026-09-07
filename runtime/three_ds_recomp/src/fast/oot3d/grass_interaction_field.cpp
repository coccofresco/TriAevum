#include "fast/oot3d/grass_interaction_field.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Fast::Oot3d {
namespace {

constexpr float kNoInteractionHeight =
    std::numeric_limits<float>::quiet_NaN();

float PlanarLength(float x, float z) { return std::sqrt(x * x + z * z); }

} // namespace

GrassInteractionField::GrassInteractionField(uint32_t resolution,
                                             float worldExtent)
    : mResolution(std::clamp(resolution, 8U, 512U)),
      mWorldExtent(std::max(1.0F, worldExtent)) {
    const size_t size = static_cast<size_t>(mResolution) * mResolution;
    mDisplacement.resize(size);
    mVelocity.resize(size);
    mInteractionHeight.assign(size, kNoInteractionHeight);
    mScratchDisplacement.resize(size);
    mScratchVelocity.resize(size);
    mScratchHeight.resize(size, kNoInteractionHeight);
}

void GrassInteractionField::Configure(uint32_t resolution,
                                      float worldExtent) {
    resolution = std::clamp(resolution, 8U, 512U);
    worldExtent = std::max(1.0F, worldExtent);
    if (resolution == mResolution &&
        std::abs(worldExtent - mWorldExtent) < 1.0e-4F) return;
    mResolution = resolution;
    mWorldExtent = worldExtent;
    const size_t size = static_cast<size_t>(mResolution) * mResolution;
    mDisplacement.assign(size, {});
    mVelocity.assign(size, {});
    mInteractionHeight.assign(size, kNoInteractionHeight);
    mScratchDisplacement.assign(size, {});
    mScratchVelocity.assign(size, {});
    mScratchHeight.assign(size, kNoInteractionHeight);
    mInitialized = false;
    mPreviousActors.clear();
}

void GrassInteractionField::ClearValues() {
    std::fill(mDisplacement.begin(), mDisplacement.end(),
              std::array<float, 2>{});
    std::fill(mVelocity.begin(), mVelocity.end(),
              std::array<float, 2>{});
    std::fill(mInteractionHeight.begin(), mInteractionHeight.end(),
              kNoInteractionHeight);
}

void GrassInteractionField::ShiftTo(const std::array<float, 2>& center) {
    if (!mInitialized) {
        mCenter = center;
        mInitialized = true;
        return;
    }
    const float cell = (mWorldExtent * 2.0F) /
                       static_cast<float>(mResolution);
    const int shiftX = static_cast<int>(
        std::lround((center[0] - mCenter[0]) / cell));
    const int shiftY = static_cast<int>(
        std::lround((center[1] - mCenter[1]) / cell));
    mCenter = center;
    if (shiftX == 0 && shiftY == 0) return;
    if (std::abs(shiftX) >= static_cast<int>(mResolution) ||
        std::abs(shiftY) >= static_cast<int>(mResolution)) {
        ClearValues();
        return;
    }

    std::fill(mScratchDisplacement.begin(), mScratchDisplacement.end(),
              std::array<float, 2>{});
    std::fill(mScratchVelocity.begin(), mScratchVelocity.end(),
              std::array<float, 2>{});
    std::fill(mScratchHeight.begin(), mScratchHeight.end(),
              kNoInteractionHeight);
    for (int y = 0; y < static_cast<int>(mResolution); ++y) {
        const int oldY = y + shiftY;
        if (oldY < 0 || oldY >= static_cast<int>(mResolution)) continue;
        for (int x = 0; x < static_cast<int>(mResolution); ++x) {
            const int oldX = x + shiftX;
            if (oldX < 0 || oldX >= static_cast<int>(mResolution)) continue;
            const size_t next = static_cast<size_t>(y) * mResolution + x;
            const size_t previous =
                static_cast<size_t>(oldY) * mResolution + oldX;
            mScratchDisplacement[next] = mDisplacement[previous];
            mScratchVelocity[next] = mVelocity[previous];
            mScratchHeight[next] = mInteractionHeight[previous];
        }
    }
    mDisplacement.swap(mScratchDisplacement);
    mVelocity.swap(mScratchVelocity);
    mInteractionHeight.swap(mScratchHeight);
}

void GrassInteractionField::Update(
    float deltaSeconds, const InteractiveGrassSettings& settings,
    const std::optional<GrassInteractor>& link) {
    if (link && link->Teleported) Reset();
    UpdateActors(deltaSeconds, settings,
                 link ? std::span<const GrassInteractor>(&*link, 1) : std::span<const GrassInteractor>{});
}

void GrassInteractionField::UpdateActors(
    float deltaSeconds, const InteractiveGrassSettings& settings,
    std::span<const GrassInteractor> actors) {
    Configure(settings.InteractionFieldResolution,
              settings.InteractionFieldRadius);

    const float cell = (mWorldExtent * 2.0F) /
                       static_cast<float>(mResolution);
    if (!actors.empty()) {
        if (!mPreviousActors.empty() && actors.front().ContextId != mPreviousActors.front().ContextId) {
            ClearValues();
            mInitialized = false;
            mPreviousActors.clear();
        }
        const auto snap = [cell](float value) {
            return std::round(value / cell) * cell;
        };
        // Keep finite trail storage around motion, not the first (often static)
        // scene object. Direct colliders remain active outside the trail field.
        const auto focus = std::max_element(actors.begin(), actors.end(), [](const auto& a, const auto& b) {
            return std::hypot(a.Velocity[0], a.Velocity[2]) < std::hypot(b.Velocity[0], b.Velocity[2]);
        });
        if (!mInitialized || std::hypot(focus->Velocity[0], focus->Velocity[2]) > 0.01F)
            ShiftTo({snap(focus->Position[0]), snap(focus->Position[2])});
    }

    const float dt = std::clamp(deltaSeconds, 0.0F, 0.1F);
    const float recovery = std::max(0.05F, settings.RecoverySeconds);
    const float omega = 4.0F / recovery;
    const float spring = omega * omega;
    const float damping = 2.0F * omega * settings.InteractionDamping;
    const uint32_t steps =
        std::max(1U, static_cast<uint32_t>(std::ceil(dt * 120.0F)));
    const float step = steps > 0U ? dt / static_cast<float>(steps) : 0.0F;
    for (uint32_t substep = 0; substep < steps && step > 0.0F; ++substep) {
        for (size_t index = 0; index < mDisplacement.size(); ++index) {
            for (size_t axis = 0; axis < 2; ++axis) {
                mVelocity[index][axis] +=
                    (-spring * mDisplacement[index][axis] -
                     damping * mVelocity[index][axis]) * step;
                mDisplacement[index][axis] +=
                    mVelocity[index][axis] * step;
            }
            const float length = PlanarLength(mDisplacement[index][0],
                                              mDisplacement[index][1]);
            if (length > settings.MaximumBend && length > 1.0e-6F) {
                const float scale = settings.MaximumBend / length;
                mDisplacement[index][0] *= scale;
                mDisplacement[index][1] *= scale;
            } else if (length < 1.0e-4F &&
                       PlanarLength(mVelocity[index][0],
                                    mVelocity[index][1]) < 1.0e-4F) {
                mInteractionHeight[index] = kNoInteractionHeight;
            }
        }
    }

    if (settings.CollisionPush > 0.0F) {
        for (const auto& actor : actors) {
            const bool consumed = std::any_of(mPreviousActors.begin(), mPreviousActors.end(), [&](const auto& old) {
                return old.StableId == actor.StableId && old.ContextId == actor.ContextId && old.FrameId == actor.FrameId;
            });
            if (!consumed && !actor.Teleported) Stamp(settings, actor);
        }
    }
    mPreviousActors.assign(actors.begin(), actors.end());
}

void GrassInteractionField::Stamp(const InteractiveGrassSettings& settings, const GrassInteractor& actor) {
    const auto* link = &actor;
    const float cell = (mWorldExtent * 2.0F) / static_cast<float>(mResolution);
    const float recovery = std::max(0.05F, settings.RecoverySeconds);

    const float radius = std::max(
        0.01F, link->Radius * settings.ColliderRadiusMultiplier);
    const float segmentX = link->Position[0] - link->PreviousPosition[0];
    const float segmentY = link->Position[1] - link->PreviousPosition[1];
    const float segmentZ = link->Position[2] - link->PreviousPosition[2];
    const float segmentLengthSquared = segmentX * segmentX + segmentZ * segmentZ;
    const float segmentLength = std::sqrt(segmentLengthSquared);
    const float speed = PlanarLength(link->Velocity[0], link->Velocity[2]);
    const float motion = std::clamp(
        std::max(segmentLength / radius, speed / 600.0F), 0.0F, 1.0F);
    if (motion <= 1.0e-4F) return;

    const float originX = mCenter[0] - mWorldExtent;
    const float originZ = mCenter[1] - mWorldExtent;
    const auto gridCoordinate = [cell](float world, float origin) {
        return (world - origin) / cell - 0.5F;
    };
    const int minX = std::max(0, static_cast<int>(std::floor(gridCoordinate(
        std::min(link->PreviousPosition[0], link->Position[0]) - radius,
        originX))));
    const int maxX = std::min(static_cast<int>(mResolution) - 1,
        static_cast<int>(std::ceil(gridCoordinate(
            std::max(link->PreviousPosition[0], link->Position[0]) + radius,
            originX))));
    const int minY = std::max(0, static_cast<int>(std::floor(gridCoordinate(
        std::min(link->PreviousPosition[2], link->Position[2]) - radius,
        originZ))));
    const int maxY = std::min(static_cast<int>(mResolution) - 1,
        static_cast<int>(std::ceil(gridCoordinate(
            std::max(link->PreviousPosition[2], link->Position[2]) + radius,
            originZ))));
    const float velocityGain = 1.0F +
        std::clamp(speed / 300.0F, 0.0F, 2.0F) *
            settings.CollisionVelocityResponse;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float worldX = originX + (static_cast<float>(x) + 0.5F) * cell;
            const float worldZ = originZ + (static_cast<float>(y) + 0.5F) * cell;
            float t = 1.0F;
            if (segmentLengthSquared > 1.0e-6F) {
                t = std::clamp(
                    ((worldX - link->PreviousPosition[0]) * segmentX +
                     (worldZ - link->PreviousPosition[2]) * segmentZ) /
                        segmentLengthSquared,
                    0.0F, 1.0F);
            }
            const float closestX = link->PreviousPosition[0] + segmentX * t;
            const float closestZ = link->PreviousPosition[2] + segmentZ * t;
            float dx = worldX - closestX;
            float dz = worldZ - closestZ;
            const float distance = PlanarLength(dx, dz);
            if (distance >= radius) continue;
            if (distance > 1.0e-5F) {
                dx /= distance;
                dz /= distance;
            } else if (segmentLength > 1.0e-5F) {
                dx = segmentX / segmentLength;
                dz = segmentZ / segmentLength;
            } else {
                dx = 1.0F;
                dz = 0.0F;
            }
            const float impulse = (1.0F - distance / radius) *
                                  settings.CollisionPush * motion *
                                  velocityGain;
            const size_t index = static_cast<size_t>(y) * mResolution +
                                 static_cast<size_t>(x);
            mDisplacement[index][0] += dx * impulse * 0.35F;
            mDisplacement[index][1] += dz * impulse * 0.35F;
            mVelocity[index][0] += dx * impulse * (2.0F / recovery);
            mVelocity[index][1] += dz * impulse * (2.0F / recovery);
            mInteractionHeight[index] =
                link->PreviousPosition[1] + segmentY * t;
            const float bend = PlanarLength(mDisplacement[index][0],
                                            mDisplacement[index][1]);
            if (bend > settings.MaximumBend && bend > 1.0e-6F) {
                const float scale = settings.MaximumBend / bend;
                mDisplacement[index][0] *= scale;
                mDisplacement[index][1] *= scale;
            }
        }
    }
}

std::array<float, 2> GrassInteractionField::Sample(float worldX,
                                                   float worldZ) const {
    return SampleImpl(worldX, worldZ, nullptr, 0.0F);
}

std::array<float, 2> GrassInteractionField::Sample(
    float worldX, float worldZ, float worldY,
    float verticalTolerance) const {
    return SampleImpl(worldX, worldZ, &worldY,
                      std::max(0.0F, verticalTolerance));
}

bool GrassInteractionField::Contains(
    float worldX, float worldZ) const noexcept {
    if (!mInitialized || mResolution == 0U) {
        return false;
    }
    const float halfCell =
        mWorldExtent / static_cast<float>(mResolution);
    return worldX >= mCenter[0] - mWorldExtent + halfCell &&
           worldX <= mCenter[0] + mWorldExtent - halfCell &&
           worldZ >= mCenter[1] - mWorldExtent + halfCell &&
           worldZ <= mCenter[1] + mWorldExtent - halfCell;
}

std::array<float, 2> GrassInteractionField::SampleImpl(
    float worldX, float worldZ, const float* worldY,
    float verticalTolerance) const {
    if (!mInitialized || mResolution == 0U) return {};
    const float cell = (mWorldExtent * 2.0F) /
                       static_cast<float>(mResolution);
    const float gridX =
        (worldX - (mCenter[0] - mWorldExtent)) / cell - 0.5F;
    const float gridY =
        (worldZ - (mCenter[1] - mWorldExtent)) / cell - 0.5F;
    if (gridX < 0.0F || gridY < 0.0F ||
        gridX > static_cast<float>(mResolution - 1U) ||
        gridY > static_cast<float>(mResolution - 1U)) return {};
    const uint32_t x0 = static_cast<uint32_t>(std::floor(gridX));
    const uint32_t y0 = static_cast<uint32_t>(std::floor(gridY));
    const uint32_t x1 = std::min(x0 + 1U, mResolution - 1U);
    const uint32_t y1 = std::min(y0 + 1U, mResolution - 1U);
    const float tx = gridX - static_cast<float>(x0);
    const float ty = gridY - static_cast<float>(y0);
    std::array<float, 2> result{};
    const auto accumulate = [&](uint32_t x, uint32_t y, float weight) {
        const size_t index = static_cast<size_t>(y) * mResolution + x;
        if (worldY != nullptr &&
            (!std::isfinite(mInteractionHeight[index]) ||
             std::abs(*worldY - mInteractionHeight[index]) >
                 verticalTolerance)) return;
        result[0] += mDisplacement[index][0] * weight;
        result[1] += mDisplacement[index][1] * weight;
    };
    accumulate(x0, y0, (1.0F - tx) * (1.0F - ty));
    accumulate(x1, y0, tx * (1.0F - ty));
    accumulate(x0, y1, (1.0F - tx) * ty);
    accumulate(x1, y1, tx * ty);
    return result;
}

void GrassInteractionField::Reset() {
    ClearValues();
    mCenter = {};
    mInitialized = false;
    mPreviousActors.clear();
}

} // namespace Fast::Oot3d
