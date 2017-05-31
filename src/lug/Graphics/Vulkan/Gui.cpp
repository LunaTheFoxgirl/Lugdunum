#include <lug/Config.hpp>
#if defined LUG_SYSTEM_WINDOWS
	#define NOMINMAX
#endif
#include <algorithm>

#include <lug/Graphics/Vulkan/Gui.hpp>
#include <lug/Graphics/Render/dear_imgui/imgui.h>
#include <lug/Graphics/Vulkan/API/Builder/Image.hpp>
#include <lug/Graphics/Vulkan/API/Builder/ImageView.hpp>
#include <lug/Graphics/Vulkan/API/Builder/GraphicsPipeline.hpp>
#include <lug/Graphics/Vulkan/API/Builder/DeviceMemory.hpp>
#include <lug/Graphics/Vulkan/API/Builder/Buffer.hpp>
#include <lug/Graphics/Vulkan/API/Builder/PipelineLayout.hpp>
#include <lug/Graphics/Vulkan/API/Builder/Semaphore.hpp>
#include <lug/Graphics/Vulkan/API/Builder/Fence.hpp>
#include <lug/Graphics/Vulkan/API/Builder/DescriptorPool.hpp>
#include <lug/Graphics/Vulkan/API/Builder/DescriptorSetLayout.hpp>
#include <lug/Graphics/Vulkan/API/Builder/RenderPass.hpp>
#include <lug/Graphics/Vulkan/API/Builder/Framebuffer.hpp>
#include <lug/Graphics/Vulkan/API/Builder/CommandBuffer.hpp>
#include <lug/Graphics/Vulkan/API/Builder/CommandPool.hpp>
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

    const API::QueueFamily *graphicsQueueFamily = _renderer.getDevice().getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
    const API::Queue* graphicsQueue = graphicsQueueFamily->getQueue("queue_graphics");

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

        // Render objects
        {
            const API::RenderPass* renderPass = _pipeline.getRenderPass();

            API::CommandBuffer::CmdBeginRenderPass beginRenderPass{
                /* beginRenderPass.framebuffer */ _framebuffers[currentImageIndex],
                /* beginRenderPass.renderArea */{},
                /* beginRenderPass.clearValues */{}
            };

            beginRenderPass.renderArea.offset = { 0, 0 };
            beginRenderPass.renderArea.extent = { _window.getWidth(), _window.getHeight() };

            beginRenderPass.clearValues.resize(2);
            beginRenderPass.clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
            beginRenderPass.clearValues[1].depthStencil = { 1.0f, 0 };

            _commandBuffers[0].beginRenderPass(*renderPass, beginRenderPass);

            const API::CommandBuffer::CmdBindDescriptors commandBind{
                /* cameraBind.pipelineLayout */ *_pipeline.getLayout(),
                /* cameraBind.pipelineBindPoint */ VK_PIPELINE_BIND_POINT_GRAPHICS,
                /* cameraBind.firstSet */ 0,
                /* cameraBind.descriptorSets */{},
                /* cameraBind.dynamicOffsets */{},
            };

            _commandBuffers[0].bindDescriptorSets(commandBind);

            VkDeviceSize offsets[1] = { 0 };
            VkBuffer vertexBuffer = static_cast<VkBuffer>(*_vertexBuffers[currentImageIndex]);

            vkCmdBindVertexBuffers(static_cast<VkCommandBuffer>(_commandBuffers[currentImageIndex]), 0, 1, &vertexBuffer, offsets);
            vkCmdBindIndexBuffer(static_cast<VkCommandBuffer>(_commandBuffers[currentImageIndex]), static_cast<VkBuffer>(*_indexBuffers[currentImageIndex]), 0, VK_INDEX_TYPE_UINT16);

            // UI scale and translate via push constants
            pushConstBlock.scale = lug::Math::Vec2f{ 2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y };
            pushConstBlock.translate = lug::Math::Vec2f{ -1.0f, -1.0f };
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

            _commandBuffers[currentImageIndex].endRenderPass();
        }

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


