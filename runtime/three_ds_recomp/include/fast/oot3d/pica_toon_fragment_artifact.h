#pragma once
#include "fast/renderer3ds/pica_fragment_artifact.h"

namespace Fast::Oot3d {
// Keep generated program data private to the toon module, out of backend TUs.
std::span<const Renderer3ds::PicaFragmentArtifact> ToonFragmentArtifacts();
}
