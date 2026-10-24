#pragma once

#include <string>

namespace Fast::Extension {

[[nodiscard]] float TemporalHistoryWeight(float configuredWeight,
                                          float disocclusion,
                                          float reactive,
                                          bool historyValid);
[[nodiscard]] std::string BuildTemporalAaComputeShader();

} // namespace Fast::Extension
