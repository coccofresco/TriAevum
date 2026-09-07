#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "oot3d_demo_host_context.h"
#include "oot3d_demo_host_window_demo.h"
#include "oot3d_native_game_bootstrap.h"
#include "oot3d_native_player_controller.h"

int main(int argc, char** argv) {
    try {
        Oot3dNativeGameLaunch launch;
        if (!ParseOot3dNativeGameArgs(argc, argv, launch)) {
            PrintOot3dNativeGameUsage();
            return 2;
        }
        if (!launch.A32ProcessManifestPath.empty()) {
            throw std::runtime_error(
                "the legacy sandbox does not execute A32 process manifests; "
                "use oot3d_native_game");
        }

        const auto bootstrap = ResolveOot3dNativeGameBootstrap(launch);
        if (!launch.PlayerCollisionActionConfigAvailable ||
            !launch.PlayerActionConfigAvailable) {
            throw std::runtime_error(
                "native player collision/action contract was not resolved");
        }
        launch.Host.PlayerController =
            std::make_shared<Oot3dNativeGame::PlayerController>(
                launch.PlayerCollisionActionConfig, launch.PlayerActionConfig);
        launch.Host.PlayerControllerId = "oot3d_native_game_player";
        launch.Host.PlayerControllerStatus =
            "native_scene_entrance_skelanime_locomotion_auto_jump_airborne_"
            "landing_ledge_surface_climb_controller";

        const auto bootstrapJson =
            Oot3dNativeGameBootstrapToJson(launch, bootstrap);
        std::cout << bootstrapJson.dump() << '\n';
        if (launch.ValidateOnly) {
            WriteOot3dNativeGameBootstrapJson(launch.Host.OutputPath,
                                              bootstrapJson);
            return 0;
        }

        RunWindowDemo(launch.Host);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "oot3d_native_game_legacy_sandbox: " << ex.what() << '\n';
        DestroyContextForDemo();
        return 1;
    }
}
