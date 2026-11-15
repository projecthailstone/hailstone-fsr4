// SPDX-License-Identifier: MIT
// AMD FSR 4 (FSR Upscaling, the machine-learned one) as a Hailstone upscaler provider (the client's
// sdk/hailstone_upscaler.h and docs/plugins.md "Upscaler providers").
//
// The runtime is AMD's signed binary distribution (the FidelityFX SDK 2.4.0's signedbin/): amd_fidelityfx_loader_vk.dll,
// which this loads by its full path, finds amd_fidelityfx_upscaler_vk.dll in its own folder (the SDK's
// docs/getting-started/ffx-api.md "Effect DLL lookup"). Only the SDK's MIT headers are compiled in.
//
// The upscaler DLL holds FSR 4 and the older FSR 3.1 and 2. Hailstone has FSR 3.1 of its own, so this provider runs
// only where FSR 4 does (an AMD Radeon RX 7000 or 9000, or a supported Ryzen APU): on any other GPU it says no at
// create, and the client uses its own FSR 3.1.
//
// What the client gives, as FSR wants it (the SDK's docs/techniques/super-resolution-ml.md and -upscaler.md):
//   colour   linear HDR before the exposure: FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE with FSR's own auto exposure, as
//            the client's own FSR 3.1 runs
//   motion   UV at the render size, previous minus current: motionVectorScale is the render size
//   depth    reverse Z with a finite far plane: FFX_UPSCALE_ENABLE_DEPTH_INVERTED
//   reactive R8 0..1: FSR's reactive mask, when the client draws one
//   jitter   render pixels in FSR's convention, as it is
//   inputs in SHADER_READ_ONLY_OPTIMAL (FSR's "compute read" state), the output in GENERAL (unordered access)
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <ffx_api.h>
#include <ffx_api_types.h>
#include <ffx_upscale.h>
#include <vk/ffx_api_vk.h>

#include <cstring>
#include <string>
#include <vector>

#include "hailstone_upscaler.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

const HailstoneUpscalerHost* g_host = nullptr;

void say(int level, const std::string& line) {
    if (g_host && g_host->log) g_host->log(g_host->host, level, line.c_str());
}

bool ok(ffxReturnCode_t r, const char* what) {
    if (r == FFX_API_RETURN_OK) return true;
    say(HAILSTONE_UPSCALER_LOG_ERROR, std::string(what) + " failed (FFX return " + std::to_string(r) + ")");
    return false;
}

#if defined(_WIN32)
constexpr const char* kLoader = "amd_fidelityfx_loader_vk.dll";
#else
constexpr const char* kLoader = "libamd_fidelityfx_loader_vk.so"; // AMD ships none yet
#endif

void* g_library = nullptr;
PfnFfxCreateContext g_create = nullptr;
PfnFfxDestroyContext g_destroy = nullptr;
PfnFfxQuery g_query = nullptr;
PfnFfxDispatch g_dispatch = nullptr;

void* load_runtime(const std::string& path) {
#if defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wide(n > 0 ? static_cast<std::size_t>(n) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), n);
    return LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* symbol(const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(g_library), name));
#else
    return dlsym(g_library, name);
#endif
}

bool load_api() {
    g_library = load_runtime(std::string(g_host->folder) + "/" + kLoader);
    if (!g_library) {
        say(HAILSTONE_UPSCALER_LOG_ERROR, std::string(kLoader) + " did not load from " + g_host->folder);
        return false;
    }
    g_create = reinterpret_cast<PfnFfxCreateContext>(symbol("ffxCreateContext"));
    g_destroy = reinterpret_cast<PfnFfxDestroyContext>(symbol("ffxDestroyContext"));
    g_query = reinterpret_cast<PfnFfxQuery>(symbol("ffxQuery"));
    g_dispatch = reinterpret_cast<PfnFfxDispatch>(symbol("ffxDispatch"));
    if (!g_create || !g_destroy || !g_query || !g_dispatch) {
        say(HAILSTONE_UPSCALER_LOG_ERROR, std::string(kLoader) + " lacks the FFX API's functions");
        return false;
    }
    return true;
}

PFN_vkGetInstanceProcAddr gipa() { return reinterpret_cast<PFN_vkGetInstanceProcAddr>(g_host->get_instance_proc_addr); }

