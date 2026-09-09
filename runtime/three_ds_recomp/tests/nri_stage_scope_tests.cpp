#include "fast/oot3d/nri_stage_scope.h"
#include <initializer_list>
#include <stdexcept>

int main() {
    using nri::StageBits;
    using Fast::Oot3d::MergeNriStageScopes;
    const auto require = [](bool condition) {
        if (!condition) throw std::runtime_error("NRI stage scope regression");
    };
    for (const auto stage : {StageBits::NONE, StageBits::ALL, StageBits::COLOR_ATTACHMENT,
                            StageBits::COPY, StageBits::FRAGMENT_SHADER, StageBits::COMPUTE_SHADER}) {
        require(MergeNriStageScopes(stage, StageBits::NONE) == stage);
        require(MergeNriStageScopes(StageBits::NONE, stage) == stage);
        require(MergeNriStageScopes(stage, StageBits::ALL) == StageBits::ALL);
        require(MergeNriStageScopes(StageBits::ALL, stage) == StageBits::ALL);
    }
    require(MergeNriStageScopes(StageBits::COPY, StageBits::COLOR_ATTACHMENT) ==
            (StageBits::COPY | StageBits::COLOR_ATTACHMENT));
    require(MergeNriStageScopes(StageBits::NONE, StageBits::COLOR_ATTACHMENT) == StageBits::COLOR_ATTACHMENT);
}
