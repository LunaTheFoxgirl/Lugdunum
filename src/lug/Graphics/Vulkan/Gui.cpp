// #include <chrono>
// #include <algorithm>
// #include <lug/Config.hpp>
// #include <lug/Graphics/Light/Directional.hpp>
// #include <lug/Graphics/Light/Point.hpp>
// #include <lug/Graphics/Light/Spot.hpp>
// #include <lug/Graphics/Render/Queue.hpp>
// #include <lug/Graphics/Render/Gui.hpp>
// #include <lug/Graphics/Scene/MeshInstance.hpp>
// #include <lug/Graphics/Scene/ModelInstance.hpp>
// #include <lug/Graphics/Scene/Node.hpp>
// #include <lug/Graphics/Vulkan/Render/Camera.hpp>
// #include <lug/Graphics/Vulkan/Render/Mesh.hpp>
// #include <lug/Graphics/Vulkan/Render/Model.hpp>
// #include <lug/Graphics/Vulkan/Render/View.hpp>
// #include <lug/Graphics/Vulkan/Renderer.hpp>
// #include <lug/Math/Matrix.hpp>
// #include <lug/Math/Vector.hpp>
// #include <lug/Math/Geometry/Transform.hpp>
// #include <lug/System/Logger/Logger.hpp>

#include <lug/Graphics/Vulkan/API/Fence.hpp>
#include <lug/Graphics/Render/dear_imgui/imgui.h>

#include <lug/Graphics/Vulkan/Gui.hpp>

