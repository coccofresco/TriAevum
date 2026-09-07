#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace Oot3dNativeGame {

struct NativeClosureFunction {
    uint32_t Address = 0;
    std::string Name;
    std::string Module;
    std::string BodyStatus;
    std::string CompileReadiness;
    std::vector<std::string> Hazards;
    bool AotBound = false;
};

class NativeClosureCatalog {
  public:
    static NativeClosureCatalog LoadFile(const std::filesystem::path& path);

    const NativeClosureFunction* Find(uint32_t address) const;
    const std::string& SourceRevision() const;
    const std::string& PayloadSha256() const;
    const std::string& RoomCompilationUnitId() const;
    const std::string& RoomCompilationPayloadSha256() const;
    size_t FunctionCount() const;
    size_t ExtractedBodyCount() const;
    size_t BodyWithoutListedHazardsCount() const;
    size_t AotBoundFunctionCount() const;
    bool AotAvailable() const;
    nlohmann::json Diagnostics() const;

  private:
    std::string mSourceRevision;
    std::string mPayloadSha256;
    std::string mRoomCompilationUnitId;
    std::string mRoomCompilationPayloadSha256;
    size_t mExtractedBodyCount = 0;
    size_t mBodyWithoutListedHazardsCount = 0;
    size_t mAotBoundFunctionCount = 0;
    std::unordered_map<uint32_t, NativeClosureFunction> mFunctions;
};

} // namespace Oot3dNativeGame
