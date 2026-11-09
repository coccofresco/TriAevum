#pragma once
// MM3D's shader-preparation facts, defined once for everything that has to
// agree on them: the title session (mm3d_title_session.cpp) tells the renderer
// which pipeline cache files are MM3D's and which startup programs its draws
// link, and Forge's device pipeline helper (tools/renderer/pipeline_prepare)
// prepares exactly those programs into exactly that file. A helper that
// writes another file, or other programs, prepares nothing the runtime reads.
//
// The namespace is also the title fact "pipeline_cache_namespace" of
// titles/mm3d/product.json, which the MM3D Forge passes to the helper
// (forge-catalog title.rs); mm3d_builtin_shader_packs_tests.cpp and the
// forge-catalog tests bind the three spellings together.
#include <string_view>

namespace Oot3dNativeGame {
// Names MM3D's pipeline cache files in the renderer cache directory
// ("nri_pipeline_cache.mm3d.bin"), apart from OoT3D's original names.
inline constexpr std::string_view kMm3dPipelineCacheNamespace = "mm3d";
// MM3D's draws take the native fragment lighting operand and lit shadow
// receivers, so its startup preparation covers the whole uber fragment family
// (Fast::Extension::StartupUberFragmentArtifacts(true, true)), not the legacy
// subset of a title without them. When the title lists its startup uber
// modules (mm3d_startup_uber_modules.h), only those of the family are
// prepared at startup; the rest stay lazy.
inline constexpr bool kMm3dNativeFragmentLighting = true;
} // namespace Oot3dNativeGame
