#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "three_ds_recomp/oot3d/Oot3dNativeResourceContract.h"

namespace {

struct Args {
    std::string ContractPath;
    std::string OutputPath;
};

void PrintUsage() {
    std::cerr << "usage: oot3d_native_resource_probe --contract <native_resource_contract.json> --output <probe.json>\n";
}

bool ParseArgs(int argc, char** argv, Args& args) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--contract" && i + 1 < argc) {
            args.ContractPath = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            args.OutputPath = argv[++i];
        } else {
            return false;
        }
    }

    return !args.ContractPath.empty() && !args.OutputPath.empty();
}

nlohmann::json ReadJsonFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open JSON file: " + path);
    }
    return nlohmann::json::parse(file);
}

void WriteJsonFile(const std::string& path, const nlohmann::json& data) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("could not write JSON file: " + path);
    }
    file << data.dump(2) << "\n";
}

} // namespace

int main(int argc, char** argv) {
    Args args;
    if (!ParseArgs(argc, argv, args)) {
        PrintUsage();
        return 64;
    }

    try {
        const auto contractJson = ReadJsonFile(args.ContractPath);
        const auto contract = ThreeDsRecomp::Oot3d::ParseNativeResourceContract(contractJson);
        const auto contractValidation = ThreeDsRecomp::Oot3d::ValidateNativeResourceContract(contract);
        const auto resourceSet = ThreeDsRecomp::Oot3d::BuildNativeDemoResourceSet(contract);
        const auto probe = ThreeDsRecomp::Oot3d::ProbeNativeDemoResourceFiles(contract);

        auto output = ThreeDsRecomp::Oot3d::NativeResourceProbeResultToJson(probe);
        output["format"] = "oot3d_runtime/three_ds_recomp_native_resource_probe_v1";
        output["contract"] = args.ContractPath;
        output["contract_status"] = contractValidation.IsValid ? "valid" : "invalid";
        output["resource_set_status"] = resourceSet.IsValid ? "valid" : "invalid";
        output["runtime_n64_asset_substitution_used"] = false;
        output["shipwright_replacement_path_used"] = false;
        output["probe_runtime"] = "runtime/three_ds_recomp_oot3d_native_resource_probe_cpp";

        WriteJsonFile(args.OutputPath, output);
        std::cout << output.dump(2) << "\n";
        return probe.IsValid ? 0 : 2;
    } catch (const std::exception& ex) {
        nlohmann::json output = {
            { "format", "oot3d_runtime/three_ds_recomp_native_resource_probe_v1" },
            { "status", "invalid" },
            { "contract", args.ContractPath },
            { "issue_count", 1 },
            { "issues",
              nlohmann::json::array({
                  {
                      { "code", "native_resource_probe_exception" },
                      { "resource_id", "" },
                      { "message", ex.what() },
                  },
              }) },
        };
        try {
            WriteJsonFile(args.OutputPath, output);
        } catch (...) {
        }
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
