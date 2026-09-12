#include "oot3d_native_pica_transferable_cache.h"

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace Oot3dNativeGame;
namespace {
void Require(bool value) { if (!value) throw std::runtime_error("test failed"); }
void U32(std::string& bytes, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<char>(value >> shift));
}
std::string Fixture(uint32_t stage) {
    std::string bytes;
    U32(bytes, 1); U32(bytes, 0); U32(bytes, 42); U32(bytes, 0);
    U32(bytes, stage); U32(bytes, 768); U32(bytes, 0);
    for (unsigned i = 0; i < 768; ++i) U32(bytes, i);
    if (stage == 0) {
        U32(bytes, 8192); U32(bytes, 0);
        for (unsigned i = 0; i < 8192; ++i) U32(bytes, i);
    }
    return bytes;
}
auto Read(const std::string& bytes, PicaTransferableDialect dialect = PicaTransferableDialect::CitraLegacy) {
    std::istringstream input(bytes);
    return ReadPicaTransferableCache(input, dialect);
}
void Reject(const std::string& bytes) {
    bool rejected = false;
    try { (void)Read(bytes); } catch (const std::runtime_error&) { rejected = true; }
    Require(rejected);
}
}
int main() {
    try {
        const auto fs = Fixture(2);
        Require(Read(fs)[0].Stage == PicaTransferableStage::Fragment);
        Require(Read(fs, PicaTransferableDialect::Azahar)[0].Stage == PicaTransferableStage::Geometry);
        Require(Read(Fixture(1), PicaTransferableDialect::Azahar)[0].Stage == PicaTransferableStage::Fragment);
        Require(Read(fs)[0].Registers.back() == 767);
        Require(Read(Fixture(0))[0].ProgramAndSwizzles.back() == 8191);
        Require(Read(fs + fs.substr(4)).size() == 2);
        for (size_t size = 0; size < fs.size(); ++size) Reject(fs.substr(0, size));
        Reject(fs + "x");
        auto invalid = fs; invalid[0] = 2; Reject(invalid);
        invalid = fs; invalid[4] = 1; Reject(invalid);
        invalid = fs; invalid[16] = 3; Reject(invalid);
        invalid = fs; invalid[24] = 1; Reject(invalid);
        invalid = Fixture(0); invalid[28 + 768 * 4 + 4] = 1; Reject(invalid);
        std::cout << "transferable cache tests passed (including all 3100 truncation offsets)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
