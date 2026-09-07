#pragma once

#include <string>

namespace Fast::Oot3d {

[[nodiscard]] float TemporalHistoryWeight(float configuredWeight,
                                          float disocclusion,
                                          float reactive,
                                          bool historyValid);
[[nodiscard]] std::string BuildTemporalAaComputeShader();

} // namespace Fast::Oot3d
