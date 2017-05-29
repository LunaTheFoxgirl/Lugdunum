#include <lug/Config.hpp>
#if defined LUG_SYSTEM_WINDOWS
	#define NOMINMAX
#endif
#include <algorithm>

#include <lug/Graphics/Vulkan/Gui.hpp>
#include <lug/Graphics/Render/dear_imgui/imgui.h>
#include <lug/Graphics/Vulkan/API/ShaderModule.hpp>
#include <lug/Graphics/Vulkan/API/Fence.hpp>
#include <lug/Window/Keyboard.hpp>


namespace lug {
namespace Graphics {
namespace Vulkan {


Gui::Gui(Renderer &renderer, Render::Window &window) : _renderer(renderer), _window(window) {
}

Gui::~Gui() {
    vkDestroySampler(static_cast<VkDevice>(_renderer.getDevice()), _sampler, nullptr);
}

bool Gui::beginFrame(const System::Time &elapsedTime) {
    ImGuiIO& io = ImGui::GetIO();

    io.DeltaTime = elapsedTime.getSeconds();

    io.DisplaySize = ImVec2(_window.getWidth(), _window.getHeight());

    uint32_t x;
    uint32_t y;
    _window.getMousePos(x, y);
    io.MousePos = ImVec2(static_cast<float>(x), static_cast<float>(y));

    io.MouseDown[0] = _window.isMousePressed(lug::Window::Mouse::Button::Left);
    io.MouseDown[1] = _window.isMousePressed(lug::Window::Mouse::Button::Right);
    io.MouseDown[2] = _window.isMousePressed(lug::Window::Mouse::Button::Middle);

    ImGui::NewFrame();

    ImGui::ShowTestWindow();
    
    return false;
}

bool Gui::endFrame(const std::vector<VkSemaphore>& waitSemaphores, uint32_t currentImageIndex) {
    ImGui::Render();
    if (!_guiFences[currentImageIndex].wait()) {
        return false;
    }
    _guiFences[currentImageIndex].reset();
    updateBuffers(currentImageIndex);

   API::Queue* graphicsQueue = _renderer.getQueue(VK_QUEUE_GRAPHICS_BIT, true);

    _commandBuffers[currentImageIndex].reset();
    _commandBuffers[currentImageIndex].begin();

    ImGuiIO& io = ImGui::GetIO();

        VkViewport vkViewport{
            vkViewport.x = 0,
            vkViewport.y = 0,
            vkViewport.width = io.DisplaySize.x,
            vkViewport.height = io.DisplaySize.y,
            vkViewport.minDepth = 0.0f,
            vkViewport.maxDepth = 1.0f,
        };
        vkCmdSetViewport(static_cast<VkCommandBuffer>(_commandBuffers[currentImageIndex]), 0, 1, &vkViewport);


    API::RenderPass* renderPass = _pipeline.getRenderPass();

	VkRenderPassBeginInfo beginInfo{
		beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		beginInfo.pNext = nullptr,
        beginInfo.renderPass = static_cast<VkRenderPass>(*renderPass),
        beginInfo.framebuffer = static_cast<VkFramebuffer>(_framebuffers[currentImageIndex]),
        {},
        beginInfo.clearValueCount = 0,
        beginInfo.pClearValues = nullptr
	};

    beginInfo.renderArea.offset = {0, 0};
    beginInfo.renderArea.extent = {_window.getWidth(), _window.getHeight()};

    vkCmdBeginRenderPass(static_cast<VkCommandBuffer>(_commandBuffers[currentImageIndex]), &beginInfo, VK_SUBPASS_CONTENTS_INLINE);

	_descriptorSet[0].bind(_pipeline.getLayout(), &_commandBuffers[currentImageIndex], 0, 0, nullptr);
	_pipeline.bind(&_commandBuffers[currentImageIndex]);

    VkDeviceSize offsets[1] = { 0 };
    VkBuffer vertexBuffer = static_cast<VkBuffer>(*_vertexBuffers[currentImageIndex]);

    vkCmdBindVertexBuffers(static_cast<VkCommandBuffer>(_commandBuffers[currentImageIndex]), 0, 1, &vertexBuffer, offsets);
    vkCmdBindIndexBuffer(static_cast<VkCommandBuffer>(_commandBuffers[currentImageIndex]), static_cast<VkBuffer>(*_indexBuffers[currentImageIndex]), 0, VK_INDEX_TYPE_UINT16);

    // UI scale and translate via push constants
	pushConstBlock.scale = lug::Math::Vec2f{ 2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y };
	pushConstBlock.translate = lug::Math::Vec2f{ -1.0f, -1.0f};
    vkCmdPushConstants(static_cast<VkCommandBuffer>(_commandBuffers[currentImageIndex]), static_cast<VkPipelineLayout>(*(_pipeline.getLayout())), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstBlock), &pushConstBlock);

