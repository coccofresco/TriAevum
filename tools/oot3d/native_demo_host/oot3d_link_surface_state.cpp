#include "oot3d_link_surface_state.h"

#include <cmath>
#include <cstddef>

int NativeCameraDataIndexForLinkFloor(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                      const LinkInstance& link) {
    if (!scene.Collision.Valid || link.FloorSurfaceType < 0 ||
        static_cast<size_t>(link.FloorSurfaceType) >= scene.Collision.SurfaceTypes.size()) {
        return -1;
    }
    const auto actor = LinkActorPosition(link);
    if (!link.Grounded || std::abs(actor.Y - link.GroundY) >= 2.0) {
        return -1;
    }
    return ThreeDsRecomp::Oot3d::NativeDemoSurfaceTypeCameraDataIndex(
        scene.Collision.SurfaceTypes[static_cast<size_t>(link.FloorSurfaceType)]);
}

int NativeLightSettingRawIndexForLinkFloor(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                           const LinkInstance& link) {
    if (!scene.Collision.Valid || link.FloorSurfaceType < 0 ||
        static_cast<size_t>(link.FloorSurfaceType) >= scene.Collision.SurfaceTypes.size()) {
        return -1;
    }
    const auto actor = LinkActorPosition(link);
    if (!link.Grounded || std::abs(actor.Y - link.GroundY) >= 2.0) {
        return -1;
    }
    return ThreeDsRecomp::Oot3d::NativeDemoSurfaceTypeLightSettingRawIndex(
        scene.Collision.SurfaceTypes[static_cast<size_t>(link.FloorSurfaceType)]);
}

int NativeLightSettingIndexForLinkFloor(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                        const LinkInstance& link) {
    const int rawIndex = NativeLightSettingRawIndexForLinkFloor(scene, link);
    return rawIndex < 0 ? -1 : ThreeDsRecomp::Oot3d::NativeDemoNormalizeLightSettingIndex(rawIndex);
}

void UpdateLinkActorShadowState(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, const LinkInstance& link,
                                ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene) {
    const auto actor = LinkActorPosition(link);
    renderScene.LinkActorShadow = ThreeDsRecomp::Oot3d::BuildOot3dNativeLinkActorShadowState(
        scene, actor, link.FloorPolygonIndex, link.FloorSurfaceType,
        NativeLightSettingRawIndexForLinkFloor(scene, link),
        NativeLightSettingIndexForLinkFloor(scene, link), link.GroundY);
}
