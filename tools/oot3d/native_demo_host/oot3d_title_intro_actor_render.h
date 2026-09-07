#pragma once

#include "oot3d_title_intro_runtime_types.h"
#include "three_ds_recomp/oot3d/Oot3dNativeDemoScene.h"
#include "three_ds_recomp/oot3d/Oot3dNativeRenderScene.h"

TitleIntroActorVisualAppendResult AppendTitleIntroActorVisual(
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene,
    TitleIntroNativeActor& actor,
    const TitleIntroCueSample& cue,
    double frame,
    const Oot3dTitleIntroOpeningActorMotionSample* actorMotion,
    bool usePairedMountMotion,
    const TitleIntroMountedLinkContext* mountedLinkContext,
    const ThreeDsRecomp::Oot3d::Oot3dNativeHorseDustEmitterProfile* horseDustEmitterProfile,
    bool buildDiagnostics,
    ThreeDsRecomp::Oot3d::Oot3dNativeDemoRenderScene& renderScene);

size_t StripSubmittedTitleIntroActorTexturePayloads(TitleIntroPlayback& playback);
