#include "ui/boolean_gpu.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <numeric>

namespace gcapp::gbool {

void BooleanGpu::free(Buffer& b) {
    if (b.buf) vkDestroyBuffer(device_, b.buf, nullptr);
    if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
    b = {};
}

bool BooleanGpu::upload(Buffer& b, const void* data, VkDeviceSize bytes) {
    if (!bytes || bytes > max_storage_) return false;
    if (bytes > b.capacity) {
        free(b);
        VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        ci.size = bytes;
        ci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &ci, nullptr, &b.buf) != VK_SUCCESS) return false;
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, b.buf, &requirements);
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(phys_, &properties);
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            const auto flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            if ((requirements.memoryTypeBits & (1u << i)) &&
                (properties.memoryTypes[i].propertyFlags & flags) == flags) { type = i; break; }
        }
        if (type == UINT32_MAX) { free(b); return false; }
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = requirements.size;
        ai.memoryTypeIndex = type;
        if (vkAllocateMemory(device_, &ai, nullptr, &b.memory) != VK_SUCCESS ||
            vkBindBufferMemory(device_, b.buf, b.memory, 0) != VK_SUCCESS) { free(b); return false; }
        b.capacity = bytes;
    }
    void* mapped = nullptr;
    if (vkMapMemory(device_, b.memory, 0, bytes, 0, &mapped) != VK_SUCCESS) return false;
    std::memcpy(mapped, data, static_cast<std::size_t>(bytes));
    vkUnmapMemory(device_, b.memory);
    return true;
}

bool BooleanGpu::build(const Deps& d, std::string& err) {
    destroy();
    device_ = d.device; phys_ = d.phys;
    const auto fail = [&](const char* message) { err = message; destroy(); return false; };
    if (!device_ || !phys_ || !d.pass || !d.frame_layout || !d.vs || !d.fs || !d.frame_count)
        return fail("boolean preview: incomplete dependencies");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(phys_, &properties);
    max_storage_ = properties.limits.maxStorageBufferRange;
    VkDescriptorSetLayoutBinding bindings[2]{};
    for (uint32_t i = 0; i < 2; ++i)
        bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 2; dl.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(device_, &dl, nullptr, &descriptor_layout_) != VK_SUCCESS)
        return fail("boolean preview: descriptor layout");
    VkDescriptorSetLayout layouts[] = {d.frame_layout, descriptor_layout_};
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Params)};
    VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    li.setLayoutCount = 2; li.pSetLayouts = layouts;
    li.pushConstantRangeCount = 1; li.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(device_, &li, nullptr, &layout_) != VK_SUCCESS)
        return fail("boolean preview: pipeline layout");
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2*d.frame_count};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = d.frame_count; pool.poolSizeCount = 1; pool.pPoolSizes = &size;
    if (vkCreateDescriptorPool(device_, &pool, nullptr, &pool_) != VK_SUCCESS)
        return fail("boolean preview: descriptor pool");
    frames_.resize(d.frame_count);
    for (auto& frame : frames_) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool_; ai.descriptorSetCount = 1; ai.pSetLayouts = &descriptor_layout_;
        if (vkAllocateDescriptorSets(device_, &ai, &frame.set) != VK_SUCCESS)
            return fail("boolean preview: descriptor set");
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto& stage : stages) { stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stage.pName = "main"; }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = d.vs;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = d.fs;
    VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.cullMode = VK_CULL_MODE_NONE; raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    samples.rasterizationSamples = static_cast<VkSampleCountFlagBits>(d.samples);
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE; depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = 0xf; attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1; blend.pAttachments = &attachment;
    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamics;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount = 2; pi.pStages = stages; pi.pVertexInputState = &vertex;
    pi.pInputAssemblyState = &assembly; pi.pViewportState = &viewport;
    pi.pRasterizationState = &raster; pi.pMultisampleState = &samples;
    pi.pDepthStencilState = &depth; pi.pColorBlendState = &blend; pi.pDynamicState = &dynamic;
    pi.layout = layout_; pi.renderPass = d.pass;
    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline_) != VK_SUCCESS)
        return fail("boolean preview: graphics pipeline");
    return true;
}

