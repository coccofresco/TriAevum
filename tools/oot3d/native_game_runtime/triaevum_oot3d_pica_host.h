#pragma once

#include "oot3d_native_pica_submission.h"
#include "triaevum/guest_memory_lease_pool.h"
#include "triaevum/pica_scanout_state.h"
#include "triaevum/runtime_session.h"
#include "triaevum_oot3d_pica_service.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace Oot3dNativeGame {

class TriAevumOot3dPicaHost final {
  public:
    static std::unique_ptr<TriAevumOot3dPicaHost> Create(
        triaevum::module::RuntimeSession& session,
        TriAevumOot3dPicaHostCallbacks callbacks = {},
        bool deferGpuBackedDisplayTransfers = true,
        std::string* error = nullptr);
    static std::unique_ptr<TriAevumOot3dPicaHost> Create(
        const triaevum::module::TamModuleMetadataV1& metadata,
        triaevum::module::GuestMemoryReadLeasePoolV1::MapFunction map,
        triaevum::module::GuestMemoryReadLeasePoolV1::UnmapFunction unmap,
        TriAevumOot3dPicaHostCallbacks callbacks = {},
        bool deferGpuBackedDisplayTransfers = true,
        std::string* error = nullptr);

    ~TriAevumOot3dPicaHost();
    TriAevumOot3dPicaHost(const TriAevumOot3dPicaHost&) = delete;
    TriAevumOot3dPicaHost& operator=(const TriAevumOot3dPicaHost&) = delete;

    triaevum::module::ServiceRegistrationResult Register(
        triaevum::module::RuntimeSession& session);
    TriAevumModuleStatusV1 Invoke(
        std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
        TriAevumMutableBytesV1 response, std::size_t* responseSize);

    Oot3dNativePicaFrontend& Frontend() noexcept;
    Oot3dNativePicaSubmissionQueue& Queue() noexcept;
    Oot3dPicaSubmissionBatch TakePendingBatch();
    void PublishCompletedInterrupt(Oot3dPicaInterruptId interrupt);
    const triaevum::module::PicaScanoutStateV1& Scanout() const noexcept;

  private:
    struct Impl;
    explicit TriAevumOot3dPicaHost(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> mImpl;
};

} // namespace Oot3dNativeGame