// What FSR 4 may use, where the GPU has it (AMD's own sample turns these on: the SDK's Cauldron2 device_vk.cpp): the
// FP8 model's cooperative matrices and 8-bit floats (RX 9000), the INT8 model's dot products and 8-bit integers
// (RX 7000, APUs), compute derivatives for its prepass, subgroup size control, 16-bit floats and storage.
struct Wanted {
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR coop{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR};
    VkPhysicalDeviceShaderFloat8FeaturesEXT float8{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT8_FEATURES_EXT};
    VkPhysicalDeviceComputeShaderDerivativesFeaturesKHR derivatives{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_KHR};
    std::vector<const char*> extensions;
};
Wanted g_wanted; // the structs stay valid until create, as the client's header asks

bool has_extension(const std::vector<VkExtensionProperties>& all, const char* name) {
    for (const auto& e : all)
        if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

int instance_requirements(void*, const char* const** names, uint32_t* count, uint32_t* min_api) {
    *names = nullptr;
    *count = 0;
    *min_api = VK_MAKE_API_VERSION(0, 1, 3, 0);
    return 1;
}

int device_requirements(void*, void* instance_handle, void* physical_handle, const char* const** names, uint32_t* count,
                        void* features) {
    auto instance = static_cast<VkInstance>(instance_handle);
    auto physical = static_cast<VkPhysicalDevice>(physical_handle);
    auto get_props = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa()(instance, "vkGetPhysicalDeviceProperties"));
    auto get_features2 =
        reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(gipa()(instance, "vkGetPhysicalDeviceFeatures2"));
    auto enum_ext = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        gipa()(instance, "vkEnumerateDeviceExtensionProperties"));
    if (!get_props || !get_features2 || !enum_ext) return 0;
    VkPhysicalDeviceProperties props{};
    get_props(physical, &props);
    if (props.vendorID != 0x1002) { // AMD's PCI vendor id: FSR 4 runs on no other vendor's GPU
        say(HAILSTONE_UPSCALER_LOG_INFO, std::string("FSR 4 needs an AMD GPU; this is ") + props.deviceName);
        return 0;
    }
    uint32_t n = 0;
    enum_ext(physical, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> all(n);
    enum_ext(physical, nullptr, &n, all.data());

    // What the GPU has of the extension features: asked for in one chain, then each kept only where it is there.
    g_wanted = Wanted{};
    Wanted& w = g_wanted;
    VkPhysicalDeviceVulkan12Features have12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features have13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan11Features have11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    VkPhysicalDeviceFeatures2 have{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    have.pNext = &have11;
    have11.pNext = &have12;
    have12.pNext = &have13;
    void** tail = &have13.pNext;
    const bool coop = has_extension(all, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);
    const bool float8 = has_extension(all, VK_EXT_SHADER_FLOAT8_EXTENSION_NAME);
    const bool deriv = has_extension(all, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);
    if (coop) { *tail = &w.coop; tail = &w.coop.pNext; }
    if (float8) { *tail = &w.float8; tail = &w.float8.pNext; }
    if (deriv) { *tail = &w.derivatives; tail = &w.derivatives.pNext; }
    get_features2(physical, &have);

    // The extension structs, holding only what the GPU said it has; their chain rebuilt for the client.
    void* first = nullptr;
    void** link = &first;
    auto keep = [&](void* s, void** next, const char* ext) {
        *link = s;
        link = next;
        w.extensions.push_back(ext);
    };
    w.coop.pNext = w.float8.pNext = w.derivatives.pNext = nullptr;
    if (coop && w.coop.cooperativeMatrix) {
        w.coop.cooperativeMatrixRobustBufferAccess = VK_FALSE;
        keep(&w.coop, &w.coop.pNext, VK_KHR_COOPERATIVE_MATRIX_EXTENSION_NAME);
    }
    if (float8 && w.float8.shaderFloat8) keep(&w.float8, &w.float8.pNext, VK_EXT_SHADER_FLOAT8_EXTENSION_NAME);
    if (deriv && (w.derivatives.computeDerivativeGroupLinear || w.derivatives.computeDerivativeGroupQuads))
        keep(&w.derivatives, &w.derivatives.pNext, VK_KHR_COMPUTE_SHADER_DERIVATIVES_EXTENSION_NAME);

    // The core features, where the GPU has them, into the client's structs.
    auto* f2 = static_cast<VkPhysicalDeviceFeatures2*>(features);
    auto* f11 = static_cast<VkPhysicalDeviceVulkan11Features*>(f2->pNext);
    auto* f12 = static_cast<VkPhysicalDeviceVulkan12Features*>(f11->pNext);
    auto* f13 = static_cast<VkPhysicalDeviceVulkan13Features*>(f12->pNext);
    f2->features.shaderStorageImageExtendedFormats = have.features.shaderStorageImageExtendedFormats;
    f2->features.shaderStorageImageWriteWithoutFormat = have.features.shaderStorageImageWriteWithoutFormat;
    f2->features.shaderInt16 = have.features.shaderInt16;
    f11->storageBuffer16BitAccess = have11.storageBuffer16BitAccess;
    f12->shaderFloat16 = have12.shaderFloat16;
    f12->shaderInt8 = have12.shaderInt8;
    f12->storageBuffer8BitAccess = have12.storageBuffer8BitAccess;
    f12->vulkanMemoryModel = have12.vulkanMemoryModel;
    f13->subgroupSizeControl = have13.subgroupSizeControl;
    f13->computeFullSubgroups = have13.computeFullSubgroups;
    f13->shaderIntegerDotProduct = have13.shaderIntegerDotProduct;
    // Appended after the client's four structs.
    if (first) {
        VkBaseOutStructure* s = reinterpret_cast<VkBaseOutStructure*>(f13);
        while (s->pNext) s = s->pNext;
        s->pNext = static_cast<VkBaseOutStructure*>(first);
    }
    *names = w.extensions.data();
    *count = static_cast<uint32_t>(w.extensions.size());
    return 1;
}

VkDeviceContext g_device{};
ffxContext g_context = nullptr;
bool g_reactive = false;
uint64_t g_fsr4_version = 0;
std::string g_version_name;

// The newest FSR 4 provider the runtime offers on this device (its name starts with "4."), or 0.
uint64_t find_fsr4() {
    ffxQueryDescGetVersions q{};
    q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    q.device = &g_device;
    uint64_t count = 0;
    q.outputCount = &count;
    if (!ok(g_query(nullptr, &q.header), "ffxQuery (versions)") || count == 0) return 0;
    std::vector<uint64_t> ids(count);
    std::vector<const char*> names(count);
    q.versionIds = ids.data();
    q.versionNames = names.data();
    if (!ok(g_query(nullptr, &q.header), "ffxQuery (versions)")) return 0;
    std::string listed;
    uint64_t best = 0;
    for (uint64_t i = 0; i < count; ++i) {
        const std::string name = names[i] ? names[i] : "";
        listed += (listed.empty() ? "" : ", ") + name;
        if (name.rfind("4.", 0) == 0 && !best) { // the runtime lists the newest first
            best = ids[i];
            g_version_name = name; // copied: the runtime's names may be overwritten by a later query
        }
    }
    say(HAILSTONE_UPSCALER_LOG_INFO, "FSR versions on this GPU: " + listed);
    return best;
}

int create(void*, const HailstoneUpscalerDevice* d) {
    g_reactive = (d->flags & HAILSTONE_UPSCALER_DEVICE_REACTIVE) != 0;
    g_device.vkDevice = static_cast<VkDevice>(d->device);
    g_device.vkPhysicalDevice = static_cast<VkPhysicalDevice>(d->physical_device);
    g_device.vkDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(d->get_device_proc_addr);
    g_fsr4_version = find_fsr4();
    if (!g_fsr4_version) {
        say(HAILSTONE_UPSCALER_LOG_INFO, "FSR 4 does not run on this GPU or driver");
        return 0;
    }
    return 1;
}

void destroy_context() {
    if (g_context) g_destroy(&g_context, nullptr);
    g_context = nullptr;
}

void ffx_message(uint32_t type, const wchar_t* message) {
    if (!message) return;
    std::string text;
    for (const wchar_t* p = message; *p; ++p) text += *p < 0x80 ? static_cast<char>(*p) : '?';
    say(type == FFX_API_MESSAGE_TYPE_ERROR ? HAILSTONE_UPSCALER_LOG_ERROR : HAILSTONE_UPSCALER_LOG_WARN, "FSR: " + text);
}

int resize(void*, uint32_t rw, uint32_t rh, uint32_t vw, uint32_t vh) {
    destroy_context(); // the device is idle
    ffxCreateBackendVKDesc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    backend.deviceContext = g_device;
    ffxOverrideVersion version{};
    version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    version.versionId = g_fsr4_version;
    ffxCreateContextDescUpscaleVersion api{};
    api.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    api.version = FFX_UPSCALER_VERSION;
    ffxCreateContextDescUpscale desc{};
    desc.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    desc.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
    desc.maxRenderSize = {rw, rh};
    desc.maxUpscaleSize = {vw, vh};
    desc.fpMessage = ffx_message;
    desc.header.pNext = &backend.header;
    backend.header.pNext = &version.header;
    version.header.pNext = &api.header;
    if (!ok(g_create(&g_context, &desc.header, nullptr), "ffxCreateContext (upscale)")) {
        g_context = nullptr;
        return 0;
    }
    say(HAILSTONE_UPSCALER_LOG_INFO, "FSR " + g_version_name + ": " + std::to_string(rw) + "x" + std::to_string(rh) +
                                         " to " + std::to_string(vw) + "x" + std::to_string(vh));
    return 1;
}

uint32_t surface_format(uint32_t vk) {
    switch (static_cast<VkFormat>(vk)) {
    case VK_FORMAT_R16G16B16A16_SFLOAT: return FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT;
    case VK_FORMAT_R8_UNORM: return FFX_API_SURFACE_FORMAT_R8_UNORM;
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT: return FFX_API_SURFACE_FORMAT_R32_FLOAT;
    case VK_FORMAT_D24_UNORM_S8_UINT: return FFX_API_SURFACE_FORMAT_R32_UINT;
    default: return FFX_API_SURFACE_FORMAT_UNKNOWN;
    }
}

FfxApiResource resource_of(const HailstoneUpscalerImage& i, bool output) {
    FfxApiResource r{};
    if (!i.image) return r;
    r.resource = reinterpret_cast<void*>(static_cast<uintptr_t>(i.image));
    r.description.type = FFX_API_RESOURCE_TYPE_TEXTURE2D;
    r.description.format = surface_format(i.format);
    r.description.width = i.width;
    r.description.height = i.height;
    r.description.depth = 1;
    r.description.mipCount = 1;
    r.description.flags = FFX_API_RESOURCE_FLAGS_NONE;
    r.description.usage = output ? FFX_API_RESOURCE_USAGE_UAV
                          : (i.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? FFX_API_RESOURCE_USAGE_DEPTHTARGET
                                                                   : FFX_API_RESOURCE_USAGE_READ_ONLY;
    r.state = output ? FFX_API_RESOURCE_STATE_UNORDERED_ACCESS : FFX_API_RESOURCE_STATE_COMPUTE_READ;
    return r;
}

int evaluate(void*, const HailstoneUpscalerFrame* f) {
    if (!g_context) return 0;
    ffxDispatchDescUpscale d{};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = f->command_buffer;
    d.color = resource_of(f->color, false);
    d.depth = resource_of(f->depth, false);
    d.motionVectors = resource_of(f->motion, false);
    if (g_reactive && f->reactive.image) d.reactive = resource_of(f->reactive, false);
    d.output = resource_of(f->output, true);
    d.jitterOffset = {f->jitter_x, f->jitter_y};
    d.motionVectorScale = {f->motion_scale_x, f->motion_scale_y};
    d.renderSize = {f->color.width, f->color.height};
    d.upscaleSize = {f->output.width, f->output.height};
    d.enableSharpening = f->sharpness > 0.0f;
    d.sharpness = f->sharpness;
    d.frameTimeDelta = f->frame_seconds * 1000.0f; // milliseconds
    d.preExposure = 1.0f;                          // the scene as drawn, as the client's own FSR 3.1 gets it
    d.reset = (f->flags & HAILSTONE_UPSCALER_FRAME_RESET) != 0;
    d.cameraNear = f->near_plane;
    d.cameraFar = f->far_plane;
    d.cameraFovAngleVertical = f->fov_y;
    d.viewSpaceToMetersFactor = 1.0f; // the SDK's default, as the client's own FSR 3.1 gives
    return ok(g_dispatch(&g_context, &d.header), "ffxDispatch (upscale)") ? 1 : 0;
}

void destroy(void*) { destroy_context(); }

const HailstoneUpscaler g_table = {
    HAILSTONE_UPSCALER_ABI,
    sizeof(HailstoneUpscaler),
    "AMD FSR 4",
    HAILSTONE_UPSCALER_SHARPENS, // FSR's own RCAS at display.sharpness
    0,
    instance_requirements,
    device_requirements,
    create,
    resize,
    evaluate,
    destroy,
};

} // namespace

HAILSTONE_UPSCALER_API const HailstoneUpscaler* hailstone_upscaler(const HailstoneUpscalerHost* host) {
    if (!host || host->abi < 1 || host->size < sizeof(HailstoneUpscalerHost)) return nullptr;
    g_host = host;
    if (!g_library && !load_api()) return nullptr;
    return &g_table;
}
