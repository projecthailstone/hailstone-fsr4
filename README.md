# AMD FSR 4 for Hailstone

An upscaler add-on for [Hailstone](https://github.com/projecthailstone/hailstone-client): AMD FSR 4, AMD's
machine-learned upscaler, in place of Hailstone's own upscalers. It is built against Hailstone's
`sdk/hailstone_upscaler.h` and ships AMD's signed FSR runtime (`amd_fidelityfx_loader_vk.dll`,
`amd_fidelityfx_upscaler_vk.dll`) beside it.

**Windows x64 only**, because AMD ships the runtime for Windows alone, and **AMD Radeon RX 7000 and 9000** (and the
Ryzen APUs AMD lists) only, because FSR 4 runs nowhere else. On any other graphics card Hailstone uses its own AMD
FSR 3.1 instead, and its log says why.

## Install

1. On Hailstone's login screen, open **Add-ons**, paste `https://github.com/projecthailstone/hailstone-fsr4` and
   press Look, then Install.
2. In the game, **Options > Graphics > Upscaler**: pick **AMD FSR 4**.
3. Restart the game. Upscale Quality, Render Scale and Upscale Sharpening work as they do with AMD FSR 3.1.

## Build

```sh
tools/fetch_sdk.sh                                  # what it needs of the FidelityFX SDK into third_party/ffx/
cmake -S . -B build-windows -DHAILSTONE_CLIENT_DIR=../hailstone-client   # with MSVC, or MSVC under Wine
cmake --build build-windows                         # build-windows/dist/: what the add-on's package holds
```

For a test, copy `build-windows/dist/` to `upscalers/fsr4/` in a Hailstone folder and start it with
`--upscaler fsr4` (the zone viewer) or `display.upscaler = fsr4` in `hailstone.ini`.

`tools/release.sh <version> [--publish]` builds with MSVC under Wine (the client's Docker image), packs and signs
the add-on, and uploads the release.

## Licence

This add-on's code is MIT (`LICENSE`). AMD's runtime is under the FidelityFX SDK's licence, shipped with it as
`FidelityFX-LICENSE.md`; see `THIRD_PARTY.md`. Hailstone's licence (GPL-2.0-or-later) has an exception for add-ons
built against its SDK, which covers this one.
