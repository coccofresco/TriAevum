#include "oot3d_a32_generated.h"
#include "oot3d_mass_generated.h"
#include "oot3d_native_mass_aot.h"
#include "oot3d_typed_gameplay_bridge.h"
#include "recomp/a32_direct_runtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

int main() {
  if (!Oot3dNativeGame::Oot3dMassAotAvailable()) {
    std::cerr << "mass AOT is not available\n";
    return 1;
  }
  const auto entries = Oot3dNativeGame::Oot3dMassAotBlockEntryPoints();
  constexpr std::array<uint32_t, 3> truncatedBoundaries{
      0x002E4348U,
      0x0044A248U,
      0x0048672CU,
  };
  for (const uint32_t pc : truncatedBoundaries) {
    if (std::binary_search(entries.begin(), entries.end(), pc)) {
      const auto *packed = oot3d::recomp::a32::FindBlock(
          oot3d::recomp::GetA32GeneratedRegistry(), pc);
      std::cerr << "truncated mass-AOT boundary remains enabled: 0x" << std::hex
                << pc << " packed_count="
                << (packed == nullptr ? 0U : packed->op_count) << '\n';
      return 1;
    }
  }
  for (const uint32_t pc : Oot3dNativeGame::Oot3dTypedGameplayEntryPoints()) {
    if (std::binary_search(entries.begin(), entries.end(), pc)) {
      std::cerr << "typed gameplay boundary remains enabled in mass "
                   "AOT: 0x"
                << std::hex << pc << '\n';
      return 1;
    }
  }
  std::cout << "safe_entries=" << entries.size() << " excluded_boundaries="
            << Oot3dNativeGame::Oot3dMassAotExcludedBoundaryCount() << '\n';
  return 0;
}
