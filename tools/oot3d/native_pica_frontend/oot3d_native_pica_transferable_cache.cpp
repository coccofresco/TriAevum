#include "oot3d_native_pica_transferable_cache.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace Oot3dNativeGame {
namespace {
// Format references: Citra gl_shader_disk_cache.cpp (NativeVersion=1) and
// gl_shader_gen.h before 50f22d1f5; Azahar shader/generator/shader_gen.h.
// Citra/Azahar and yuzu donor sources are GPL-2.0-or-later; see THIRD_PARTY_NOTICES.md.
class Reader {
  public:
    explicit Reader(std::istream& input) : Input(input) {}
    uint32_t U32() {
        uint32_t result = 0;
        for (unsigned shift = 0; shift < 32; shift += 8) {
            const auto byte = Input.get();
            if (byte == std::char_traits<char>::eof())
                throw std::runtime_error("truncated transferable cache at byte " +
                                         std::to_string(Bytes));
            if (++Bytes > 512ULL * 1024 * 1024)
                throw std::runtime_error("transferable cache exceeds 512 MiB");
            result |= static_cast<uint32_t>(static_cast<uint8_t>(byte)) << shift;
        }
        return result;
    }
    uint64_t U64() {
        const uint64_t low = U32();
        return low | (static_cast<uint64_t>(U32()) << 32);
    }
  private:
    std::istream& Input;
    uint64_t Bytes = 0;
};
}

std::vector<PicaTransferableRecord> ReadPicaTransferableCache(
    std::istream& input, PicaTransferableDialect dialect) {
    Reader reader(input);
    if (reader.U32() != 1)
        throw std::runtime_error("unsupported transferable cache version (expected 1)");
    std::vector<PicaTransferableRecord> records;
    while (input.peek() != std::char_traits<char>::eof()) {
        if (records.size() >= 65536)
            throw std::runtime_error("too many transferable shader records");
        if (reader.U32() != 0)
            throw std::runtime_error("unsupported transferable entry kind");
        PicaTransferableRecord record;
        record.SourceIdentifier = reader.U64();
        const auto stage = reader.U32();
        if (stage > 2)
            throw std::runtime_error("unsupported transferable shader stage");
        record.Stage = stage == 0 ? PicaTransferableStage::Vertex
            : stage == (dialect == PicaTransferableDialect::CitraLegacy ? 2U : 1U)
                ? PicaTransferableStage::Fragment : PicaTransferableStage::Geometry;
        if (reader.U64() != record.Registers.size())
            throw std::runtime_error("unsupported transferable register count");
        for (auto& word : record.Registers)
            word = reader.U32();
        if (record.Stage == PicaTransferableStage::Vertex) {
            if (reader.U64() != 8192)
                throw std::runtime_error("expected 4096 program and 4096 swizzle words");
            record.ProgramAndSwizzles.resize(8192);
            for (auto& word : record.ProgramAndSwizzles)
                word = reader.U32();
        }
        records.push_back(std::move(record));
    }
    if (input.bad())
        throw std::runtime_error("could not read transferable cache");
    if (records.empty())
        throw std::runtime_error("transferable cache has no records");
    return records;
}
} // namespace Oot3dNativeGame
