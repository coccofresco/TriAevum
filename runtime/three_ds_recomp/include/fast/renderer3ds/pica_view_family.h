#pragma once

#include "fast/renderer3ds/pica_scene_payloads.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Fast::Renderer3ds {

inline constexpr uint32_t kPicaFrameViewSchemaVersion = 1U;
inline constexpr uint32_t kPicaViewFamilySchemaVersion = 1U;
inline constexpr size_t kMaximumPicaViewFamilyViews = 2U;

enum class PicaFrameViewRole : uint8_t {
    Mono,
    StereoLeft,
    StereoRight,
};

struct PicaFrameView {
    uint32_t SchemaVersion = 0U;
    uint64_t ViewId = 0U;
    uint64_t PoseVersion = 0U;
    uint64_t ProjectionVersion = 0U;
    PicaFrameViewRole Role = PicaFrameViewRole::Mono;
    PicaPerspectiveCameraState Perspective;

    [[nodiscard]] bool Available() const noexcept {
        return SchemaVersion == kPicaFrameViewSchemaVersion &&
               ViewId != 0U;
    }
};

// A renderer-facing set of views evaluated at one temporal sample. Camera
// selection remains title policy; this contract only describes the result.
struct PicaViewFamily {
    uint32_t SchemaVersion = 0U;
    uint64_t FamilyId = 0U;
    uint8_t ViewCount = 0U;
    std::array<PicaFrameView, kMaximumPicaViewFamilyViews> Views{};

    [[nodiscard]] bool Available() const noexcept {
        if (SchemaVersion != kPicaViewFamilySchemaVersion ||
            FamilyId == 0U || ViewCount == 0U ||
            ViewCount > Views.size()) {
            return false;
        }
        for (size_t index = 0U; index < ViewCount; ++index) {
            if (!Views[index].Available()) {
                return false;
            }
            for (size_t previous = 0U; previous < index; ++previous) {
                if (Views[previous].ViewId == Views[index].ViewId) {
                    return false;
                }
            }
        }
        if (ViewCount == 1U) {
            return Views[0].Role == PicaFrameViewRole::Mono;
        }
        return Views[0].Role == PicaFrameViewRole::StereoLeft &&
               Views[1].Role == PicaFrameViewRole::StereoRight;
    }

    [[nodiscard]] bool Stereoscopic() const noexcept {
        return Available() && ViewCount == 2U;
    }
};

[[nodiscard]] inline PicaViewFamily BuildMonoPicaViewFamily(
    const PicaPerspectiveCameraState& perspective,
    uint64_t familyId = 1U, uint64_t viewId = 1U) noexcept {
    PicaViewFamily family;
    family.SchemaVersion = kPicaViewFamilySchemaVersion;
    family.FamilyId = familyId;
    family.ViewCount = 1U;
    family.Views[0] = {
        .SchemaVersion = kPicaFrameViewSchemaVersion,
        .ViewId = viewId,
        .PoseVersion = perspective.Serial,
        .ProjectionVersion = perspective.Serial,
        .Role = PicaFrameViewRole::Mono,
        .Perspective = perspective,
    };
    return family;
}

} // namespace Fast::Renderer3ds
