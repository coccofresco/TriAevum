#!/usr/bin/env bash
set -euo pipefail

# Run through run-in-steamrt4.sh. Package hashes come from the pinned SDK's
# authenticated APT indexes; never update those indexes during this operation.
output="${1:?Specify a dependency staging directory}"
output="$(realpath -m "$output")"
if [[ "$output" != /home/work/* ]]; then
  printf 'Dependency staging must be inside /home/work.\n' >&2
  exit 2
fi
mkdir -p "$output/root"
archives="$(mktemp -d "$output/archives.XXXXXX")"
cd "$archives"
packages=(
  libfmt-dev=10.1.1+ds1-4
  libshaderc1=2025.2-1
  libshaderc-dev=2025.2-1
  libspdlog-dev=1:1.15.2+ds-2
  libtinyxml2-11=11.0.0+dfsg-1+b1
  libtinyxml2-dev=11.0.0+dfsg-1+b1
  libzip5=1.11.3-2
  libzip-dev=1.11.3-2
  zipcmp=1.11.3-2
  zipmerge=1.11.3-2
  ziptool=1.11.3-2
  nlohmann-json3-dev=3.11.3-2.1
)
# Bubblewrap maps only the invoking user; the separate _apt UID is unavailable.
apt-get -o APT::Sandbox::User=root download "${packages[@]}"
sha256sum -- *.deb > "$output/SHA256SUMS"
for package in *.deb; do
  dpkg-deb --extract "$package" "$output/root"
done
