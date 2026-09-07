#include "oot3d_source_data_bindings.h"
#include "oot3d_source_process_image.h"
#include "oot3d_source_process_profile.h"

#include <cstdint>
#include <filesystem>
#include <iostream>

extern "C" std::uint32_t oot3d_source_absolute_data_pilot_read();

int main(int argc, char** argv) {
    using namespace Oot3dSourceRuntime;
    if (argc != 2) {
        std::cerr << "usage: oot3d_source_absolute_data_pilot <code.bin>\n";
        return 2;
    }
    std::string error;
    auto process = LoadDirectMappedSourceProcessImage(
        Oot3dSourceProcessGenerated::kDescriptor,
        std::filesystem::path(argv[1]), &error);
    if (!process.has_value()) {
        std::cerr << error << '\n';
        return 1;
    }
    ScopedSourceAddressSpace binding(process->Memory);
    const std::uint32_t expected = SourceReadRef<std::uint32_t>(0x00314094);
    const std::uint32_t actual = oot3d_source_absolute_data_pilot_read();
    if (actual != expected) {
        std::cerr << "absolute source data mismatch\n";
        return 1;
    }
    std::cout << "absolute source data binding verified at 0x00314094\n";
    return 0;
}
