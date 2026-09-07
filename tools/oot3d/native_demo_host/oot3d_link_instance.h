#pragma once

#include "oot3d_link_runtime_types.h"

LinkInstance InitialLinkInstance(const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
                                 const LinkNativeLocomotionConfig& config);
ThreeDsRecomp::Oot3d::Oot3dDemoVec3 LinkActorPosition(const LinkInstance& link);
void SetLinkActorPosition(LinkInstance& link, const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& actorPosition);
double DistanceSqXZ(const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& left,
                    const ThreeDsRecomp::Oot3d::Oot3dDemoVec3& right);
