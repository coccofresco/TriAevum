#include "oot3d_ctr_pica_backend.h"

namespace Oot3dSourceRuntime {

CtrPicaBackend::CtrPicaBackend(
    Oot3dNativeGame::Oot3dNativePicaFrontend& frontend,
    CtrPresentationBackend* presentation)
    : mFrontend(frontend), mPresentation(presentation) {}

bool CtrPicaBackend::WriteRegisters(
    std::uint32_t base, std::span<const std::uint32_t> values,
    std::span<const std::uint32_t> masks) {
    if (masks.empty()) return mFrontend.WriteHardwareRegisters(base, values);
    return masks.size() == values.size() &&
           mFrontend.WriteHardwareRegistersWithMask(base, values, masks);
}

bool CtrPicaBackend::SubmitCommand(
    const CtrGspCommand& command,
    std::span<const std::uint32_t> commandList) {
    Oot3dNativeGame::Oot3dGspCommandPacket packet;
    packet.Control = command.Control;
    packet.Parameters = command.Parameters;
    return mFrontend.SubmitGspCommand(packet, commandList);
}

bool CtrPicaBackend::SetFramebuffer(const CtrGspFramebuffer& framebuffer) {
    return mPresentation == nullptr || mPresentation->SetFramebuffer(framebuffer);
}

void CtrPicaBackend::SetLcdForceBlack(bool forceBlack) {
    if (mPresentation != nullptr) mPresentation->SetLcdForceBlack(forceBlack);
}

std::vector<std::uint8_t> CtrPicaBackend::TakeInterrupts() {
    std::vector<std::uint8_t> result;
    for (const auto interrupt : mFrontend.TakePendingInterrupts())
        result.push_back(static_cast<std::uint8_t>(interrupt));
    return result;
}

} // namespace Oot3dSourceRuntime
