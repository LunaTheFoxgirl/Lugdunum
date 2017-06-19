#include <lug/Graphics/Vulkan/Render/Technique/Forward.hpp>

#include <algorithm>

#include <lug/Config.hpp>
#include <lug/Graphics/Render/Light.hpp>
#include <lug/Graphics/Render/Queue.hpp>
#include <lug/Graphics/Scene/Node.hpp>
#include <lug/Graphics/Vulkan/API/Builder/CommandBuffer.hpp>
#include <lug/Graphics/Vulkan/API/Builder/CommandPool.hpp>
#include <lug/Graphics/Vulkan/API/Builder/DeviceMemory.hpp>
#include <lug/Graphics/Vulkan/API/Builder/DescriptorSetLayout.hpp>
#include <lug/Graphics/Vulkan/API/Builder/Fence.hpp>
#include <lug/Graphics/Vulkan/API/Builder/Framebuffer.hpp>
#include <lug/Graphics/Vulkan/API/Builder/GraphicsPipeline.hpp>
#include <lug/Graphics/Vulkan/API/Builder/Image.hpp>
#include <lug/Graphics/Vulkan/API/Builder/ImageView.hpp>
#include <lug/Graphics/Vulkan/API/Builder/PipelineLayout.hpp>
#include <lug/Graphics/Vulkan/API/Builder/RenderPass.hpp>
#include <lug/Graphics/Vulkan/Render/Material.hpp>
#include <lug/Graphics/Vulkan/Render/Mesh.hpp>
#include <lug/Graphics/Vulkan/Render/View.hpp>
#include <lug/Graphics/Vulkan/Renderer.hpp>
#include <lug/Math/Matrix.hpp>
#include <lug/Math/Vector.hpp>
#include <lug/Math/Geometry/Transform.hpp>
#include <lug/System/Logger/Logger.hpp>

