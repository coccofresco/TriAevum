#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ThreeDsRecomp::Oot3d {

const std::vector<std::string>& EngineCapabilities();
bool HasEngineCapability(std::string_view capability);

} // namespace ThreeDsRecomp::Oot3d
