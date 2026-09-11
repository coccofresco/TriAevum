#!/usr/bin/env bash
set -euo pipefail

# Invoke through run-in-steamrt4.sh: use its authenticated, pinned APT indexes
# without updating or installing anything into the SDK or the host system.
output="$(realpath -m "${1:?Specify a Forge workspace inside /home/work}")"
if [[ "$output" != /home/work/* ]]; then
  printf 'Forge staging must be inside /home/work.\n' >&2
  exit 2
fi
mkdir -p "$output/archives" "$output/deps"
packages=(
  python3-pyinstaller=6.13.0+ds-2
  python3-altgraph=0.17.4+ds-2
  python3-tk=3.13.5-1
  python3.13-tk=3.13.5-2+deb13u4
  blt=2.5.3+dfsg-8
  tk8.6-blt2.5=2.5.3+dfsg-8
  libtk8.6=8.6.16-1
  libtcl8.6=8.6.16+dfsg-1
  python3-capstone=5.0.7-1~deb13u1
  libcapstone5=5.0.7-1~deb13u1
  python3-msgpack=1.0.3-3+b4
)
cd "$output/archives"
apt-get -o APT::Sandbox::User=root download "${packages[@]}"
sha256sum -- *.deb > "$output/SHA256SUMS"
for package in *.deb; do
  dpkg-deb --extract "$package" "$output/deps"
done
export PYTHONPATH="$output/deps/usr/lib/python3/dist-packages:$output/deps/usr/lib/python3.13:$output/deps/usr/lib/python3.13/lib-dynload:/home/src"
export LD_LIBRARY_PATH="$output/deps/usr/lib/x86_64-linux-gnu"
export TCL_LIBRARY="$output/deps/usr/share/tcltk/tcl8.6"
export TK_LIBRARY="$output/deps/usr/share/tcltk/tk8.6"
python3 -c 'import tkinter, capstone, msgpack, PyInstaller; print("Forge SDK imports ready")'
python3 /home/src/tools/triaevum_release/build_forge_binary.py \
  --output "$output/dist" --work "$output/build" \
  --nlohmann-include /home/work/sdk-dependencies/root/usr/include
