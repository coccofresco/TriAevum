#include "oot3d_decomp_profile.h"
#include "oot3d_source_game_session.h"

#include <iostream>

#ifdef OOT3D_SOURCE_SNAPSHOT_AVAILABLE
#include "oot3d_source_snapshot_profile.h"
#endif

int main(int argc, char** argv) {
    using namespace Oot3dSourceRuntime;

    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    std::cout << "OOT3D source-native runtime bootstrap\n"
              << "profile: " << kDecompProfile.ProfileId << '\n'
              << "decomp revision: " << kDecompProfile.Revision << '\n'
              << "canonical sources: " << kDecompProfile.CanonicalSourceCount
              << '\n';

#ifdef OOT3D_SOURCE_SNAPSHOT_AVAILABLE
    if (Oot3dSourceSnapshotGenerated::kRevision != kDecompProfile.Revision ||
        Oot3dSourceSnapshotGenerated::kCanonicalSourceCount !=
            kDecompProfile.CanonicalSourceCount) {
        std::cerr << "compiled source snapshot does not match the consumer profile\n";
        return 1;
    }
    std::cout << "snapshot verified: "
              << Oot3dSourceSnapshotGenerated::kCanonicalSourceCount
              << " canonical sources\n";
#else
    std::cout << "source snapshot is not bound to this bootstrap build\n";
#endif
#ifdef OOT3D_SOURCE_EXECUTABLE_CLOSURE
    std::cout << "native nnMain closure is linked; A32 execution is disabled.\n";
#else
    std::cout << "native nnMain closure is not linked into this target.\n";
#endif

    if (argc == 1) {
        return 0;
    }
    std::optional<SourceGameLaunchOptions> options;
    try {
        options = ParseSourceGameLaunchOptions(argc, argv);
    } catch (const std::exception&) {
        options = std::nullopt;
    }
    if (!options.has_value()) {
        PrintSourceGameUsage();
        return 2;
    }
    return RunSourceGameSession(std::move(*options));
}
