#include "fast/oot3d/pica_pipeline_manifest.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::vector<std::filesystem::path> Inputs;
    std::filesystem::path Output;
};

Options ParseOptions(int argc, char** argv) {
    Options result;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--inventory" && index + 1 < argc) {
            result.Inputs.emplace_back(argv[++index]);
        } else if (argument == "--output" && index + 1 < argc) {
            result.Output = argv[++index];
        } else {
            throw std::runtime_error(
                "usage: oot3d_native_pica_pipeline_manifest "
                "--inventory <capture.json> [--inventory <capture.json>...] "
                "--output <manifest.json>");
        }
    }
    if (result.Inputs.empty() || result.Output.empty()) {
        throw std::runtime_error(
            "pipeline manifest requires at least one inventory and an output");
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = ParseOptions(argc, argv);
        uint32_t descriptorSchemaVersion = 0U;
        std::vector<Fast::Oot3d::PicaGraphicsPipelineManifestEntry> entries;
        for (const auto& input : options.Inputs) {
            Fast::Oot3d::PicaGraphicsPipelineManifest manifest;
            std::string error;
            if (!manifest.Load(input, &error)) {
                throw std::runtime_error(error);
            }
            if (descriptorSchemaVersion == 0U) {
                descriptorSchemaVersion =
                    manifest.DescriptorSchemaVersion();
            } else if (descriptorSchemaVersion !=
                       manifest.DescriptorSchemaVersion()) {
                throw std::runtime_error(
                    "pipeline inventories use different descriptor schemas");
            }
            entries.insert(entries.end(), manifest.Entries().begin(),
                           manifest.Entries().end());
        }
        std::string error;
        if (!Fast::Oot3d::WritePicaGraphicsPipelineManifest(
                options.Output, descriptorSchemaVersion, entries, &error)) {
            throw std::runtime_error(error);
        }
        Fast::Oot3d::PicaGraphicsPipelineManifest merged;
        if (!merged.Load(options.Output, &error)) {
            throw std::runtime_error(error);
        }
        std::cout << "PICA pipeline manifest: "
                  << merged.Entries().size() << " pipelines, schema "
                  << merged.DescriptorSchemaVersion() << ", "
                  << options.Output.string() << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "oot3d_native_pica_pipeline_manifest: "
                  << exception.what() << '\n';
        return 1;
    }
}
