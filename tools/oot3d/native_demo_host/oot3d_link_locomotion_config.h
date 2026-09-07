#pragma once

#include <cstddef>

#include "oot3d_link_runtime_types.h"

bool LinkCsabClipIndexValid(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene, size_t clipIndex);
LinkNativeLocomotionConfig BuildLinkNativeLocomotionConfig(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene);
bool LinkNativeLocomotionConfigSupported(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                         const LinkNativeLocomotionConfig& config);
const char* NativeBootsName(Oot3dNativeBoots boots);