//    API::Queue* graphicsQueue = _renderer.getQueue(VK_QUEUE_GRAPHICS_BIT, true);
//    _commandBuffers = graphicsQueue->getCommandPool().createCommandBuffers(VK_COMMAND_BUFFER_LEVEL_PRIMARY, static_cast<uint32_t>(imageViews.size()));

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
    // Create FontsTexture image
    {
        API::Builder::Image imageBuilder(_renderer.getDevice());

        imageBuilder.setUsage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        imageBuilder.setPreferedFormats({ VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT });
        imageBuilder.setTiling(VK_IMAGE_TILING_OPTIMAL);
        imageBuilder.setFeatureFlags(VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);

        VkExtent3D extent {
            extent.width = texWidth,
            extent.height = texHeight,
            extent.depth = 1
        };

        imageBuilder.setExtent(extent);

        API::Builder::DeviceMemory deviceMemoryBuilder(_renderer.getDevice());
        deviceMemoryBuilder.setMemoryFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        // Create depth buffer image
        {
            VkResult result{ VK_SUCCESS };
            if (!imageBuilder.build(*_image, &result)) {
                LUG_LOG.error("Forward::initDepthBuffers: Can't create depth buffer image: {}", result);
                return false;
            }

            if (!deviceMemoryBuilder.addImage(*_image)) {
                LUG_LOG.error("Forward::initDepthBuffers: Can't add image to device memory");
                return false;
            }
        }

        // Initialize depth buffer memory (This memory is common for all depth buffer images)
        {
            VkResult result{ VK_SUCCESS };
            if (!deviceMemoryBuilder.build(*_fontsTextureHostMemory, &result)) {
                LUG_LOG.error("Forward::initDepthBuffers: Can't create device memory: {}", result);
                return false;
            }
        }
    }

    // Create FontsTexture image view
    {
        // Create depth buffer image view
        API::Builder::ImageView imageViewBuilder(_renderer.getDevice(), *_image);

        imageViewBuilder.setFormat(_image->getFormat());
        imageViewBuilder.setAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT);

        VkResult result{ VK_SUCCESS };
        if (!imageViewBuilder.build(*_imageView, &result)) {
            LUG_LOG.error("Forward::initDepthBuffers: Can't create depth buffer image view: {}", result);
            return false;
        }
    }

    // Create staging buffers for font data upload
    {
        std::set<uint32_t> queueFamilyIndices = { (uint32_t)_renderer.getDevice().getQueueFamily(VK_QUEUE_TRANSFER_BIT, false)->getIdx()};

        API::Builder::Buffer bufferBuilder(_renderer.getDevice());
        bufferBuilder.setQueueFamilyIndices(queueFamilyIndices);
        bufferBuilder.setSize(uploadSize);
        bufferBuilder.setUsage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

        API::Buffer stagingBuffer;

        VkResult result{ VK_SUCCESS };
        if (!bufferBuilder.build(stagingBuffer, &result)) {
            LUG_LOG.error("Gui::CreateFontTexture: Can't create vertex buffer: {}", result);
            return false;
        }

        // Create device memory
        API::DeviceMemory deviceMemory;
        {
            API::Builder::DeviceMemory deviceMemoryBuilder(_renderer.getDevice());
            deviceMemoryBuilder.setMemoryFlags(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

            if (!deviceMemoryBuilder.addBuffer(stagingBuffer)) {
                LUG_LOG.error("Gui::CreateFontTexture: Can't add buffer to device memory");
                return false;
            }
            if (!deviceMemoryBuilder.build(deviceMemory, &result)) {
                LUG_LOG.error("Gui::CreateFontTexture: Can't create a device memory: {}", result);
                return false;
            }
        }

        // Copy buffer data to font image
        {
            API::Fence fence;

            API::Builder::Fence fenceBuilder(_renderer.getDevice());
            fenceBuilder.setFlags(VK_FENCE_CREATE_SIGNALED_BIT); // Signaled state
            if (!fenceBuilder.build(fence, &result)) {
                LUG_LOG.error("GUI::createFontTexture Can't create fence: {}", result);
                return false;
            }

            const API::QueueFamily* graphicsQueueFamily = _renderer.getDevice().getQueueFamily(VK_QUEUE_TRANSFER_BIT);
            if (!graphicsQueueFamily) {
                LUG_LOG.error("Forward::init: Can't find VK_QUEUE_GRAPHICS_BIT queue family");
                return false;
            }
            const API::Queue *transfertQueue = graphicsQueueFamily->getQueue("queue_transfer");
            if (!transfertQueue) {
                LUG_LOG.error("Gui::creatFontTexture: Can't find queue with name queue_graphics");
                return false;
            }
            
            API::CommandPool commandPool;
            API::Builder::CommandPool commandPoolBuilder(_renderer.getDevice(), *graphicsQueueFamily);
            VkResult result{ VK_SUCCESS };
            if (!commandPoolBuilder.build(commandPool, &result)) {
                LUG_LOG.error("Gui::creatFontTexture: Can't create a command pool: {}", result);
                return false;
            }

            API::Builder::Fence fenceBuilder(_renderer.getDevice());
            fenceBuilder.setFlags(VK_FENCE_CREATE_SIGNALED_BIT); // Signaled state

            API::Builder::CommandBuffer commandBufferBuilder(_renderer.getDevice(), commandPool);
            commandBufferBuilder.setLevel(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

            API::CommandBuffer commandBuffer;
            if (!commandBufferBuilder.build(commandBuffer, &result)) {
                LUG_LOG.error("Gui::creatFontTexture: Can't create the command buffer: {}", result);
                return false;
            }

            commandBuffer.begin();

            commandBuffer.
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
                static_cast<VkBuffer>(stagingBuffer),
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
            if (!fence.wait()) {
                LUG_LOG.error("Gui: Can't vkWaitForFences");
                return false;
            }

           fence.destroy();
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

    API::Builder::Semaphore semaphoreBuilder(_renderer.getDevice());
    API::Builder::Fence fenceBuilder(_renderer.getDevice());
    fenceBuilder.setFlags(VK_FENCE_CREATE_SIGNALED_BIT);

    for (int i = 0; i < 3; i++) {
        {
            // Create the Semaphores
            VkResult result{ VK_SUCCESS };
            if (!semaphoreBuilder.build(_guiSemaphores[i], &result)) {
                LUG_LOG.error("View::init: Can't create semaphore: {}", result);
                return false;
            }

        }

        {
            // Create the Fences
            VkResult result{ VK_SUCCESS };
            if (!fenceBuilder.build(_guiFences[i], &result)) {
                LUG_LOG.error("Forward::init: Can't create swapchain fence: {}", result);
                return false;
            }
        }
    }

    return true;
}

bool Gui::initPipeline() {
    auto device = &_renderer.getDevice();
    API::Builder::GraphicsPipeline graphicsPipelineBuilder(_renderer.getDevice());

    // Set shaders state
    if (!graphicsPipelineBuilder.setShaderFromFile(VK_SHADER_STAGE_VERTEX_BIT, "main", _renderer.getInfo().shadersRoot + "gui.vert.spv")
        || !graphicsPipelineBuilder.setShaderFromFile(VK_SHADER_STAGE_FRAGMENT_BIT, "main", _renderer.getInfo().shadersRoot + "gui.frag.spv")) {
        LUG_LOG.error("Gui: Can't create pipeline's shaders.");
        return false;
    }

    // Set vertex input state
    auto vertexBinding = graphicsPipelineBuilder.addInputBinding(sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX);

    vertexBinding.addAttributes(VK_FORMAT_R32G32B32_SFLOAT, 0); // Position
    vertexBinding.addAttributes(VK_FORMAT_R32G32B32_SFLOAT, offsetof(ImDrawVert, uv)); // UV
    vertexBinding.addAttributes(VK_FORMAT_R8G8B8A8_UNORM, offsetof(ImDrawVert, col)); // Color

    // Set viewport state
    const VkViewport viewport{
        /* viewport.x */ 0.0f,
        /* viewport.y */ 0.0f,
        /* viewport.width */ 0.0f,
        /* viewport.height */ 0.0f,
        /* viewport.minDepth */ 0.0f,
        /* viewport.maxDepth */ 1.0f,
    };

    const VkRect2D scissor{
        /* scissor.offset */{ 0, 0 },
        /* scissor.extent */{ 0, 0 }
    };

    auto viewportState = graphicsPipelineBuilder.getViewportState();
    viewportState.addViewport(viewport);
    viewportState.addScissor(scissor);

    // Set rasterization state
    auto rasterizationState = graphicsPipelineBuilder.getRasterizationState();
    rasterizationState.setFrontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE);
    rasterizationState.setPolygonMode(VK_POLYGON_MODE_FILL);
    rasterizationState.setLineWidth(1.0f);

    // Set multisampling state
    auto multisamplingState = graphicsPipelineBuilder.getMultisampleState();
    multisamplingState.setRasterizationSamples(VK_SAMPLE_COUNT_1_BIT);

    // Set depth stencil state
    auto depthStencilState = graphicsPipelineBuilder.getDepthStencilState();

    // Set color blend state
    const VkPipelineColorBlendAttachmentState colorBlendAttachment{
        /* colorBlendAttachment.blendEnable */ VK_TRUE,
        /* colorBlendAttachment.srcColorBlendFactor */ VK_BLEND_FACTOR_SRC_ALPHA,
        /* colorBlendAttachment.dstColorBlendFactor */ VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        /* colorBlendAttachment.colorBlendOp */ VK_BLEND_OP_ADD,
        /* colorBlendAttachment.srcAlphaBlendFactor */ VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        /* colorBlendAttachment.dstAlphaBlendFactor */ VK_BLEND_FACTOR_ZERO,
        /* colorBlendAttachment.alphaBlendOp */ VK_BLEND_OP_ADD,
        /* colorBlendAttachment.colorWriteMask */ VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };

    auto colorBlendState = graphicsPipelineBuilder.getColorBlendState();
    colorBlendState.addAttachment(colorBlendAttachment);
    colorBlendState.enableLogicOp(VK_LOGIC_OP_COPY);

    // Set dynamic states
    graphicsPipelineBuilder.setDynamicStates({
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    });

    // Set pipeline layout
    {
        std::vector<Vulkan::API::DescriptorSetLayout> descriptorSetLayouts(1);
        {
            {
                API::Builder::DescriptorPool descriptorPoolBuilder(_renderer.getDevice());

                descriptorPoolBuilder.setFlags(0);
                descriptorPoolBuilder.setMaxSets(1);
                VkDescriptorPoolSize poolSize{
                    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    poolSize.descriptorCount = 1
                };
                descriptorPoolBuilder.setPoolSizes({ poolSize });

                VkResult result{ VK_SUCCESS };
                if (!descriptorPoolBuilder.build(_descriptorPool, &result)) {
                    LUG_LOG.error("Window::initDescriptorPool: Can't create the descriptor pool: {}", result);
                    return false;
                }
            }

            {
                VkResult result{ VK_SUCCESS };
                API::Builder::DescriptorSetLayout descriptorSetLayoutBuilder(_renderer.getDevice());

                {
                    // Camera uniform buffer
                    const VkDescriptorSetLayoutBinding binding{
                        /* binding.binding */ 0,
                        /* binding.descriptorType */ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        /* binding.descriptorCount */ 1,
                        /* binding.stageFlags */ VK_SHADER_STAGE_FRAGMENT_BIT,
                        /* binding.pImmutableSamplers */ nullptr
                    };

                    descriptorSetLayoutBuilder.setBindings({ binding });
                    if (!descriptorSetLayoutBuilder.build(descriptorSetLayouts[0], &result)) {
                        LUG_LOG.error("ForwardRenderer: Can't create pipeline descriptor sets layout 0: {}", result);
                        return false;
                    }
                }
            }
        }


        VkPushConstantRange pushConstant = {
            pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            pushConstant.offset = 0,
            pushConstant.size = sizeof(PushConstBlock)
        };

        VkDescriptorSetLayout set_layout[1] = { static_cast<VkDescriptorSetLayout>(descriptorSetLayouts[0]) };
        VkPipelineLayoutCreateInfo createInfo{
            createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            createInfo.pNext = nullptr,
            createInfo.flags = 0,
            createInfo.setLayoutCount = 1,
            createInfo.pSetLayouts = set_layout,
            createInfo.pushConstantRangeCount = 1,
            createInfo.pPushConstantRanges = &pushConstant
        };

        API::Builder::PipelineLayout pipelineLayoutBuilder(_renderer.getDevice());

        pipelineLayoutBuilder.setPushConstants({ pushConstant });
        pipelineLayoutBuilder.setDescriptorSetLayouts(std::move(descriptorSetLayouts));

        VkResult result{ VK_SUCCESS };
        API::PipelineLayout pipelineLayout;
        if (!pipelineLayoutBuilder.build(pipelineLayout, &result)) {
            LUG_LOG.error("ForwardRenderer: Can't create pipeline layout: {}", result);
            return false;
        }

        graphicsPipelineBuilder.setPipelineLayout(std::move(pipelineLayout));
    }

    // Set render pass
    {
        API::Builder::RenderPass renderPassBuilder(_renderer.getDevice());

        auto colorFormat = _window.getSwapchain().getFormat().format;
    
        // Create renderpass
        {
            const VkAttachmentDescription colorAttachment{
                /* colorAttachment.flags */ 0,
                /* colorAttachment.format */ colorFormat,
                /* colorAttachment.samples */ VK_SAMPLE_COUNT_1_BIT,
                /* colorAttachment.loadOp */ VK_ATTACHMENT_LOAD_OP_LOAD,
                /* colorAttachment.storeOp */ VK_ATTACHMENT_STORE_OP_STORE,
                /* colorAttachment.stencilLoadOp */ VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                /* colorAttachment.stencilStoreOp */ VK_ATTACHMENT_STORE_OP_DONT_CARE,
                /* colorAttachment.initialLayout */ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                /* colorAttachment.finalLayout */ VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            };

            auto colorAttachmentIndex = renderPassBuilder.addAttachment(colorAttachment);
    
            const API::Builder::RenderPass::SubpassDescription subpassDescription{
                /* subpassDescription.pipelineBindPoint */ VK_PIPELINE_BIND_POINT_GRAPHICS,
                /* subpassDescription.inputAttachments */{},
                /* subpassDescription.colorAttachments */{{colorAttachmentIndex, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}},
                /* subpassDescription.resolveAttachments */{},
                /* subpassDescription.depthStencilAttachment */{},
                /* subpassDescription.preserveAttachments */{},
            };

            renderPassBuilder.addSubpass(subpassDescription);

            VkResult result{ VK_SUCCESS };
            API::RenderPass renderPass;
            if (!renderPassBuilder.build(renderPass, &result)) {
                LUG_LOG.error("ForwardRenderer: Can't create render pass: {}", result);
                return false;
            }

            graphicsPipelineBuilder.setRenderPass(std::move(renderPass), 0);

        }
    
        VkResult result{ VK_SUCCESS };
        if (!graphicsPipelineBuilder.build(_pipeline, &result)) {
            LUG_LOG.error("ForwardRenderer: Can't create pipeline: {}", result);
            return false;
        }
    }

    return true;
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
    const API::RenderPass* renderPass = _pipeline.getRenderPass();
    auto device = &_renderer.getDevice();

    VkResult result;

    _framebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); i++) {
        // Create depth buffer image view
        API::Builder::Framebuffer framebufferBuilder(_renderer.getDevice());

        framebufferBuilder.setRenderPass(renderPass);
        framebufferBuilder.addAttachment(imageViews[i].get());
        framebufferBuilder.setWidth(imageViews[i]->getImage()->getExtent().width);
        framebufferBuilder.setHeight(imageViews[i]->getImage()->getExtent().height);

        VkResult result{ VK_SUCCESS };
        if (!framebufferBuilder.build(_framebuffers[i], &result)) {
            LUG_LOG.error("Forward::initFramebuffers: Can't create framebuffer: {}", result);
            return false;
        }
    }

    return true;
}

