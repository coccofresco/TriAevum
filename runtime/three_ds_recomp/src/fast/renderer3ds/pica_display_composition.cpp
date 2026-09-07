#include "fast/renderer3ds/pica_display_composition.h"

namespace Fast::Renderer3ds {

PicaDisplayComposition ResolvePicaDisplayComposition(
    const PicaCompositionSchedule& schedule,
    const PicaCompositionTargetReference& target)
    noexcept {
    PicaDisplayComposition result;
    result.SourceTarget = target;
    if (!schedule.Valid()) {
        return result;
    }
    result.SequenceId = schedule.SequenceId();

    const auto* sceneResolved = schedule.FindAnchor(
        ::Fast::Renderer::ExtensionStage::SceneResolved, target);
    if (sceneResolved != nullptr && schedule.Reached(*sceneResolved)) {
        result.Domain = PicaCompositionDomain::Scene;
        result.SceneResolved = true;
        return result;
    }

    const auto* targetSchedule = schedule.FindTarget(target);
    if (targetSchedule != nullptr &&
        targetSchedule->WorldStageIssue ==
            PicaCompositionScheduleIssue::NoSceneDomain &&
        targetSchedule->DrawCount != 0U &&
        targetSchedule->UiDrawCount == targetSchedule->DrawCount) {
        result.Domain = PicaCompositionDomain::Ui;
    }
    return result;
}

} // namespace Fast::Renderer3ds
