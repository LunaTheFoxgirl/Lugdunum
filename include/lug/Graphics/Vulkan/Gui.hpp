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

// #include <lug/Graphics/Render/dear_imgui/imgui.h>
#include <lug/Graphics/Vulkan/Renderer.hpp>

namespace lug {
namespace Graphics {
namespace Vulkan {
namespace Render {

class LUG_GRAPHICS_API Gui {
// private:
//     // UI params are set via push constants
//     struct PushConstBlock {
//         lug::Math::Vec2<float> scale;
//         lug::Math::Vec2<float> translate;
//     } pushConstBlock;
public:
    Gui() = delete;

    Gui(Renderer& renderer);

    Gui(const Gui&) = delete;
    Gui(Window&&) = delete;

    Gui& operator=(const Gui&) = delete;
    Gui& operator=(Gui&&) = delete;

    ~Gui();

    // bool beginFrame();   
    // bool render();
    // bool endFrame();
    // void destroy();

    // void init(float width, float height);
    // bool initRessources(VkRenderPass renderPass, VkQueue copyQueue);


private:

    Renderer& _renderer;


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

} // Render
} // Vulkan
} // Graphics
} // lug
