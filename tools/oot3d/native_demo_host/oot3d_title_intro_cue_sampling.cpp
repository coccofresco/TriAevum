#include "oot3d_title_intro_cue_sampling.h"

TitleIntroCueSample SampleTitleIntroCue(uint16_t qdbIndex, uint32_t commandId, double frame) {
    Oot3dTitleIntroOpeningActorCueSample nativeSample{};
    TitleIntroCueSample sample;
    sample.Frame = frame;
    if (Oot3d_TitleIntroOpeningActorRuntimeSampleQdbCue(
            qdbIndex,
            commandId,
            static_cast<float>(frame),
            &nativeSample) == 0) {
        sample.Status = nativeSample.status != nullptr ? nativeSample.status : "no_qdb_actor_cue_for_command";
        return sample;
    }

    sample.Valid = nativeSample.valid != 0;
    sample.Active = nativeSample.active != 0;
    sample.Row = nativeSample.row;
    sample.Frame = static_cast<double>(nativeSample.frame);
    sample.Interpolation = static_cast<double>(nativeSample.interpolation);
    sample.Position = { static_cast<double>(nativeSample.positionX),
                        static_cast<double>(nativeSample.positionY),
                        static_cast<double>(nativeSample.positionZ) };
    sample.Rotation = { static_cast<double>(nativeSample.rotX),
                        static_cast<double>(nativeSample.rotY),
                        static_cast<double>(nativeSample.rotZ) };
    sample.Status = nativeSample.status != nullptr ? nativeSample.status : "";
    return sample;
}
