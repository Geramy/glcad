#include <algorithm>
#if defined(GLCAD_SURFACE_XLIB)
// First, so X11's macros never reach the project headers.
#include "ui/xlib.hpp"
#endif

#include "ui/renderer.hpp"
#include "ui/resource_path.hpp"

#include <cstdio>
#include <cstring>
#if defined(__APPLE__)
#include <dlfcn.h>
#endif
#include "engine/stream.hpp"

namespace gcapp {

static constexpr int kFramesInFlight = 2;

static std::vector<gcm::u8> read_file(const std::string& p) {
    gio::FileReader f;
    if (!f.open(p).ok) return {};
    std::vector<gcm::u8> b;
    f.read_all(b);
    return b;
}

static VkShaderModule load_shader(VkDevice d, const char* name) {
    std::string path = std::string(GLCAD_SHADER_DIR) + "/" + name + ".spv";
    auto code = read_file(path);
    if (code.empty()) {
        const std::string alt =
            resource_fallback(std::string("shaders/") + name + ".spv");
        if (!alt.empty()) {
            path = alt;
            code = read_file(path);
        }
    }
    if (code.empty()) {
        std::fprintf(stderr, "shader missing: %s\n", path.c_str());
        return VK_NULL_HANDLE;
    }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = code.size();
    ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m{};
    vkCreateShaderModule(d, &ci, nullptr, &m);
    return m;
}

static uint32_t find_mem(VkPhysicalDevice p, uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(p, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return 0;
}

void Renderer::ensure(Buffer& b, size_t bytes, VkBufferUsageFlags usage) {
    if (bytes <= b.capacity) return;
    size_t want = b.capacity ? b.capacity : 4096;
    while (want < bytes) want *= 2;
    destroy(b);

    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size  = want;
    bi.usage = usage;
    vkCreateBuffer(device_, &bi, nullptr, &b.buf);

    VkMemoryRequirements mr{};
    vkGetBufferMemoryRequirements(device_, b.buf, &mr);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize  = mr.size;
    ma.memoryTypeIndex = find_mem(phys_, mr.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(device_, &ma, nullptr, &b.mem);
    vkBindBufferMemory(device_, b.buf, b.mem, 0);
    b.capacity = want;
    vkMapMemory(device_, b.mem, 0, VK_WHOLE_SIZE, 0, &b.mapped);
}

void Renderer::upload(Buffer& b, const void* data, size_t bytes) {
    if (!bytes || !b.mapped || bytes > b.capacity) return;
    std::memcpy(b.mapped, data, bytes);
}

void Renderer::destroy(Buffer& b) {
    if (b.mapped) vkUnmapMemory(device_, b.mem);
    if (b.buf) vkDestroyBuffer(device_, b.buf, nullptr);
    if (b.mem) vkFreeMemory(device_, b.mem, nullptr);
    b = Buffer{};
}

bool Renderer::init(const SurfaceHandle& surface, uint32_t w, uint32_t h,
                    std::string& err) {
#if !defined(GLCAD_STATIC_MOLTENVK)
#if defined(__APPLE__)
        void* loader = dlopen("@executable_path/../Frameworks/libvulkan.1.dylib",
                              RTLD_NOW | RTLD_LOCAL);
        if (!loader) loader = dlopen("@executable_path/../Frameworks/libMoltenVK.dylib",
                                    RTLD_NOW | RTLD_LOCAL);
        auto gipa = loader ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(
                                 dlsym(loader, "vkGetInstanceProcAddr"))
                           : nullptr;
        if (!gipa) { err = "bundled Vulkan runtime is missing or invalid"; return false; }
        volkInitializeCustom(gipa);
#else
    if (volkInitialize() != VK_SUCCESS) {
        err = "no Vulkan loader";
        return false;
    }
#endif
#endif

    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "GLCAD";
    app.apiVersion       = VK_API_VERSION_1_2;

    std::vector<const char*> exts{VK_KHR_SURFACE_EXTENSION_NAME};
#if defined(GLCAD_SURFACE_METAL)
    exts.push_back(VK_EXT_METAL_SURFACE_EXTENSION_NAME);
    exts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    // portability_enumeration needs the loader; static MoltenVK has none.
    bool portability = false;
    {
        uint32_t n = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> have(n);
        if (n) vkEnumerateInstanceExtensionProperties(nullptr, &n, have.data());
        for (const auto& e : have)
            if (std::strcmp(e.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
                portability = true;
    }
    if (portability) exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
#elif defined(GLCAD_SURFACE_XLIB)
    exts.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#elif defined(GLCAD_SURFACE_WIN32)
    exts.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo        = &app;
    ici.enabledExtensionCount   = static_cast<uint32_t>(exts.size());
    ici.ppEnabledExtensionNames = exts.data();
#if defined(GLCAD_SURFACE_METAL)
    if (portability) ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    if (vkCreateInstance(&ici, nullptr, &instance_) != VK_SUCCESS) {
        err = "vkCreateInstance failed"; return false;
    }
#if !defined(GLCAD_STATIC_MOLTENVK)
    volkLoadInstance(instance_);
#endif

#if defined(GLCAD_SURFACE_METAL)
    VkMetalSurfaceCreateInfoEXT sci{VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT};
    sci.pLayer = static_cast<const CAMetalLayer*>(surface.metal_layer);
    if (vkCreateMetalSurfaceEXT(instance_, &sci, nullptr, &surface_) != VK_SUCCESS) {
        err = "vkCreateMetalSurfaceEXT failed"; return false;
    }
#elif defined(GLCAD_SURFACE_XLIB)
    VkXlibSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
    sci.dpy    = static_cast<Display*>(surface.xlib_display);
    sci.window = static_cast<::Window>(surface.xlib_window);
    if (vkCreateXlibSurfaceKHR(instance_, &sci, nullptr, &surface_) != VK_SUCCESS) {
        err = "vkCreateXlibSurfaceKHR failed"; return false;
    }
#elif defined(GLCAD_SURFACE_WIN32)
    VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    sci.hinstance = static_cast<HINSTANCE>(surface.win32_instance);
    sci.hwnd      = static_cast<HWND>(surface.win32_window);
    if (vkCreateWin32SurfaceKHR(instance_, &sci, nullptr, &surface_) != VK_SUCCESS) {
        err = "vkCreateWin32SurfaceKHR failed"; return false;
    }
#else
    (void)surface;
    err = "this build has no window-system surface";
    return false;
#endif

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance_, &n, devs.data());
    if (!n) { err = "no Vulkan device"; return false; }
    phys_ = devs[0];

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qs(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(phys_, &qn, qs.data());
    bool found = false;
    for (uint32_t i = 0; i < qn; ++i) {
        VkBool32 present = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(phys_, i, surface_, &present);
        if ((qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
            qfam_ = i; found = true; break;
        }
    }
    if (!found) { err = "no graphics+present queue"; return false; }

    uint32_t den = 0;
    vkEnumerateDeviceExtensionProperties(phys_, nullptr, &den, nullptr);
    std::vector<VkExtensionProperties> de(den);
    vkEnumerateDeviceExtensionProperties(phys_, nullptr, &den, de.data());
    std::vector<const char*> want{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    for (const auto& e : de)
        if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0)
            want.push_back("VK_KHR_portability_subset");

    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qfam_;
    qci.queueCount       = 1;
    qci.pQueuePriorities = &prio;

    VkPhysicalDeviceFeatures feats{};
    feats.largePoints = VK_TRUE;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount    = 1;
    dci.pQueueCreateInfos       = &qci;
    dci.enabledExtensionCount   = static_cast<uint32_t>(want.size());
    dci.ppEnabledExtensionNames = want.data();
    dci.pEnabledFeatures        = &feats;
    if (vkCreateDevice(phys_, &dci, nullptr, &device_) != VK_SUCCESS) {
        err = "vkCreateDevice failed"; return false;
    }
#if !defined(GLCAD_STATIC_MOLTENVK)
    volkLoadDevice(device_);
#endif
    vkGetDeviceQueue(device_, qfam_, 0, &queue_);

    extent_ = {w, h};
    if (!create_descriptors(err)) return false;
    if (!create_swapchain(err)) return false;
    if (!create_pipelines(err)) return false;

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = qfam_;
    vkCreateCommandPool(device_, &pci, nullptr, &cmd_pool_);

    cmds_.resize(kFramesInFlight);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool        = cmd_pool_;
    ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = kFramesInFlight;
    vkAllocateCommandBuffers(device_, &ai, cmds_.data());

    sem_avail_.resize(kFramesInFlight);
    fences_.resize(kFramesInFlight);
    for (int i = 0; i < kFramesInFlight; ++i) {
        VkSemaphoreCreateInfo s{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(device_, &s, nullptr, &sem_avail_[i]);
        VkFenceCreateInfo f{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        f.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device_, &f, nullptr, &fences_[i]);
    }
    return true;
}

bool Renderer::create_descriptors(std::string& err) {
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(device_, &si, nullptr, &sampler_) != VK_SUCCESS) {
        err = "vkCreateSampler failed"; return false;
    }

    VkDescriptorSetLayoutBinding b[2]{};
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    b[1].binding = 1;
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[1].descriptorCount = 1;
    b[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2;
    dl.pBindings = b;
    vkCreateDescriptorSetLayout(device_, &dl, nullptr, &dset_layout_);

    VkDescriptorPoolSize ps[2] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kFramesInFlight},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kFramesInFlight}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = kFramesInFlight;
    dp.poolSizeCount = 2;
    dp.pPoolSizes = ps;
    vkCreateDescriptorPool(device_, &dp, nullptr, &dpool_);

    std::vector<VkDescriptorSetLayout> layouts(kFramesInFlight, dset_layout_);
    dsets_.resize(kFramesInFlight);
    VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = dpool_;
    da.descriptorSetCount = kFramesInFlight;
    da.pSetLayouts = layouts.data();
    vkAllocateDescriptorSets(device_, &da, dsets_.data());

    ubos_.resize(kFramesInFlight);
    for (int i = 0; i < kFramesInFlight; ++i) {
        ensure(ubos_[i], sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        VkDescriptorBufferInfo bi{ubos_[i].buf, 0, sizeof(FrameUniforms)};
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = dsets_[i];
        w.dstBinding = 1;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        w.pBufferInfo = &bi;
        vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    }
    return true;
}

bool Renderer::set_font_atlas(const std::vector<uint8_t>& pixels, int width,
                              int height, std::string& err) {
    if (pixels.empty() || width <= 0 || height <= 0 ||
        std::size_t(width) > pixels.size() / std::size_t(height)) {
        err = "empty font atlas"; return false;
    }

    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8_UNORM;
    ii.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ii, nullptr, &atlas_img_) != VK_SUCCESS) {
        err = "atlas vkCreateImage failed"; return false;
    }

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(device_, atlas_img_, &mr);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = find_mem(phys_, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device_, &ma, nullptr, &atlas_mem_) != VK_SUCCESS ||
        vkBindImageMemory(device_, atlas_img_, atlas_mem_, 0) != VK_SUCCESS) {
        err = "atlas device memory allocation failed"; return false;
    }

    Buffer staging;
    const std::size_t bytes = std::size_t(width) * std::size_t(height);
    ensure(staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!staging.buf || !staging.mem) {
        destroy(staging); err = "atlas staging allocation failed"; return false;
    }
    upload(staging, pixels.data(), bytes);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = atlas_img_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_, &vi, nullptr, &atlas_view_) != VK_SUCCESS) {
        destroy(staging); err = "atlas image view failed"; return false;
    }

    {
        VkCommandBufferAllocateInfo ai{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = cmd_pool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb{};
        if (vkAllocateCommandBuffers(device_, &ai, &cb) != VK_SUCCESS) {
            destroy(staging); err = "atlas upload command allocation failed"; return false;
        }

        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS) {
            vkFreeCommandBuffers(device_, cmd_pool_, 1, &cb);
            destroy(staging); err = "atlas upload command begin failed"; return false;
        }

        VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = atlas_img_;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &bar);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {uint32_t(width), uint32_t(height), 1};
        vkCmdCopyBufferToImage(cb, staging.buf, atlas_img_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        bar.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &bar);
        if (vkEndCommandBuffer(cb) != VK_SUCCESS) {
            vkFreeCommandBuffers(device_, cmd_pool_, 1, &cb);
            destroy(staging); err = "atlas upload command end failed"; return false;
        }

        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        if (vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(device_, cmd_pool_, 1, &cb);
            destroy(staging); err = "atlas upload submit failed"; return false;
        }
        const VkResult uploaded = vkQueueWaitIdle(queue_);
        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cb);
        destroy(staging);
        if (uploaded != VK_SUCCESS) { err = "atlas upload failed"; return false; }
    }

    VkDescriptorImageInfo di{sampler_, atlas_view_,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (VkDescriptorSet set : dsets_) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &di;
        vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    }

    atlas_ready_ = true;
    return true;
}

