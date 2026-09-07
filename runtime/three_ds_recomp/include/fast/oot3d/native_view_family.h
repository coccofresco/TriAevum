#pragma once

#include "fast/oot3d/scene_view_runtime.h"
#include "fast/renderer3ds/pica_view_family.h"

namespace Fast::Oot3d {

using NativeFrameViewRole = ::Fast::Renderer3ds::PicaFrameViewRole;
using NativeFrameView = ::Fast::Renderer3ds::PicaFrameView;
using NativeViewFamily = ::Fast::Renderer3ds::PicaViewFamily;

inline constexpr uint32_t kNativeFrameViewSchemaVersion =
    ::Fast::Renderer3ds::kPicaFrameViewSchemaVersion;
inline constexpr uint32_t kNativeViewFamilySchemaVersion =
    ::Fast::Renderer3ds::kPicaViewFamilySchemaVersion;
inline constexpr std::size_t kMaximumNativeViewFamilyViews =
    ::Fast::Renderer3ds::kMaximumPicaViewFamilyViews;

[[nodiscard]] inline NativeViewFamily BuildMonoNativeViewFamily(
    const PerspectiveViewState& perspective, uint64_t familyId = 1U,
    uint64_t viewId = 1U) noexcept {
    return ::Fast::Renderer3ds::BuildMonoPicaViewFamily(
        perspective, familyId, viewId);
}

} // namespace Fast::Oot3d
