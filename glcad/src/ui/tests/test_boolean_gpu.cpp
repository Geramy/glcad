#include "ui/boolean_gpu.hpp"
#include "geometry/extrude.hpp"
#include "geometry/triangulate.hpp"
#include "render/camera.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdlib>
#include <chrono>

using namespace gcapp;
namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void vkcheck_at(VkResult result, int line) {
    if (result != VK_SUCCESS) throw std::runtime_error("Vulkan result " + std::to_string(result) + " at line " + std::to_string(line));
}
#define vkcheck(result) vkcheck_at((result), __LINE__)
struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; };
struct Image { VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; };
struct Fixture {
    VkInstance instance{}; VkPhysicalDevice physical{}; VkDevice device{}; VkQueue queue{};
    VkDescriptorSetLayout descriptor{}; VkDescriptorPool descriptors{}; VkDescriptorSet set{};
    VkRenderPass pass{}; VkFramebuffer framebuffer{}; VkCommandPool commands{}; VkCommandBuffer cb{};
    Image color, depth;
    Buffer uniform, readback;
    std::vector<unsigned char> colors;
    gbool::BooleanGpu preview;
    static constexpr uint32_t width = 64;
    uint32_t extent;
    VkQueryPool timestamps{};
    double timestamp_period{}, gpu_ms{}, record_ms{};
    void set_uniforms(const FrameUniforms& u) {
        void* data{}; vkcheck(vkMapMemory(device,uniform.memory,0,sizeof(u),0,&data));
        std::memcpy(data,&u,sizeof(u)); vkUnmapMemory(device,uniform.memory);
    }
    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags) {
        VkPhysicalDeviceMemoryProperties p{}; vkGetPhysicalDeviceMemoryProperties(physical,&p);
        for (uint32_t i=0;i<p.memoryTypeCount;++i)
            if ((bits&(1u<<i)) && (p.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw std::runtime_error("no suitable memory type");
    }
    Buffer buffer(VkDeviceSize bytes, VkBufferUsageFlags usage) {
        Buffer b;
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size=bytes; ci.usage=usage;
        vkcheck(vkCreateBuffer(device,&ci,nullptr,&b.buffer));
        VkMemoryRequirements req{}; vkGetBufferMemoryRequirements(device,b.buffer,&req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=req.size;
        ai.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        vkcheck(vkAllocateMemory(device,&ai,nullptr,&b.memory)); vkcheck(vkBindBufferMemory(device,b.buffer,b.memory,0)); return b;
    }
    Image image(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
        Image out;
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ci.imageType=VK_IMAGE_TYPE_2D;
        ci.format=format; ci.extent={extent,extent,1}; ci.mipLevels=ci.arrayLayers=1;
        ci.samples=VK_SAMPLE_COUNT_1_BIT; ci.tiling=VK_IMAGE_TILING_OPTIMAL; ci.usage=usage;
        vkcheck(vkCreateImage(device,&ci,nullptr,&out.image));
        VkMemoryRequirements req{}; vkGetImageMemoryRequirements(device,out.image,&req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize=req.size;
        ai.memoryTypeIndex=memory_type(req.memoryTypeBits,0);
        vkcheck(vkAllocateMemory(device,&ai,nullptr,&out.memory)); vkcheck(vkBindImageMemory(device,out.image,out.memory,0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image=out.image;
        vi.viewType=VK_IMAGE_VIEW_TYPE_2D; vi.format=format; vi.subresourceRange={aspect,0,1,0,1};
        vkcheck(vkCreateImageView(device,&vi,nullptr,&out.view)); return out;
    }
    VkShaderModule shader(const char* name) {
        std::ifstream file(std::string(GLCAD_SHADER_DIR)+"/"+name+".spv",std::ios::binary|std::ios::ate);
        check(bool(file),"shader missing"); auto bytes=file.tellg(); file.seekg(0);
        std::vector<uint32_t> code((std::size_t(bytes)+3)/4); file.read(reinterpret_cast<char*>(code.data()),bytes);
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize=std::size_t(bytes); ci.pCode=code.data();
        VkShaderModule module{}; vkcheck(vkCreateShaderModule(device,&ci,nullptr,&module)); return module;
    }
    explicit Fixture(uint32_t dimension = width, bool timing = false) : extent(dimension) {
        vkcheck(volkInitialize());
        uint32_t count=0; vkcheck(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr));
        std::vector<VkExtensionProperties> extensions(count); vkcheck(vkEnumerateInstanceExtensionProperties(nullptr,&count,extensions.data()));
        std::vector<const char*> enabled;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        for (const auto& e:extensions) if (!std::strcmp(e.extensionName,VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            enabled.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME); ci.flags|=VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_1; ci.pApplicationInfo=&app;
        ci.enabledExtensionCount=uint32_t(enabled.size()); ci.ppEnabledExtensionNames=enabled.data();
        vkcheck(vkCreateInstance(&ci,nullptr,&instance)); volkLoadInstance(instance);
        vkcheck(vkEnumeratePhysicalDevices(instance,&count,nullptr)); check(count>0,"no Vulkan device");
        std::vector<VkPhysicalDevice> devices(count); vkcheck(vkEnumeratePhysicalDevices(instance,&count,devices.data())); physical=devices[0];
        vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
        std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
        uint32_t family=0; while (family<count && !(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) ++family;
        check(family<count,"no graphics queue");
        vkcheck(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr)); extensions.resize(count);
        vkcheck(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,extensions.data())); enabled.clear();
        for (const auto& e:extensions) if (!std::strcmp(e.extensionName,"VK_KHR_portability_subset")) enabled.push_back("VK_KHR_portability_subset");
        float priority=1; VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qi.queueFamilyIndex=family; qi.queueCount=1; qi.pQueuePriorities=&priority;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.queueCreateInfoCount=1; di.pQueueCreateInfos=&qi;
        di.enabledExtensionCount=uint32_t(enabled.size()); di.ppEnabledExtensionNames=enabled.data();
        vkcheck(vkCreateDevice(physical,&di,nullptr,&device)); volkLoadDevice(device); vkGetDeviceQueue(device,family,0,&queue);
        if (timing && families[family].timestampValidBits == 64) {
            VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(physical,&properties);
            timestamp_period=properties.limits.timestampPeriod;
            VkQueryPoolCreateInfo queries{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            queries.queryType=VK_QUERY_TYPE_TIMESTAMP; queries.queryCount=2;
            vkcheck(vkCreateQueryPool(device,&queries,nullptr,&timestamps));
        }
        color=image(VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        depth=image(VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        uniform=buffer(sizeof(FrameUniforms),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        readback=buffer(extent*extent*(sizeof(float)+4),VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        FrameUniforms u{};
        for (int i=0;i<4;++i) u.view_proj[i*5]=u.inv_view_proj[i*5]=1;
        u.eye[2]=-2; u.viewport[0]=u.viewport[1]=extent;
        void* data{}; vkcheck(vkMapMemory(device,uniform.memory,0,sizeof(u),0,&data)); std::memcpy(data,&u,sizeof(u)); vkUnmapMemory(device,uniform.memory);
        VkDescriptorSetLayoutBinding binding{1,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; li.bindingCount=1; li.pBindings=&binding;
        vkcheck(vkCreateDescriptorSetLayout(device,&li,nullptr,&descriptor));
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1};
        VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets=1; dpi.poolSizeCount=1; dpi.pPoolSizes=&size;
        vkcheck(vkCreateDescriptorPool(device,&dpi,nullptr,&descriptors));
        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; dai.descriptorPool=descriptors; dai.descriptorSetCount=1; dai.pSetLayouts=&descriptor;
        vkcheck(vkAllocateDescriptorSets(device,&dai,&set));
        VkDescriptorBufferInfo dbi{uniform.buffer,0,sizeof(u)}; VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet=set; write.dstBinding=1; write.descriptorCount=1; write.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; write.pBufferInfo=&dbi;
        vkUpdateDescriptorSets(device,1,&write,0,nullptr);
        VkAttachmentDescription attachments[2]{};
        for (auto& a:attachments) { a.samples=VK_SAMPLE_COUNT_1_BIT; a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR; a.storeOp=VK_ATTACHMENT_STORE_OP_STORE; }
        attachments[0].format=VK_FORMAT_R8G8B8A8_UNORM; attachments[0].finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        attachments[1].format=VK_FORMAT_D32_SFLOAT; attachments[1].finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference color_ref{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, depth_ref{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{}; sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount=1; sub.pColorAttachments=&color_ref; sub.pDepthStencilAttachment=&depth_ref;
        VkSubpassDependency dependency{}; dependency.srcSubpass=0; dependency.dstSubpass=VK_SUBPASS_EXTERNAL;
        dependency.srcStageMask=VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT; dependency.srcAccessMask=VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; dependency.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; rp.attachmentCount=2; rp.pAttachments=attachments; rp.subpassCount=1; rp.pSubpasses=&sub; rp.dependencyCount=1; rp.pDependencies=&dependency;
        vkcheck(vkCreateRenderPass(device,&rp,nullptr,&pass));
        VkImageView views[]={color.view,depth.view}; VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fb.renderPass=pass; fb.attachmentCount=2; fb.pAttachments=views; fb.width=fb.height=extent; fb.layers=1;
        vkcheck(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer));
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cp.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; cp.queueFamilyIndex=family;
        vkcheck(vkCreateCommandPool(device,&cp,nullptr,&commands));
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ca.commandPool=commands; ca.commandBufferCount=1; ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        vkcheck(vkAllocateCommandBuffers(device,&ca,&cb));
        const auto vs=shader("canvas.vert"), fs=shader("bool_preview.frag"); std::string error;
        check(preview.build({device,physical,pass,descriptor,vs,fs,1,2},error),error.c_str());
        vkDestroyShaderModule(device,vs,nullptr); vkDestroyShaderModule(device,fs,nullptr);
    }
    std::vector<float> render(const gbool::PreviewState& state, uint32_t slot=0) {
        vkcheck(vkResetCommandBuffer(cb,0)); VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkcheck(vkBeginCommandBuffer(cb,&begin));
        if (timestamps) {
            vkCmdResetQueryPool(cb,timestamps,0,2);
            vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,timestamps,0);
        }
        VkClearValue clear[2]{}; clear[1].depthStencil.depth=1;
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rp.renderPass=pass; rp.framebuffer=framebuffer;
        rp.renderArea.extent={extent,extent}; rp.clearValueCount=2; rp.pClearValues=clear; vkCmdBeginRenderPass(cb,&rp,VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0,0,float(extent),float(extent),0,1}; VkRect2D scissor{{0,0},{extent,extent}};
        vkCmdSetViewport(cb,0,1,&viewport); vkCmdSetScissor(cb,0,1,&scissor);
        const auto record_start=std::chrono::steady_clock::now();
        check(preview.record(cb,slot,set,state),"preview declined valid input");
        record_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-record_start).count();
        vkCmdEndRenderPass(cb);
        if (timestamps) vkCmdWriteTimestamp(cb,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timestamps,1);
        VkBufferImageCopy copy{}; copy.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1}; copy.imageExtent={extent,extent,1};
        vkCmdCopyImageToBuffer(cb,depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);
        copy.bufferOffset=extent*extent*sizeof(float); copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
        vkCmdCopyImageToBuffer(cb,color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        vkcheck(vkEndCommandBuffer(cb)); VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO}; submit.commandBufferCount=1; submit.pCommandBuffers=&cb;
        vkcheck(vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE)); vkcheck(vkQueueWaitIdle(queue));
        if (timestamps) {
            uint64_t ticks[2]{};
            vkcheck(vkGetQueryPoolResults(device,timestamps,0,2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WAIT_BIT));
            gpu_ms=double(ticks[1]-ticks[0])*timestamp_period/1e6;
        }
        std::vector<float> result(extent*extent); colors.resize(extent*extent*4);
        void* data{}; vkcheck(vkMapMemory(device,readback.memory,0,result.size()*sizeof(float)+colors.size(),0,&data));
        std::memcpy(result.data(),data,result.size()*sizeof(float));
        std::memcpy(colors.data(),static_cast<unsigned char*>(data)+result.size()*sizeof(float),colors.size());
        vkUnmapMemory(device,readback.memory); return result;
    }
    ~Fixture() {
        if (!device) return;
        vkDeviceWaitIdle(device); preview.destroy();
        if (timestamps) vkDestroyQueryPool(device,timestamps,nullptr);
        vkDestroyCommandPool(device,commands,nullptr); vkDestroyFramebuffer(device,framebuffer,nullptr); vkDestroyRenderPass(device,pass,nullptr);
        vkDestroyDescriptorPool(device,descriptors,nullptr); vkDestroyDescriptorSetLayout(device,descriptor,nullptr);
        for (auto i:{color,depth}) { vkDestroyImageView(device,i.view,nullptr); vkDestroyImage(device,i.image,nullptr); vkFreeMemory(device,i.memory,nullptr); }
        for (auto b:{uniform,readback}) { vkDestroyBuffer(device,b.buffer,nullptr); vkFreeMemory(device,b.memory,nullptr); }
        vkDestroyDevice(device,nullptr); vkDestroyInstance(instance,nullptr);
    }
};
std::vector<MeshVertex> box(float x0,float y0,float z0,float x1,float y1,float z1) {
    std::vector<MeshVertex> out;
    std::array<std::array<float,3>,8> points{{{x0,y0,z0},{x1,y0,z0},{x1,y1,z0},{x0,y1,z0},
                                            {x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}}};
    const int faces[6][4]={{0,3,2,1},{4,5,6,7},{0,1,5,4},{3,7,6,2},{0,4,7,3},{1,2,6,5}};
    const float normals[6][3]={{0,0,-1},{0,0,1},{0,-1,0},{0,1,0},{-1,0,0},{1,0,0}};
    for (int f=0;f<6;++f) for (int k:{0,1,2,0,2,3}) {
        MeshVertex v{}; std::copy(points[faces[f][k]].begin(),points[faces[f][k]].end(),v.pos);
        std::copy(normals[f],normals[f]+3,v.normal); for (auto& c:v.color) c=1; out.push_back(v);
    }
    return out;
}
std::vector<MeshVertex> ring(int sides=80) {
    gcm::WorkingVolume volume; volume.extent=10; volume.resolution_bits=32;
    gtopo::Profile profile;
    profile.holes.resize(1);
    for (int i=0;i<sides;++i) {
        const double angle=2*std::acos(-1.0)*i/sides;
        profile.outer.push_back(gcm::to_lattice(volume,.6*std::cos(angle),.6*std::sin(angle)));
        profile.holes[0].push_back(gcm::to_lattice(volume,.2*std::cos(angle),.2*std::sin(angle)));
    }
    profile.normalize();
    auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();
    frame.origin=ggeo::d3::Point<gcm::F64>::from(0,0,.1);
    const auto body=gtopo::extrude(profile,volume,frame,.4);
    const auto mesh=gtopo::triangulate(body,volume);
    std::vector<MeshVertex> out;
    for (std::size_t i=0;i<mesh.indices.size();++i) {
        double x,y,z; gcm::to_world(volume,body.vertex_point[mesh.indices[i]],x,y,z);
        const auto& n=mesh.normals[i/3]; MeshVertex v{};
        v.pos[0]=float(x); v.pos[1]=float(y); v.pos[2]=float(z);
        v.normal[0]=float(n.x.to_f64()); v.normal[1]=float(n.y.to_f64()); v.normal[2]=float(n.z.to_f64());
        for (auto& c:v.color) c=1; out.push_back(v);
    }
    return out;
}
void expect(const std::vector<float>& pixels, int x, int y, float depth, const char* name) {
    if (std::fabs(pixels[y*Fixture::width+x]-depth)>2e-5f) {
        std::fprintf(stderr,"%s at (%d,%d): got %.8f, expected %.8f\n",name,x,y,pixels[y*Fixture::width+x],depth);
        throw std::runtime_error("incorrect GPU depth");
    }
}

void circular_reversal() {
    Fixture fixture(192);
    gcm::WorkingVolume volume; volume.extent=500; volume.resolution_bits=32;
    gtopo::Profile profile;
    for (int i=0;i<80;++i) {
        const double angle=2*std::acos(-1.0)*i/80;
        profile.outer.push_back(gcm::to_lattice(volume,std::cos(angle),std::sin(angle)));
    }
    profile.normalize();
    const auto cylinder=[&](double base,double height) {
        auto frame=ggeo::d3::Frame<gcm::F64>::world_xy();
        frame.origin=ggeo::d3::Point<gcm::F64>::from(0,0,base);
        const auto body=gtopo::extrude(profile,volume,frame,height);
        const auto mesh=gtopo::triangulate(body,volume);
        std::vector<MeshVertex> out;
        for (std::size_t i=0;i<mesh.indices.size();++i) {
            double x,y,z; gcm::to_world(volume,body.vertex_point[mesh.indices[i]],x,y,z);
            MeshVertex v{}; v.pos[0]=float(x); v.pos[1]=float(y); v.pos[2]=float(z);
            const auto& n=mesh.normals[i/3];
            v.normal[0]=float(n.x.to_f64()); v.normal[1]=float(n.y.to_f64()); v.normal[2]=float(n.z.to_f64());
            for (auto& c:v.color) c=1;
            out.push_back(v);
        }
        return out;
    };
    gview::Camera camera; camera.set_viewport(fixture.extent,fixture.extent);
    camera.set_target(gview::Point3::from(0,0,1)); camera.set_distance(6);
    camera.look(gview::StandardView::Iso);
    for (auto projection:{gview::Projection::Orthographic,gview::Projection::Perspective}) {
        camera.set_projection(projection);
        FrameUniforms u{}; const auto vp=camera.view_projection();
        vp.to_f32(u.view_proj); vp.inverse().to_f32(u.inv_view_proj);
        const auto eye=camera.eye();
        u.eye[0]=float(eye.x.to_f64()); u.eye[1]=float(eye.y.to_f64()); u.eye[2]=float(eye.z.to_f64());
        u.viewport[0]=u.viewport[1]=fixture.extent; fixture.set_uniforms(u);
        uint32_t slot=0;
        for (double amount:{.731,-.519,.283,-1.137,.0001,-.0001,.731,-.519}) {
            gbool::PreviewState actual,reference;
            actual.body=cylinder(0,2);
            actual.tool=cylinder(std::min(2.0,2+amount),std::fabs(amount));
            actual.operation=amount<0 ? gbool::Operation::Subtract : gbool::Operation::Union;
            reference.tool=cylinder(0,2+amount);
            const auto expected=fixture.render(reference,slot);
            const auto pixels=fixture.render(actual,slot^1u);
            slot ^= 1u;
            int errors=0;
            for (std::size_t i=0;i<pixels.size();++i)
                if (std::fabs(pixels[i]-expected[i])>(projection==gview::Projection::Orthographic ? 1.2e-7f : 5e-7f)) { if (errors<12) std::fprintf(stderr,"  pixel %zu,%zu: %.9g vs %.9g\n",i%fixture.extent,i/fixture.extent,pixels[i],expected[i]); ++errors; }
            if (errors) {
                std::fprintf(stderr,"circular reversal amount %.3f projection %d: %d incorrect pixels\n",amount,int(projection),errors);
                throw std::runtime_error("reversing a circular extrusion left incorrect surfaces");
            }
        }
    }
}

void side_face_cut(Fixture& fixture, gview::Projection projection) {
    gview::Camera camera;
    camera.set_viewport(Fixture::width,Fixture::width);
    camera.set_target(gview::Point3::from(1,0,1.043));
    camera.set_distance(4);
    camera.look(gview::StandardView::Iso);
    camera.set_projection(projection);
    FrameUniforms u{};
    const auto vp=camera.view_projection();
    vp.to_f32(u.view_proj); vp.inverse().to_f32(u.inv_view_proj);
    const auto eye=camera.eye();
    u.eye[0]=float(eye.x.to_f64()); u.eye[1]=float(eye.y.to_f64()); u.eye[2]=float(eye.z.to_f64());
    u.viewport[0]=u.viewport[1]=Fixture::width;
    u.plane_origin[0]=1; u.plane_origin[2]=1.043f;
    u.plane_n[0]=1; u.plane_u[1]=1; u.plane_v[2]=1;
    u.plane[0]=u.plane[1]=-2; u.plane[2]=u.plane[3]=2;
    u.plane_style[0]=.12f; u.plane_style[1]=2; u.plane_style[2]=1; u.plane_style[3]=.1f;
    for (int k=0;k<3;++k) { u.grid_minor[k]=.25f; u.grid_major[k]=.1f; }
    fixture.set_uniforms(u);
    gbool::PreviewState state;
    state.body=box(-1,-1,0,1,1,2.086f);
    state.tool=box(.8782f,-.35f,.65f,1,.35f,1.45f);
    state.operation=gbool::Operation::Subtract;
    const auto pixels=fixture.render(state);
    int cut_pixels=0;
    for (int y=0;y<int(Fixture::width);++y) for (int x=0;x<int(Fixture::width);++x) {
        const auto ray=camera.ray_through(x+.5,y+.5);
        const double o[]={ray.origin.x.to_f64(),ray.origin.y.to_f64(),ray.origin.z.to_f64()};
        const double d[]={ray.direction.x.to_f64(),ray.direction.y.to_f64(),ray.direction.z.to_f64()};
        const auto interval=[&](std::array<float,3> lo,std::array<float,3> hi) {
            double near=-INFINITY,far=INFINITY;
            for (int k=0;k<3;++k) {
                if (std::fabs(d[k])<1e-12) { if (o[k]<lo[k] || o[k]>hi[k]) return std::array<double,2>{1,0}; }
                else {
                    const double a=(lo[k]-o[k])/d[k],b=(hi[k]-o[k])/d[k];
                    near=std::max(near,std::min(a,b)); far=std::min(far,std::max(a,b));
                }
            }
            return std::array<double,2>{near,far};
        };
        const auto a=interval({-1,-1,0},{1,1,2.086f});
        const auto b=interval({.8782f,-.35f,.65f},{1,.35f,1.45f});
        double t=a[0];
        if (b[0]<=b[1] && b[0]<=t+1e-9 && b[1]>t) { t=b[1]; ++cut_pixels; }
        double expected=1;
        if (a[0]<=a[1] && t<a[1]) {
            double p[4]={o[0]+t*d[0],o[1]+t*d[1],o[2]+t*d[2],1},z=0,w=0;
            for (int k=0;k<4;++k) { z+=vp.m[k*4+2]*p[k]; w+=vp.m[k*4+3]*p[k]; }
            expected=z/w;
        }
        if (std::fabs(pixels[y*Fixture::width+x]-expected)>(projection==gview::Projection::Orthographic ? 1.2e-7 : 5e-7)) {
            std::fprintf(stderr,"side cut (%d,%d): %.9f expected %.9f\n",x,y,pixels[y*Fixture::width+x],expected);
            throw std::runtime_error("side-face preview depth differs from analytic box subtraction");
        }
    }
    check(cut_pixels>10,"side-face fixture must see inside the cut");
    const auto colors=fixture.colors;
    std::vector<MeshVertex> subdivided;
    for (std::size_t i=0;i<state.tool.size();i+=3) {
        MeshVertex mid=state.tool[i];
        for (int k=0;k<3;++k) mid.pos[k]=(state.tool[i].pos[k]+state.tool[i+1].pos[k]+state.tool[i+2].pos[k])/3;
        for (int j=0;j<3;++j) {
            subdivided.push_back(state.tool[i+j]); subdivided.push_back(state.tool[i+(j+1)%3]); subdivided.push_back(mid);
        }
    }
    state.tool=std::move(subdivided);
    fixture.render(state);
    for (std::size_t i=0;i<colors.size();++i)
        check(std::abs(int(colors[i])-int(fixture.colors[i]))<=2,"face grid changed after cap retriangulation");
    if (const char* path=std::getenv("GLCAD_GPU_TEST_IMAGE")) {
        std::ofstream image(std::string(path)+(projection==gview::Projection::Orthographic ? "-ortho.ppm" : "-perspective.ppm"),std::ios::binary);
        image<<"P6\n"<<Fixture::width<<' '<<Fixture::width<<"\n255\n";
        for (std::size_t i=0;i<colors.size();i+=4) image.write(reinterpret_cast<const char*>(colors.data()+i),3);
    }
}
}
int main() {
    try {
        Fixture f; gbool::PreviewState p;
        p.body=box(-.8f,-.8f,.2f,.8f,.8f,.8f); p.tool=box(-.3f,-.3f,.1f,.3f,.3f,.5f);
        auto pixels=f.render(p); expect(pixels,32,32,.1f,"union"); expect(pixels,10,32,.2f,"union body");
        const auto first=f.preview.statistics();
        check(first.preparations==1 && first.uploads==1,"initial preview preparation");
        f.render(p,1);
        const auto warm=f.preview.statistics();
        for(unsigned i=0;i<6;++i) check(f.render(p,i%2)==pixels,"cached depth changed");
        const auto cached=f.preview.statistics();
        check(cached.preparations==1 && cached.uploads==2,"unchanged preview rebuilt or uploaded");
        check(cached.uploaded_bytes==warm.uploaded_bytes,"unchanged preview transferred bytes");
        p.tool[0].color[0]=.125f; f.render(p);
        check(f.preview.statistics().preparations==2,"color change did not invalidate cache");
        p.tool=box(-.3f,-.3f,.15f,.3f,.3f,.5f);
        auto moved=f.render(p,1); expect(moved,32,32,.15f,"same-sized operand movement");
        check(f.preview.statistics().preparations==3,"movement did not invalidate cache");
        p.tool=box(-.3f,-.3f,.1f,.3f,.3f,.5f);

        p.operation=gbool::Operation::Subtract; pixels=f.render(p,1);
        expect(pixels,32,32,.5f,"blind cut floor"); expect(pixels,10,32,.2f,"cut surrounding face");
        p.operation=gbool::Operation::Intersect; pixels=f.render(p);
        expect(pixels,32,32,.2f,"intersection"); expect(pixels,10,32,1,"intersection outside");
        p.operation=gbool::Operation::Subtract; p.tool=box(-.3f,-.3f,.1f,.3f,.3f,.9f); pixels=f.render(p);
        expect(pixels,32,32,1,"through cut"); expect(pixels,10,32,.2f,"through cut rim");
        p.tool=p.body; pixels=f.render(p); expect(pixels,32,32,1,"complete removal");
        p.operation=gbool::Operation::Union; pixels=f.render(p); expect(pixels,32,32,.2f,"coincident union");
        p.operation=gbool::Operation::Intersect; pixels=f.render(p); expect(pixels,32,32,.2f,"coincident intersection");
        p.tool=box(-.3f,-.3f,.8f,.3f,.3f,.95f); pixels=f.render(p); expect(pixels,32,32,1,"touching intersection");
        p.operation=gbool::Operation::Union; pixels=f.render(p); expect(pixels,32,32,.2f,"touching union");
        p.body.clear(); p.tool=box(-.3f,-.3f,.1f,.3f,.3f,.5f); pixels=f.render(p); expect(pixels,32,32,.1f,"new solid");
        for (auto op:{gbool::Operation::Subtract,gbool::Operation::Intersect}) {
            p.operation=op; pixels=f.render(p);
            check(std::all_of(pixels.begin(),pixels.end(),[](float d){return d==1;}),"empty body must produce an empty cut/intersection");
        }
        p.body=box(-.8f,-.8f,.2f,.8f,.8f,.8f); p.tool=box(2,2,.1f,3,3,.5f);
        p.operation=gbool::Operation::Intersect; pixels=f.render(p);
        check(std::all_of(pixels.begin(),pixels.end(),[](float d){return d==1;}),"disjoint intersection must be empty");
        p.operation=gbool::Operation::Subtract; pixels=f.render(p); expect(pixels,32,32,.2f,"distant cutter retains body");
        p.tool=box(-.3f,-.3f,.1f,.3f,.3f,.5f);
        p.body=box(-.8f,-.8f,.2f,-.4f,.8f,.8f);
        auto right=box(.4f,-.8f,.2f,.8f,.8f,.8f); p.body.insert(p.body.end(),right.begin(),right.end());
        for (int i=0;i<64;++i) { auto far=box(2+i*.1f,0,.2f,2.05f+i*.1f,.1f,.4f); p.body.insert(p.body.end(),far.begin(),far.end()); }
        p.operation=gbool::Operation::Subtract; pixels=f.render(p,1); expect(pixels,32,32,1,"existing hole"); expect(pixels,10,32,.2f,"disconnected body");
        p.body=box(-.8f,-.8f,.2f,.8f,.8f,.8f); p.tool=ring();
        check(p.tool.size()>512*3,"ring must exceed the old triangle capacity");
        for (auto op:{gbool::Operation::Union,gbool::Operation::Subtract,gbool::Operation::Intersect}) {
            p.operation=op; pixels=f.render(p);
            for (int y=0;y<64;++y) for (int x=0;x<64;++x) {
                const float px=(x+.5f)/32-1, py=(y+.5f)/32-1;
                const float radius=std::sqrt(px*px+py*py);
                if (std::fabs(radius-.6f)<.002f || std::fabs(radius-.2f)<.002f) continue;
                const bool in_body=std::fabs(px)<.8f && std::fabs(py)<.8f;
                const bool in_ring=radius<.6f && radius>.2f;
                float expected=1;
                if (op==gbool::Operation::Union) expected=in_ring ? .1f : in_body ? .2f : 1;
                if (op==gbool::Operation::Subtract) expected=in_body ? (in_ring ? .5f : .2f) : 1;
                if (op==gbool::Operation::Intersect) expected=in_ring ? .2f : 1;
                expect(pixels,x,y,expected,"holed curved profile");
            }
        }
        p.body=box(-.8f,-.8f,-.2f,.8f,.8f,.8f); p.tool=box(2,2,.1f,3,3,.5f);
        p.operation=gbool::Operation::Union; pixels=f.render(p); expect(pixels,32,32,.8f,"camera inside solid");
        circular_reversal();
        side_face_cut(f,gview::Projection::Orthographic);
        side_face_cut(f,gview::Projection::Perspective);
        std::puts("GPU boolean depth checks passed");
        if (std::getenv("GLCAD_GPU_BENCHMARK")) {
            const uint32_t size=std::getenv("GLCAD_GPU_BENCH_SIZE")?uint32_t(std::atoi(std::getenv("GLCAD_GPU_BENCH_SIZE"))):512;
            Fixture benchmark(size,true);
            check(benchmark.timestamps != VK_NULL_HANDLE,"GPU benchmark requires 64-bit timestamps");
            const int sides=std::getenv("GLCAD_GPU_BENCH_SIDES")?std::atoi(std::getenv("GLCAD_GPU_BENCH_SIDES")):80;
            p.body=box(-.8f,-.8f,.2f,.8f,.8f,.8f); p.tool=ring(sides);
            for (auto op:{gbool::Operation::Union,gbool::Operation::Subtract,gbool::Operation::Intersect}) {
                p.operation=op;
                std::vector<double> times,cpu;
                for (int i=0;i<40;++i) { benchmark.render(p,uint32_t(i%2)); if (i>=10) {times.push_back(benchmark.gpu_ms);cpu.push_back(benchmark.record_ms);} }
                std::sort(times.begin(),times.end());std::sort(cpu.begin(),cpu.end());
                std::printf("GPU benchmark %ux%u %d sides operation %u: GPU %.3f ms, CPU record %.3f ms\n",size,size,sides,uint32_t(op),times[times.size()/2],cpu[cpu.size()/2]);
            }
            std::vector<double> moving;
            for(int i=0;i<40;++i) {
                for(auto& v:p.tool)v.pos[2]+=.0001f;
                benchmark.render(p,uint32_t(i%2));
                if(i>=10)moving.push_back(benchmark.record_ms);
            }
            std::sort(moving.begin(),moving.end());
            std::printf("GPU benchmark moving operands: CPU prepare + upload %.3f ms\n",moving[moving.size()/2]);
        }
    } catch (const std::exception& e) { std::fprintf(stderr,"%s\n",e.what()); return 1; }
}