bool Renderer::create_depth(std::string& err) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = depth_format_;
    ii.extent = {extent_.width, extent_.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = static_cast<VkSampleCountFlagBits>(samples_);
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (vkCreateImage(device_, &ii, nullptr, &depth_img_) != VK_SUCCESS) {
        err = "depth vkCreateImage failed"; return false;
    }
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(device_, depth_img_, &mr);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = find_mem(phys_, mr.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &ma, nullptr, &depth_mem_);
    vkBindImageMemory(device_, depth_img_, depth_mem_, 0);

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = depth_img_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = depth_format_;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    vkCreateImageView(device_, &vi, nullptr, &depth_view_);
    return true;
}

bool Renderer::create_swapchain(std::string& err) {
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys_, surface_, &caps);
    if (caps.currentExtent.width != UINT32_MAX) extent_ = caps.currentExtent;
    if (extent_.width == 0 || extent_.height == 0) return true;

    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys_, surface_, &fn, fmts.data());
    format_ = fmts[0].format;
    for (const auto& f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) { format_ = f.format; break; }

    uint32_t count = caps.minImageCount + 1;
    if (caps.maxImageCount && count > caps.maxImageCount) count = caps.maxImageCount;

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface          = surface_;
    ci.minImageCount    = count;
    ci.imageFormat      = format_;
    ci.imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent      = extent_;
    ci.imageArrayLayers = 1;
    ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform     = caps.currentTransform;
    ci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped          = VK_TRUE;
    if (vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_) != VK_SUCCESS) {
        err = "vkCreateSwapchainKHR failed"; return false;
    }

    uint32_t in = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &in, nullptr);
    images_.resize(in);
    vkGetSwapchainImagesKHR(device_, swapchain_, &in, images_.data());
    sem_done_.resize(in, VK_NULL_HANDLE);
    for (auto& semaphore : sem_done_) {
        VkSemaphoreCreateInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (vkCreateSemaphore(device_, &info, nullptr, &semaphore) != VK_SUCCESS) {
            err = "presentation semaphore allocation failed";
            return false;
        }
    }


    if (!create_depth(err)) return false;
    if (samples_ > 1 && !create_msaa_target(err)) return false;

    if (!pass_) {
        const auto samp = static_cast<VkSampleCountFlagBits>(samples_);
        VkAttachmentDescription at[3]{};
        at[0].format         = format_;
        at[0].samples        = samp;
        at[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[0].storeOp        = samples_ > 1 ? VK_ATTACHMENT_STORE_OP_DONT_CARE
                                            : VK_ATTACHMENT_STORE_OP_STORE;
        at[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        at[0].finalLayout    = samples_ > 1 ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                            : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        at[1].format         = depth_format_;
        at[1].samples        = samp;
        at[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        at[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        at[2].format         = format_;
        at[2].samples        = VK_SAMPLE_COUNT_1_BIT;
        at[2].loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at[2].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        at[2].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        at[2].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        at[2].finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkAttachmentReference resolve{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &color;
        sub.pDepthStencilAttachment = &depth;
        if (samples_ > 1) sub.pResolveAttachments = &resolve;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = samples_ > 1 ? 3u : 2u;
        rp.pAttachments = at;
        rp.subpassCount    = 1; rp.pSubpasses = &sub;
        rp.dependencyCount = 1; rp.pDependencies = &dep;
        vkCreateRenderPass(device_, &rp, nullptr, &pass_);
    }

    views_.resize(in);
    fbs_.resize(in);
    for (uint32_t i = 0; i < in; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image    = images_[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format   = format_;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCreateImageView(device_, &vi, nullptr, &views_[i]);

        VkImageView att[3] = {views_[i], depth_view_, VK_NULL_HANDLE};
        if (samples_ > 1) { att[0] = msaa_view_; att[2] = views_[i]; }
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass      = pass_;
        fi.attachmentCount = samples_ > 1 ? 3u : 2u;
        fi.pAttachments    = att;
        fi.width           = extent_.width;
        fi.height          = extent_.height;
        fi.layers          = 1;
        vkCreateFramebuffer(device_, &fi, nullptr, &fbs_[i]);
    }
    return true;
}

bool Renderer::create_msaa_target(std::string& err) {
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format_;
    ii.extent = {extent_.width, extent_.height, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = static_cast<VkSampleCountFlagBits>(samples_);
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    if (vkCreateImage(device_, &ii, nullptr, &msaa_img_) != VK_SUCCESS) {
        err = "msaa vkCreateImage failed"; return false;
    }
    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(device_, msaa_img_, &mr);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = find_mem(phys_, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device_, &ma, nullptr, &msaa_mem_);
    vkBindImageMemory(device_, msaa_img_, msaa_mem_, 0);
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = msaa_img_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format_;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(device_, &vi, nullptr, &msaa_view_);
    return true;
}

void Renderer::destroy_msaa_target() {
    if (msaa_view_) vkDestroyImageView(device_, msaa_view_, nullptr);
    if (msaa_img_) vkDestroyImage(device_, msaa_img_, nullptr);
    if (msaa_mem_) vkFreeMemory(device_, msaa_mem_, nullptr);
    msaa_view_ = VK_NULL_HANDLE; msaa_img_ = VK_NULL_HANDLE; msaa_mem_ = VK_NULL_HANDLE;
}

void Renderer::set_msaa(uint32_t samples) noexcept {
    if (samples != 1 && samples != 2 && samples != 4 && samples != 8 && samples != 16) samples = 1;
    if (phys_) {
        VkPhysicalDeviceProperties pr{};
        vkGetPhysicalDeviceProperties(phys_, &pr);
        const VkSampleCountFlags ok =
            pr.limits.framebufferColorSampleCounts & pr.limits.framebufferDepthSampleCounts;
        while (samples > 1 && !(ok & samples)) samples /= 2;
    }
    pending_samples_ = samples;
}

bool Renderer::apply_msaa(std::string& err) {
    vkDeviceWaitIdle(device_);
    destroy_swapchain();
    if (pass_) vkDestroyRenderPass(device_, pass_, nullptr);
    pass_ = VK_NULL_HANDLE;
    for (VkPipeline* pp : {&pipe_thin_lines_, &pipe_thin_depth_, &pipe_thin_arcs_, &pipe_thin_arcs_depth_, &pipe_canvas_, &pipe_lines_, &pipe_lines_depth_, &pipe_points_,
                           &pipe_ui_, &pipe_text_, &pipe_mesh_, &pipe_mesh_overlay_, &pipe_flat_, &pipe_image_,
                           &pipe_arcs_, &pipe_arcs_depth_}) {
        if (*pp) vkDestroyPipeline(device_, *pp, nullptr);
        *pp = VK_NULL_HANDLE;
    }
    boolean_gpu_.destroy();
    if (layout_) vkDestroyPipelineLayout(device_, layout_, nullptr);
    layout_ = VK_NULL_HANDLE;
    samples_ = pending_samples_;
    std::string e2;
    if (!create_swapchain(err)) return false;
    if (!create_pipelines(err)) return false;
    return true;
}

void Renderer::destroy_swapchain() {
    for (auto semaphore : sem_done_) if (semaphore) vkDestroySemaphore(device_, semaphore, nullptr);
    sem_done_.clear();
    for (auto f : fbs_) vkDestroyFramebuffer(device_, f, nullptr);
    for (auto v : views_) vkDestroyImageView(device_, v, nullptr);
    fbs_.clear(); views_.clear();
    destroy_msaa_target();
    if (depth_view_) vkDestroyImageView(device_, depth_view_, nullptr);
    if (depth_img_) vkDestroyImage(device_, depth_img_, nullptr);
    if (depth_mem_) vkFreeMemory(device_, depth_mem_, nullptr);
    depth_view_ = VK_NULL_HANDLE; depth_img_ = VK_NULL_HANDLE; depth_mem_ = VK_NULL_HANDLE;
    if (swapchain_) vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

bool Renderer::create_pipelines(std::string& err) {
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 1;
    li.pSetLayouts = &dset_layout_;
    vkCreatePipelineLayout(device_, &li, nullptr, &layout_);

    struct Attr { uint32_t loc, off; VkFormat fmt; };

    auto build = [&](const char* vs, const char* fs, VkPrimitiveTopology topo,
                     uint32_t stride, const std::vector<Attr>& attrs,
                     VkVertexInputRate rate, VkPipelineLayout lay, bool depth,
                     VkPipeline& out, bool color_write = true, bool overlay = false) -> bool {
        VkShaderModule v = load_shader(device_, vs);
        VkShaderModule f = load_shader(device_, fs);
        if (!v || !f) { err = std::string("shader load failed: ") + vs; return false; }

        VkPipelineShaderStageCreateInfo st[2]{};
        st[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = v; st[0].pName = "main";
        st[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = f; st[1].pName = "main";

        VkVertexInputBindingDescription bind{0, stride, rate};
        std::vector<VkVertexInputAttributeDescription> ad;
        for (const auto& a : attrs) ad.push_back({a.loc, 0, a.fmt, a.off});

        VkPipelineVertexInputStateCreateInfo vi{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        if (stride) {
            vi.vertexBindingDescriptionCount   = 1;
            vi.pVertexBindingDescriptions      = &bind;
            vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(ad.size());
            vi.pVertexAttributeDescriptions    = ad.data();
        }

        VkPipelineInputAssemblyStateCreateInfo ia{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = topo;

        VkPipelineViewportStateCreateInfo vp{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1; vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_NONE;
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.0f;
        rs.depthBiasEnable = overlay ? VK_TRUE : VK_FALSE;
        rs.depthBiasConstantFactor = overlay ? -1.0f : 0.0f;
        rs.depthBiasSlopeFactor = overlay ? -1.0f : 0.0f;

        VkPipelineMultisampleStateCreateInfo ms{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = static_cast<VkSampleCountFlagBits>(samples_);

        VkPipelineDepthStencilStateCreateInfo ds{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable  = depth ? VK_TRUE : VK_FALSE;
        ds.depthWriteEnable = depth && !overlay ? VK_TRUE : VK_FALSE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState cba{};
        cba.blendEnable         = color_write ? VK_TRUE : VK_FALSE;
        cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.colorBlendOp        = VK_BLEND_OP_ADD;
        cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        cba.alphaBlendOp        = VK_BLEND_OP_ADD;
        cba.colorWriteMask      = color_write ? 0xF : 0x0;
        VkPipelineColorBlendStateCreateInfo cb{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &cba;

        VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dst{
            VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dst.dynamicStateCount = 2; dst.pDynamicStates = dyn;

        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2; gp.pStages = st;
        gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp; gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms; gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb; gp.pDynamicState = &dst;
        gp.layout = lay; gp.renderPass = pass_;

        const VkResult r = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp,
                                                     nullptr, &out);
        vkDestroyShaderModule(device_, v, nullptr);
        vkDestroyShaderModule(device_, f, nullptr);
        if (r != VK_SUCCESS) { err = std::string("pipeline failed: ") + vs; return false; }
        return true;
    };

    const std::vector<Attr> line_attrs = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT},
        {1, sizeof(float) * 3, VK_FORMAT_R32G32B32A32_SFLOAT}};
    const std::vector<Attr> stroke_attrs = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT},
        {1, 12, VK_FORMAT_R32G32B32A32_SFLOAT},
        {2, 28, VK_FORMAT_R32_SFLOAT},
        {3, sizeof(LineVertex), VK_FORMAT_R32G32B32_SFLOAT}};
    const std::vector<Attr> ui_attrs = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT},
        {1, 16, VK_FORMAT_R32G32B32A32_SFLOAT},
        {2, 32, VK_FORMAT_R32G32B32A32_SFLOAT},
        {3, 48, VK_FORMAT_R32G32B32A32_SFLOAT}};
    const std::vector<Attr> text_attrs = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT},
        {1, 8, VK_FORMAT_R32G32_SFLOAT},
        {2, 16, VK_FORMAT_R32G32B32A32_SFLOAT},
        {3, 32, VK_FORMAT_R32G32B32A32_SFLOAT}};
    const std::vector<Attr> arc_attrs = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT},
        {1, 16, VK_FORMAT_R32G32B32A32_SFLOAT},
        {2, 32, VK_FORMAT_R32G32B32A32_SFLOAT},
        {3, 48, VK_FORMAT_R32G32B32A32_SFLOAT}};
    const std::vector<Attr> mesh_attrs = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT},
        {1, 12, VK_FORMAT_R32G32B32_SFLOAT},
        {2, 24, VK_FORMAT_R32G32B32A32_SFLOAT}};

    if (!build("canvas.vert", "canvas.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
               0, {}, VK_VERTEX_INPUT_RATE_VERTEX, layout_, false, pipe_canvas_))
        return false;
    if (!build("geom.vert", "geom.frag", VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
               sizeof(LineVertex), line_attrs, VK_VERTEX_INPUT_RATE_VERTEX, layout_, false, pipe_thin_lines_)) return false;
    if (!build("geom.vert", "geom.frag", VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
               sizeof(LineVertex), line_attrs, VK_VERTEX_INPUT_RATE_VERTEX, layout_, true, pipe_thin_depth_)) return false;
    if (!build("arc_thin.vert", "geom.frag", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
               sizeof(ArcInstance), arc_attrs, VK_VERTEX_INPUT_RATE_INSTANCE, layout_, false, pipe_thin_arcs_)) return false;
    if (!build("arc_thin.vert", "geom.frag", VK_PRIMITIVE_TOPOLOGY_LINE_STRIP,
               sizeof(ArcInstance), arc_attrs, VK_VERTEX_INPUT_RATE_INSTANCE, layout_, true, pipe_thin_arcs_depth_)) return false;
    if (!build("line.vert", "geom.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
               sizeof(LineVertex)*2, stroke_attrs, VK_VERTEX_INPUT_RATE_INSTANCE, layout_,
               false, pipe_lines_)) return false;
    if (!build("line.vert", "geom.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
               sizeof(LineVertex)*2, stroke_attrs, VK_VERTEX_INPUT_RATE_INSTANCE, layout_,
               true, pipe_lines_depth_)) return false;
    if (!build("geom.vert", "geom.frag", VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
               sizeof(LineVertex), line_attrs, VK_VERTEX_INPUT_RATE_VERTEX, layout_,
               false, pipe_points_)) return false;
    if (!build("arc.vert", "geom.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
               sizeof(ArcInstance), arc_attrs, VK_VERTEX_INPUT_RATE_INSTANCE,
               layout_, false, pipe_arcs_)) return false;
    if (!build("arc.vert", "geom.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
               sizeof(ArcInstance), arc_attrs, VK_VERTEX_INPUT_RATE_INSTANCE,
               layout_, true, pipe_arcs_depth_)) return false;
    if (!build("mesh.vert", "mesh.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
               sizeof(MeshVertex), mesh_attrs, VK_VERTEX_INPUT_RATE_VERTEX,
               layout_, true, pipe_mesh_)) return false;
    if (!build("mesh.vert", "mesh.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
               sizeof(MeshVertex), mesh_attrs, VK_VERTEX_INPUT_RATE_VERTEX,
               layout_, true, pipe_mesh_overlay_, true, true)) return false;
    {
        const VkShaderModule vs = load_shader(device_, "canvas.vert");
        const VkShaderModule fs = load_shader(device_, "bool_preview.frag");
        gbool::BooleanGpu::Deps deps{};
        deps.device = device_; deps.phys = phys_; deps.pass = pass_;
        deps.frame_layout = dset_layout_; deps.vs = vs; deps.fs = fs;
        deps.samples = samples_; deps.frame_count = kFramesInFlight;
        std::string error;
        const bool ok = boolean_gpu_.build(deps, error);
        if (vs) vkDestroyShaderModule(device_, vs, nullptr);
        if (fs) vkDestroyShaderModule(device_, fs, nullptr);
        if (!ok) std::fprintf(stderr, "gpu preview disabled: %s\n", error.c_str());
    }
    if (!build("ui.vert", "ui.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
               sizeof(UiInstance), ui_attrs, VK_VERTEX_INPUT_RATE_INSTANCE,
               layout_, false, pipe_ui_)) return false;
    if (!build("text.vert", "text.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
               sizeof(TextVertex), text_attrs, VK_VERTEX_INPUT_RATE_VERTEX,
               layout_, false, pipe_text_)) return false;
    if (!build("text.vert", "image.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
               sizeof(TextVertex), text_attrs, VK_VERTEX_INPUT_RATE_VERTEX,
               layout_, false, pipe_image_)) return false;
    if (!build("text.vert", "flat.frag", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
               sizeof(TextVertex), text_attrs, VK_VERTEX_INPUT_RATE_VERTEX,
               layout_, false, pipe_flat_)) return false;
    return true;
}

void Renderer::resize(uint32_t w, uint32_t h) {
    needs_resize_ = true; pending_w_ = w; pending_h_ = h;
}

void Renderer::record_ground(const Recording& r) {
    VkDeviceSize off = 0; (void)off;
    vkCmdBindPipeline(r.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_canvas_);
    vkCmdDraw(r.cb, 3, 1, 0, 0);
}
void Renderer::record_images(const Recording& r) {
    VkDeviceSize off = 0; (void)off;
    for(const auto& batch:(*r.images)) {
        vkCmdBindDescriptorSets(r.cb,VK_PIPELINE_BIND_POINT_GRAPHICS,layout_,0,1,&batch.texture->sets[r.fi],0,nullptr);
        vkCmdBindPipeline(r.cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe_image_);
        vkCmdBindVertexBuffers(r.cb,0,1,&r.vb->images.buf,&off);
        vkCmdDraw(r.cb,uint32_t(batch.count),1,uint32_t(batch.first),0);
    }
    vkCmdBindDescriptorSets(r.cb,VK_PIPELINE_BIND_POINT_GRAPHICS,layout_,0,1,&dsets_[r.fi],0,nullptr);
}
void Renderer::record_solids(const Recording& r) {
    if (!r.n_mesh) return;
    const bool gpu_active = r.preview_gpu &&
        boolean_gpu_.record(r.cb, r.fi, dsets_[r.fi], *r.preview_gpu);
    vkCmdBindDescriptorSets(r.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                            &dsets_[r.fi], 0, nullptr);
    vkCmdBindPipeline(r.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_mesh_);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(r.cb, 0, 1, &r.vb->mesh.buf, &offset);
    const auto replaced = [&](std::size_t first, std::size_t count) {
        if (!gpu_active) return false;
        const auto& p = *r.preview_gpu;
        return (first == p.target_body_first && count == p.target_body_count) ||
               (first == p.tool_body_first && count == p.tool_body_count) ||
               (first == r.preview_first && count == r.preview_count);
    };
    if (r.f->mesh_items && !r.f->mesh_items->empty()) {
        VkPipeline current_pipeline = pipe_mesh_;
        for (bool translucent : {false, true}) {
            std::size_t first = 0, count = 0;
            auto flush = [&] {
                if (count) vkCmdDraw(r.cb, uint32_t(count), 1, uint32_t(first), 0);
                count = 0;
            };
            for (const auto& item : *r.f->mesh_items) {
                if (!item.visible || !item.count || item.material.translucent != translucent ||
                    item.first > r.n_mesh || item.count > r.n_mesh - item.first ||
                    replaced(item.first, item.count)) continue;
                const auto pipeline = item.kind == grender::DrawKind::Overlay ? pipe_mesh_overlay_ : pipe_mesh_;
                if (pipeline != current_pipeline || (count && first + count != item.first)) {
                    flush();
                    if (pipeline != current_pipeline) {
                        vkCmdBindPipeline(r.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                        current_pipeline = pipeline;
                    }
                }
                if (!count) first = item.first;
                count += item.count;
            }
            flush();
        }
    } else {
        std::vector<std::pair<std::size_t, std::size_t>> excluded;
        if (gpu_active) {
            const auto& p = *r.preview_gpu;
            for (auto range : {std::make_pair(p.target_body_first, p.target_body_count),
                               std::make_pair(p.tool_body_first, p.tool_body_count),
                               std::make_pair(r.preview_first, r.preview_count)})
                if (range.second && range.first <= r.n_mesh && range.second <= r.n_mesh-range.first)
                    excluded.push_back(range);
        }
        std::sort(excluded.begin(), excluded.end());
        std::size_t first = 0;
        for (const auto& range : excluded) {
            if (range.first > first) vkCmdDraw(r.cb, uint32_t(range.first-first), 1, uint32_t(first), 0);
            first = std::max(first, range.first+range.second);
        }
        if (first < r.n_mesh) vkCmdDraw(r.cb, uint32_t(r.n_mesh-first), 1, uint32_t(first), 0);
    }
}
void Renderer::record_drawing(const Recording& r) {
    VkDeviceSize off = 0; (void)off;
    if (r.n_static && r.vb->static_lines.buf) {
        vkCmdBindPipeline(r.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r.vb->static_wide_lines ? pipe_lines_depth_ : pipe_thin_depth_);
        vkCmdBindVertexBuffers(r.cb, 0, 1, &r.vb->static_lines.buf, &off);
        if(r.vb->static_wide_lines) vkCmdDraw(r.cb, 6, uint32_t(r.n_static/2), 0, 0);
        else vkCmdDraw(r.cb,uint32_t(r.n_static),1,0,0);
    }
    if (r.vb->static_arc_count && r.vb->static_arcs.buf) {
        vkCmdBindPipeline(r.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, r.vb->static_wide_arcs ? pipe_arcs_depth_ : pipe_thin_arcs_depth_);
        vkCmdBindVertexBuffers(r.cb, 0, 1, &r.vb->static_arcs.buf, &off);
        vkCmdDraw(r.cb, r.vb->static_wide_arcs ? 255*6 : 256, uint32_t(r.vb->static_arc_count), 0, 0);
    }
    if (r.n_arcs) {
        const bool wide=std::any_of(r.f->canvas_arcs->begin(),r.f->canvas_arcs->end(),[](const auto& a){return a.minor[3]>1;});
        vkCmdBindPipeline(r.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, wide ? pipe_arcs_ : pipe_thin_arcs_);
        vkCmdBindVertexBuffers(r.cb, 0, 1, &r.vb->arcs.buf, &off);
        vkCmdDraw(r.cb, wide ? 255*6 : 256, uint32_t(r.n_arcs), 0, 0);
    }
    if (r.n_lines) {
        const bool wide=std::any_of(r.f->canvas_lines->begin(),r.f->canvas_lines->end(),[](const auto& v){return v.width_px>1;});
        const auto n_sketch = std::min<std::size_t>(r.f->sketch_lines, r.n_lines);
        vkCmdBindVertexBuffers(r.cb, 0, 1, &r.vb->lines.buf, &off);
        if (n_sketch) {
            vkCmdBindPipeline(r.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, wide ? pipe_lines_depth_ : pipe_thin_depth_);
            if(wide) vkCmdDraw(r.cb, 6, uint32_t(n_sketch/2), 0, 0);
            else vkCmdDraw(r.cb,uint32_t(n_sketch),1,0,0);
        }
        if (r.n_lines > n_sketch) {
            vkCmdBindPipeline(r.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, wide ? pipe_lines_ : pipe_thin_lines_);
            if(wide) {
                const VkDeviceSize overlay_offset = VkDeviceSize(n_sketch) * sizeof(LineVertex);
                vkCmdBindVertexBuffers(r.cb, 0, 1, &r.vb->lines.buf, &overlay_offset);
                vkCmdDraw(r.cb, 6, uint32_t((r.n_lines-n_sketch)/2), 0, 0);
            }
            else vkCmdDraw(r.cb,uint32_t(r.n_lines-n_sketch),1,uint32_t(n_sketch),0);
        }
    }
    if (r.n_points) {
        vkCmdBindPipeline(r.cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_points_);
        vkCmdBindVertexBuffers(r.cb, 0, 1, &r.vb->points.buf, &off);
        vkCmdDraw(r.cb, uint32_t(r.n_points), 1, 0, 0);
    }
}
void Renderer::record_ui(const Recording& r) {
    VkDeviceSize off = 0; (void)off;
    {
        UiLayers all{};
        all.ui[UiLayers::kCount]   = r.n_ui;
        all.tris[UiLayers::kCount] = r.n_flat;
        all.text[UiLayers::kCount] = r.n_text;
        if (!r.f->layers) {
            all.ui[1] = 0; all.tris[1] = 0; all.text[1] = 0;
            for (int k = 2; k < UiLayers::kCount; ++k) {
                all.ui[k] = r.n_ui; all.tris[k] = r.n_flat; all.text[k] = r.n_text;
            }
        }
        const UiLayers& L = r.f->layers ? *r.f->layers : all;
        std::vector<UiLayers::Run> fallback;
        if (L.runs.empty()) {
            for (int k=0; k<UiLayers::kCount; ++k) {
                fallback.push_back({UiLayers::Kind::Surface,L.ui[k],L.ui[k+1]-L.ui[k]});
                fallback.push_back({UiLayers::Kind::Triangle,L.tris[k],L.tris[k+1]-L.tris[k]});
                fallback.push_back({UiLayers::Kind::Text,L.text[k],L.text[k+1]-L.text[k]});
            }
        }
        for (const auto& run : L.runs.empty() ? fallback : L.runs) {
            const auto limit=run.kind==UiLayers::Kind::Surface ? r.n_ui :
                             run.kind==UiLayers::Kind::Triangle ? r.n_flat : r.n_text;
            if (run.first>=limit) continue;
            const auto count=std::min(run.count,limit-run.first);
            if (!count) continue;
            if (run.kind==UiLayers::Kind::Surface) {
                vkCmdBindPipeline(r.cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe_ui_);
                const VkDeviceSize offset=VkDeviceSize(run.first)*sizeof(UiInstance);
                vkCmdBindVertexBuffers(r.cb,0,1,&r.vb->ui.buf,&offset);
                vkCmdDraw(r.cb,4,uint32_t(count),0,0);
            } else if (run.kind==UiLayers::Kind::Triangle) {
                vkCmdBindPipeline(r.cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe_flat_);
                vkCmdBindVertexBuffers(r.cb,0,1,&r.vb->flat.buf,&off);
                vkCmdDraw(r.cb,uint32_t(count),1,uint32_t(run.first),0);
            } else if (atlas_ready_) {
                vkCmdBindPipeline(r.cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipe_text_);
                vkCmdBindVertexBuffers(r.cb,0,1,&r.vb->text.buf,&off);
                vkCmdDraw(r.cb,uint32_t(count),1,uint32_t(run.first),0);
            }
        }
    }
}

void Renderer::build_render_graph() {
    graph_.clear();
    const auto pass_body = [](void (Renderer::*fn)(const Recording&), Renderer* self) {
        return [fn, self](grender::PassContext& c) {
            (self->*fn)(*static_cast<const Recording*>(c.target));
        };
    };

    grender::Pass ground;
    ground.name = "ground";
    ground.writes = {"colour", "depth"};
    ground.execute = pass_body(&Renderer::record_ground, this);
    graph_.add(std::move(ground));

    grender::Pass images;
    images.name = "images";
    images.reads  = {"colour", "depth"};
    images.writes = {"colour"};
    images.execute = pass_body(&Renderer::record_images, this);
    graph_.add(std::move(images));

    grender::Pass solids;
    solids.name = "solids";
    solids.reads  = {"colour", "depth"};
    solids.writes = {"colour", "depth"};
    solids.execute = pass_body(&Renderer::record_solids, this);
    graph_.add(std::move(solids));

    grender::Pass drawing;
    drawing.name = "drawing";
    drawing.reads  = {"colour", "depth"};
    drawing.writes = {"colour", "depth"};
    drawing.execute = pass_body(&Renderer::record_drawing, this);
    graph_.add(std::move(drawing));

    grender::Pass ui;
    ui.name = "ui";
    ui.reads    = {"colour"};
    ui.writes   = {"colour"};
    ui.presents = true;
    ui.execute  = pass_body(&Renderer::record_ui, this);
    graph_.add(std::move(ui));

    plan_ = graph_.compile();
}

void Renderer::draw(const FrameData& f) {
    if (pending_samples_ != samples_) {
        std::string e;
        if (!apply_msaa(e)) pending_samples_ = samples_;
    }
    if (!device_) return;

    if (needs_resize_) {
        vkDeviceWaitIdle(device_);
        destroy_swapchain();
        extent_ = {pending_w_, pending_h_};
        std::string e;
        create_swapchain(e);
        needs_resize_ = false;
    }
    if (!swapchain_ || extent_.width == 0) return;

    const auto n_lines  = f.canvas_lines ? f.canvas_lines->size() : 0;
    const auto n_points = f.canvas_points ? f.canvas_points->size() : 0;
    const auto n_ui     = f.ui ? f.ui->size() : 0;
    const auto n_text   = f.text ? f.text->size() : 0;
    const auto n_mesh   = f.mesh ? f.mesh->size() : 0;
    const auto n_flat   = f.tris ? f.tris->size() : 0;
    const auto n_arcs   = f.canvas_arcs ? f.canvas_arcs->size() : 0;

    const uint32_t fi = frame_ % kFramesInFlight;
    if (vbs_.size() < kFramesInFlight) vbs_.resize(kFramesInFlight);
    FrameBuffers& vb = vbs_[fi];
    vkWaitForFences(device_, 1, &fences_[fi], VK_TRUE, UINT64_MAX);
    ensure(vb.lines,  n_lines  * sizeof(LineVertex));
    ensure(vb.points, n_points * sizeof(LineVertex));
    ensure(vb.ui,     n_ui     * sizeof(UiInstance));
    ensure(vb.text,   n_text   * sizeof(TextVertex));
    ensure(vb.mesh,   n_mesh   * sizeof(MeshVertex));
    ensure(vb.flat,   n_flat   * sizeof(TextVertex));
    ensure(vb.arcs,   n_arcs   * sizeof(ArcInstance));

    const std::size_t n_static = f.static_lines ? f.static_lines->size() : 0;
    const std::size_t n_static_arcs = f.static_arcs ? f.static_arcs->size() : 0;
    if (f.static_version != vb.static_version || n_static != vb.static_count || n_static_arcs != vb.static_arc_count) {
        if (n_static) {
            ensure(vb.static_lines, n_static * sizeof(LineVertex));
            upload(vb.static_lines, f.static_lines->data(), n_static * sizeof(LineVertex));
        }
        if (n_static_arcs) {
            ensure(vb.static_arcs, n_static_arcs * sizeof(ArcInstance));
            upload(vb.static_arcs, f.static_arcs->data(), n_static_arcs * sizeof(ArcInstance));
        }
        vb.static_wide_lines=f.static_lines && std::any_of(f.static_lines->begin(),f.static_lines->end(),[](const auto& v){return v.width_px>1;});
        vb.static_wide_arcs=f.static_arcs && std::any_of(f.static_arcs->begin(),f.static_arcs->end(),[](const auto& a){return a.minor[3]>1;});
        vb.static_version = f.static_version;
        vb.static_count = n_static;
        vb.static_arc_count = n_static_arcs;
    }

    uint32_t idx = 0;
    const VkResult ar = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                              sem_avail_[fi], VK_NULL_HANDLE, &idx);
    if (ar == VK_ERROR_OUT_OF_DATE_KHR) { needs_resize_ = true; return; }
    if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) return;
    vkResetFences(device_, 1, &fences_[fi]);

    if (n_lines)  upload(vb.lines,  f.canvas_lines->data(),  n_lines  * sizeof(LineVertex));
    if (n_points) upload(vb.points, f.canvas_points->data(), n_points * sizeof(LineVertex));
    if (n_ui)     upload(vb.ui,     f.ui->data(),            n_ui     * sizeof(UiInstance));
    if (n_text)   upload(vb.text,   f.text->data(),          n_text   * sizeof(TextVertex));
    if (!f.mesh_version || f.mesh_version != vb.mesh_version || n_mesh != vb.mesh_count) {
        if (n_mesh) upload(vb.mesh, f.mesh->data(), n_mesh * sizeof(MeshVertex));
        vb.mesh_version = f.mesh_version;
        vb.mesh_count = n_mesh;
    }
    if (n_flat)   upload(vb.flat,   f.tris->data(),          n_flat   * sizeof(TextVertex));
    if (n_arcs)   upload(vb.arcs,   f.canvas_arcs->data(),   n_arcs   * sizeof(ArcInstance));

    std::vector<TextVertex> image_vertices;
    std::vector<ImageBatch> image_batches;
    if(std::any_of(image_textures_.begin(),image_textures_.end(),[&](const auto& t){
        return !f.images||std::none_of(f.images->begin(),f.images->end(),[&](const auto& im){return im.image==t.source;});
    })) {
        vkDeviceWaitIdle(device_);
        for(auto it=image_textures_.begin();it!=image_textures_.end();) {
            if(!f.images||std::none_of(f.images->begin(),f.images->end(),[&](const auto& im){return im.image==it->source;})){destroy_image(*it);it=image_textures_.erase(it);}else ++it;
        }
    }
    image_textures_.reserve(image_textures_.size()+(f.images?f.images->size():0));
    if(f.images)for(const auto& im:*f.images) {
        if(!im.image||!im.image->valid()||im.vertices.empty())continue;
        auto it=std::find_if(image_textures_.begin(),image_textures_.end(),[&](const auto& t){return t.source==im.image;});
        if(it==image_textures_.end()) {
            ImageTexture t;t.source=im.image;
            if(!upload_image(t)){destroy_image(t);continue;}
            image_textures_.push_back(std::move(t));it=std::prev(image_textures_.end());
        }
        image_batches.push_back({&*it,image_vertices.size(),im.vertices.size()});
        image_vertices.insert(image_vertices.end(),im.vertices.begin(),im.vertices.end());
    }
    if (!image_vertices.empty()) {
        ensure(vb.images, image_vertices.size() * sizeof(TextVertex));
        upload(vb.images, image_vertices.data(), image_vertices.size() * sizeof(TextVertex));
    }

    VkCommandBuffer cb = cmds_[fi];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    FrameUniforms uni = f.uniforms;
    uni.viewport[0] = float(extent_.width);
    uni.viewport[1] = float(extent_.height);
    upload(ubos_[fi], &uni, sizeof(uni));

    VkClearValue clear[2]{};
    clear[0].color = {{uni.background[0], uni.background[1], uni.background[2],
                       uni.background[3]}};
    clear[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass      = pass_;
    rb.framebuffer     = fbs_[idx];
    rb.renderArea      = {{0, 0}, extent_};
    rb.clearValueCount = 2;
    rb.pClearValues    = clear;
    vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{0, 0, float(extent_.width), float(extent_.height), 0, 1};
    VkRect2D sc{{0, 0}, extent_};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);

    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1,
                            &dsets_[fi], 0, nullptr);

    if (graph_.size() == 0) build_render_graph();
    Recording rec;
    rec.cb = cb;
    rec.f = &f;
    rec.vb = &vb;
    rec.fi = fi;
    rec.n_lines = n_lines; rec.n_points = n_points; rec.n_ui = n_ui;
    rec.n_text = n_text;   rec.n_mesh = n_mesh;     rec.n_flat = n_flat;
    rec.n_arcs = n_arcs;   rec.n_static = n_static;
    rec.images = &image_batches;
    rec.preview_first = f.preview_first;
    rec.preview_count = f.preview_count;
    rec.preview_gpu = f.preview_gpu;
    grender::PassContext ctx;
    ctx.target = &rec;
    graph_.execute(ctx, plan_);

    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);

    VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount   = 1;
    si.pWaitSemaphores      = &sem_avail_[fi];
    si.pWaitDstStageMask    = &wait;
    si.commandBufferCount   = 1;
    si.pCommandBuffers      = &cb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores    = &sem_done_[idx];
    vkQueueSubmit(queue_, 1, &si, fences_[fi]);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &sem_done_[idx];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &swapchain_;
    pi.pImageIndices      = &idx;
    const VkResult pr = vkQueuePresentKHR(queue_, &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) needs_resize_ = true;

    ++frame_;
}

