#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Oot3dNativeGame {

struct NativeA32CtrPicaFramebuffer {
    uint32_t Screen = 0;
    uint32_t ActiveBuffer = 0;
    uint32_t AddressLeft = 0;
    uint32_t AddressRight = 0;
    uint32_t Stride = 0;
    uint32_t Format = 0;
    uint32_t ShownBuffer = 0;
};

struct NativeA32CtrPicaSubmissionResult {
    bool DisplayTransferDeferredToGpu = false;
    bool MemoryFillDeferredToGpu = false;
    bool DisplayTransferCpuCopySuppressed = false;
};

// Keeps a title process independent from a renderer implementation. A linked
// development executable may use its frontend directly; a packaged title
// module routes the same operations through the stable host-service ABI.
class NativeA32CtrPicaBridge {
  public:
    virtual ~NativeA32CtrPicaBridge() = default;

    virtual bool WriteHardwareRegisters(uint32_t base, std::span<const uint32_t> values,
                                        std::span<const uint32_t> masks, std::string* error) = 0;
    virtual bool SubmitGspCommand(uint32_t control, const std::array<uint32_t, 7>& parameters,
                                  std::span<const uint32_t> commandWords, NativeA32CtrPicaSubmissionResult* result,
                                  std::string* error) = 0;
    virtual bool SetFramebuffer(const NativeA32CtrPicaFramebuffer& framebuffer, std::string* error) = 0;
    virtual bool SetLcdForceBlack(bool forceBlack, std::string* error) = 0;
    virtual bool TakePendingInterrupts(std::vector<uint8_t>* interrupts, std::string* error) = 0;
};

} // namespace Oot3dNativeGame