    // Render commands
    ImDrawData* imDrawData = ImGui::GetDrawData();

    int vertexCount = 0;
    int indexCount = 0;

    for (int32_t i = 0; i < imDrawData->CmdListsCount; i++) {
        const ImDrawList* cmd_list = imDrawData->CmdLists[i];
        for (int32_t j = 0; j < cmd_list->CmdBuffer.Size; j++) {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[j];
            VkRect2D scissorRect;
            scissorRect.offset.x = std::max(static_cast<int32_t>(pcmd->ClipRect.x), 0);
            scissorRect.offset.y = std::max(static_cast<int32_t>(pcmd->ClipRect.y), 0);
            scissorRect.extent.width = (uint32_t)(pcmd->ClipRect.z - pcmd->ClipRect.x);
            scissorRect.extent.height = (uint32_t)(pcmd->ClipRect.w - pcmd->ClipRect.y);
            vkCmdSetScissor(static_cast<VkCommandBuffer>(_commandBuffers[currentImageIndex]), 0, 1, &scissorRect);
            vkCmdDrawIndexed(static_cast<VkCommandBuffer>(_commandBuffers[currentImageIndex]), pcmd->ElemCount, 1, indexCount, vertexCount, 0);
			indexCount += pcmd->ElemCount;
        }
		vertexCount += cmd_list->VtxBuffer.Size;
    }

    renderPass->end(&_commandBuffers[currentImageIndex]);
    _commandBuffers[currentImageIndex].end();