namespace lug {
namespace Graphics {
namespace Vulkan {


Gui::Gui(Renderer &renderer) : _renderer(renderer) {}

Gui::~Gui() {
}

// bool Gui::beginFrame()
// {
//     return false;
// }

// bool Gui::render() {
//     return true;
// }

// bool Gui::endFrame()
// {
//     return false;
// }

// void Gui::destroy() {
// }

void Gui::createFontsTexture() {
    ImGuiIO& io = ImGui::GetIO();

    // Create font texture
    unsigned char* fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    size_t uploadSize = texWidth * texHeight * 4 * sizeof(char);

    auto device = &_renderer.getDevice();

    // find format available on the device 
    VkFormat imagesFormat = API::Image::findSupportedFormat(device,
                                                       {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                                       VK_IMAGE_TILING_OPTIMAL,
                                                       VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    VkExtent3D extent {
        extent.width = texWidth,
        extent.height = texHeight,
        extent.depth = 1
    };

//    API::Queue* graphicsQueue = _renderer.getQueue(VK_QUEUE_GRAPHICS_BIT, false);
    API::Queue* transfertQueue = _renderer.getQueue(VK_QUEUE_TRANSFER_BIT, false);

    // Create FontsTexture image
    {
        // Create Vulkan Image

        // TODO chopper queue de graphics && transfert pour le rendre available sur les deux. Si les deux queue sont les meme alors renvoei qu'un seul int.
        // si deux queue : mode de partage partager , si une seule, en exclusive 

        _image = API::Image::create(device, imagesFormat, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        if (!_image) {
            LUG_LOG.error("Forward: Can't create depth buffer image");
            return ;
        }

        auto& imageRequirements = _image->getRequirements();

        uint32_t memoryTypeIndex = API::DeviceMemory::findMemoryType(device, imageRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   
        // Allocate image requirements size for image
        _fontsTextureHostMemory = API::DeviceMemory::allocate(device, imageRequirements.size, memoryTypeIndex);
        if (!_fontsTextureHostMemory) {
            LUG_LOG.error("Forward: Can't allocate device memory for depth buffer images");
            return ;
        }

        // Bind memory to image
        _image->bindMemory(_fontsTextureHostMemory.get(), imageRequirements.size);
    }

    // Create FontsTexture image view
    {
        // Create Vulkan Image View
        _imageView = API::ImageView::create(device, _image.get(), imagesFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!_imageView) {
            LUG_LOG.error("GUI: Can't create image view");
            return;
        }
    }

    // Create staging buffers for font data upload
    {
        std::vector<uint32_t> queueFamilyIndices = { (uint32_t)_renderer.getQueue(VK_QUEUE_TRANSFER_BIT, false)->getFamilyIdx()};


        // TODO chopper queue de graphics && transfert pour le rendre available sur les deux. Si les deux queue sont les meme alors renvoei qu'un seul int.
        // si deux queue : mode de partage partager , si une seule, en exclusive 
        auto stagingBuffer = API::Buffer::create(device, (uint32_t)queueFamilyIndices.size(), queueFamilyIndices.data(), uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_SHARING_MODE_EXCLUSIVE);
        if (!stagingBuffer) {
            LUG_LOG.error("GUI: Can't create buffer");
            return ;
        }

        auto& requirements = stagingBuffer->getRequirements();
        uint32_t memoryTypeIndex = API::DeviceMemory::findMemoryType(device, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        auto fontsTextureDeviceMemory = API::DeviceMemory::allocate(device, requirements.size, memoryTypeIndex);
        if (!fontsTextureDeviceMemory) {
            return ;
        }

        stagingBuffer->bindMemory(fontsTextureDeviceMemory.get());
        stagingBuffer->updateData(fontData, (uint32_t)uploadSize);
    }

    // Copy buffer data to font image
    {
        auto commandBuffer = transfertQueue->getCommandPool().createCommandBuffers();

        // Fence
        {
            VkFence fence;
            VkFenceCreateInfo createInfo{
                createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                createInfo.pNext = nullptr,
                createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT
            };
            VkResult result = vkCreateFence(static_cast<VkDevice>(_renderer.getDevice()), &createInfo, nullptr, &fence);
            if (result != VK_SUCCESS) {
                LUG_LOG.error("RendererVulkan: Can't create swapchain fence: {}", result);
                return ;
            }
            _fence = Vulkan::API::Fence(fence, &_renderer.getDevice());

            if (transfertQueue->submit(commandBuffer[0], {}, {}, {}, fence) == false) {
                LUG_LOG.error("Gui: Can't submit commandBuffer");
                return;
            }
        }

        // TODO : set a define for the fence timeout 
        if (_fence.wait() == false) {
            LUG_LOG.error("Gui: Can't vkWaitForFences");
            return;
        }
 
        _fence.destroy();
//        commandBuffer.destroy();
//        stagingBuffer.destroy();
    }

    // Font texture Sampler
    {
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

        vkCreateSampler(static_cast<VkDevice>(_renderer.getDevice()), &samplerInfo, nullptr, &_sampler);
    }

    {
        // creating descriptor pool
        VkDescriptorPoolSize descriptorPoolSize;
        descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorPoolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo;
        descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCreateInfo.pNext = nullptr;
        descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        descriptorPoolCreateInfo.maxSets = 1;
        descriptorPoolCreateInfo.poolSizeCount = 1;
        descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;
    
        VkDescriptorPool descPool;
        vkCreateDescriptorPool(static_cast<VkDevice>(_renderer.getDevice()), &descriptorPoolCreateInfo, nullptr, &descPool);

        _descriptorPool = Vulkan::API::DescriptorPool(descPool, &_renderer.getDevice());


        // descriptorSetLayout
        VkDescriptorSetLayoutBinding descriptorLayoutBinding;
        descriptorLayoutBinding.binding = 0;   // maybe modify that when shader is coded
        descriptorLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorLayoutBinding.descriptorCount = 1;
        descriptorLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        descriptorLayoutBinding.pImmutableSamplers = nullptr;

        _descriptorSetLayout = Vulkan::API::DescriptorSetLayout::create(&_renderer.getDevice(), &descriptorLayoutBinding, 1);


        // create descriptor set
        _descriptorSet = static_cast<VkDescriptorSet>(_descriptorPool.createDescriptorSets({static_cast<VkDescriptorSetLayout>(*_descriptorSetLayout)})[0]);

        // write descriptor set
        VkDescriptorImageInfo descriptorImageInfo;
        descriptorImageInfo.sampler = _sampler;
        descriptorImageInfo.imageView = static_cast<VkImageView>(*_imageView);
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            write.pNext = nullptr,
            write.dstSet = _descriptorSet,
            write.dstBinding = 0, // maybe modify that when shader is coded
            write.dstArrayElement = 0,
            write.descriptorCount = 1,
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            write.pImageInfo = &descriptorImageInfo,
            write.pBufferInfo = nullptr,
            write.pTexelBufferView = nullptr
        };


        // update descriptor set
        vkUpdateDescriptorSets(static_cast<VkDevice>(_renderer.getDevice()), 1, &write, 0, nullptr);
    }


    VkPushConstantRange pushConstants[] = {
        // Model transformation
        {
            pushConstants[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            pushConstants[0].offset = 0,
            pushConstants[0].size = sizeof(Math::Mat4x4f)
        }
    };

    VkDescriptorSetLayout set_layout[1] = { static_cast<VkDescriptorSetLayout>(*_descriptorSetLayout) };
    VkPipelineLayoutCreateInfo createInfo{
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        createInfo.pNext = nullptr,
        createInfo.flags = 0,
        createInfo.setLayoutCount = 1,
        createInfo.pSetLayouts = set_layout,
        createInfo.pushConstantRangeCount = 1,
        createInfo.pPushConstantRanges = pushConstants
    };

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkResult result = vkCreatePipelineLayout(static_cast<VkDevice>(_renderer.getDevice()), &createInfo, nullptr, &pipelineLayout);

    if (result != VK_SUCCESS) {
        LUG_LOG.error("RendererVulkan: Can't create pipeline layout: {}", result);
        return;
    }

    VkVertexInputAttributeDescription vertexInputAttributesDesc[3] = {
        {
            vertexInputAttributesDesc[0].location = 0,
            vertexInputAttributesDesc[0].binding = 0,
            vertexInputAttributesDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT,
            vertexInputAttributesDesc[0].offset = 0
        },
        {
            vertexInputAttributesDesc[1].location = 1,
            vertexInputAttributesDesc[1].binding = 0,
            vertexInputAttributesDesc[1].format = VK_FORMAT_R32G32_SFLOAT,
            vertexInputAttributesDesc[1].offset = offsetof(Render::Mesh::Vertex, uv)
        },
        {
            vertexInputAttributesDesc[2].location = 2,
            vertexInputAttributesDesc[2].binding = 0,
            vertexInputAttributesDesc[2].format = VK_FORMAT_R32G32B32_SFLOAT,
            vertexInputAttributesDesc[2].offset = offsetof(Render::Mesh::Vertex, color)
        }
    };


    VkVertexInputBindingDescription vertexInputBindingDesc{
        vertexInputBindingDesc.binding = 0,
        vertexInputBindingDesc.stride = sizeof(Render::Mesh::Vertex) - sizeof(Render::Mesh::Vertex::normal),
        vertexInputBindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
    };

        // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        vertexInputInfo.pNext = nullptr,
        vertexInputInfo.flags = 0,
        vertexInputInfo.vertexBindingDescriptionCount = 1,
        vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindingDesc,
        vertexInputInfo.vertexAttributeDescriptionCount = 3,
        vertexInputInfo.pVertexAttributeDescriptions = vertexInputAttributesDesc
    };

    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{
        inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        inputAssemblyInfo.pNext = nullptr,
        inputAssemblyInfo.flags = 0,
        inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        inputAssemblyInfo.primitiveRestartEnable = VK_FALSE // because VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkPipelineRasterizationStateCreateInfo rasterizer{
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        rasterizer.pNext = nullptr,
        rasterizer.flags = 0,
        rasterizer.depthClampEnable = VK_FALSE,
        rasterizer.rasterizerDiscardEnable = VK_FALSE,
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL,
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT,
        rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE,
        rasterizer.depthBiasEnable = VK_FALSE,
        rasterizer.depthBiasConstantFactor = 0.0f,
        rasterizer.depthBiasClamp = 0.0f,
        rasterizer.depthBiasSlopeFactor = 0.0f,
        rasterizer.lineWidth = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo multisampling{
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        multisampling.pNext = nullptr,
        multisampling.flags = 0,
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        multisampling.sampleShadingEnable = VK_FALSE,
        multisampling.minSampleShading = 0.0f,
        multisampling.pSampleMask = nullptr,
        multisampling.alphaToCoverageEnable = VK_FALSE,
        multisampling.alphaToOneEnable = VK_FALSE
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        depthStencil.pNext = nullptr,
        depthStencil.flags = 0,
        depthStencil.depthTestEnable = VK_TRUE,
        depthStencil.depthWriteEnable = VK_TRUE,
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        depthStencil.depthBoundsTestEnable = VK_FALSE,
        depthStencil.stencilTestEnable = VK_FALSE,
        {}, // depthStencil.front (Used for stencil, we don't need)
        {}, // depthStencil.back (Used for stencil, we don't need)
        depthStencil.minDepthBounds = 0.0f, // For depth bound, we don't care
        depthStencil.maxDepthBounds = 0.0f // For depth bound, we don't care
    };

    VkPipelineColorBlendAttachmentState colorBlendAttachment{
        colorBlendAttachment.blendEnable = VK_TRUE,
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_CONSTANT_COLOR,
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD,
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_CONSTANT_COLOR,
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD,
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    VkPipelineColorBlendStateCreateInfo colorBlending{
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        colorBlending.pNext = nullptr,
        colorBlending.flags = 0,
        colorBlending.logicOpEnable = VK_FALSE,
        colorBlending.logicOp = VK_LOGIC_OP_COPY,
        colorBlending.attachmentCount = 1,
        colorBlending.pAttachments = &colorBlendAttachment,
        {} // colorBlending.blendConstants
    };

    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    VkViewport viewport{
        viewport.x = 0.0f,
        viewport.y = 0.0f,
        viewport.width = 0.0f,
        viewport.height = 0.0f,
        viewport.minDepth = 0.0f,
        viewport.maxDepth = 1.0f,
    };

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {0, 0};


    VkPipelineViewportStateCreateInfo viewportState{
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        viewportState.pNext = nullptr,
        viewportState.flags = 0,
        viewportState.viewportCount = 1,
        viewportState.pViewports = &viewport,
        viewportState.scissorCount = 1,
        viewportState.pScissors = &scissor
    };

    const VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS
    };

    VkPipelineDynamicStateCreateInfo dynamicStateInfo{
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        dynamicStateInfo.pNext = nullptr,
        dynamicStateInfo.flags = 0,
        dynamicStateInfo.dynamicStateCount = 3,
        dynamicStateInfo.pDynamicStates = dynamicStates
    };

    VkPipelineShaderStageCreateInfo shaderStages[]{
        vertexShaderStage,
        fragmentShaderStage
    };

    auto colorFormat = _renderView->getFormat().format;
    auto renderPass = Vulkan::API::RenderPass::create(device, colorFormat);

    VkGraphicsPipelineCreateInfo graphicPipelineCreateInfo{
        graphicPipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        graphicPipelineCreateInfo.pNext = nullptr,
        graphicPipelineCreateInfo.flags = 0,
        graphicPipelineCreateInfo.stageCount = 2,
        graphicPipelineCreateInfo.pStages = shaderStages,
        graphicPipelineCreateInfo.pVertexInputState = &vertexInputInfo,
        graphicPipelineCreateInfo.pInputAssemblyState = &inputAssemblyInfo,
        graphicPipelineCreateInfo.pTessellationState = nullptr,
        graphicPipelineCreateInfo.pViewportState = &viewportState,
        graphicPipelineCreateInfo.pRasterizationState = &rasterizer,
        graphicPipelineCreateInfo.pMultisampleState = &multisampling,
        graphicPipelineCreateInfo.pDepthStencilState = &depthStencil,
        graphicPipelineCreateInfo.pColorBlendState = &colorBlending,
        graphicPipelineCreateInfo.pDynamicState = &dynamicStateInfo,
        graphicPipelineCreateInfo.layout = pipelineLayout,
        graphicPipelineCreateInfo.renderPass = static_cast<VkRenderPass>(*renderPass),
        graphicPipelineCreateInfo.subpass = 0,
        graphicPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE,
        graphicPipelineCreateInfo.basePipelineIndex = 0
    };

    VkPipeline pipeline = VK_NULL_HANDLE;

    // TODO: create with VkPipelineCache
    // TODO: create multiple pipelines with one call
    VkResult result = vkCreateGraphicsPipelines(static_cast<VkDevice>(*device), VK_NULL_HANDLE, 1, &graphicPipelineCreateInfo, nullptr, &pipeline);

    if (result != VK_SUCCESS) {
        LUG_LOG.error("RendererVulkan: Can't create graphics pipeline: {}", result);
        return ;
    }


}

} // Vulkan
} // Graphics
} // lug
