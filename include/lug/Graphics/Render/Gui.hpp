#pragma once

#include <unordered_map>
#include <lug/Graphics/Export.hpp>
#include <lug/Graphics/Light/Light.hpp>
#include <lug/Graphics/Vulkan/API/Buffer.hpp>
#include <lug/Graphics/Vulkan/API/DescriptorSet.hpp>
#include <lug/Graphics/Vulkan/API/DeviceMemory.hpp>
#include <lug/Graphics/Vulkan/API/Fence.hpp>
#include <lug/Graphics/Vulkan/API/Image.hpp>
#include <lug/Graphics/Vulkan/API/ImageView.hpp>
#include <lug/Graphics/Vulkan/Render/BufferPool.hpp>
#include <lug/Graphics/Vulkan/Render/Technique/Technique.hpp>
#include <lug/System/Clock.hpp>
#include <lug/Math/Vector.hpp>

#include <lug/Graphics/Render/dear_imgui/imgui.h>

namespace lug {
namespace Graphics {
namespace Vulkan {
namespace Render {

class LUG_GRAPHICS_API Gui {
public:
    // UI params are set via push constants
    struct PushConstBlock {
        lug::Math::Vec2<float> scale;
        lug::Math::Vec2<float> translate;
    } pushConstBlock;

private:
    struct DepthBuffer {
        std::unique_ptr<API::Image> image;
        std::unique_ptr<API::ImageView> imageView;
    };

    struct FrameData {
        DepthBuffer depthBuffer;
        API::Framebuffer frameBuffer;
        API::Fence fence;

        // There is actually only 1 command buffer
        std::vector<API::CommandBuffer> cmdBuffers;

        std::vector<BufferPool::SubBuffer*> freeSubBuffers;
    };

public:
    Gui();

    Gui(const Gui&) = delete;
    Gui(Gui&&) = delete;

    Gui& operator=(const Gui&) = delete;
    Gui& operator=(Gui&&) = delete;

    ~Gui() = default;

    bool beginFrame();
    bool render();
    bool endFrame();
    void destroy();

    void init(float width, float height);
    void initRessources(VkRenderPass renderPass, VkQueue copyQueue);

    bool initDepthBuffers(const std::vector<std::unique_ptr<API::ImageView>>& imageViews);
    bool initFramebuffers(const std::vector<std::unique_ptr<API::ImageView>>& imageViews);

private:
    std::unique_ptr<API::DeviceMemory> _depthBufferMemory{nullptr};
    std::vector<FrameData> _framesData;

    // Vulkan resources for rendering the UI
    VkSampler sampler;
    VkBuffer vertexBuffer;
    VkBuffer indexBuffer;
    int32_t vertexCount = 0;
    int32_t indexCount = 0;
    VkDeviceMemory fontMemory = VK_NULL_HANDLE;
    VkImage fontImage = VK_NULL_HANDLE;
    VkImageView fontView = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache;
    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    VkDescriptorPool descriptorPool;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorSet descriptorSet;
};

} // Render
} // Vulkan
} // Graphics
} // lug
