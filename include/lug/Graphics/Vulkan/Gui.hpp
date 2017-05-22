#pragma once

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
        lug::Math::Vec2f scale;
        lug::Math::Vec2f translate;
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
    bool endFrame(const std::vector<VkSemaphore>& waitSemaphores, uint32_t currentImageIndex);
    const Vulkan::API::Semaphore& getGuiSemaphore(uint32_t currentImageIndex) const;
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
    std::unique_ptr<Vulkan::API::PipelineLayout> _pipelineLayout;
    Vulkan::API::Pipeline _pipeline;
    std::vector<Vulkan::API::Framebuffer> _framebuffers;
    std::unique_ptr<Vulkan::API::Buffer> _indexBuffer{nullptr};
    std::unique_ptr<Vulkan::API::Buffer> _vertexBuffer{nullptr};
    std::unique_ptr<Vulkan::API::DeviceMemory> _vertexDeviceMemory{nullptr};
    std::unique_ptr<Vulkan::API::DeviceMemory> _indexDeviceMemory{nullptr};

    int vertexCount = 0;
    int indexCount = 0;

    std::vector<Vulkan::API::CommandBuffer> _commandBuffers;
    std::vector<Vulkan::API::Semaphore> _guiSemaphores;
    size_t realSize;
};

#include "Gui.inl"

} // Vulkan
} // Graphics
} // lug