namespace lug {
namespace Graphics {
namespace Vulkan {
namespace Render {
namespace Technique {

using MeshInstance = ::lug::Graphics::Scene::Node::MeshInstance;

Forward::Forward(Renderer& renderer, const Render::View& renderView) :
    Technique(renderer, renderView) {}

bool Forward::render(
    const ::lug::Graphics::Render::Queue& renderQueue,
    const API::Semaphore& imageReadySemaphore,
    const API::Semaphore& drawCompleteSemaphore,
    uint32_t currentImageIndex) {
    FrameData& frameData = _framesData[currentImageIndex];

    auto& viewport = _renderView.getViewport();

    frameData.fence.wait();
    frameData.fence.reset();
    auto& cmdBuffer = frameData.cmdBuffers[0];

    for (auto subBuffer: frameData.freeSubBuffers) {
        subBuffer->free();
    }

    frameData.freeSubBuffers.clear();

    if (!cmdBuffer.reset() || !cmdBuffer.begin()) {
        return false;
    }

    // Init render pass
    {
        const VkViewport vkViewport{
            /* vkViewport.x */ viewport.offset.x,
            /* vkViewport.y */ viewport.offset.y,
            /* vkViewport.width */ viewport.extent.width,
            /* vkViewport.height */ viewport.extent.height,
            /* vkViewport.minDepth */ viewport.minDepth,
            /* vkViewport.maxDepth */ viewport.maxDepth,
        };

        const VkRect2D scissor{
            /* scissor.offset */ {
                (int32_t)_renderView.getScissor().offset.x,
                (int32_t)_renderView.getScissor().offset.y},
            /* scissor.extent */ {
                (uint32_t)_renderView.getScissor().extent.width,
                (uint32_t)_renderView.getScissor().extent.height
            }
        };

        cmdBuffer.setViewport({vkViewport});
        cmdBuffer.setScissor({scissor});
    }

    Resource::SharedPtr<lug::Graphics::Render::Camera::Camera> camera = _renderView.getCamera();
    // Update camera buffer data
    BufferPool::SubBuffer* cameraBuffer = _subBuffers[static_cast<uint32_t>(_renderView.getCamera()->getHandle())];
    {
        // Return buffer to pool
        if (cameraBuffer) {
            frameData.freeSubBuffers.push_back(cameraBuffer);
            cameraBuffer = nullptr;
        }

        // Init buffer
        if (!cameraBuffer) {
            cameraBuffer = _cameraPool->allocate();
            if (!cameraBuffer) {
                return false;
            }

            _subBuffers[static_cast<uint32_t>(_renderView.getCamera()->getHandle())] = cameraBuffer;

        }

        // Update buffer
        // TODO(nokitoo): handle dirty
        const Math::Mat4x4f cameraData[] = {
            camera->getViewMatrix(),
            camera->getProjectionMatrix()
        };

        cmdBuffer.updateBuffer(*cameraBuffer->buffer, cameraData, sizeof(cameraData), cameraBuffer->offset);
    }

    // Update lights buffers data
    // TODO(nokitoo): batch lights and handle dirty
    {
        for (std::size_t i = 0; i < renderQueue.getLightsNb(); ++i) {
            const auto& light = renderQueue.getLights()[i]->getLight();

            BufferPool::SubBuffer* lightBuffer = _subBuffers[static_cast<uint32_t>(light->getHandle())];

            // Return buffer to pool
            if (lightBuffer) {
                frameData.freeSubBuffers.push_back(lightBuffer);
                lightBuffer = nullptr;
            }

            // Init buffer
            if (!lightBuffer) {
                lightBuffer = _lightsPool->allocate();
                if (!lightBuffer) {
                    return false;
                }

                _subBuffers[static_cast<uint32_t>(light->getHandle())] = lightBuffer;
            }

            // Update buffer
            // TODO(nokitoo): handle dirty
            ::lug::Graphics::Render::Light::Data lightData;
            light->getData(lightData);
            cmdBuffer.updateBuffer(*lightBuffer->buffer, &lightData, sizeof(lightData), lightBuffer->offset);
        }
    }

    // Render objects
    {
        // All the pipelines have the same renderPass
        const API::RenderPass* renderPass = _renderer.getPipeline(0)->getPipelineAPI().getRenderPass();

        API::CommandBuffer::CmdBeginRenderPass beginRenderPass{
            /* beginRenderPass.framebuffer */ frameData.framebuffer,
            /* beginRenderPass.renderArea */ {},
            /* beginRenderPass.clearValues */ {}
        };

        beginRenderPass.renderArea.offset = {static_cast<int32_t>(viewport.offset.x), static_cast<int32_t>(viewport.offset.y)};
        beginRenderPass.renderArea.extent = {static_cast<uint32_t>(viewport.extent.width), static_cast<uint32_t>(viewport.extent.height)};

        beginRenderPass.clearValues.resize(2);
        beginRenderPass.clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        beginRenderPass.clearValues[1].depthStencil = {1.0f, 0};

        cmdBuffer.beginRenderPass(*renderPass, beginRenderPass);


        // TODO(nokitoo) Bind camera descriptor set

        // Blend constants are used as dst blend factor
        // We set them to 0 so that there is no blending
        {
            const float blendConstants[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            cmdBuffer.setBlendConstants(blendConstants);
        }

        for (std::size_t i = 0; i < renderQueue.getLightsNb(); ++i) {

            {
                if (i == 1) {
                    // Blend constants are used as dst blend factor
                    // Now the depth buffer is filled, we can set the blend constants to 1 to enable blending
                    const float blendConstants[4] = {1.0f, 1.0f, 1.0f, 1.0f};
                    cmdBuffer.setBlendConstants(blendConstants);
                }
            }

            //const auto& light = renderQueue.getLights()[i]->getLight();

            //cmdBuffer.bindPipeline(_pipeline);

            //BufferPool::SubBuffer* lightBuffer = _subBuffers[static_cast<uint32_t>(light->getHandle())];

            // TODO(nokitoo) Bind lights descriptor set

            for (std::size_t j = 0; j < renderQueue.getMeshsNb(); ++j) {
                Scene::Node* node = renderQueue.getMeshs()[j];
                lug::Graphics::Render::Mesh* mesh = node->getMeshInstance().mesh.get();

                const Math::Mat4x4f pushConstants[] = {
                    node->getTransform()
                };

                const API::CommandBuffer::CmdPushConstants cmdPushConstants{
                    /* cmdPushConstants.layout */ static_cast<VkPipelineLayout>(*_renderer.getPipeline(0)->getPipelineAPI().getLayout()),
                    /* cmdPushConstants.stageFlags */ VK_SHADER_STAGE_VERTEX_BIT,
                    /* cmdPushConstants.offset */ 0,
                    /* cmdPushConstants.size */ sizeof(pushConstants),
                    /* cmdPushConstants.values */ pushConstants
                };
                cmdBuffer.pushConstants(cmdPushConstants);

                for (const auto& primitiveSet : mesh->getPrimitiveSets()) {
                    if (!primitiveSet.position) {
                        LUG_LOG.warn("Forward::render: Mesh with no vertices data");
                    }

                    cmdBuffer.bindVertexBuffers({static_cast<API::Buffer*>(primitiveSet.position->_data)}, {0});

                    // TODO(nokitoo) bind other attributes

                    if (primitiveSet.indices) {
                        API::Buffer* indicesBuffer = static_cast<API::Buffer*>(primitiveSet.indices->_data);
                        cmdBuffer.bindIndexBuffer(*indicesBuffer, VK_INDEX_TYPE_UINT32);
                        const API::CommandBuffer::CmdDrawIndexed cmdDrawIndexed {
                            /* cmdDrawIndexed.indexCount */ static_cast<uint32_t>(indicesBuffer->getRequirements().size),
                            /* cmdDrawIndexed.instanceCount */ 1,
                        };

                        cmdBuffer.drawIndexed(cmdDrawIndexed);
                    }
                    else {
                        API::Buffer* verticesBuffer = static_cast<API::Buffer*>(primitiveSet.indices->_data);
                        const API::CommandBuffer::CmdDraw cmdDraw {
                            /* cmdDrawIndexed.vertexCount */ static_cast<uint32_t>(verticesBuffer->getRequirements().size),
                            /* cmdDrawIndexed.instanceCount */ 1,
                        };

                        cmdBuffer.draw(cmdDraw);
                    }
                }


            }
        }

        cmdBuffer.endRenderPass();
    }

    if (!cmdBuffer.end()) {
        return false;
    }

    return _graphicsQueue->submit(
        cmdBuffer,
        {static_cast<VkSemaphore>(drawCompleteSemaphore)},
        {static_cast<VkSemaphore>(imageReadySemaphore)},
        {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
        static_cast<VkFence>(frameData.fence)
    );
}

bool Forward::init(API::DescriptorPool* descriptorPool, const std::vector<API::ImageView>& imageViews) {
    (void)descriptorPool;
    (void)imageViews;

    _framesData.resize(imageViews.size());

    const API::QueueFamily* graphicsQueueFamily = _renderer.getDevice().getQueueFamily(VK_QUEUE_GRAPHICS_BIT);
    if (!graphicsQueueFamily) {
        LUG_LOG.error("Forward::init: Can't find VK_QUEUE_GRAPHICS_BIT queue family");
        return false;
    }
    _graphicsQueue = graphicsQueueFamily->getQueue("queue_graphics");
    if (!_graphicsQueue) {
        LUG_LOG.error("Forward::init: Can't find queue with name queue_graphics");
        return false;
    }

    API::Builder::CommandPool commandPoolBuilder(_renderer.getDevice(), *graphicsQueueFamily);
    VkResult result{VK_SUCCESS};
    if (!commandPoolBuilder.build(_commandPool, &result)) {
        LUG_LOG.error("Forward::init: Can't create a command pool: {}", result);
        return false;
    }

    API::Builder::Fence fenceBuilder(_renderer.getDevice());
    fenceBuilder.setFlags(VK_FENCE_CREATE_SIGNALED_BIT); // Signaled state

    API::Builder::CommandBuffer commandBufferBuilder(_renderer.getDevice(), _commandPool);
    commandBufferBuilder.setLevel(VK_COMMAND_BUFFER_LEVEL_PRIMARY);

    for (uint32_t i = 0; i < _framesData.size(); ++i) {
        // Create the Fence
        if (!fenceBuilder.build(_framesData[i].fence, &result)) {
            LUG_LOG.error("Forward::init: Can't create swapchain fence: {}", result);
            return false;
        }

        // Create command buffers
        _framesData[i].cmdBuffers.resize(1); // The builder will build according to the array size.

        if (!commandBufferBuilder.build(_framesData[i].cmdBuffers, &result)) {
            LUG_LOG.error("Forward::init: Can't create the command buffer: {}", result);
            return false;
        }
    }

    std::set<uint32_t> queueFamilyIndices = {graphicsQueueFamily->getIdx()};
    _cameraPool = std::make_unique<BufferPool>(
        (uint32_t)_framesData.size(),
        (uint32_t)sizeof(Math::Mat4x4f) * 2,
        _renderer.getDevice(),
        queueFamilyIndices
    );

    _lightsPool = std::make_unique<BufferPool>(
        static_cast<uint32_t>(_framesData.size() * 50),
        static_cast<uint32_t>(sizeof(::lug::Graphics::Render::Light::Data)),
        _renderer.getDevice(),
        queueFamilyIndices
    );

    return initDepthBuffers(imageViews) && initFramebuffers(imageViews);
}

void Forward::destroy() {
    _graphicsQueue->waitIdle();

    _framesData.clear();

    _depthBufferMemory.destroy();

    _cameraPool.reset();
    _lightsPool.reset();

    _commandPool.destroy();
}

bool Forward::initDepthBuffers(const std::vector<API::ImageView>& imageViews) {
    API::Builder::Image imageBuilder(_renderer.getDevice());

    imageBuilder.setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    imageBuilder.setPreferedFormats({VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT});
    imageBuilder.setFeatureFlags(VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);

    _framesData.resize(imageViews.size());

    API::Builder::DeviceMemory deviceMemoryBuilder(_renderer.getDevice());
    deviceMemoryBuilder.setMemoryFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // Create images and add them to API::Builder::DeviceMemory
    for (uint32_t i = 0; i < imageViews.size(); ++i) {
        const VkExtent3D extent{
            /* extent.width */ imageViews[i].getImage()->getExtent().width,
            /* extent.height */ imageViews[i].getImage()->getExtent().height,
            /* extent.depth */ 1
        };

        imageBuilder.setExtent(extent);

        // Create depth buffer image
        {
            VkResult result{VK_SUCCESS};
            if (!imageBuilder.build(_framesData[i].depthBuffer.image, &result)) {
                LUG_LOG.error("Forward::initDepthBuffers: Can't create depth buffer image: {}", result);
                return false;
            }

            if (!deviceMemoryBuilder.addImage(_framesData[i].depthBuffer.image)) {
                LUG_LOG.error("Forward::initDepthBuffers: Can't add image to device memory");
                return false;
            }
        }
    }

    // Initialize depth buffer memory (This memory is common for all depth buffer images)
    {
        VkResult result{VK_SUCCESS};
        if (!deviceMemoryBuilder.build(_depthBufferMemory, &result)) {
            LUG_LOG.error("Forward::initDepthBuffers: Can't create device memory: {}", result);
            return false;
        }
    }

    // Create images views
    for (uint32_t i = 0; i < imageViews.size(); ++i) {

        // Create depth buffer image view
        API::Builder::ImageView imageViewBuilder(_renderer.getDevice(), _framesData[i].depthBuffer.image);

        imageViewBuilder.setFormat(_framesData[i].depthBuffer.image.getFormat());
        imageViewBuilder.setAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT);

        VkResult result{VK_SUCCESS};
        if (!imageViewBuilder.build(_framesData[i].depthBuffer.imageView, &result)) {
            LUG_LOG.error("Forward::initDepthBuffers: Can't create depth buffer image view: {}", result);
            return false;
        }
    }

    return true;
}

bool Forward::initFramebuffers(const std::vector<API::ImageView>& imageViews) {
    // The lights pipelines renderpass are compatible, so we don't need to create different frame buffers for each pipeline
    const API::RenderPass* renderPass = _renderer.getPipeline(0)->getPipelineAPI().getRenderPass();

    _framesData.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++) {
        // Create depth buffer image view
        API::Builder::Framebuffer framebufferBuilder(_renderer.getDevice());

        framebufferBuilder.setRenderPass(renderPass);
        framebufferBuilder.addAttachment(&imageViews[i]);
        framebufferBuilder.addAttachment(&_framesData[i].depthBuffer.imageView);
        framebufferBuilder.setWidth(imageViews[i].getImage()->getExtent().width);
        framebufferBuilder.setHeight(imageViews[i].getImage()->getExtent().height);

        VkResult result{VK_SUCCESS};
        if (!framebufferBuilder.build(_framesData[i].framebuffer, &result)) {
            LUG_LOG.error("Forward::initFramebuffers: Can't create framebuffer: {}", result);
            return false;
        }
    }

    return true;
}

} // Technique
} // Render
} // Vulkan
} // Graphics
} // lug
