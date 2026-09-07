#pragma once

#include <filesystem>
#include <string>

namespace Oot3dNativeGame {

bool InstallWholeAotCrashDiagnostics(const std::filesystem::path& directory,
                                     std::string* error = nullptr);

bool WriteWholeAotDiagnosticMinidump(const std::filesystem::path& path,
                                     std::string* error = nullptr);

} // namespace Oot3dNativeGame