bool BooleanGpu::record(VkCommandBuffer cb, uint32_t slot, VkDescriptorSet frame_set,
                        const PreviewState& p) {
    if (!ready() || !cb || !frame_set || slot >= frames_.size() || p.tool.empty() ||
        p.body.size()%3 || p.tool.size()%3) return false;
    if (p.body.empty() && p.operation != Operation::Union) return true;
    const std::size_t count = (p.body.size()+p.tool.size())/3;
    if (count > UINT32_MAX/2 || count*sizeof(Triangle) > max_storage_ ||
        count*2*sizeof(Node) > max_storage_) return false;
    const auto equal=[](const std::vector<MeshVertex>& a,const std::vector<MeshVertex>& b) {
        return a.size()==b.size() && (a.empty() || std::memcmp(a.data(),b.data(),a.size()*sizeof(MeshVertex))==0);
    };
    if (!revision_ || prepared_input_.operation!=p.operation ||
        !equal(prepared_input_.body,p.body) || !equal(prepared_input_.tool,p.tool)) {
        std::array<float, 3> lo{INFINITY, INFINITY, INFINITY}, hi{-INFINITY, -INFINITY, -INFINITY};
        auto body_lo = lo, body_hi = hi, tool_lo = lo, tool_hi = hi;
        for (const auto* mesh : {&p.body, &p.tool}) for (const auto& v : *mesh)
            for (int k = 0; k < 3; ++k) {
                if (!std::isfinite(v.pos[k]) || !std::isfinite(v.normal[k])) return false;
                lo[k] = std::min(lo[k], v.pos[k]); hi[k] = std::max(hi[k], v.pos[k]);
                auto& operand_lo = mesh == &p.body ? body_lo : tool_lo;
                auto& operand_hi = mesh == &p.body ? body_hi : tool_hi;
                operand_lo[k] = std::min(operand_lo[k], v.pos[k]);
                operand_hi[k] = std::max(operand_hi[k], v.pos[k]);
            }
        Params params{};
        params.operation = static_cast<uint32_t>(p.operation);
        float span = 0;
        for (int k = 0; k < 3; ++k) {
            params.origin_epsilon[k] = lo[k]*0.5f + hi[k]*0.5f;
            span = std::max(span, hi[k]-lo[k]);
        }
        params.origin_epsilon[3] = std::max(span*2e-7f, 1e-7f);
        for (int k = 0; k < 3; ++k) {
            float result_lo = lo[k], result_hi = hi[k];
            if (p.operation == Operation::Subtract) { result_lo = body_lo[k]; result_hi = body_hi[k]; }
            if (p.operation == Operation::Intersect) {
                result_lo = std::max(body_lo[k], tool_lo[k]); result_hi = std::min(body_hi[k], tool_hi[k]);
                if (result_lo > result_hi) return true;
            }
            params.result_lo[k] = result_lo - params.origin_epsilon[k] - params.origin_epsilon[3];
            params.result_hi[k] = result_hi - params.origin_epsilon[k] + params.origin_epsilon[3];
        }
        auto& triangles=prepared_triangles_;triangles.clear();
        triangles.reserve(count);
        for (uint32_t operand = 0; operand < 2; ++operand) {
            const auto& mesh = operand ? p.tool : p.body;
            for (std::size_t i = 0; i < mesh.size(); i += 3) {
                Triangle t{};
                for (int k = 0; k < 3; ++k) {
                    t.a[k] = mesh[i].pos[k] - params.origin_epsilon[k];
                    t.edge1[k] = (mesh[i+1].pos[k] - params.origin_epsilon[k]) - t.a[k];
                    t.edge2[k] = (mesh[i+2].pos[k] - params.origin_epsilon[k]) - t.a[k];
                    t.normal[k] = mesh[i].normal[k];
                }
                float squared1 = 0, squared2 = 0;
                for (int k = 0; k < 3; ++k) { squared1 += t.edge1[k]*t.edge1[k]; squared2 += t.edge2[k]*t.edge2[k]; }
                t.a[3] = std::sqrt(squared1)*std::sqrt(squared2)*1e-8f;
                t.normal[3] = float(operand);
                std::copy(mesh[i].color, mesh[i].color+4, t.color);
                triangles.push_back(t);
            }
        }
        struct Box {
            std::array<float,3> lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
            void add(const Box& b) { for(int k=0;k<3;++k) {lo[k]=std::min(lo[k],b.lo[k]);hi[k]=std::max(hi[k],b.hi[k]);} }
            double area() const {
                if(lo[0]>hi[0])return 0;
                const double x=double(hi[0])-lo[0],y=double(hi[1])-lo[1],z=double(hi[2])-lo[2];
                return 2*(x*y+x*z+y*z);
            }
        };
        std::vector<Box> bounds(count);
        std::vector<std::array<float,3>> centres(count);
        std::vector<uint32_t> order(count);std::iota(order.begin(),order.end(),0);
        for(uint32_t i=0;i<count;++i)for(int k=0;k<3;++k) {
            const auto& t=triangles[i];
            for(double value:{double(t.a[k]),double(t.a[k])+t.edge1[k],double(t.a[k])+t.edge2[k]}) {
                const float rounded=float(value);
                bounds[i].lo[k]=std::min(bounds[i].lo[k],double(rounded)>value?std::nextafter(rounded,-INFINITY):rounded);
                bounds[i].hi[k]=std::max(bounds[i].hi[k],double(rounded)<value?std::nextafter(rounded,INFINITY):rounded);
            }
            centres[i][k]=t.a[k]+(t.edge1[k]+t.edge2[k])/3;
        }
        auto& nodes=prepared_nodes_;nodes.clear();nodes.reserve(count*2);
        std::function<void(uint32_t,uint32_t)> build_node=[&](uint32_t first,uint32_t end) {
            const uint32_t index=uint32_t(nodes.size());
            Box box,centroid_box;Node node{};
            for(uint32_t i=first;i<end;++i) {
                const auto id=order[i];box.add(bounds[id]);
                Box c;c.lo=c.hi=centres[id];centroid_box.add(c);
            }
            for(int k=0;k<3;++k) {node.lo[k]=box.lo[k];node.hi[k]=box.hi[k];}
            nodes.push_back(node);
            if(end-first<=4) {nodes[index].first=first;nodes[index].count=end-first;}
            else {
                constexpr int bins=16;
                double best=INFINITY;int axis=-1,split=0;
                for(int k=0;k<3;++k) {
                    const float span=centroid_box.hi[k]-centroid_box.lo[k];if(span<=0)continue;
                    Box bucket[bins],suffix[bins];uint32_t sizes[bins]{},right[bins]{};
                    for(uint32_t i=first;i<end;++i) {
                        const auto id=order[i];
                        const int bin=std::clamp(int((centres[id][k]-centroid_box.lo[k])/span*bins),0,bins-1);
                        bucket[bin].add(bounds[id]);++sizes[bin];
                    }
                    Box tail;uint32_t tail_count=0;
                    for(int b=bins-1;b>=0;--b) {tail.add(bucket[b]);tail_count+=sizes[b];suffix[b]=tail;right[b]=tail_count;}
                    Box left;uint32_t left_count=0;
                    for(int b=0;b<bins-1;++b) {
                        left.add(bucket[b]);left_count+=sizes[b];
                        if(!left_count || !right[b+1])continue;
                        const double cost=left.area()*left_count+suffix[b+1].area()*right[b+1];
                        if(cost<best) {best=cost;axis=k;split=b;}
                    }
                }
                uint32_t mid=first;
                if(axis>=0) {
                    const float span=centroid_box.hi[axis]-centroid_box.lo[axis];
                    const auto it=std::partition(order.begin()+first,order.begin()+end,[&](uint32_t id) {
                        return std::clamp(int((centres[id][axis]-centroid_box.lo[axis])/span*bins),0,bins-1)<=split;
                    });
                    mid=uint32_t(it-order.begin());
                }
                if(mid==first || mid==end)mid=first+(end-first)/2;
                build_node(first,mid);build_node(mid,end);
            }
            nodes[index].escape=uint32_t(nodes.size());
        };
        build_node(0,uint32_t(count));
        std::vector<Triangle> sorted;sorted.reserve(count);
        for(auto id:order)sorted.push_back(triangles[id]);
        triangles.swap(sorted);
        params.nodes = uint32_t(nodes.size()); params.triangles = uint32_t(count);
        prepared_params_=params;
        prepared_input_=p;
        ++revision_;++statistics_.preparations;
    }
    const auto& params=prepared_params_;
    const auto& triangles=prepared_triangles_;
    const auto& nodes=prepared_nodes_;
    auto& frame = frames_[slot];
    if (frame.revision!=revision_) {
        if (!upload(frame.triangles, triangles.data(), triangles.size()*sizeof(Triangle)) ||
            !upload(frame.nodes, nodes.data(), nodes.size()*sizeof(Node))) return false;
        VkDescriptorBufferInfo buffers[] = {{frame.triangles.buf, 0, triangles.size()*sizeof(Triangle)},
                                           {frame.nodes.buf, 0, nodes.size()*sizeof(Node)}};
        VkWriteDescriptorSet writes[2]{};
        for (uint32_t i = 0; i < 2; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = frame.set; writes[i].dstBinding = i;
            writes[i].descriptorCount = 1; writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &buffers[i];
        }
        vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
        frame.revision=revision_;
        ++statistics_.uploads;
        statistics_.uploaded_bytes+=triangles.size()*sizeof(Triangle)+nodes.size()*sizeof(Node);
    }

    const VkDescriptorSet sets[] = {frame_set, frame.set};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);
    vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(params), &params);
    vkCmdDraw(cb, 3, 1, 0, 0);
    return true;
}

void BooleanGpu::destroy() {
    if (!device_) return;
    for (auto& frame : frames_) { free(frame.triangles); free(frame.nodes); }
    frames_.clear();
    prepared_input_={};prepared_triangles_.clear();prepared_nodes_.clear();prepared_params_={};
    revision_=0;statistics_={};
    if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pool_) vkDestroyDescriptorPool(device_, pool_, nullptr);
    if (layout_) vkDestroyPipelineLayout(device_, layout_, nullptr);
    if (descriptor_layout_) vkDestroyDescriptorSetLayout(device_, descriptor_layout_, nullptr);
    pipeline_ = VK_NULL_HANDLE; pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE; descriptor_layout_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE; phys_ = VK_NULL_HANDLE;
}
}
