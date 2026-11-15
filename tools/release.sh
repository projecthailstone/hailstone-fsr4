#!/usr/bin/env bash
# Makes a release of the FSR 4 upscaler add-on (the client's docs/addons.md "Upscalers"): builds it for Windows x64
# with MSVC under Wine, packs build-windows/dist/ (the client's tools/make_addon.py), signs hailstone-addon.txt, and
# with --publish uploads everything as a GitHub release of this repository, the one players paste on the Add-ons
# screen.
#
#   tools/release.sh 1.0.0              build and pack into release/v1.0.0/, nothing uploaded
#   tools/release.sh 1.0.0 --publish    the same, then gh release create v1.0.0
#
# Needs:
#   a hailstone-client checkout ($HAILSTONE_CLIENT, default ../hailstone-client) with sdk/hailstone_upscaler.h,
#     tools/make_addon.py and a built hailstone_sign ($HAILSTONE_SIGN, default $HAILSTONE_CLIENT/build/hailstone_sign);
#   the add-on key's secret half ($ADDON_KEY, default ~/.config/hailstone-release/addon-key.secret: keep it off
#     GitHub, and keep using the same one, since a player's client refuses an update signed with another key);
#   Docker with the client's hailstone-msvc-build:24.04 image and msvc-wine in ~/opt/msvc (the client's docs/ci.md
#     "MSVC under Wine"). $JOBS caps the parallel jobs (default 20).
set -euo pipefail

NAME=fsr4
RELEASE_REPO=projecthailstone/hailstone-fsr4
DESCRIPTION="AMD FSR 4, the machine-learned upscaler, for Hailstone (Windows; AMD Radeon RX 7000 and 9000)."
AUTHOR="Project Hailstone"
MIN_CLIENT=0.7.2-beta

version=${1:?usage: tools/release.sh <version> [--publish]}
version=${version#v}
publish=${2:-}
here=$(cd "$(dirname "$0")/.." && pwd)
client=$(cd "${HAILSTONE_CLIENT:-$here/../hailstone-client}" && pwd)
sign=${HAILSTONE_SIGN:-$client/build/hailstone_sign}
key=${ADDON_KEY:-$HOME/.config/hailstone-release/addon-key.secret}
jobs=${JOBS:-20}
out=$here/release/v$version
tag=v$version

[ -f "$client/sdk/hailstone_upscaler.h" ] || { echo "no sdk/hailstone_upscaler.h at $client (set HAILSTONE_CLIENT)" >&2; exit 1; }
[ -x "$sign" ] || { echo "no hailstone_sign at $sign: build the client, or set HAILSTONE_SIGN" >&2; exit 1; }
[ -f "$key" ] || { echo "no add-on key at $key: '$sign keygen $key' once, or set ADDON_KEY" >&2; exit 1; }
grep -q "project(hailstone-fsr4 VERSION ${version%%-*} " "$here/CMakeLists.txt" ||
  { echo "CMakeLists.txt's project version is not ${version%%-*}" >&2; exit 1; }
if [ -n "$(git -C "$here" status --porcelain)" ]; then
  echo "the working tree has changes: commit them first, so the release matches a commit" >&2
  exit 1
fi

"$here/tools/fetch_sdk.sh"
rm -rf "$here/build-windows" "$out"

# windows-x64, MSVC under Wine (the client's tools/docker/msvc_build.sh), inside a timeout: a Wine program that cannot
# start hangs the build rather than failing it.
docker run --rm --cpus "$jobs" -u "$(id -u):$(id -g)" -e HOME="$HOME/opt/msvc-home" -e MSVC_DIR="$HOME/opt/msvc" \
  -e JOBS="$jobs" -v "${HOME}/opt:${HOME}/opt" -v "${here}:${here}" -v "${client}:${client}:ro" -w "$here" \
  hailstone-msvc-build:24.04 timeout 1h "$client/tools/docker/msvc_build.sh" . build-windows \
  -DHAILSTONE_CLIENT_DIR="$client"
for f in fsr4.dll amd_fidelityfx_loader_vk.dll amd_fidelityfx_upscaler_vk.dll FidelityFX-LICENSE.md; do
  [ -f "$here/build-windows/dist/$f" ] || { echo "the Windows build has no dist/$f" >&2; exit 1; }
done

python3 "$client/tools/make_addon.py" --name "$NAME" --kind upscaler --version "$version" \
    --platform "windows-x64=$here/build-windows/dist" \
    --author "$AUTHOR" --description "$DESCRIPTION" --min-client "$MIN_CLIENT" \
    --notes "https://github.com/$RELEASE_REPO/releases/tag/$tag" --out "$out"
"$sign" sign "$key" "$out/hailstone-addon.txt"
"$sign" verify "$out/hailstone-addon.txt"
echo "release in $out"

if [ "$publish" = "--publish" ]; then
  gh release create "$tag" -R "$RELEASE_REPO" --title "FSR 4 upscaler $version" \
    --notes "AMD FSR 4 for Hailstone $version. Install it from Add-ons on the login screen with this repository's link." \
    "$out"/*
fi
