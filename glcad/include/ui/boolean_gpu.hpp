#pragma once

#include <string>
#include <vector>
#if defined(GLCAD_STATIC_MOLTENVK)
#include <vulkan/vulkan.h>
#else
#include <volk.h>
#endif
// X11 (via vulkan.h on Linux) defines True/None as macros; undo them here.
#ifdef __linux__
#  undef True
#  undef None
#endif
#include "ui/boolean_preview.hpp"

namespace gcapp::gbool {
class BooleanGpu {
public:
    struct Deps {
        VkDevice device{};
        VkPhysicalDevice phys{};
        VkRenderPass pass{};
        VkDescriptorSetLayout frame_layout{};
        VkShaderModule vs{}, fs{};
        uint32_t samples{1}, frame_count{};
    };
    bool build(const Deps&, std::string& err);
    void destroy();
    bool ready() const noexcept { return pipeline_ != VK_NULL_HANDLE; }
    bool record(VkCommandBuffer, uint32_t frame, VkDescriptorSet frame_set,
                const PreviewState&);
    struct Statistics { uint64_t preparations{}, uploads{}, uploaded_bytes{}; };
    Statistics statistics() const noexcept { return statistics_; }

private:
    struct Triangle { float a[4], edge1[4], edge2[4], normal[4], color[4]; };
    struct Node { float lo[4], hi[4]; uint32_t first, count, escape, pad; };
    struct Params { uint32_t nodes, triangles, operation, pad; float origin_epsilon[4], result_lo[4], result_hi[4]; };
    static_assert(sizeof(Triangle)==80 && sizeof(Node)==48 && sizeof(Params)==64);
    struct Buffer { VkBuffer buf{}; VkDeviceMemory memory{}; VkDeviceSize capacity{}; };
    struct Frame { Buffer triangles, nodes; VkDescriptorSet set{}; uint64_t revision{}; };
    bool upload(Buffer&, const void*, VkDeviceSize);
    void free(Buffer&);
    VkDevice device_{};
    VkPhysicalDevice phys_{};
    VkDeviceSize max_storage_{};
    VkPipeline pipeline_{};
    VkPipelineLayout layout_{};
    VkDescriptorSetLayout descriptor_layout_{};
    VkDescriptorPool pool_{};
    std::vector<Frame> frames_;
    PreviewState prepared_input_;
    std::vector<Triangle> prepared_triangles_;
    std::vector<Node> prepared_nodes_;
    Params prepared_params_{};
    uint64_t revision_{};
    Statistics statistics_{};
};
}
