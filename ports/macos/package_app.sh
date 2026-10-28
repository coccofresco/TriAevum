#!/bin/bash
# Copyright TriAevum contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lays out an ad-hoc signed TriAevum.app from a ports/macos/runtime build.
# Allowlist only: the runtime, SDL2, the Khronos Vulkan loader, MoltenVK and
# its relative ICD manifest. ROM-derived data, title data and caches never
# enter the bundle; the precompiled title module is added only when given
# explicitly (developer runs), since the release ships it as its own
# catalogued download.
#
# usage: package_app.sh <runtime-build-dir> <vulkan-prefix> <output-dir> [title.dylib]
set -euo pipefail
build=$1 vk=$2 out=$3 title=${4:-}
app="$out/TriAevum.app"
contents="$app/Contents"
rm -rf "$app"
mkdir -p "$contents/MacOS" "$contents/Frameworks" "$contents/Resources/vulkan/icd.d"

cp "$build/product/TriAevum" "$contents/MacOS/TriAevum"
cp "$build/_deps/sdl2-build/libSDL2-2.0.0.dylib" "$contents/Frameworks/"
loader=$(basename "$(readlink -f "$vk/lib/libvulkan.1.dylib")")
cp "$vk/lib/$loader" "$contents/Frameworks/"
ln -s "$loader" "$contents/Frameworks/libvulkan.1.dylib"
cp "$vk/lib/libMoltenVK.dylib" "$contents/Frameworks/"
cat > "$contents/Resources/vulkan/icd.d/MoltenVK_icd.json" <<'JSON'
{
  "file_format_version": "1.0.0",
  "ICD": {
    "library_path": "../../../Frameworks/libMoltenVK.dylib",
    "api_version": "1.4.0",
    "is_portability_driver": true
  }
}
JSON
if [ -n "$title" ]; then
  cp "$title" "$contents/MacOS/triaevum_title_aot.dylib"
fi

# Build-tree rpaths must not reach users: only the bundle's Frameworks folder.
exe="$contents/MacOS/TriAevum"
for rpath in $(otool -l "$exe" | awk '/LC_RPATH/{getline; getline; print $2}'); do
  install_name_tool -delete_rpath "$rpath" "$exe"
done
install_name_tool -add_rpath "@executable_path/../Frameworks" "$exe"
install_name_tool -id "@rpath/libMoltenVK.dylib" "$contents/Frameworks/libMoltenVK.dylib"

minos=$(vtool -show-build "$exe" | awk '/minos/{print $2; exit}')
cat > "$contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>TriAevum</string>
  <key>CFBundleIdentifier</key><string>io.github.coccofresco.TriAevum</string>
  <key>CFBundleName</key><string>TriAevum</string>
  <key>CFBundleDisplayName</key><string>TriAevum</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${TRIAEVUM_VERSION:-0.8.0}</string>
  <key>CFBundleVersion</key><string>${TRIAEVUM_VERSION:-0.8.0}</string>
  <key>LSMinimumSystemVersion</key><string>${minos}</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
  <key>GCSupportsControllerUserInteraction</key><true/>
</dict>
</plist>
PLIST

# Every Mach-O must not require a newer macOS than the bundle declares.
while IFS= read -r -d '' file; do
  if file "$file" | grep -q "Mach-O"; then
    file_minos=$(vtool -show-build "$file" | awk '/minos/{print $2; exit}')
    if [ "$(printf '%s\n%s\n' "$file_minos" "$minos" | sort -V | tail -1)" != "$minos" ]; then
      echo "error: $file requires macOS $file_minos, above the bundle's $minos" >&2
      exit 1
    fi
  fi
done < <(find "$contents" -type f -print0)

# Inside-out ad-hoc signing, then a strict verification of the sealed bundle.
find "$contents/Frameworks" "$contents/MacOS" -type f -name '*.dylib' -exec codesign --force --sign - {} \;
codesign --force --sign - "$exe"
codesign --force --sign - "$app"
codesign --verify --deep --strict --verbose=2 "$app"
(cd "$out" && rm -f TriAevum-macos-arm64.zip &&
  ditto -c -k --norsrc --noextattr --keepParent TriAevum.app TriAevum-macos-arm64.zip)
echo "packaged $app (minimum macOS $minos)"
