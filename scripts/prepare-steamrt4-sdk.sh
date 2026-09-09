#!/usr/bin/env bash
set -euo pipefail

# Publisher tool. Keep this SDK out of the player package and outside the repo.
if [[ $# != 2 ]]; then
  printf 'Usage: bash %s SDK_DIRECTORY WORK_DIRECTORY\n' "$0" >&2
  exit 2
fi
sdk="$(realpath -m "$1")"
work="$(realpath -m "$2")"
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
version=4.0.20260805.254769
expected=d105e579c1261755714b403bafd842e9acb3d79d5a13df869693818a49b72379
if [[ "$sdk" == / || "$work" == / || "$sdk/" == "$work/"* ||
      "$work/" == "$sdk/"* || "$root/" == "$sdk/"* ||
      "$sdk/" == "$root/"* || "$root/" == "$work/"* || "$work/" == "$root/"* ]]; then
  printf 'SDK, work and repository must be separate directories.\n' >&2
  exit 2
fi
stamp="$sdk/.triaevum-steamrt4-sdk"
recipe="$(sha256sum "$root/scripts/steamrt4-sdk-dependencies.sh" | cut -d ' ' -f 1)"
identity="$expected:$recipe"
if [[ -f "$stamp" && "$(<"$stamp")" == "$identity" ]]; then
  printf 'SDK already prepared: %s\n' "$sdk"
  exit 0
fi
if [[ -e "$sdk" ]]; then
  printf 'Refusing to overwrite an existing or incomplete SDK: %s\n' "$sdk" >&2
  exit 2
fi
command -v bwrap >/dev/null
mkdir -p "$work" "$(dirname -- "$sdk")"
archive="$work/sdk-$version.tar.gz"
if [[ ! -f "$archive" ]]; then
  curl --fail --location --retry 2 --continue-at - --output "$archive.part" \
    "https://repo.steampowered.com/steamrt4/images/$version/com.valvesoftware.SteamRuntime.Sdk-amd64,i386-steamrt4-sysroot.tar.gz"
  printf '%s  %s\n' "$expected" "$archive.part" | sha256sum --check
  mv -- "$archive.part" "$archive"
fi
printf '%s  %s\n' "$expected" "$archive" | sha256sum --check
mkdir "$sdk"
tar --extract --gzip --file "$archive" --directory "$sdk" --no-same-owner
staging="$(mktemp -d "$work/sdk-dependencies.XXXXXX")"
bash "$root/scripts/run-in-steamrt4.sh" "$sdk" "$work" \
  bash /home/src/scripts/steamrt4-sdk-dependencies.sh "/home/work/$(basename -- "$staging")"
cp -a "$staging/root/usr/." "$sdk/usr/"
cp "$staging/SHA256SUMS" "$sdk/.triaevum-dependency-sha256sums"
printf '%s\n' "$identity" > "$stamp"
printf 'Prepared Steam Runtime %s SDK: %s\n' "$version" "$sdk"
