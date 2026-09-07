#include "renderer_3ds_pica_capture_conformance.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::filesystem::path> CollectInputs(const std::vector<std::filesystem::path>& roots) {
    std::vector<std::filesystem::path> inputs;
    for (const auto& root : roots) {
        if (std::filesystem::is_regular_file(root)) {
            inputs.push_back(root);
            continue;
        }
        if (!std::filesystem::is_directory(root)) {
            throw std::runtime_error("input does not exist: " + root.string());
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
                inputs.push_back(entry.path());
            }
        }
    }
    std::ranges::sort(inputs);
    inputs.erase(std::unique(inputs.begin(), inputs.end()), inputs.end());
    if (inputs.empty()) {
        throw std::runtime_error("no JSONL capture files found");
    }
    return inputs;
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::filesystem::path> inputRoots;
        std::filesystem::path outputPath;
        std::string corpusId;
        std::string sourceImageSha256;
        for (int index = 1; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "--input" && index + 1 < argc) {
                inputRoots.emplace_back(argv[++index]);
            } else if (argument == "--output" && index + 1 < argc) {
                outputPath = argv[++index];
            } else if (argument == "--corpus-id" && index + 1 < argc) {
                corpusId = argv[++index];
            } else if (argument == "--source-image-sha256" && index + 1 < argc) {
                sourceImageSha256 = argv[++index];
            } else {
                throw std::runtime_error("usage: renderer_3ds_pica_capture_inspector --input "
                                         "<capture.jsonl-or-directory> [--input <...>] --output "
                                         "<report.json> --corpus-id <id> "
                                         "[--source-image-sha256 <sha256>]");
            }
        }
        if (inputRoots.empty() || outputPath.empty() || corpusId.empty()) {
            throw std::runtime_error("input, output and corpus-id are required");
        }

        Fast::Renderer3ds::Diagnostics::PicaCaptureConformanceReport report;
        for (const auto& inputPath : CollectInputs(inputRoots)) {
            std::ifstream input(inputPath, std::ios::binary);
            if (!input) {
                throw std::runtime_error("could not open capture: " + inputPath.string());
            }
            std::string error;
            if (!Fast::Renderer3ds::Diagnostics::AnalyzePicaCaptureStream(input, report, &error)) {
                throw std::runtime_error(error + ": " + inputPath.string());
            }
        }

        const auto document = report.ToJson(corpusId, sourceImageSha256);
        const auto parent = outputPath.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent);
        std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("could not create report: " + outputPath.string());
        }
        output << document.dump(2) << '\n';
        if (!output) {
            throw std::runtime_error("could not write report: " + outputPath.string());
        }
        std::cout << document.at("status").get<std::string>() << ": " << report.DecodedDrawCount << " decoded draws, "
                  << report.ContractRepresentableDrawCount << " contract-representable, " << report.IssueCounts.size()
                  << " issue categories\n";
        return report.Conformant() ? 0 : 2;
    } catch (const std::exception& exception) {
        std::cerr << "renderer 3DS PICA capture inspection failed: " << exception.what() << '\n';
        return 1;
    }
}
