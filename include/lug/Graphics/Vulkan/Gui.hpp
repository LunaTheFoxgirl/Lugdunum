#pragma once

#include <lug/Graphics/Vulkan/API/Buffer.hpp>
#include <lug/Graphics/Vulkan/API/DeviceMemory.hpp>
#include <lug/Graphics/Vulkan/API/Framebuffer.hpp>
#include <lug/Graphics/Vulkan/API/GraphicsPipeline.hpp>
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

    Gui& operator=(const Gui&) = delete;
    Gui& operator=(Gui&&) = delete;

    ~Gui();

    bool beginFrame(const lug::System::Time &elapsedTime);
    bool endFrame(const std::vector<VkSemaphore>& waitSemaphores, uint32_t currentImageIndex);
    const Vulkan::API::Semaphore& getGuiSemaphore(uint32_t currentImageIndex) const;
    bool init(const std::vector<std::unique_ptr<API::ImageView>>& imageViews);
    void initKeyMapping();
    bool createFontsTexture();
    bool initFramebuffers(const std::vector<std::unique_ptr<API::ImageView>>& imageViews);
    bool initPipeline();
    void processEvents(lug::Window::Event event);

private:
    void updateBuffers(uint32_t currentImageIndex);

private:
    Renderer& _renderer;
    Render::Window& _window;

    API::Image *_image = nullptr;
    API::ImageView *_imageView = nullptr;
    API::DeviceMemory *_fontsTextureHostMemory = nullptr;
    Vulkan::API::DescriptorSetLayout *_descriptorSetLayout;

    VkSampler _sampler;

    Vulkan::API::DescriptorPool _descriptorPool;
    std::vector<Vulkan::API::DescriptorSet> _descriptorSet;
    Vulkan::API::PipelineLayout *_pipelineLayout;
    Vulkan::API::GraphicsPipeline _pipeline;
    std::vector<Vulkan::API::Framebuffer> _framebuffers;


    std::vector<Vulkan::API::DeviceMemory *> _vertexDeviceMemories;
    std::vector<Vulkan::API::DeviceMemory *> _indexDeviceMemories;

    std::vector<Vulkan::API::Buffer *> _indexBuffers;
    std::vector<Vulkan::API::Buffer *> _vertexBuffers;

    std::vector<int> _vertexCounts;
    std::vector<int> _indexCounts;

    std::vector<Vulkan::API::CommandBuffer> _commandBuffers;
    std::vector<Vulkan::API::Semaphore> _guiSemaphores;
    std::vector<Vulkan::API::Fence> _guiFences;
};

#include "Gui.inl"

} // Vulkan
} // Graphics
} // lug
