#include "oot3d_link_locomotion_config.h"

bool LinkCsabClipIndexValid(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, size_t clipIndex) {
    return clipIndex < scene.LinkCsabClips.size();
}

LinkNativeLocomotionConfig BuildLinkNativeLocomotionConfig(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene) {
    LinkNativeLocomotionConfig config;
    config.GravityUnitsPerTickSquared = NativeRegHundredthsToUnitsPerTick(config.BootData.Reg68);
    config.Clips.IdleClipIndex = scene.LinkStandingClipIndex;
    config.Clips.WalkClipIndex = scene.LinkWalkClipIndex;
    config.Clips.RunClipIndex = scene.LinkMovementClipIndex;
    config.Clips.WalkEndLeftClipIndex = scene.LinkWalkEndLeftClipIndex;
    config.Clips.WalkEndRightClipIndex = scene.LinkWalkEndRightClipIndex;
    return config;
}

bool LinkNativeLocomotionConfigSupported(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                         const LinkNativeLocomotionConfig& config) {
    return LinkCsabClipIndexValid(scene, config.Clips.IdleClipIndex) &&
           LinkCsabClipIndexValid(scene, config.Clips.WalkClipIndex) &&
           LinkCsabClipIndexValid(scene, config.Clips.RunClipIndex) &&
           LinkCsabClipIndexValid(scene, config.Clips.WalkEndLeftClipIndex) &&
           LinkCsabClipIndexValid(scene, config.Clips.WalkEndRightClipIndex);
}

const char* NativeBootsName(Oot3dNativeBoots boots) {
    switch (boots) {
        case Oot3dNativeBoots::Kokiri:
            return "PLAYER_BOOTS_KOKIRI";
        case Oot3dNativeBoots::Iron:
            return "PLAYER_BOOTS_IRON";
        case Oot3dNativeBoots::Hover:
            return "PLAYER_BOOTS_HOVER";
        case Oot3dNativeBoots::Unused3:
            return "PLAYER_BOOTS_UNUSED_3";
        case Oot3dNativeBoots::IronUnderwater:
            return "PLAYER_BOOTS_IRON_UNDERWATER";
        case Oot3dNativeBoots::KokiriChild:
            return "PLAYER_BOOTS_KOKIRI_CHILD";
    }
    return "unknown";
}
