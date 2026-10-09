#pragma once
#include <string>
#include <cstdint>
#include <vector>
// iPadOS: static MoltenVK, no volk (volk's globals collide with its symbols).
#if defined(GLCAD_STATIC_MOLTENVK)
#include <vulkan/vulkan.h>
#else
#include <volk.h>
#endif

#include "render/render_graph.hpp"
#include "ui/frame_types.hpp"
#include "ui/boolean_gpu.hpp"

namespace gcapp {

class Renderer {
public:
    void set_msaa(uint32_t samples) noexcept;
    uint32_t msaa() const noexcept { return samples_; }

    struct SurfaceHandle {
        void* metal_layer{nullptr};
        void*         xlib_display{nullptr};
        unsigned long xlib_window{0};
        void* win32_instance{nullptr};
        void* win32_window{nullptr};
    };

    bool init(const SurfaceHandle& surface, uint32_t w, uint32_t h,
              std::string& err);

    bool set_font_atlas(const std::vector<uint8_t>& pixels, int width, int height,
                        std::string& err);

    void resize(uint32_t w, uint32_t h);
    void draw(const FrameData& frame);
    void shutdown();

    uint32_t width() const { return extent_.width; }
    uint32_t height() const { return extent_.height; }

private:
    bool create_swapchain(std::string& err);
    void destroy_swapchain();
    bool create_depth(std::string& err);
    bool create_pipelines(std::string& err);
    bool create_descriptors(std::string& err);

    struct Buffer {
        VkBuffer       buf{};
        VkDeviceMemory mem{};
        size_t         capacity{0};
        void*          mapped{nullptr};
    };
    void ensure(Buffer& b, size_t bytes,
                VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    void upload(Buffer& b, const void* data, size_t bytes);
    void destroy(Buffer& b);

    struct ImageTexture {
        std::shared_ptr<const gplot::RasterImage> source;
        VkImage image{};VkDeviceMemory memory{};VkImageView view{};VkDescriptorPool pool{};
        std::vector<VkDescriptorSet> sets;
    };
    std::vector<ImageTexture> image_textures_;
    bool upload_image(ImageTexture& texture);
    void destroy_image(ImageTexture& texture);
    bool create_msaa_target(std::string& err);
    void destroy_msaa_target();
    bool apply_msaa(std::string& err);

    VkInstance       instance_{};
    VkPhysicalDevice phys_{};
    VkDevice         device_{};
    VkQueue          queue_{};
    uint32_t         qfam_{0};
    VkSurfaceKHR     surface_{};
    VkSwapchainKHR   swapchain_{};
    VkFormat         format_{VK_FORMAT_B8G8R8A8_UNORM};
    VkExtent2D       extent_{};

    std::vector<VkImage>       images_;
    std::vector<VkImageView>   views_;
    std::vector<VkFramebuffer> fbs_;
    VkRenderPass               pass_{};

    VkImage        depth_img_{};
    VkDeviceMemory depth_mem_{};
    VkImageView    depth_view_{};
    VkImage        msaa_img_{};
    VkDeviceMemory msaa_mem_{};
    VkImageView    msaa_view_{};
    uint32_t       samples_{1};
    uint32_t       pending_samples_{1};
    VkFormat       depth_format_{VK_FORMAT_D32_SFLOAT};

    VkPipelineLayout layout_{};
    VkPipeline       pipe_thin_lines_{}, pipe_thin_depth_{}, pipe_thin_arcs_{}, pipe_thin_arcs_depth_{};
    VkPipeline       pipe_canvas_{}, pipe_lines_{}, pipe_lines_depth_{}, pipe_points_{};
    VkPipeline       pipe_ui_{}, pipe_text_{}, pipe_mesh_{}, pipe_mesh_overlay_{}, pipe_flat_{}, pipe_image_{};
    VkPipeline       pipe_arcs_{}, pipe_arcs_depth_{};
    gbool::BooleanGpu boolean_gpu_;

    VkDescriptorSetLayout dset_layout_{};
    VkDescriptorPool      dpool_{};
    std::vector<VkDescriptorSet> dsets_;
    std::vector<Buffer>          ubos_;
    VkSampler             sampler_{};
    VkImage               atlas_img_{};
    VkDeviceMemory        atlas_mem_{};
    VkImageView           atlas_view_{};
    bool                  atlas_ready_{false};

    VkCommandPool                cmd_pool_{};
    std::vector<VkCommandBuffer> cmds_;
    std::vector<VkSemaphore>     sem_avail_;  // per frame slot
    std::vector<VkSemaphore>     sem_done_;   // per swapchain image
    std::vector<VkFence>         fences_;
    uint32_t                     frame_{0};

    struct FrameBuffers {
        Buffer lines, points, ui, text, mesh, flat, arcs, images;
        Buffer static_lines, static_arcs;
        std::uint64_t static_version{0}, mesh_version{0};
        std::size_t mesh_count{0};
        std::size_t static_count{0}, static_arc_count{0};
        bool static_wide_lines{false}, static_wide_arcs{false};
    };

    struct ImageBatch { ImageTexture* texture{nullptr}; std::size_t first{0}, count{0}; };

    struct Recording {
        VkCommandBuffer cb{VK_NULL_HANDLE};
        const FrameData* f{nullptr};
        FrameBuffers*    vb{nullptr};
        uint32_t         fi{0};
        std::size_t n_lines{0}, n_points{0}, n_ui{0}, n_text{0}, n_mesh{0},
                    n_flat{0}, n_arcs{0}, n_static{0};
        const std::vector<ImageBatch>* images{nullptr};
        std::size_t preview_first{0}, preview_count{0};
        const gbool::PreviewState* preview_gpu{nullptr};
    };

    void build_render_graph();
    void record_ground(const Recording& r);
    void record_images(const Recording& r);
    void record_solids(const Recording& r);
    void record_drawing(const Recording& r);
    void record_ui(const Recording& r);

    grender::RenderGraph   graph_;
    grender::CompileResult plan_;

    std::vector<FrameBuffers> vbs_;

    bool     needs_resize_{false};
    uint32_t pending_w_{0}, pending_h_{0};
};

}