    std::vector<VkPipelineStageFlags> waitDstStageMasks(waitSemaphores.size(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
	if (graphicsQueue->submit(_commandBuffers[currentImageIndex], {static_cast<VkSemaphore>(_guiSemaphores[currentImageIndex])}, waitSemaphores, waitDstStageMasks, static_cast<VkFence>(_guiFences[currentImageIndex])) == false) {
        LUG_LOG.error("GUI: Can't submit commandBuffer");
        return false;
    }

    return true;
}

bool Gui::init(const std::vector<std::unique_ptr<API::ImageView>>& imageViews) {
    ImGuiIO& io = ImGui::GetIO();

    initKeyMapping();

    io.DisplaySize = ImVec2(_window.getWidth(), _window.getHeight());
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    API::Queue* graphicsQueue = _renderer.getQueue(VK_QUEUE_GRAPHICS_BIT, true);
    _commandBuffers = graphicsQueue->getCommandPool().createCommandBuffers(VK_COMMAND_BUFFER_LEVEL_PRIMARY, static_cast<uint32_t>(imageViews.size()));

    _indexBuffers.resize(imageViews.size());
    _vertexBuffers.resize(imageViews.size());
    _vertexDeviceMemories.resize(imageViews.size());
    _indexDeviceMemories.resize(imageViews.size());
    _vertexCounts.resize(imageViews.size());
    _indexCounts.resize(imageViews.size());

    return createFontsTexture() && initPipeline() && initFramebuffers(imageViews);
}

void Gui::initKeyMapping(){
    ImGuiIO& io = ImGui::GetIO();
    io.KeyMap[ImGuiKey_Tab] = static_cast<int>(lug::Window::Keyboard::Key::Tab);    // Keyboard mapping. ImGui will use those indices to peek into the io.KeyDown[] array.
    io.KeyMap[ImGuiKey_LeftArrow] = static_cast<int>(lug::Window::Keyboard::Key::Left);
    io.KeyMap[ImGuiKey_RightArrow] = static_cast<int>(lug::Window::Keyboard::Key::Right);
    io.KeyMap[ImGuiKey_UpArrow] = static_cast<int>(lug::Window::Keyboard::Key::Up);
    io.KeyMap[ImGuiKey_DownArrow] = static_cast<int>(lug::Window::Keyboard::Key::Down);
    io.KeyMap[ImGuiKey_PageUp] = static_cast<int>(lug::Window::Keyboard::Key::PageUp);
    io.KeyMap[ImGuiKey_PageDown] = static_cast<int>(lug::Window::Keyboard::Key::PageDown);
    io.KeyMap[ImGuiKey_Home] = static_cast<int>(lug::Window::Keyboard::Key::Home);
    io.KeyMap[ImGuiKey_End] = static_cast<int>(lug::Window::Keyboard::Key::End);
    io.KeyMap[ImGuiKey_Delete] = static_cast<int>(lug::Window::Keyboard::Key::Delete);
    io.KeyMap[ImGuiKey_Backspace] = static_cast<int>(lug::Window::Keyboard::Key::BackSpace);
    io.KeyMap[ImGuiKey_Enter] = static_cast<int>(lug::Window::Keyboard::Key::Return);
    io.KeyMap[ImGuiKey_Escape] = static_cast<int>(lug::Window::Keyboard::Key::Escape);
    io.KeyMap[ImGuiKey_A] = static_cast<int>(lug::Window::Keyboard::Key::A);
    io.KeyMap[ImGuiKey_C] = static_cast<int>(lug::Window::Keyboard::Key::C);
    io.KeyMap[ImGuiKey_V] = static_cast<int>(lug::Window::Keyboard::Key::V);
    io.KeyMap[ImGuiKey_X] = static_cast<int>(lug::Window::Keyboard::Key::X);
    io.KeyMap[ImGuiKey_Y] = static_cast<int>(lug::Window::Keyboard::Key::Y);
    io.KeyMap[ImGuiKey_Z] = static_cast<int>(lug::Window::Keyboard::Key::Z);
}

bool Gui::createFontsTexture() {
    ImGuiIO& io = ImGui::GetIO();

    // Create font texture
    unsigned char* fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    size_t uploadSize = texWidth * texHeight * 4 * sizeof(char);

    auto device = &_renderer.getDevice();

    //Formats that are required to support VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT must also support VK_FORMAT_FEATURE_TRANSFER_SRC_BIT_KHR and VK_FORMAT_FEATURE_TRANSFER_DST_BIT_KHR.
    VkFormat imagesFormat = API::Image::findSupportedFormat(device,
                                                       {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                                        VK_IMAGE_TILING_OPTIMAL,
                                                        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);


    if (imagesFormat == VK_FORMAT_UNDEFINED) {
        LUG_LOG.error("Gui::createFontsTexture: Can't find any supported Format");
        return false;
    }


    VkExtent3D extent {
        extent.width = texWidth,
        extent.height = texHeight,
        extent.depth = 1
    };

    API::Queue* transfertQueue = _renderer.getQueue(VK_QUEUE_TRANSFER_BIT, false);

    // Create FontsTexture image
    {
        _image = API::Image::create(device, imagesFormat, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        if (!_image) {
            LUG_LOG.error("GUI: Can't create depth buffer image");
            return false;
        }

        auto& imageRequirements = _image->getRequirements();

        uint32_t memoryTypeIndex = API::DeviceMemory::findMemoryType(device, imageRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        _fontsTextureHostMemory = API::DeviceMemory::allocate(device, imageRequirements.size, memoryTypeIndex);
        if (!_fontsTextureHostMemory) {
            LUG_LOG.error("GUI: Can't allocate device memory for depth buffer images");
            return false;
        }

        _image->bindMemory(_fontsTextureHostMemory.get());
    }

    // Create FontsTexture image view
    {
        _imageView = API::ImageView::create(device, _image.get(), imagesFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!_imageView) {
            LUG_LOG.error("GUI: Can't create image view");
            return false;
        }
    }

    // Create staging buffers for font data upload
    {
        std::vector<uint32_t> queueFamilyIndices = { (uint32_t)_renderer.getQueue(VK_QUEUE_TRANSFER_BIT, false)->getFamilyIdx()};

        auto stagingBuffer = API::Buffer::create(device, (uint32_t)queueFamilyIndices.size(), queueFamilyIndices.data(), uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_SHARING_MODE_EXCLUSIVE);
        if (!stagingBuffer) {
            LUG_LOG.error("GUI: Can't create buffer");
            return false;
        }

        auto& requirements = stagingBuffer->getRequirements();
        uint32_t memoryTypeIndex = API::DeviceMemory::findMemoryType(device, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        auto fontsTextureDeviceMemory = API::DeviceMemory::allocate(device, requirements.size, memoryTypeIndex);
        if (!fontsTextureDeviceMemory) {
            LUG_LOG.error("GUI: Can't allocate device memory");
            return false;
        }

        stagingBuffer->bindMemory(fontsTextureDeviceMemory.get());
        stagingBuffer->updateData(fontData, (uint32_t)uploadSize);

        // Copy buffer data to font image
        {
            auto commandBuffer = transfertQueue->getCommandPool().createCommandBuffers();

            VkFence fence;
            VkFenceCreateInfo createInfo{
                createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                createInfo.pNext = nullptr,
                createInfo.flags = 0
            };
            VkResult result = vkCreateFence(static_cast<VkDevice>(_renderer.getDevice()), &createInfo, nullptr, &fence);
            if (result != VK_SUCCESS) {
                LUG_LOG.error("GUI: Can't create swapchain fence: {}", result);
                return false;
            }
            auto _fence = Vulkan::API::Fence(fence, &_renderer.getDevice());

            commandBuffer[0].begin();
            // Prepare for transfer
            _image->changeLayout(commandBuffer[0], 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            // Copy
            VkBufferImageCopy bufferCopyRegion = {};
            bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            bufferCopyRegion.imageSubresource.layerCount = 1;
            bufferCopyRegion.imageExtent.width = texWidth;
            bufferCopyRegion.imageExtent.height = texHeight;
            bufferCopyRegion.imageExtent.depth = 1;

            vkCmdCopyBufferToImage(
                static_cast<VkCommandBuffer>(commandBuffer[0]),
                static_cast<VkBuffer>(*stagingBuffer),
                static_cast<VkImage>(*_image),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &bufferCopyRegion
            );

            // Prepare for shader read
            _image->changeLayout(commandBuffer[0], VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

            commandBuffer[0].end();
            if (transfertQueue->submit(commandBuffer[0], {}, {}, {}, fence) == false) {
                LUG_LOG.error("GUI: Can't submit commandBuffer");
                return false;
            }

            // TODO : set a define for the fence timeout
            if (!_fence.wait()) {
                LUG_LOG.error("Gui: Can't vkWaitForFences");
                return false;
            }

            _fence.destroy();
          commandBuffer[0].destroy();
          stagingBuffer->destroy();
        }
    }

    // Font texture Sampler
    {
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.pNext = nullptr;
        samplerInfo.flags = 0;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

        vkCreateSampler(static_cast<VkDevice>(_renderer.getDevice()), &samplerInfo, nullptr, &_sampler);
    }

    // TODO
    _guiSemaphores.resize(3);
    _guiFences.resize(3);
    for (int i = 0; i < 3; i++) {
    {
        VkSemaphore semaphore;
        VkSemaphoreCreateInfo semaphoreCreateInfo{
            semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            semaphoreCreateInfo.pNext = nullptr,
            semaphoreCreateInfo.flags = 0
        };
        VkResult result = vkCreateSemaphore(static_cast<VkDevice>(*device), &semaphoreCreateInfo, nullptr, &semaphore);
        if (result != VK_SUCCESS) {
            LUG_LOG.error("RendererVulkan: Can't create swapchain semaphore: {}", result);
            return false;
        }

        _guiSemaphores[i] = API::Semaphore(semaphore, &_renderer.getDevice());
    }

    {
        VkFence fence;
        VkFenceCreateInfo fenceCreateInfo{
            fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            fenceCreateInfo.pNext = nullptr,
            fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT
        };
        VkResult result = vkCreateFence(static_cast<VkDevice>(*device), &fenceCreateInfo, nullptr, &fence);

        if (result != VK_SUCCESS) {
            LUG_LOG.error("RendererVulkan: Can't create swapchain fence: {}", result);
            return false;
        }

        _guiFences[i] = API::Fence(fence, device);
    }
}


    return true;
}

bool Gui::initPipeline() {
	auto device = &_renderer.getDevice();

	std::vector<std::unique_ptr<Vulkan::API::DescriptorSetLayout>> descriptorSetLayouts;
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

        descriptorSetLayouts.push_back(Vulkan::API::DescriptorSetLayout::create(&_renderer.getDevice(), &descriptorLayoutBinding, 1));


        // create descriptor set
        _descriptorSet = _descriptorPool.createDescriptorSets({ static_cast<VkDescriptorSetLayout>(*descriptorSetLayouts[0]) });

        // write descriptor set
        VkDescriptorImageInfo descriptorImageInfo;
        descriptorImageInfo.sampler = _sampler;
        descriptorImageInfo.imageView = static_cast<VkImageView>(*_imageView);
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            write.pNext = nullptr,
            write.dstSet = static_cast<VkDescriptorSet>(_descriptorSet[0]),
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


    VkPushConstantRange pushConstant = {
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        pushConstant.offset = 0,
        pushConstant.size = sizeof(PushConstBlock)
    };

    VkDescriptorSetLayout set_layout[1] = { static_cast<VkDescriptorSetLayout>(*descriptorSetLayouts[0]) };
    VkPipelineLayoutCreateInfo createInfo{
        createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        createInfo.pNext = nullptr,
        createInfo.flags = 0,
        createInfo.setLayoutCount = 1,
        createInfo.pSetLayouts = set_layout,
        createInfo.pushConstantRangeCount = 1,
        createInfo.pPushConstantRanges = &pushConstant
    };

    VkPipelineLayout vkPipelineLayout = VK_NULL_HANDLE;
    {
        VkResult result = vkCreatePipelineLayout(static_cast<VkDevice>(_renderer.getDevice()), &createInfo, nullptr, &vkPipelineLayout);

        if (result != VK_SUCCESS) {
            LUG_LOG.error("GUI: Can't create pipeline layout: {}", result);
            return false;
        }
    }

    _pipelineLayout = std::make_unique<Vulkan::API::PipelineLayout>(descriptorSetLayouts, vkPipelineLayout, device);

    VkVertexInputAttributeDescription vertexInputAttributesDesc[3] = {
        {
            vertexInputAttributesDesc[0].location = 0,
            vertexInputAttributesDesc[0].binding = 0,
            vertexInputAttributesDesc[0].format = VK_FORMAT_R32G32B32_SFLOAT,
            vertexInputAttributesDesc[0].offset = offsetof(ImDrawVert, pos)
        },
        {
            vertexInputAttributesDesc[1].location = 1,
            vertexInputAttributesDesc[1].binding = 0,
            vertexInputAttributesDesc[1].format = VK_FORMAT_R32G32_SFLOAT,
            vertexInputAttributesDesc[1].offset = offsetof(ImDrawVert, uv)
        },
        {
            vertexInputAttributesDesc[2].location = 2,
            vertexInputAttributesDesc[2].binding = 0,
            vertexInputAttributesDesc[2].format = VK_FORMAT_R8G8B8A8_UNORM,
            vertexInputAttributesDesc[2].offset = offsetof(ImDrawVert, col)
        }
    };


    VkVertexInputBindingDescription vertexInputBindingDesc{
        vertexInputBindingDesc.binding = 0,
        vertexInputBindingDesc.stride = sizeof(ImDrawVert),
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
        rasterizer.cullMode = VK_CULL_MODE_NONE,
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
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
        depthStencil.depthTestEnable = VK_FALSE,
        depthStencil.depthWriteEnable = VK_FALSE,
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
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD,
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
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
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicStateInfo{
        dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        dynamicStateInfo.pNext = nullptr,
        dynamicStateInfo.flags = 0,
        dynamicStateInfo.dynamicStateCount = 2,
        dynamicStateInfo.pDynamicStates = dynamicStates
    };

    auto vertexShader = API::ShaderModule::create(_renderer.getInfo().shadersRoot + "gui.vert.spv", device);
    auto fragmentShader = API::ShaderModule::create(_renderer.getInfo().shadersRoot + "gui.frag.spv", device);

    if (vertexShader == nullptr || fragmentShader == nullptr) {
        if (vertexShader == nullptr) {
            LUG_LOG.error("GUI: Can't create create gui vertex shader");
        }
        if (fragmentShader == nullptr) {
            LUG_LOG.error("GUI: Can't create create gui fragment shader");
        }
        return false;
    }

    // Vertex shader stage
    VkPipelineShaderStageCreateInfo vertexShaderStage{
        vertexShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        vertexShaderStage.pNext = nullptr,
        vertexShaderStage.flags = 0,
        vertexShaderStage.stage = VK_SHADER_STAGE_VERTEX_BIT,
        vertexShaderStage.module = static_cast<VkShaderModule>(*vertexShader),
        vertexShaderStage.pName = "main",
        vertexShaderStage.pSpecializationInfo = nullptr
    };

    // Fragment shader stage
    VkPipelineShaderStageCreateInfo fragmentShaderStage{
        fragmentShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        fragmentShaderStage.pNext = nullptr,
        fragmentShaderStage.flags = 0,
        fragmentShaderStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        fragmentShaderStage.module = static_cast<VkShaderModule>(*fragmentShader),
        fragmentShaderStage.pName = "main",
        fragmentShaderStage.pSpecializationInfo = nullptr
    };


    VkPipelineShaderStageCreateInfo shaderStages[]{
        vertexShaderStage,
        fragmentShaderStage
    };
    auto colorFormat = _window.getSwapchain().getFormat().format;

    // Create renderpass
    std::unique_ptr<Vulkan::API::RenderPass> renderPass = nullptr;
    {
        VkAttachmentDescription attachments [1]{
            // Color attachment
            {
                attachments[0].flags = 0,
                attachments[0].format = colorFormat,
                attachments[0].samples = VK_SAMPLE_COUNT_1_BIT,
                attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            }
        };

        VkAttachmentReference colorAttachmentRef{
            colorAttachmentRef.attachment = 0,
            colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };

        VkSubpassDescription subpass{
            subpass.flags = 0,
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            subpass.inputAttachmentCount = 0,
            subpass.pInputAttachments = nullptr,
            subpass.colorAttachmentCount = 1,
            subpass.pColorAttachments = &colorAttachmentRef,
            subpass.pResolveAttachments = nullptr,
            subpass.pDepthStencilAttachment = nullptr,
            subpass.preserveAttachmentCount = 0,
            subpass.pPreserveAttachments = nullptr
        };

        VkRenderPassCreateInfo renderPassCreateInfo{
            renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            renderPassCreateInfo.pNext = nullptr,
            renderPassCreateInfo.flags = 0,
            renderPassCreateInfo.attachmentCount = 1,
            renderPassCreateInfo.pAttachments = attachments,
            renderPassCreateInfo.subpassCount = 1,
            renderPassCreateInfo.pSubpasses = &subpass,
            renderPassCreateInfo.dependencyCount = 0,
            renderPassCreateInfo.pDependencies = nullptr
        };

        VkRenderPass vkRenderPass = VK_NULL_HANDLE;
        VkResult result = vkCreateRenderPass(static_cast<VkDevice>(*device), &renderPassCreateInfo, nullptr, &vkRenderPass);
        if (result != VK_SUCCESS) {
            LUG_LOG.error("RendererVulkan: Can't create render pass: {}", result);
            return false;
        }
        renderPass = std::make_unique<Vulkan::API::RenderPass>(vkRenderPass, device);
    }


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
        graphicPipelineCreateInfo.layout = vkPipelineLayout,
        graphicPipelineCreateInfo.renderPass = static_cast<VkRenderPass>(*renderPass),
        graphicPipelineCreateInfo.subpass = 0,
        graphicPipelineCreateInfo.basePipelineHandle = VK_NULL_HANDLE,
        graphicPipelineCreateInfo.basePipelineIndex = 0
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    {
        VkResult result = vkCreateGraphicsPipelines(static_cast<VkDevice>(*device), VK_NULL_HANDLE, 1, &graphicPipelineCreateInfo, nullptr, &pipeline);

        if (result != VK_SUCCESS) {
            LUG_LOG.error("RendererVulkan: Can't create graphics pipeline: {}", result);
            return false;
        }
    }

    _pipeline = Vulkan::API::Pipeline(pipeline, device, std::move(_pipelineLayout), std::move(renderPass));
    return (true);
}

void Gui::processEvents(lug::Window::Event event){
    ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
        case (lug::Window::Event::Type::KeyPressed):
        case (lug::Window::Event::Type::KeyReleased):
            io.KeysDown[static_cast<int>(event.key.code)] = (event.type == lug::Window::Event::Type::KeyPressed) ? true : false;

            io.KeyCtrl = static_cast<int>(event.key.ctrl);
            io.KeyShift = static_cast<int>(event.key.shift);
            io.KeyAlt = static_cast<int>(event.key.alt);
            io.KeySuper = static_cast<int>(event.key.system);
            break;
        case (lug::Window::Event::Type::CharEntered):
            if (event.character.val > 0 && event.character.val < 0x10000) {
                io.AddInputCharacter(static_cast<unsigned short>(event.character.val));
            }
            break;
    }
}

bool Gui::initFramebuffers(const std::vector<std::unique_ptr<API::ImageView>>& imageViews) {
    API::RenderPass* renderPass = _pipeline.getRenderPass();
    auto device = &_renderer.getDevice();

    VkResult result;

    _framebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); i++) {
        VkImageView attachments[1]{
            static_cast<VkImageView>(*imageViews[i])
        };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = static_cast<VkRenderPass>(*renderPass);
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = imageViews[i]->getExtent().width;
        framebufferInfo.height = imageViews[i]->getExtent().height;
        framebufferInfo.layers = 1;

        VkFramebuffer fb;
        result = vkCreateFramebuffer(static_cast<VkDevice>(*device), &framebufferInfo, nullptr, &fb);
        if (result != VK_SUCCESS) {
            LUG_LOG.error("RendererVulkan: Failed to create framebuffer: {}", result);
            return false;
        }
        _framebuffers[i] = API::Framebuffer(fb, device, {imageViews[i]->getExtent().width, imageViews[i]->getExtent().height});
    }

    return true;
}

void    Gui::updateBuffers(uint32_t currentImageIndex) {
    ImDrawData* imDrawData = ImGui::GetDrawData();
    if (!imDrawData) {
        return;
    }
    auto device = &_renderer.getDevice();

    // Note: Alignment is done inside buffer creation
    VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
    VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

    // Update buffers only if vertex or index count has been changed compared to current buffer size
    {
        // Vertex buffer
        if ((_vertexBuffers[currentImageIndex] == nullptr) || (_vertexCounts[currentImageIndex] != imDrawData->TotalVtxCount)) {
            {
                API::Queue* transfertQueue = _renderer.getQueue(VK_QUEUE_TRANSFER_BIT, false);
                std::vector<uint32_t> queueFamilyIndices = {(uint32_t)transfertQueue->getFamilyIdx()};
                _vertexBuffers[currentImageIndex] = API::Buffer::create(device, (uint32_t)queueFamilyIndices.size(), queueFamilyIndices.data(), (uint32_t)vertexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                if (!_vertexBuffers[currentImageIndex])
                    return;

                auto& requirements = _vertexBuffers[currentImageIndex]->getRequirements();

                uint32_t memoryTypeIndex = API::DeviceMemory::findMemoryType(device, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
                _vertexDeviceMemories[currentImageIndex] = API::DeviceMemory::allocate(device, requirements.size, memoryTypeIndex);
                if (!_vertexDeviceMemories[currentImageIndex]) {
                    return;
                }

                _vertexBuffers[currentImageIndex]->bindMemory(_vertexDeviceMemories[currentImageIndex].get());
            }

            _vertexCounts[currentImageIndex] = imDrawData->TotalVtxCount;
        }

        // IndexBuffer
        if ((_indexBuffers[currentImageIndex] == nullptr) || (_indexCounts[currentImageIndex] < imDrawData->TotalIdxCount)) {
            {
                API::Queue* transfertQueue = _renderer.getQueue(VK_QUEUE_TRANSFER_BIT, false);
                std::vector<uint32_t> queueFamilyIndices = {(uint32_t)transfertQueue->getFamilyIdx()};
                _indexBuffers[currentImageIndex] = API::Buffer::create(device, (uint32_t)queueFamilyIndices.size(), queueFamilyIndices.data(), (uint32_t)indexBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                if (!_indexBuffers[currentImageIndex])
                    return;

                auto& requirements = _indexBuffers[currentImageIndex]->getRequirements();
                uint32_t memoryTypeIndex = API::DeviceMemory::findMemoryType(device, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
                _indexDeviceMemories[currentImageIndex] = API::DeviceMemory::allocate(device, requirements.size, memoryTypeIndex);
                if (!_indexDeviceMemories[currentImageIndex]) {
                    return;
                }

                _indexBuffers[currentImageIndex]->bindMemory(_indexDeviceMemories[currentImageIndex].get());
            }
            _indexCounts[currentImageIndex] = imDrawData->TotalIdxCount;
        }        
    }

    // Upload data
    {
        ImDrawIdx* idxDst = (ImDrawIdx*)_indexBuffers[currentImageIndex]->getGpuPtr();
        ImDrawVert* vtxDst = (ImDrawVert*)_vertexBuffers[currentImageIndex]->getGpuPtr();

        for (int n = 0; n < imDrawData->CmdListsCount; n++) {
            const ImDrawList* cmd_list = imDrawData->CmdLists[n];
            memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
            vtxDst += cmd_list->VtxBuffer.Size;
            idxDst += cmd_list->IdxBuffer.Size;
        }
    }

    // Flush to make writes visible to GPU TODO()
    // vertexBuffer.flush();
    // indexBuffer.flush();
}

} // Vulkan
} // Graphics
} // lug
