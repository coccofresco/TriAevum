#include "oot3d_native_pica_submission.h"

#include "oot3d_native_a32_memory.h"

namespace Oot3dNativeGame {

Oot3dPicaPhysicalMemoryView::Oot3dPicaPhysicalMemoryView(
    const NativeA32Memory& memory,
    std::vector<Oot3dPicaPhysicalMemoryRegion> regions)
    : Oot3dPicaPhysicalMemoryView(
          std::move(regions),
          [&memory](uint32_t address, size_t size) {
              const auto* bytes = memory.GetReadPointer(address, size);
              return bytes == nullptr ? std::span<const uint8_t>{}
                                      : std::span<const uint8_t>(bytes, size);
          },
          [&memory](uint32_t address, size_t size) {
              return memory.RangeWriteGeneration(address, size);
          }) {}

} // namespace Oot3dNativeGame
