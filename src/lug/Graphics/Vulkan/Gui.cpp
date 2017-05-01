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

#include <lug/Graphics/Vulkan/API/Fence.hpp>
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

void Gui::createFontsTexture() {
    ImGuiIO& io = ImGui::GetIO();

    // Create font texture
    unsigned char* fontData;
    int texWidth, texHeight;
    io.Fonts->GetTexDataAsRGBA32(&fontData, &texWidth, &texHeight);
    size_t uploadSize = texWidth * texHeight * 4 * sizeof(char);

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

//    API::Queue* graphicsQueue = _renderer.getQueue(VK_QUEUE_GRAPHICS_BIT, false);
    API::Queue* transfertQueue = _renderer.getQueue(VK_QUEUE_TRANSFER_BIT, false);

    // Create FontsTexture image
    {
        // Create Vulkan Image

        // TODO chopper queue de graphics && transfert pour le rendre available sur les deux. Si les deux queue sont les meme alors renvoei qu'un seul int.
        // si deux queue : mode de partage partager , si une seule, en exclusive 

        _image = API::Image::create(device, imagesFormat, extent, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        if (!_image) {
            LUG_LOG.error("Forward: Can't create depth buffer image");
            return ;
        }

        auto& imageRequirements = _image->getRequirements();

        uint32_t memoryTypeIndex = API::DeviceMemory::findMemoryType(device, imageRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
   
        // Allocate image requirements size for image
        _fontsTextureHostMemory = API::DeviceMemory::allocate(device, imageRequirements.size, memoryTypeIndex);
        if (!_fontsTextureHostMemory) {
            LUG_LOG.error("Forward: Can't allocate device memory for depth buffer images");
            return ;
        }

        // Bind memory to image
        _image->bindMemory(_fontsTextureHostMemory.get(), imageRequirements.size);
    }

    // Create FontsTexture image view
    {
        // Create Vulkan Image View
        _imageView = API::ImageView::create(device, _image.get(), imagesFormat, VK_IMAGE_ASPECT_COLOR_BIT);
        if (!_imageView) {
            LUG_LOG.error("GUI: Can't create image view");
            return;
        }
    }

    // Create staging buffers for font data upload
    {
        std::vector<uint32_t> queueFamilyIndices = { (uint32_t)_renderer.getQueue(VK_QUEUE_TRANSFER_BIT, false)->getFamilyIdx()};


        // TODO chopper queue de graphics && transfert pour le rendre available sur les deux. Si les deux queue sont les meme alors renvoei qu'un seul int.
        // si deux queue : mode de partage partager , si une seule, en exclusive 
        auto stagingBuffer = API::Buffer::create(device, (uint32_t)queueFamilyIndices.size(), queueFamilyIndices.data(), uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_SHARING_MODE_EXCLUSIVE);
        if (!stagingBuffer) {
            LUG_LOG.error("GUI: Can't create buffer");
            return ;
        }

        auto& requirements = stagingBuffer->getRequirements();
        uint32_t memoryTypeIndex = API::DeviceMemory::findMemoryType(device, requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        auto fontsTextureDeviceMemory = API::DeviceMemory::allocate(device, requirements.size, memoryTypeIndex);
        if (!fontsTextureDeviceMemory) {
            return ;
        }

        stagingBuffer->bindMemory(fontsTextureDeviceMemory.get());
        stagingBuffer->updateData(fontData, (uint32_t)uploadSize);
    }

    // Copy buffer data to font image
    {
        auto commandBuffer = transfertQueue->getCommandPool().createCommandBuffers();

        // Fence
        {
            VkFence fence;
            VkFenceCreateInfo createInfo{
                createInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                createInfo.pNext = nullptr,
                createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT
            };
            VkResult result = vkCreateFence(static_cast<VkDevice>(_renderer.getDevice()), &createInfo, nullptr, &fence);
            if (result != VK_SUCCESS) {
                LUG_LOG.error("RendererVulkan: Can't create swapchain fence: {}", result);
                return ;
            }
            _fence = Vulkan::API::Fence(fence, &_renderer.getDevice());

            if (transfertQueue->submit(commandBuffer[0], {}, {}, {}, fence) == false) {
                LUG_LOG.error("Gui: Can't submit commandBuffer");
                return;
            }
        }

        // TODO : set a define for the fence timeout 
        if (_fence.wait() == false) {
            LUG_LOG.error("Gui: Can't vkWaitForFences");
            return;
        }
 
        _fence.destroy();
//        commandBuffer.destroy();
//        stagingBuffer.destroy();
    }

    // Font texture Sampler
    {
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

        vkCreateSampler(static_cast<VkDevice>(_renderer.getDevice()), &samplerInfo, nullptr, &_sampler);
    }

    {
        // creating descriptor pool
        VkDescriptorPoolSize descriptorPoolSize;
        descriptorPoolSize.VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorPoolSize.descriptorCount = 1;

        VkDescriptorPoolCreateInfo descriptorPoolCreateInfo;
        descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolCreateInfo.pNext = nullptr;
        descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        descriptorPoolCreateInfo.maxSets = 1;
        descriptorPoolCreateInfo.pPoolSizes = 1;
        descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;
    
        VkDescriptorPool descPool = vkCreateDescriptorPool(_renderer.getDevice(), &descriptorPoolCreateInfo, nullptr);

        _descriptorPool = Vulkan::API::DescriptorPool(descPool, _renderer.getDevice());


        // descriptorSetLayout
        VkDescriptorSetLayoutBinding descriptorLayoutBinding;
        descriptorLayoutBinding.binding = 0;   // maybe modify that when shader is coded
        descriptorLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorLayoutBinding.descriptorCount = 1;
        descriptorLayoutBinding.VK_SHADER_STAGE_FRAGMENT_BIT;
        descriptorLayoutBinding.pImmutableSamplers = nullptr;

        _descriptorSetLayout = Vulkan::API::DescriptorSetLayout::create(_renderer.getDevice(), &descriptorLayoutBinding, 1);


        // create descriptor set
        _descriptorSet = _descriptorPool.createDescriptorSets({static_cast<VkDescriptorSetLayout>(*_descriptorSetLayout)})[0];

        // write descriptor set
        VkDescriptorImageInfo descriptorImageInfo;
        descriptorImageInfo.sampler = _sampler;
        descriptorImageInfo.imageView = static_cast<VkImageLayout>(*_imageView);
        descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            write.pNext = nullptr,
            write.dstSet = _descriptorSet,
            write.dstBinding = 0, // maybe modify that when shader is coded
            write.dstArrayElement = 0,
            write.descriptorCount = 1,
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            write.pImageInfo = &descriptorImageInfo,
            write.pBufferInfo = nullptr,
            write.pTexelBufferView = nullptr
        };


        // update descriptor set
        vkUpdateDescriptorSets(s_renderer.getDevice(), 1, &write, 0, nullptr);
    }

    // VkImageView vkImageView = static_cast<VkImageView>(*_imageView);
    // VkDescriptorImageInfo descriptorImageInfo = {};
    // descriptorImageInfo.sampler = sampler;
    // descriptorImageInfo.imageView = vkImageView;
    // descriptorImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

}

} // Vulkan
} // Graphics
} // lug
