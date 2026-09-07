#pragma once

#include "fast/renderer3ds/pica_composition_schedule.h"

#include <cstdint>

namespace Fast::Renderer3ds {

struct PicaDisplayComposition {
    uint64_t SequenceId = 0U;
    PicaCompositionTargetReference SourceTarget;
    PicaCompositionDomain Domain = PicaCompositionDomain::Unknown;
    bool SceneResolved = false;
};

[[nodiscard]] PicaDisplayComposition ResolvePicaDisplayComposition(
    const PicaCompositionSchedule& schedule,
    const PicaCompositionTargetReference& target)
    noexcept;

} // namespace Fast::Renderer3ds
