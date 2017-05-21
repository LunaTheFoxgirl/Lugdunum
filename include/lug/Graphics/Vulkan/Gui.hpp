#pragma once

// #include <unordered_map>
// #include <lug/Graphics/Export.hpp>
// #include <lug/Graphics/Light/Light.hpp>
// #include <lug/Graphics/Vulkan/API/Buffer.hpp>
// #include <lug/Graphics/Vulkan/API/DescriptorSet.hpp>
// #include <lug/Graphics/Vulkan/API/DeviceMemory.hpp>
// #include <lug/Graphics/Vulkan/API/Fence.hpp>
// #include <lug/Graphics/Vulkan/API/Image.hpp>
// #include <lug/Graphics/Vulkan/API/ImageView.hpp>
// #include <lug/Graphics/Vulkan/Render/BufferPool.hpp>
// #include <lug/Graphics/Vulkan/Render/Technique/Technique.hpp>
// #include <lug/System/Clock.hpp>
// #include <lug/Math/Vector.hpp>

#include <lug/Graphics/Vulkan/API/Buffer.hpp>
#include <lug/Graphics/Vulkan/API/DeviceMemory.hpp>
#include <lug/Graphics/Vulkan/API/Framebuffer.hpp>
#include <lug/Graphics/Vulkan/API/Pipeline.hpp>
#include <lug/Graphics/Vulkan/Renderer.hpp>
#include <lug/Graphics/Vulkan/API/CommandBuffer.hpp>

namespace lug {
namespace Graphics {
namespace Vulkan {

class LUG_GRAPHICS_API Gui {
public:
    // UI params are set via push constants
    struct PushConstBlock {
        lug::Math::Vec2<float> scale;
        lug::Math::Vec2<float> translate;
    } pushConstBlock;
public:
    Gui() = delete;

    Gui(Renderer& renderer, Render::Window &window);

    Gui(const Gui&) = delete;
//    Gui(lug::Graphics::Render::Window&&) = delete;

    Gui& operator=(const Gui&) = delete;
    Gui& operator=(Gui&&) = delete;

    ~Gui();

    bool beginFrame();
    // bool render();
    bool endFrame(API::Semaphore& allDrawsFinishedSemaphore, uint32_t currentImageIndex);
    // void destroy();

    const Vulkan::API::Semaphore& Gui::getGuiSemaphore() const;
    bool init(const std::vector<std::unique_ptr<API::ImageView>>& imageViews);
    bool createFontsTexture();
    bool initFramebuffers(const std::vector<std::unique_ptr<API::ImageView>>& imageViews);

private:
    void updateBuffers();

private:
    Renderer& _renderer;
    Render::Window& _window;
    Vulkan::API::Fence _fence;
    std::unique_ptr<API::Image> _image = nullptr;
    std::unique_ptr<API::ImageView> _imageView = nullptr;
    std::unique_ptr<API::DeviceMemory> _fontsTextureHostMemory = nullptr;
    std::unique_ptr<Vulkan::API::DescriptorSetLayout> _descriptorSetLayout;
    VkSampler _sampler;
    Vulkan::API::DescriptorPool _descriptorPool;

    std::vector<Vulkan::API::DescriptorSet> _descriptorSet;
//    VkDescriptorSet _descriptorSet;

    std::unique_ptr<Vulkan::API::PipelineLayout> _pipelineLayout;

    Vulkan::API::Pipeline _pipeline;
    Vulkan::API::Framebuffer _framebuffer;

    std::unique_ptr<Vulkan::API::Buffer> _indexBuffer{nullptr};
    std::unique_ptr<Vulkan::API::Buffer> _vertexBuffer{nullptr};
    std::unique_ptr<Vulkan::API::DeviceMemory> _vertexDeviceMemory{nullptr};
    std::unique_ptr<Vulkan::API::DeviceMemory> _indexDeviceMemory{nullptr};

    int vertexCount = 0;
    int indexCount = 0;

    std::vector<Vulkan::API::CommandBuffer> _commandBuffers;
    Vulkan::API::Semaphore _guiSemaphore;
    size_t realSize;

    // API::Queue* _graphicsQueue{nullptr};
    // API::Queue* _Queue{nullptr};

    // std::unique_ptr<API::Image> image;
    // std::unique_ptr<API::ImageView> imageView;

    // // Vulkan resources for rendering the UI
    // VkSampler sampler;
    // VkBuffer vertexBuffer;
    // VkBuffer indexBuffer;
    // int32_t indexCount = 0;
    // VkDeviceMemory fontMemory = VK_NULL_HANDLE;
    // VkImage fontImage = VK_NULL_HANDLE;
    // VkImageView fontView = VK_NULL_HANDLE;
};

#include "Gui.inl"

} // Vulkan
} // Graphics
} // lug
