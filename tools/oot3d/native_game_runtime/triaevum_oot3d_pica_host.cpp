#include "triaevum_oot3d_pica_host.h"

#include "triaevum/service_abi.h"
#include "triaevum_oot3d_pica_memory.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace Oot3dNativeGame {

struct TriAevumOot3dPicaHost::Impl {
    triaevum::module::PicaScanoutStateV1 Scanout;
    std::unique_ptr<triaevum::module::GuestMemoryReadLeasePoolV1> Leases;
    std::unique_ptr<Oot3dNativePicaSubmissionQueue> Queue;
    Oot3dNativePicaFrontend Frontend;
    std::unique_ptr<TriAevumOot3dPicaServiceBackend> Backend;
    std::unique_ptr<triaevum::module::PicaHostServiceAdapterV1> Adapter;
    triaevum::module::RuntimeSession* Session = nullptr;
};

namespace {

void SetError(std::string* error, const char* message) {
    if (error != nullptr) {
        *error = message;
    }
}

TriAevumOot3dPicaHostCallbacks BindScanoutCallbacks(
    triaevum::module::PicaScanoutStateV1& scanout,
    TriAevumOot3dPicaHostCallbacks callbacks) {
    auto framebufferObserver = std::move(callbacks.SetFramebuffer);
    callbacks.SetFramebuffer =
        [&scanout, observer = std::move(framebufferObserver)](
            const triaevum::module::PicaFramebufferV1& framebuffer) {
            const TriAevumModuleStatusV1 validation =
                triaevum::module::PicaScanoutStateV1::ValidateFramebuffer(
                    framebuffer);
            if (validation != TRIAEVUM_MODULE_OK_V1) {
                return validation;
            }
            if (observer) {
                const TriAevumModuleStatusV1 status = observer(framebuffer);
                if (status != TRIAEVUM_MODULE_OK_V1) {
                    return status;
                }
            }
            return scanout.SetFramebuffer(framebuffer);
        };

    auto forceBlackObserver = std::move(callbacks.SetLcdForceBlack);
    callbacks.SetLcdForceBlack =
        [&scanout, observer = std::move(forceBlackObserver)](bool forceBlack) {
            if (observer) {
                const TriAevumModuleStatusV1 status = observer(forceBlack);
                if (status != TRIAEVUM_MODULE_OK_V1) {
                    return status;
                }
            }
            return scanout.SetLcdForceBlack(forceBlack);
        };
    return callbacks;
}

} // namespace

std::unique_ptr<TriAevumOot3dPicaHost> TriAevumOot3dPicaHost::Create(
    triaevum::module::RuntimeSession& session,
    TriAevumOot3dPicaHostCallbacks callbacks,
    bool deferGpuBackedDisplayTransfers, std::string* error) {
    if (!session.IsLoaded() || session.IsRunning() ||
        session.Metadata() == nullptr) {
        SetError(error,
                 "TriAevum PICA host requires a loaded, uninitialized session");
        return nullptr;
    }
    auto host = Create(
        *session.Metadata(),
        [&session](const TriAevumGuestMemoryMapRequestV1& request,
                   TriAevumGuestMemoryViewV1* view) {
            return session.MapGuestMemory(request, view);
        },
        [&session](std::uint64_t token, std::uint32_t flags) {
            return session.UnmapGuestMemory(token, flags);
        },
        std::move(callbacks), deferGpuBackedDisplayTransfers, error);
    if (host != nullptr) {
        host->mImpl->Session = &session;
    }
    return host;
}

