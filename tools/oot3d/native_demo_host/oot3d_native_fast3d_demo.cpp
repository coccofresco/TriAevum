#include <filesystem>
#include <iostream>
#include <stdexcept>

#include "ship/Context.h"

#include "oot3d_demo_host_cli.h"
#include "oot3d_demo_host_self_test.h"
#include "oot3d_demo_host_title_trace.h"
#include "oot3d_demo_host_types.h"
#include "oot3d_demo_host_window_demo.h"

int main(int argc, char** argv) {
    try {
        Args args;
        if (!ParseArgs(argc, argv, args)) {
            PrintUsage();
            return 2;
        }
        if (!std::filesystem::exists(args.ManifestPath)) {
            throw std::runtime_error("OOT3D demo manifest not found: " + args.ManifestPath.string());
        }
        if (!std::filesystem::is_directory(args.ResourceRoot)) {
            throw std::runtime_error("Fast3D resource root is not a directory: " + args.ResourceRoot.string());
        }

        if (args.TitleIntroTrace) {
            RunTitleIntroTrace(args);
        } else if (args.SelfTest) {
            RunSelfTest(args);
        } else {
            RunWindowDemo(args);
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "oot3d_native_fast3d_demo: " << ex.what() << "\n";
        Ship::Context::DestroyInstance();
        return 1;
    }
}
