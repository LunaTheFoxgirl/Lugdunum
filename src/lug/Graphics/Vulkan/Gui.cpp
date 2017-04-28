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

void Gui::init(float width, float height) {
    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = ImVec2(width, height); // screen size

    // Create font texture
    unsigned char* fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    uint32_t uploadSize = texWidth*texHeight * sizeof(int);

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

    std::unique_ptr<API::Image> image = nullptr;
    std::unique_ptr<API::ImageView> imageView = nullptr;
    std::unique_ptr<API::DeviceMemory> depthBufferMemory = nullptr;

    // Create depth buffer image
    {
        // Create Vulkan Image
        image = API::Image::create(device, imagesFormat, extent, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        if (!image) {
            LUG_LOG.error("Forward: Can't create depth buffer image");
            return ;
        }

        auto& imageRequirements = image->getRequirements();

        uint32_t memoryTypeIndex = API::DeviceMemory::findMemoryType(device, imageRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        // Allocate image requirements size for all images
        depthBufferMemory = API::DeviceMemory::allocate(device, imageRequirements.size, memoryTypeIndex);
        if (!depthBufferMemory) {
            LUG_LOG.error("Forward: Can't allocate device memory for depth buffer images");
            return ;
        }

        // Bind memory to image
        image->bindMemory(depthBufferMemory.get(), imageRequirements.size);
    }

    // Create depth buffer image view
    {
        // Create Vulkan Image View
        imageView = API::ImageView::create(device, image.get(), imagesFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
        if (!imageView) {
            LUG_LOG.error("GUI: Can't create image view");
            return;
        }
    }

    // Create Vulkan Buffer
    // TODO demander a quentin comment choisir la queue VK_QUEUE_TRANSFER_BIT plutot que celle de VK_QUEUE_GRAPHICS_BIT
    std::vector<uint32_t> queueFamilyIndices = { (uint32_t)_renderer.getQueue(VK_QUEUE_TRANSFER_BIT, false)->getFamilyIdx()};

    auto stagingBuffer = API::Buffer::create(device, (uint32_t)queueFamilyIndices.size(), queueFamilyIndices.data(), uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    if (!stagingBuffer) {
        LUG_LOG.error("GUI: Can't create buffer");
        return ;
    }

    auto& requirements = stagingBuffer->getRequirements();
    uint32_t memoryTypeIndex = API::DeviceMemory::findMemoryType(device, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    auto fontDeviceMemory = API::DeviceMemory::allocate(device, requirements.size, memoryTypeIndex);
    if (!fontDeviceMemory) {
        return ;
    }

    stagingBuffer->bindMemory(fontDeviceMemory.get());
    stagingBuffer->updateData(fontData, uploadSize);

    //TODO : pourquoi mapMemory n'a pas de valuer "WHOLE_MEMORY"
    // move form  char * to Vulkan Buffer
//    stagingBuffer.updateData(fontData, uploadSize, 0); // pas besoin d'une semaphore c'est synchrone cote CPU 
//    stagingBuffer.unmapMemory();


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

} // Vulkan
} // Graphics
} // lug
