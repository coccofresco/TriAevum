#include "triaevum_oot3d_pica_service.h"
#include "triaevum_oot3d_pica_host.h"
#include "triaevum_oot3d_pica_memory.h"

#include "triaevum/service_codec.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "triaevum_oot3d_pica_service_tests: " << message << '\n';
    }
    return condition;
}

} // namespace

int main() {
    bool ok = true;
    Oot3dNativeGame::Oot3dNativePicaFrontend frontend;
    bool framebufferObserved = false;
    bool forceBlackObserved = false;
    std::uint32_t beginMemoryAccessCount = 0U;
    std::uint32_t endMemoryAccessCount = 0U;
    Oot3dNativeGame::TriAevumOot3dPicaHostCallbacks callbacks;
    callbacks.BeginSubmissionMemoryAccess = [&]() {
        ++beginMemoryAccessCount;
        return TRIAEVUM_MODULE_OK_V1;
    };
    callbacks.EndSubmissionMemoryAccess = [&]() {
        ++endMemoryAccessCount;
        return TRIAEVUM_MODULE_OK_V1;
    };
    callbacks.SetFramebuffer = [&](const auto& framebuffer) {
        framebufferObserved = framebuffer.addressLeft == 0x18000000U;
        return TRIAEVUM_MODULE_OK_V1;
    };
    callbacks.SetLcdForceBlack = [&](bool forceBlack) {
        forceBlackObserved = forceBlack;
        return TRIAEVUM_MODULE_OK_V1;
    };
    Oot3dNativeGame::TriAevumOot3dPicaServiceBackend backend(frontend,
                                                              callbacks);
    triaevum::module::PicaHostServiceAdapterV1 adapter(backend);

    const std::array<std::uint32_t, 2> values = {0x11223344U, 0x55667788U};
    TriAevumPicaWriteRegistersRequestV1 write{};
    write.header = triaevum::module::RequestHeader<
        TriAevumPicaWriteRegistersRequestV1>();
    write.base_register = 0x100U;
    write.register_count = static_cast<std::uint32_t>(values.size());
    write.values = {sizeof(write), sizeof(values)};
    std::vector<std::uint8_t> request(sizeof(write) + sizeof(values));
    std::memcpy(request.data(), &write, sizeof(write));
    std::memcpy(request.data() + sizeof(write), values.data(), sizeof(values));
    TriAevumPicaWriteRegistersResponseV1 writeResponse{};
    std::size_t responseSize = 0U;
    auto status = triaevum::module::PicaHostServiceAdapterV1::Invoke(
        &adapter, TRIAEVUM_PICA_WRITE_REGISTERS_V1,
        {request.data(), request.size()},
        {reinterpret_cast<std::uint8_t*>(&writeResponse),
         sizeof(writeResponse)},
        &responseSize);
    ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 &&
                     frontend.ReadHardwareRegister(0x100U).value_or(0U) ==
                         values[0] &&
                     frontend.ReadHardwareRegister(0x104U).value_or(0U) ==
                         values[1],
                 "typed service did not reach the native PICA frontend");

    TriAevumPicaSubmitGspCommandRequestV1 gsp{};
    gsp.header = triaevum::module::RequestHeader<
        TriAevumPicaSubmitGspCommandRequestV1>();
    gsp.frame_sequence = 1U;
    gsp.control = static_cast<std::uint32_t>(
        Oot3dNativeGame::Oot3dGspCommandId::CacheFlush);
    TriAevumPicaSubmitGspCommandResponseV1 gspResponse{};
    status = triaevum::module::PicaHostServiceAdapterV1::Invoke(
        &adapter, TRIAEVUM_PICA_SUBMIT_GSP_COMMAND_V1,
        {reinterpret_cast<const std::uint8_t*>(&gsp), sizeof(gsp)},
        {reinterpret_cast<std::uint8_t*>(&gspResponse), sizeof(gspResponse)},
        &responseSize);
    ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 &&
                     gspResponse.submission_id == 1U &&
                     frontend.PendingGspCommands().size() == 1U &&
                     beginMemoryAccessCount == 1U &&
                     endMemoryAccessCount == 1U,
                 "typed GSP command did not reach the native PICA frontend");

    TriAevumPicaSetFramebufferRequestV1 framebuffer{};
    framebuffer.header = triaevum::module::RequestHeader<
        TriAevumPicaSetFramebufferRequestV1>();
    framebuffer.address_left = 0x18000000U;
    TriAevumPicaSetFramebufferResponseV1 framebufferResponse{};
    status = triaevum::module::PicaHostServiceAdapterV1::Invoke(
        &adapter, TRIAEVUM_PICA_SET_FRAMEBUFFER_V1,
        {reinterpret_cast<const std::uint8_t*>(&framebuffer),
         sizeof(framebuffer)},
        {reinterpret_cast<std::uint8_t*>(&framebufferResponse),
         sizeof(framebufferResponse)},
        &responseSize);
    ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 && framebufferObserved,
                 "framebuffer callback was not dispatched");

    TriAevumPicaSetLcdForceBlackRequestV1 forceBlack{};
    forceBlack.header = triaevum::module::RequestHeader<
        TriAevumPicaSetLcdForceBlackRequestV1>();
    forceBlack.force_black = 1U;
    TriAevumPicaSetLcdForceBlackResponseV1 forceBlackResponse{};
    status = triaevum::module::PicaHostServiceAdapterV1::Invoke(
        &adapter, TRIAEVUM_PICA_SET_LCD_FORCE_BLACK_V1,
        {reinterpret_cast<const std::uint8_t*>(&forceBlack),
         sizeof(forceBlack)},
        {reinterpret_cast<std::uint8_t*>(&forceBlackResponse),
         sizeof(forceBlackResponse)},
        &responseSize);
    ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 && forceBlackObserved,
                 "LCD force-black callback was not dispatched");

    backend.PublishCompletedInterrupt(
        Oot3dNativeGame::Oot3dPicaInterruptId::Ppf);
    std::vector<std::uint8_t> completedInterrupts;
    status = backend.TakeInterrupts(&completedInterrupts);
    ok &= Expect(status == TRIAEVUM_MODULE_OK_V1 &&
                     completedInterrupts == std::vector<std::uint8_t>{
                         static_cast<std::uint8_t>(
                             Oot3dNativeGame::Oot3dPicaInterruptId::Ppf)},
                 "renderer completion did not reach the module interrupt queue");

    std::array<std::uint8_t, 256> guestMemory{};
    guestMemory[8] = 0x5AU;
    std::uint64_t nextToken = 1U;
    std::uint32_t releasedLeases = 0U;
    const auto mapGuestMemory =
        [&](const TriAevumGuestMemoryMapRequestV1& request,
            TriAevumGuestMemoryViewV1* view) -> TriAevumModuleStatusV1 {
            constexpr std::uint32_t guestBase = 0x1000U;
            if (request.guest_address < guestBase) {
                return TRIAEVUM_MODULE_TITLE_ERROR_V1;
            }
            const std::uint32_t offset = request.guest_address - guestBase;
            if (offset > guestMemory.size() ||
                request.byte_count > guestMemory.size() - offset) {
                return TRIAEVUM_MODULE_TITLE_ERROR_V1;
            }
            *view = {sizeof(TriAevumGuestMemoryViewV1),
                     TRIAEVUM_GUEST_MEMORY_READ_V1,
                     guestMemory.data() + offset,
                     request.byte_count,
                     17U,
                     nextToken++};
            return TRIAEVUM_MODULE_OK_V1;
        };
    const auto unmapGuestMemory =
        [&](std::uint64_t, std::uint32_t) {
            ++releasedLeases;
            return TRIAEVUM_MODULE_OK_V1;
        };
    triaevum::module::GuestMemoryReadLeasePoolV1 leases(
        mapGuestMemory, unmapGuestMemory);
    triaevum::module::TamModuleMetadataV1 metadata;
    metadata.requiredServices.push_back(
        {TRIAEVUM_SERVICE_PICA_V1, TRIAEVUM_SERVICE_SCHEMA_V1});
    metadata.physicalMemoryRegions.push_back(
        {0x20000000U, 0x1000U, 0x100U});
    std::string memoryError;
    auto physicalMemory =
        Oot3dNativeGame::BuildTriAevumOot3dPicaPhysicalMemoryView(
            metadata, leases, &memoryError);
    const auto mapped = physicalMemory.has_value()
                            ? physicalMemory->View(0x20000008U, 8U)
                            : std::nullopt;
    ok &= Expect(mapped.has_value() && mapped->front() == 0x5AU &&
                     physicalMemory->RangeWriteGeneration(0x20000008U, 8U) ==
                         17U &&
                     leases.ReleaseAll() == TRIAEVUM_MODULE_OK_V1 &&
                     releasedLeases == 1U && memoryError.empty(),
                 "verified TAM memory mapping did not reach a guest lease");

    auto host = Oot3dNativeGame::TriAevumOot3dPicaHost::Create(
        metadata, mapGuestMemory, unmapGuestMemory, {}, true, &memoryError);
    gspResponse = {};
    status = host != nullptr
                 ? host->Invoke(
                       TRIAEVUM_PICA_SUBMIT_GSP_COMMAND_V1,
                       {reinterpret_cast<const std::uint8_t*>(&gsp),
                        sizeof(gsp)},
                       {reinterpret_cast<std::uint8_t*>(&gspResponse),
                        sizeof(gspResponse)},
                       &responseSize)
                 : TRIAEVUM_MODULE_HOST_ERROR_V1;
    ok &= Expect(host != nullptr && status == TRIAEVUM_MODULE_OK_V1 &&
                     host->Frontend().PendingGspCommands().size() == 1U &&
                     host->Queue().IsQuiescent() && memoryError.empty(),
                 "assembled TriAevum PICA host did not own the native path");

    framebuffer.screen = 0U;
    framebuffer.active_buffer = 0U;
    framebuffer.address_left = 0x14001000U;
    framebuffer.address_right = 0x14002000U;
    framebuffer.stride = 960U;
    framebuffer.format = 0U;
    framebuffer.shown_buffer = 0U;
    status = host != nullptr
                 ? host->Invoke(
                       TRIAEVUM_PICA_SET_FRAMEBUFFER_V1,
                       {reinterpret_cast<const std::uint8_t*>(&framebuffer),
                        sizeof(framebuffer)},
                       {reinterpret_cast<std::uint8_t*>(&framebufferResponse),
                        sizeof(framebufferResponse)},
                       &responseSize)
                 : TRIAEVUM_MODULE_HOST_ERROR_V1;
    ok &= Expect(host != nullptr && status == TRIAEVUM_MODULE_OK_V1 &&
                     host->Scanout().ShouldPresent(0U, 0x14001000U) &&
                     !host->Scanout().ShouldPresent(1U, 0x14001000U),
                 "assembled PICA host did not retain generic CTR scanout state");

    metadata.physicalMemoryRegions.clear();
    physicalMemory =
        Oot3dNativeGame::BuildTriAevumOot3dPicaPhysicalMemoryView(
            metadata, leases, &memoryError);
    ok &= Expect(!physicalMemory.has_value() && !memoryError.empty(),
                 "empty TAM memory layout produced a PICA memory view");
    return ok ? 0 : 1;
}
