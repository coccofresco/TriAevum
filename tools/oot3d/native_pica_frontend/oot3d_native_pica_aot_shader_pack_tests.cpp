#include "fast/oot3d/pica_aot_shader_pack.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <nlohmann/json.hpp>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "oot3d_native_pica_aot_shader_pack_tests: "
                  << message << '\n';
        std::exit(1);
    }
}

void RequireSuccess(bool condition, const std::string& error) {
    Require(condition, error.c_str());
}

} // namespace

int main() {
    try {
    using namespace Fast::Oot3d;
    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = std::filesystem::temp_directory_path() /
                           ("oot3d-pica-aot-tests-" + suffix);
    const auto packPath = directory / "test.o3ps";
    const auto inventoryPath = directory / "inventory.json";
    std::filesystem::create_directories(directory);

    const std::string vertexSource = "#version 450\nvoid main(){}\n";
    const std::string fragmentSource =
        "#version 450\nlayout(location=0) out vec4 c;"
        "void main(){c=vec4(1);}\n";
    const std::vector<uint32_t> vertexSpirv{
        0x07230203U, 0x00010000U, 0U, 1U, 0U};
    const std::vector<uint32_t> fragmentSpirv{
        0x07230203U, 0x00010000U, 0U, 2U, 0U};
    const std::vector<PicaAotShaderBinary> shaders{
        {PicaAotShaderStage::Vertex,
         IdentifyPicaAotShaderSource(vertexSource), vertexSpirv},
        {PicaAotShaderStage::Fragment,
         IdentifyPicaAotShaderSource(fragmentSource), fragmentSpirv},
    };
    std::string error;
    RequireSuccess(WritePicaAotShaderPack(packPath, 2U, shaders, &error),
                   error);

    PicaAotShaderPack pack;
    RequireSuccess(pack.Load(packPath, &error), error);
    Require(pack.Loaded() && pack.EntryCount() == 2U &&
                pack.DescriptorSchemaVersion() == 2U,
            "loaded pack metadata is wrong");
    const auto vertex =
        pack.Find(PicaAotShaderStage::Vertex, vertexSource);
    Require(std::vector<uint32_t>(vertex.begin(), vertex.end()) ==
                vertexSpirv,
            "vertex SPIR-V did not survive the pack round trip");
    Require(pack.Find(PicaAotShaderStage::Fragment, vertexSource).empty(),
            "stage-specific lookup accepted the wrong stage");
    Require(pack.Find(PicaAotShaderStage::Vertex, "different").empty(),
            "source-specific lookup accepted the wrong source");

    PicaEffectiveShaderInventory inventory;
    RequireSuccess(inventory.Configure(inventoryPath, &error), error);
    inventory.Observe(PicaAotShaderStage::Vertex, vertexSource,
                      2U, 0x1234U, 0x5678U, 9U);
    inventory.Observe(PicaAotShaderStage::Vertex, vertexSource,
                      2U, 0x1234U, 0x5678U, 9U);
    RequireSuccess(inventory.EntryCount() == 1U &&
                       inventory.Finish(&error),
                   error);
    std::ifstream inventoryInput(inventoryPath);
    const auto inventoryJson = nlohmann::json::parse(inventoryInput);
    inventoryInput.close();
    Require(inventoryJson.at("descriptor_schema_version") == 2U &&
                inventoryJson.at("shader_count") == 1U &&
                inventoryJson.at("shaders").at(0).at(
                    "observation_count") == 2U,
            "effective inventory did not deduplicate observations");

    std::fstream corrupt(packPath,
                         std::ios::binary | std::ios::in | std::ios::out);
    corrupt.seekp(-1, std::ios::end);
    const char changed = '\x5a';
    corrupt.write(&changed, 1);
    corrupt.close();
    pack.Clear();
    Require(!pack.Load(packPath, &error),
            "pack corruption was not detected");

    std::filesystem::remove_all(directory);
    std::cout << "oot3d_native_pica_aot_shader_pack_tests: ok\n";
    return 0;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_pica_aot_shader_pack_tests: exception: "
                  << exception.what() << '\n';
        return 1;
    }
}