void Renderer::shutdown() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    boolean_gpu_.destroy();

    for (auto& vb : vbs_) {
        destroy(vb.lines); destroy(vb.points); destroy(vb.ui);
        destroy(vb.text); destroy(vb.mesh); destroy(vb.flat); destroy(vb.arcs); destroy(vb.images);
        destroy(vb.static_lines); destroy(vb.static_arcs);
    }
    vbs_.clear();
    for(auto& image:image_textures_)destroy_image(image);
    image_textures_.clear();
    for (auto& u : ubos_) destroy(u);

    if (atlas_view_) vkDestroyImageView(device_, atlas_view_, nullptr);
    if (atlas_img_) vkDestroyImage(device_, atlas_img_, nullptr);
    if (atlas_mem_) vkFreeMemory(device_, atlas_mem_, nullptr);
    if (sampler_) vkDestroySampler(device_, sampler_, nullptr);
    if (dpool_) vkDestroyDescriptorPool(device_, dpool_, nullptr);
    if (dset_layout_) vkDestroyDescriptorSetLayout(device_, dset_layout_, nullptr);

    for (int i = 0; i < kFramesInFlight; ++i) {
        vkDestroySemaphore(device_, sem_avail_[i], nullptr);
        vkDestroyFence(device_, fences_[i], nullptr);
    }
    vkDestroyCommandPool(device_, cmd_pool_, nullptr);
    for (VkPipeline p : {pipe_thin_lines_, pipe_thin_depth_, pipe_thin_arcs_, pipe_thin_arcs_depth_, pipe_canvas_, pipe_lines_, pipe_lines_depth_, pipe_points_, pipe_ui_,
                         pipe_text_, pipe_mesh_, pipe_mesh_overlay_, pipe_flat_, pipe_image_, pipe_arcs_, pipe_arcs_depth_})
        if (p) vkDestroyPipeline(device_, p, nullptr);
    for (VkPipelineLayout l : {layout_})
        if (l) vkDestroyPipelineLayout(device_, l, nullptr);

    destroy_swapchain();
    vkDestroyRenderPass(device_, pass_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
    vkDestroyInstance(instance_, nullptr);
    device_ = VK_NULL_HANDLE;
}

}

