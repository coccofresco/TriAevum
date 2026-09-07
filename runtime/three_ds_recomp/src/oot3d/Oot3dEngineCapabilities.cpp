#include "three_ds_recomp/oot3d/Oot3dEngineCapabilities.h"

#include <algorithm>

namespace ThreeDsRecomp::Oot3d {

const std::vector<std::string>& EngineCapabilities() {
    static const std::vector<std::string> capabilities = {
        "native_cmb_geometry",
        "native_cmb_skinning",
        "native_csab_animation",
        "native_ctxb_texture",
        "native_kankyo_environment",
        "native_pica_material",
        "native_pica_fog",
        "native_pica_scene_lighting",
        "native_qdb_command_stream",
        "native_qdb_source",
        "native_zsi_scene_commands",
        "native_zsi_source_archive",
        "native_zsi_collision",
    };
    return capabilities;
}

bool HasEngineCapability(std::string_view capability) {
    const auto& capabilities = EngineCapabilities();
    return std::find(capabilities.begin(), capabilities.end(), capability) != capabilities.end();
}

} // namespace ThreeDsRecomp::Oot3d
