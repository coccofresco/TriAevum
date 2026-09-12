#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "${script_dir}/../.." && pwd)
build_dir=${1:-${repo_root}/build-macos}
output=${2:-${build_dir}/TriAevum.app}
if [[ $(uname -s) != Darwin || $(uname -m) != arm64 ]]; then
    printf 'This development application build requires an Apple Silicon Mac.\n' >&2
    exit 2
fi
if [[ -e ${output} ]]; then
    printf 'Output already exists; supply a new .app path: %s\n' "${output}" >&2
    exit 2
fi
cd -- "${repo_root}"
mkdir -p "${build_dir}"
build_dir=$(cd -- "${build_dir}" && pwd)
bootstrap_python=${TRIAEVUM_PYTHON:-$(brew --prefix)/bin/python3}
"${bootstrap_python}" -c 'import sys; assert sys.version_info >= (3, 10), "Python 3.10 or newer is required"'
"${bootstrap_python}" -m venv "${build_dir}/venv"
python_bin=${build_dir}/venv/bin/python
"${python_bin}" -m pip install capstone pycryptodome zstandard certifi pyinstaller
"${python_bin}" -m tools.triaevum_release.prepare_macos_inputs --build "${build_dir}"
"${script_dir}/build.sh" runtime "${build_dir}"
cmake -S tools/triaevum_release/macos_title -B "${build_dir}/title-alpha2" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-$(sw_vers -productVersion)}" \
    -DTRIAEVUM_TRANSLATED_TITLE_DIR="${build_dir}/translated-title-alpha2"
cmake --build "${build_dir}/title-alpha2" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-6}"
"${python_bin}" -m PyInstaller --noconfirm --onedir --name TriAevumForge \
    --distpath "${build_dir}/forge-dist" --workpath "${build_dir}/forge-work" \
    --specpath "${build_dir}" --paths "${repo_root}" \
    --paths "${repo_root}/tools/oot3d/native_a32_runtime/upstream/src" \
    --paths "${repo_root}/tools/oot3d/oot3d_asset_tool/src" --collect-submodules oot3d_pack \
    --add-data "${repo_root}/config/topscreen_ui.example.json:config" \
    tools/triaevum_release/macos_forge.py
"${python_bin}" -m tools.triaevum_release.package_macos --build "${build_dir}" \
    --title "${build_dir}/title-alpha2/triaevum_title_aot.dylib" --output "${output}"
