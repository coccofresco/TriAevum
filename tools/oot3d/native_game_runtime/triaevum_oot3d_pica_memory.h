#pragma once

#include "oot3d_native_pica_submission.h"
#include "triaevum/guest_memory_lease_pool.h"
#include "triaevum/tam_metadata.h"

#include <optional>
#include <string>

namespace Oot3dNativeGame {

std::optional<Oot3dPicaPhysicalMemoryView>
BuildTriAevumOot3dPicaPhysicalMemoryView(
    const triaevum::module::TamModuleMetadataV1& metadata,
    triaevum::module::GuestMemoryReadLeasePoolV1& leases,
    std::string* error = nullptr);

} // namespace Oot3dNativeGame
