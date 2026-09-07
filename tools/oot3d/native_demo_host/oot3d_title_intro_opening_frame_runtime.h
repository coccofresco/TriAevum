#pragma once

#include <cstdint>

#include "oot3d_title_intro_runtime_types.h"

int32_t TitleIntroOpeningFrameRuntimeSampleFrame(const TitleIntroPlayback& playback);
double TitleIntroTimelineFrame(const TitleIntroPlayback& playback);
bool SampleTitleIntroOpeningFrameRuntime(
    TitleIntroPlayback& playback,
    const ThreeDsRecomp::Oot3d::Oot3dNativeDemoScene& scene);
TitleIntroCueSample SampleTitleIntroOpeningFrameRuntimeLinkCue(const TitleIntroPlayback& playback);
TitleIntroCueSample SampleTitleIntroOpeningFrameRuntimePairedMountCue(const TitleIntroPlayback& playback);
