#pragma once

#include "triaevum/dlss_module.h"
#include <string>

namespace Fast::Oot3d {

// The separately licensed DLSS module (triaevum/dlss_module.h): its shared
// library beside the runtime executable (or in its lib/ directory), loaded on
// first use and kept for the process lifetime. Null when the library is
// absent or does not implement this interface version; DLSS is then not
// offered and every other feature works.
[[nodiscard]] const TriaevumDlssModule* DlssModule();
// Why DlssModule() is null (empty when it is not).
[[nodiscard]] const std::string& DlssModuleUnavailableReason();

} // namespace Fast::Oot3d
