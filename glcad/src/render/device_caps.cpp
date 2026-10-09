#define VOLK_IMPLEMENTATION
#if defined(GLCAD_STATIC_MOLTENVK)
#include <vulkan/vulkan.h>
#else
#include <volk.h>
#endif

#include <cstring>

#include "render/device_caps.hpp"

namespace gcvk {

const char* profile_name(Profile p) noexcept {
    switch (p) {
        case Profile::AppleM:              return "AppleM";
        case Profile::AppleA:              return "AppleA";
        case Profile::DiscreteConsumer:    return "DiscreteConsumer";
        case Profile::DiscreteWorkstation: return "DiscreteWorkstation";
        default:                           return "Unknown";
    }
}

bool DeviceCaps::prefers_df64() const noexcept {
    if (!shader_float64) return true;
    return profile != Profile::DiscreteWorkstation;
}

std::vector<std::string> DeviceCaps::trait_conflicts() const {
    std::vector<std::string> out;

    auto expect = [&](bool actual, bool declared, const char* what) {
        if (actual != declared) {
            out.push_back(std::string(what) + ": device reports " +
                          (actual ? "yes" : "no") + ", profile declares " +
                          (declared ? "yes" : "no"));
        }
    };

    switch (profile) {
        case Profile::AppleM: {
            using T = PlatformTraits<AppleM>;
            expect(shader_float64, T::has_native_f64, "shaderFloat64");
            expect(shader_int64, T::has_int64, "shaderInt64");
            expect(atomic_int64_buffer, T::has_atomic_i64, "shaderBufferInt64Atomics");
            break;
        }
        case Profile::AppleA: {
            using T = PlatformTraits<AppleA>;
            expect(shader_float64, T::has_native_f64, "shaderFloat64");
            expect(shader_int64, T::has_int64, "shaderInt64");
            expect(atomic_int64_buffer, T::has_atomic_i64, "shaderBufferInt64Atomics");
            break;
        }
        case Profile::DiscreteConsumer: {
            using T = PlatformTraits<DiscreteConsumer>;
            expect(shader_int64, T::has_int64, "shaderInt64");
            break;
        }
        default: break;
    }

    if (!shader_int64)
        out.push_back("FATAL: no shaderInt64 -- the exact predicate tier "
                      "cannot run on this device");
    return out;
}

static Profile classify(const VkPhysicalDeviceProperties& props, bool moltenvk) {
    constexpr u32 kApple  = 0x106B;
    constexpr u32 kNvidia = 0x10DE;
    constexpr u32 kAmd    = 0x1002;

    if (props.vendorID == kApple || moltenvk) {
        // MoltenVK reports the Metal device name verbatim.
        if (std::strstr(props.deviceName, "Apple M") != nullptr) return Profile::AppleM;
        if (std::strstr(props.deviceName, "Apple A") != nullptr) return Profile::AppleA;
        return Profile::AppleM;
    }
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        if (props.vendorID == kNvidia || props.vendorID == kAmd)
            return Profile::DiscreteConsumer;
        return Profile::DiscreteConsumer;
    }
    return Profile::Unknown;
}

ProbeResult probe() {
    ProbeResult r;

#if !defined(GLCAD_STATIC_MOLTENVK)
    if (volkInitialize() != VK_SUCCESS) {
        r.error = "volkInitialize failed -- no Vulkan loader or ICD present";
        return r;
    }
#endif

    u32 layer_count = 0;
    vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
    std::vector<VkLayerProperties> layers(layer_count);
    if (layer_count) vkEnumerateInstanceLayerProperties(&layer_count, layers.data());
    for (const auto& l : layers) {
        r.instance_layers.emplace_back(l.layerName);
        if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0)
            r.validation_available = true;
    }

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "gcvk_probe";
    app.apiVersion       = VK_API_VERSION_1_2;

    std::vector<const char*> exts;
#ifdef VK_KHR_portability_enumeration
    // MoltenVK is non-conformant; the loader hides it without this flag.
    exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo        = &app;
    ci.enabledExtensionCount   = static_cast<u32>(exts.size());
    ci.ppEnabledExtensionNames = exts.empty() ? nullptr : exts.data();
#ifdef VK_KHR_portability_enumeration
    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    VkInstance instance = VK_NULL_HANDLE;
    VkResult   res      = vkCreateInstance(&ci, nullptr, &instance);
    if (res != VK_SUCCESS) {
        // Retry without portability, for drivers that reject the flag.
        ci.enabledExtensionCount = 0;
        ci.flags                 = 0;
        res = vkCreateInstance(&ci, nullptr, &instance);
    }
    if (res != VK_SUCCESS) {
        r.error = "vkCreateInstance failed (VkResult " + std::to_string(res) + ")";
        return r;
    }
#if !defined(GLCAD_STATIC_MOLTENVK)
    volkLoadInstance(instance);
#endif

    u32 count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    if (count) vkEnumeratePhysicalDevices(instance, &count, devices.data());

    for (VkPhysicalDevice pd : devices) {
        DeviceCaps c;

        VkPhysicalDeviceDriverProperties driver{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceSubgroupProperties subgroup{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
        subgroup.pNext = &driver;
        VkPhysicalDeviceProperties2 props2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
        props2.pNext = &subgroup;
        vkGetPhysicalDeviceProperties2(pd, &props2);

        VkPhysicalDeviceVulkan12Features v12{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
        VkPhysicalDeviceFeatures2 feats2{
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        feats2.pNext = &v12;
        vkGetPhysicalDeviceFeatures2(pd, &feats2);

        const auto& p = props2.properties;
        c.name        = p.deviceName;
        c.driver      = driver.driverName[0] ? driver.driverName : "(unreported)";
        c.api_version = p.apiVersion;
        c.api_version_str = std::to_string(VK_API_VERSION_MAJOR(p.apiVersion)) + "." +
                            std::to_string(VK_API_VERSION_MINOR(p.apiVersion)) + "." +
                            std::to_string(VK_API_VERSION_PATCH(p.apiVersion));
        c.vendor_id   = p.vendorID;
        c.is_moltenvk = (std::strstr(c.driver.c_str(), "MoltenVK") != nullptr) ||
                        (driver.driverID == VK_DRIVER_ID_MOLTENVK);

        c.shader_float64      = feats2.features.shaderFloat64 == VK_TRUE;
        c.shader_int64        = feats2.features.shaderInt64 == VK_TRUE;
        c.atomic_int64_buffer = v12.shaderBufferInt64Atomics == VK_TRUE;
        c.atomic_int64_shared = v12.shaderSharedInt64Atomics == VK_TRUE;

        c.subgroup_arithmetic =
            (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_ARITHMETIC_BIT) != 0;
        c.subgroup_ballot =
            (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_BALLOT_BIT) != 0;
        c.subgroup_size      = subgroup.subgroupSize;
        c.max_workgroup_size = p.limits.maxComputeWorkGroupInvocations;

        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(pd, &mem);
        for (u32 i = 0; i < mem.memoryHeapCount; ++i)
            if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                c.device_local_bytes += mem.memoryHeaps[i].size;

        c.is_unified_memory =
            p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ||
            c.is_moltenvk;

        c.profile = classify(p, c.is_moltenvk);
        r.devices.push_back(std::move(c));
    }

    vkDestroyInstance(instance, nullptr);
    r.ok = true;
    return r;
}

}
