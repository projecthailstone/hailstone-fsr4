#!/usr/bin/env bash
# Downloads what this add-on needs of AMD's FidelityFX SDK 2.4.0 into third_party/ffx/ (not committed): the FSR API's
# MIT headers, AMD's signed Vulkan loader and upscaler DLLs (redistributed in the add-on's package, not kept in this
# repository) and the SDK's licence file. Each file is checked against its SHA-256.
set -euo pipefail
TAG=v2.4.0
BASE=https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/$TAG/Kits/FidelityFX
FILES=(
  "38a56a01fb3565c0757d9f5c1e4e69de4b65e5f2e5769b323b200dc1f142bab9 signedbin/amd_fidelityfx_loader_vk.dll"
  "4a7e895af6e0b615d40a84d59eb8c2b1054ad91cdd0c739567940270d51bdab7 signedbin/amd_fidelityfx_upscaler_vk.dll"
  "4d9dcf243ef5efbf03340a02f16b51a22053243a22e8a9a0b3ce91c26982b4bf api/include/ffx_api.h"
  "53f07c113a9ba493f297656e4481b6539aed0afc9f3cc8da682aa477d270c46c api/include/ffx_api_types.h"
  "09ac1855bc4c2f711243d6982909385072ca3f8d30db44d1a82ee5bd637832ad api/include/vk/ffx_api_vk.h"
  "13091e7fe5fd9c82b1fb7c044b9f08f664d9653f599d2ea500738709236fca4d upscalers/include/ffx_upscale.h"
  "32357259b965d6a8001e9e508ba60eda6034027c5923a7d226ffbaff813c2bfb docs/license.md"
)
here=$(cd "$(dirname "$0")/.." && pwd)
dest=$here/third_party/ffx
[ -f "$dest/.version-$TAG" ] && exit 0
rm -rf "$dest"
for entry in "${FILES[@]}"; do
  sum=${entry%% *}
  file=${entry#* }
  mkdir -p "$dest/$(dirname "$file")"
  curl -fsSL -o "$dest/$file" "$BASE/$file"
  echo "$sum  $dest/$file" | sha256sum -c --quiet
done
touch "$dest/.version-$TAG"
echo "FidelityFX SDK $TAG files in $dest"
