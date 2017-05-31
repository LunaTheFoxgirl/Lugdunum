#include <lug/Graphics/Vulkan/API/Buffer.hpp>

#include <cstring>

#include <lug/Graphics/Vulkan/API/Device.hpp>
#include <lug/Graphics/Vulkan/API/DeviceMemory.hpp>

namespace lug {
namespace Graphics {
namespace Vulkan {
namespace API {

Buffer::Buffer(
    VkBuffer buffer,
    const Device* device) : _buffer(buffer), _device(device) {
    vkGetBufferMemoryRequirements(static_cast<VkDevice>(*device), _buffer, &_requirements);
}

Buffer::Buffer(Buffer&& buffer) {
    _buffer = buffer._buffer;
    _device = buffer._device;
    _gpuPtr = buffer._gpuPtr;
    _deviceMemory = buffer._deviceMemory;
    _requirements = buffer._requirements;
<<<<<<< HEAD

=======
    _deviceMemoryOffset = buffer._deviceMemoryOffset;
>>>>>>> dev
    buffer._buffer = VK_NULL_HANDLE;
    buffer._device = nullptr;
    buffer._gpuPtr = nullptr;
    buffer._deviceMemory = nullptr;
    buffer._requirements = {};
    buffer._deviceMemoryOffset = 0;
}

Buffer& Buffer::operator=(Buffer&& buffer) {
    destroy();

    _buffer = buffer._buffer;
    _device = buffer._device;
    _gpuPtr = buffer._gpuPtr;
    _deviceMemory = buffer._deviceMemory;
    _requirements = buffer._requirements;
<<<<<<< HEAD

=======
    _deviceMemoryOffset = buffer._deviceMemoryOffset;
>>>>>>> dev
    buffer._buffer = VK_NULL_HANDLE;
    buffer._device = nullptr;
    buffer._gpuPtr = nullptr;
    buffer._deviceMemory = nullptr;
    buffer._requirements = {};
    buffer._deviceMemoryOffset = 0;

    return *this;
}

Buffer::~Buffer() {
    destroy();
}

void Buffer::destroy() {
    if (_buffer != VK_NULL_HANDLE) {
        if (_gpuPtr != nullptr) {
            unmapMemory();
        }
        vkDestroyBuffer(static_cast<VkDevice>(*_device), _buffer, nullptr);
        _buffer = VK_NULL_HANDLE;
    }
}

<<<<<<< HEAD
void Buffer::bindMemory(DeviceMemory* deviceMemory, VkDeviceSize memoryOffset) {
    _deviceMemory = deviceMemory;
    vkBindBufferMemory(static_cast<VkDevice>(*_device), static_cast<VkBuffer>(_buffer), static_cast<VkDeviceMemory>(*deviceMemory), memoryOffset);
}

void* Buffer::mapMemory(VkDeviceSize size, VkDeviceSize offset) {
    _gpuPtr = nullptr;
    vkMapMemory(static_cast<VkDevice>(*_device), static_cast<VkDeviceMemory>(*_deviceMemory), offset, size, 0, &_gpuPtr);
    return _gpuPtr;
}

void Buffer::unmapMemory() {
    vkUnmapMemory(static_cast<VkDevice>(*_device), static_cast<VkDeviceMemory>(*_deviceMemory));
    _gpuPtr = nullptr;
}

void Buffer::updateData(void* data, uint32_t size, uint32_t memoryOffset) {
    void* gpuData = mapMemory(size, memoryOffset);

    memcpy(gpuData, data, size);
    unmapMemory();
}
=======
void Buffer::bindMemory(const DeviceMemory& deviceMemory, VkDeviceSize memoryOffset) {
    _deviceMemory = &deviceMemory;
    _deviceMemoryOffset = memoryOffset;

    vkBindBufferMemory(static_cast<VkDevice>(*_device), static_cast<VkBuffer>(_buffer), static_cast<VkDeviceMemory>(deviceMemory), memoryOffset);
}

bool Buffer::updateData(const void* data, VkDeviceSize size, VkDeviceSize offset) const {
    void* gpuData = _deviceMemory->mapBuffer(*this, size, offset);
>>>>>>> dev

    if (!gpuData) {
        return false;
    }

    memcpy(gpuData, data, size);
    _deviceMemory->unmap();

    return true;
}

void* Buffer::getGpuPtr() {
    if (!_gpuPtr) {
        return mapMemory(VK_WHOLE_SIZE);
    }
    return _gpuPtr;
}

} // API
} // Vulkan
} // Graphics
} // lug
