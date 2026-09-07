#include "oot3d_source_gameplay_profile.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        using namespace Oot3dNativeGame;

        SourceGameplayOwnerRequest request;
        request.TypedGameStateUpdate = true;
        auto selection = ResolveSourceGameplayOwnerSelection(request);
        Expect(
            selection.EnabledOwnerCount() == 1U &&
                selection.GameStateUpdate && !selection.Complete(),
            "typed GameState_Update was not represented independently");

        request.PlayerUpdate = true;
        request.PlayerUpdateCommon = true;
        selection = ResolveSourceGameplayOwnerSelection(request);
        Expect(
            selection.EnabledOwnerCount() == 3U &&
                selection.PlayerUpdate &&
                selection.PlayerUpdateCommon &&
                !selection.ActorUpdateAll,
            "individual source-owner selection was not preserved");

        request = {};
        request.EnableProfile = true;
        request.TypedGameStateUpdate = true;
        selection = ResolveSourceGameplayOwnerSelection(request);
        Expect(
            selection.ProfileRequested && selection.Complete() &&
                selection.EnabledOwnerCount() ==
                    kSourceGameplayOwnerCount &&
                selection.ActorInitContext &&
                selection.ActorUpdateAll &&
                selection.CutsceneUpdateFrame &&
                selection.CutsceneProcessCommands &&
                selection.CameraUpdate &&
                selection.PlayerUpdate &&
                selection.PlayerUpdateCommon,
            "composed source-gameplay profile did not select all owners");

        request.TypedGameStateUpdate = false;
        selection = ResolveSourceGameplayOwnerSelection(request);
        Expect(
            selection.EnabledOwnerCount() ==
                    kSourceGameplayOwnerCount - 1U &&
                !selection.Complete(),
            "profile incorrectly claimed completeness without typed "
            "GameState_Update");

        std::cout << "source_gameplay_profile_tests: ok\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr
            << "source_gameplay_profile_tests: "
            << exception.what() << '\n';
        return 1;
    }
}
