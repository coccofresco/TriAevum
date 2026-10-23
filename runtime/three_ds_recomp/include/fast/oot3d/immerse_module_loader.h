#pragma once

#include "triaevum/immerse_module.h"
#include <string>

namespace Fast::Oot3d {

// The separately licensed iMMERSE module (triaevum/immerse_module.h): its
// shared library beside the runtime executable (or in its lib/ directory),
// loaded on first use and kept for the process lifetime. Null when the
// library is absent or does not implement this interface version; MXAO is
// then unavailable and every other feature works.
[[nodiscard]] const TriaevumImmerseModule* ImmerseModule();
// Why ImmerseModule() is null (empty when it is not).
[[nodiscard]] const std::string& ImmerseModuleUnavailableReason();

} // namespace Fast::Oot3d