namespace gcapp {
bool Renderer::upload_image(ImageTexture& texture) {
    const auto& pixels=texture.source->rgba;const int width=texture.source->width,height=texture.source->height;
    VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,kFramesInFlight},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,kFramesInFlight}};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=kFramesInFlight;pool.poolSizeCount=2;pool.pPoolSizes=sizes;
    if(vkCreateDescriptorPool(device_,&pool,nullptr,&texture.pool)!=VK_SUCCESS)return false;
    std::vector<VkDescriptorSetLayout> layouts(kFramesInFlight,dset_layout_);texture.sets.resize(kFramesInFlight);
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};alloc.descriptorPool=texture.pool;alloc.descriptorSetCount=kFramesInFlight;alloc.pSetLayouts=layouts.data();
    if(vkAllocateDescriptorSets(device_,&alloc,texture.sets.data())!=VK_SUCCESS)return false;
    for(int i=0;i<kFramesInFlight;++i){VkDescriptorBufferInfo bi{ubos_[i].buf,0,sizeof(FrameUniforms)};VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};w.dstSet=texture.sets[i];w.dstBinding=1;w.descriptorCount=1;w.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&bi;vkUpdateDescriptorSets(device_,1,&w,0,nullptr);}
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = VK_FORMAT_R8G8B8A8_UNORM;
    ii.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ii, nullptr, &texture.image) != VK_SUCCESS) {
        return false;
    }

    VkMemoryRequirements mr{};
    vkGetImageMemoryRequirements(device_, texture.image, &mr);
    VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex=find_mem(phys_,mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if(vkAllocateMemory(device_,&ma,nullptr,&texture.memory)!=VK_SUCCESS)return false;
    if(vkBindImageMemory(device_,texture.image,texture.memory,0)!=VK_SUCCESS)return false;
    Buffer staging;ensure(staging,pixels.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if(!staging.buf||!staging.mem){destroy(staging);return false;}
    upload(staging,pixels.data(),pixels.size());

    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = texture.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = VK_FORMAT_R8G8B8A8_UNORM;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if(vkCreateImageView(device_, &vi, nullptr, &texture.view)!=VK_SUCCESS){destroy(staging);return false;}

    {
        VkCommandBufferAllocateInfo ai{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = cmd_pool_;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb{};
        if(vkAllocateCommandBuffers(device_, &ai, &cb)!=VK_SUCCESS){destroy(staging);return false;}

        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(vkBeginCommandBuffer(cb, &bi)!=VK_SUCCESS){vkFreeCommandBuffers(device_,cmd_pool_,1,&cb);destroy(staging);return false;}

        VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.image = texture.image;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        bar.srcAccessMask = 0;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
                             0, nullptr, 1, &bar);
        VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={uint32_t(width),uint32_t(height),1};
        vkCmdCopyBufferToImage(cb,staging.buf,texture.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
        bar.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;bar.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;bar.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&bar);
        if(vkEndCommandBuffer(cb)!=VK_SUCCESS){vkFreeCommandBuffers(device_,cmd_pool_,1,&cb);destroy(staging);return false;}

        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        if(vkQueueSubmit(queue_,1,&si,VK_NULL_HANDLE)!=VK_SUCCESS){vkFreeCommandBuffers(device_,cmd_pool_,1,&cb);destroy(staging);return false;}
        vkQueueWaitIdle(queue_);
        vkFreeCommandBuffers(device_, cmd_pool_, 1, &cb);
    }

    destroy(staging);
    VkDescriptorImageInfo di{sampler_, texture.view,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    for (VkDescriptorSet set : texture.sets) {
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &di;
        vkUpdateDescriptorSets(device_, 1, &w, 0, nullptr);
    }

    return true;
}
void Renderer::destroy_image(ImageTexture& t) {
    if(t.pool)vkDestroyDescriptorPool(device_,t.pool,nullptr);
    if(t.view)vkDestroyImageView(device_,t.view,nullptr);
    if(t.image)vkDestroyImage(device_,t.image,nullptr);
    if(t.memory)vkFreeMemory(device_,t.memory,nullptr);
    t={};
}
}
