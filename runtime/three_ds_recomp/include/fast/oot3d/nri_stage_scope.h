#pragma once
#ifdef ENABLE_OOT3D_NRI
#include <NRI.h>

namespace Fast::Oot3d {
inline nri::StageBits MergeNriStageScopes(nri::StageBits first, nri::StageBits second) {
    // NRI uses sentinel values: ALL == 0, NONE == 0x7fffffff, not empty bits.
    if (first == nri::StageBits::ALL || second == nri::StageBits::ALL) return nri::StageBits::ALL;
    if (first == nri::StageBits::NONE) return second;
    if (second == nri::StageBits::NONE) return first;
    return first | second;
}
} // namespace Fast::Oot3d
#endif
