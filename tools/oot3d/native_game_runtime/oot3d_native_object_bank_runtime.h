#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "oot3d_room_compilation_unit.h"
#include "three_ds_recomp/oot3d/Oot3dNativeSourceProvider.h"

namespace Oot3dNativeGame {

class NativeObjectBankRuntime {
  public:
    NativeObjectBankRuntime(
        const Oot3d::RoomCompilationUnit& unit,
        ThreeDsRecomp::Oot3d::NativeSourceProvider& sources);
    ~NativeObjectBankRuntime();

    NativeObjectBankRuntime(const NativeObjectBankRuntime&) = delete;
    NativeObjectBankRuntime& operator=(const NativeObjectBankRuntime&) = delete;

    bool SetResidentRooms(std::vector<int32_t> roomIndices);
    bool IsReady(int32_t objectId) const;
    const std::vector<int32_t>& ResidentRooms() const;
    const std::string& Status() const;
    const std::string& Error() const;
    nlohmann::json Diagnostics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace Oot3dNativeGame
