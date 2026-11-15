# Third-party software

| What | Where | Licence |
|---|---|---|
| AMD FidelityFX SDK 2.4.0, the FSR API headers (`api/include/`, `upscalers/include/ffx_upscale.h`) | compiled in, from `third_party/ffx/` (tools/fetch_sdk.sh) | MIT |
| AMD FidelityFX SDK 2.4.0, `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` (signed binaries) | shipped beside the provider in the add-on's package, unmodified | the SDK's `docs/license.md`, shipped as `FidelityFX-LICENSE.md` |
| Vulkan-Headers (vulkan-sdk-1.4.341.0) | compiled in, fetched by CMake | Apache-2.0 or MIT |
| Hailstone's `sdk/hailstone_upscaler.h` | compiled in, from a client checkout | MIT |

The SDK's licence file opens with AMD's binary licence (redistribution in binary form, unmodified, with its notices;
no reverse engineering) and then lists the signed Vulkan DLLs among the files under the MIT licence. Either way the
DLLs are shipped unmodified with the licence file beside them. Whether AMD means the signed DLLs to be MIT is still
to be confirmed with AMD. "AMD" and "FSR" are AMD's trademarks, used here only to say what the add-on runs.