void Gui::updateBuffers(uint32_t currentImageIndex) {
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
        API::Builder::Buffer bufferBuilder(_renderer.getDevice());
        bufferBuilder.setUsage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);


        // Vertex buffer
        if ((_vertexBuffers[currentImageIndex] == nullptr) || (_vertexCounts[currentImageIndex] != imDrawData->TotalVtxCount)) {
            {
                std::set<uint32_t> queueFamilyIndices = { (uint32_t)_renderer.getDevice().getQueueFamily(VK_QUEUE_TRANSFER_BIT, false)->getIdx() };

                bufferBuilder.setQueueFamilyIndices(queueFamilyIndices);
                bufferBuilder.setSize(vertexBufferSize);

                VkResult result{ VK_SUCCESS };
                if (!bufferBuilder.build(*_vertexBuffers[currentImageIndex], &result)) {
                    LUG_LOG.error("Gui::updateBuffers: Can't create vertex buffer: {}", result);
                    return;
                }
            }

            _vertexCounts[currentImageIndex] = imDrawData->TotalVtxCount;
        }

        // IndexBuffer
        if ((_indexBuffers[currentImageIndex] == nullptr) || (_indexCounts[currentImageIndex] < imDrawData->TotalIdxCount)) {
            {
                std::set<uint32_t> queueFamilyIndices = { (uint32_t)_renderer.getDevice().getQueueFamily(VK_QUEUE_TRANSFER_BIT, false)->getIdx() };

                bufferBuilder.setQueueFamilyIndices(queueFamilyIndices);
                bufferBuilder.setSize(indexBufferSize);

                VkResult result{ VK_SUCCESS };
                if (!bufferBuilder.build(*_vertexBuffers[currentImageIndex], &result)) {
                    LUG_LOG.error("Gui::updateBuffers: Can't create vertex buffer: {}", result);
                    return;
                }
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