std::unique_ptr<TriAevumOot3dPicaHost> TriAevumOot3dPicaHost::Create(
    const triaevum::module::TamModuleMetadataV1& metadata,
    triaevum::module::GuestMemoryReadLeasePoolV1::MapFunction map,
    triaevum::module::GuestMemoryReadLeasePoolV1::UnmapFunction unmap,
    TriAevumOot3dPicaHostCallbacks callbacks,
    bool deferGpuBackedDisplayTransfers, std::string* error) {
    const bool requiresPica = std::any_of(
        metadata.requiredServices.begin(), metadata.requiredServices.end(),
        [](const auto& service) {
            return service.id == TRIAEVUM_SERVICE_PICA_V1;
        });
    if (!requiresPica || !map || !unmap) {
        SetError(error,
                 "TriAevum PICA host requires its declared service and memory API");
        return nullptr;
    }
    if (callbacks.BeginSubmissionMemoryAccess ||
        callbacks.EndSubmissionMemoryAccess) {
        SetError(error, "TriAevum PICA host owns submission memory scopes");
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->Leases =
        std::make_unique<triaevum::module::GuestMemoryReadLeasePoolV1>(
            std::move(map), std::move(unmap));
    auto memory = BuildTriAevumOot3dPicaPhysicalMemoryView(
        metadata, *impl->Leases, error);
    if (!memory.has_value()) {
        return nullptr;
    }
    impl->Queue = std::make_unique<Oot3dNativePicaSubmissionQueue>(
        std::move(*memory), deferGpuBackedDisplayTransfers);
    impl->Frontend.SetPacketSink(impl->Queue.get());
    auto* const leases = impl->Leases.get();
    callbacks.BeginSubmissionMemoryAccess = [leases]() {
        return leases->ReleaseAll();
    };
    callbacks.EndSubmissionMemoryAccess = [leases]() {
        return leases->ReleaseAll();
    };
    callbacks = BindScanoutCallbacks(impl->Scanout, std::move(callbacks));
    impl->Backend = std::make_unique<TriAevumOot3dPicaServiceBackend>(
        impl->Frontend, std::move(callbacks));
    impl->Adapter =
        std::make_unique<triaevum::module::PicaHostServiceAdapterV1>(
            *impl->Backend);
    if (error != nullptr) {
        error->clear();
    }
    return std::unique_ptr<TriAevumOot3dPicaHost>(
        new TriAevumOot3dPicaHost(std::move(impl)));
}

TriAevumOot3dPicaHost::TriAevumOot3dPicaHost(std::unique_ptr<Impl> impl)
    : mImpl(std::move(impl)) {}

TriAevumOot3dPicaHost::~TriAevumOot3dPicaHost() {
    if (mImpl != nullptr) {
        mImpl->Frontend.SetPacketSink(nullptr);
        mImpl->Leases->ReleaseAll();
    }
}

triaevum::module::ServiceRegistrationResult
TriAevumOot3dPicaHost::Register(
    triaevum::module::RuntimeSession& session) {
    if (mImpl->Session != nullptr && mImpl->Session != &session) {
        return triaevum::module::ServiceRegistrationResult::InvalidArgument;
    }
    return session.RegisterService(
        TRIAEVUM_SERVICE_PICA_V1,
        triaevum::module::PicaHostServiceAdapterV1::Invoke,
        mImpl->Adapter.get());
}

TriAevumModuleStatusV1 TriAevumOot3dPicaHost::Invoke(
    std::uint32_t operation, TriAevumReadOnlyBytesV1 request,
    TriAevumMutableBytesV1 response, std::size_t* responseSize) {
    return triaevum::module::PicaHostServiceAdapterV1::Invoke(
        mImpl->Adapter.get(), operation, request, response, responseSize);
}

Oot3dNativePicaFrontend& TriAevumOot3dPicaHost::Frontend() noexcept {
    return mImpl->Frontend;
}

Oot3dNativePicaSubmissionQueue& TriAevumOot3dPicaHost::Queue() noexcept {
    return *mImpl->Queue;
}

Oot3dPicaSubmissionBatch TriAevumOot3dPicaHost::TakePendingBatch() {
    return mImpl->Queue->TakePendingBatch();
}

void TriAevumOot3dPicaHost::PublishCompletedInterrupt(
    Oot3dPicaInterruptId interrupt) {
    mImpl->Backend->PublishCompletedInterrupt(interrupt);
}

const triaevum::module::PicaScanoutStateV1&
TriAevumOot3dPicaHost::Scanout() const noexcept {
    return mImpl->Scanout;
}

} // namespace Oot3dNativeGame
