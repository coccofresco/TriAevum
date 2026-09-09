#!/usr/bin/env bash
# Stage a tinyxml2 CMake package under build-linux-deps for distributions
# whose tinyxml2 package ships no tinyxml2-config.cmake (e.g. Arch).
set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
dependency_root=${repo_root}/build-linux-deps
version=${TINYXML2_VERSION:-11.0.0}
prefix=${dependency_root}/tinyxml2/usr

if [[ -f ${prefix}/lib/cmake/tinyxml2/tinyxml2-config.cmake ]]; then
    printf 'tinyxml2 already staged: %s\n' "${prefix}"
    exit 0
fi
mkdir -p "${dependency_root}"
cd "${dependency_root}"
curl -fsSL "https://github.com/leethomason/tinyxml2/archive/refs/tags/${version}.tar.gz" | tar xz
cmake -S "tinyxml2-${version}" -B tinyxml2-build \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX="${prefix}"
cmake --build tinyxml2-build --parallel "$(nproc)"
cmake --install tinyxml2-build
printf 'tinyxml2 staged: %s\n' "${prefix}"
