#pragma once

#include <array>
#include <cstdint>
#include <istream>
#include <vector>

namespace Oot3dNativeGame {

// Both dialects used header version 1; the file cannot identify its enum order.
enum class PicaTransferableDialect { CitraLegacy, Azahar };
enum class PicaTransferableStage { Vertex, Fragment, Geometry };

struct PicaTransferableRecord {
    uint64_t SourceIdentifier = 0;
    PicaTransferableStage Stage = PicaTransferableStage::Fragment;
    std::array<uint32_t, 0x300> Registers{};
    std::vector<uint32_t> ProgramAndSwizzles;
};

// Read-only, bounded parser. Throws on unknown/truncated data; never invalidates
// or removes the user's source cache, unlike the donor's cache recovery path.
std::vector<PicaTransferableRecord> ReadPicaTransferableCache(
    std::istream& input, PicaTransferableDialect dialect);

} // namespace Oot3dNativeGame
