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

#include <lug/Graphics/Render/Vulkan.Gui.hpp>

namespace lug {
namespace Graphics {
namespace Vulkan {
namespace Render {


Gui::Gui(Renderer &renderer) : _renderer(renderer) {}

Gui::~Gui() {
    destroyRender();
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

void Gui::init(float width, float height)
{
    // Color scheme
    // ImGuiStyle& style = ImGui::GetStyle();
    // style.Colors[ImGuiCol_TitleBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.6f);
    // style.Colors[ImGuiCol_TitleBgActive] = ImVec4(1.0f, 0.0f, 0.0f, 0.8f);
    // style.Colors[ImGuiCol_MenuBarBg] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    // style.Colors[ImGuiCol_Header] = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);
    // style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);

    // Dimensions
    ImGuiIO& io = ImGui::GetIO(); 
    io.DisplaySize = ImVec2(width, height); // screen size
    // io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
}

bool Gui::initRessources() { //VkRenderPass /*renderPass*/, VkQueue /*copyQueue*/) {
    ImGuiIO& io = ImGui::GetIO();

    // Create font texture
    unsigned char* fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    VkDeviceSize uploadSize = texWidth*texHeight * sizeof(int);

    // find format available on the device 
    VkFormat imagesFormat = API::Image::findSupportedFormat(_device,
                                                       {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
                                                       VK_IMAGE_TILING_OPTIMAL,
                                                       VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);



      VkExtent3D extent {
            extent.width = texWidth,
            extent.height = texHeight,
            extent.depth = 1
        };

    // Create Vulkan Image
    image = API::Image::create(_device, imagesFormat, extent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    if (!image) {
        LUG_LOG.error("GUI: Can't create depth buffer image");
        return false;
    }


    // Create Vulkan Image View
    imageView = API::ImageView::create(_device, image.get(), imagesFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
    if (!imageView) {
        LUG_LOG.error("GUI: Can't create image view");
        return false;
    }

     // Create Vulkan Buffer
    // TODO demander a quentin comment choisir la queue VK_QUEUE_TRANSFER_BIT plutot que celle de VK_QUEUE_GRAPHICS_BIT
    Vulkan::API::Buffer stagingBuffer = API::Buffer::create(_device, (uint32_t)_queueFamilyIndices.size(), _queueFamilyIndices.data(), uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!stagingBuffer) {
        LUG_LOG.error("GUI: Can't create buffer");
        return false;
    }
     
    //TODO : pourquoi mapMemory n'a pas de valuer "WHOLE_MEMORY"
    // move form  char * to Vulkan Buffer
    stagingBuffer.updateData(suploadSize, 0); // pas besoin d'une semaphore c'est synchrone cote CPU
    
    stagingBuffer.unmapMemory();


        // stagingBuffer.map();
        // memcpy(stagingBuffer.mapped, fontData, uploadSize);
        // stagingBuffer.unmap();


// texture to framedata
    // Create Command Buffer to move from buffer to Image

    // Create Pipeline to render

    // reussir a compiler <3

}

// bool Gui::initDepthBuffers(const std::vector<std::unique_ptr<API::ImageView>>& /*imageViews*/) {
//     return true;
// }

// bool Gui::initFramebuffers(const std::vector<std::unique_ptr<API::ImageView>>& /*imageViews*/) {
//     return true;
// }

} // Render
} // Vulkan
} // Graphics
} // lug
