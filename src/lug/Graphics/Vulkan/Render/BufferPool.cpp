#include <lug/Graphics/Vulkan/Render/BufferPool.hpp>

#include <lug/Graphics/Vulkan/API/Builder/Buffer.hpp>
#include <lug/Graphics/Vulkan/API/Builder/DeviceMemory.hpp>
#include <lug/Graphics/Vulkan/API/Device.hpp>
#include <lug/System/Logger/Logger.hpp>

namespace lug {
namespace Graphics {
namespace Vulkan {
namespace Render {

BufferPool::SubBuffer::SubBuffer(const API::Buffer* buffer, uint32_t offset, uint32_t size, BufferPool::Chunk* chunk) :
    buffer(buffer), offset(offset), size(size), chunk(chunk) {}

BufferPool::BufferPool(
    uint32_t countPerChunk,
    uint32_t subBufferSize,
    const API::Device& device,
    const std::set<uint32_t>& queueFamilyIndices) :
    _countPerChunk(countPerChunk), _subBufferSize(subBufferSize), _device(device), _queueFamilyIndices(queueFamilyIndices) {}

void BufferPool::SubBuffer::free() {
    chunk->subBuffersFree.push_back(this);
}

BufferPool::SubBuffer* BufferPool::allocate() {
    // Search free sub-buffer
    {
        for (auto& chunk: _chunks) {
            BufferPool::SubBuffer* subBuffer = chunk.getFreeSubBuffer();

            if (subBuffer) {
                return subBuffer;
            }
        }
    }

    // No free sub-buffer found, allocate new chunk
    const auto& alignment = _device.getPhysicalDeviceInfo()->properties.limits.minUniformBufferOffsetAlignment;
    VkDeviceSize subBufferSizeAligned = _subBufferSize;

    if (subBufferSizeAligned % alignment) {
        subBufferSizeAligned += alignment - subBufferSizeAligned % alignment;
    }

    {
        BufferPool::Chunk chunk{};
        VkResult result{VK_SUCCESS};

        // Create buffer
        API::Builder::Buffer bufferBuilder(_device);

        bufferBuilder.setQueueFamilyIndices(_queueFamilyIndices);
        bufferBuilder.setSize(subBufferSizeAligned * _countPerChunk);
        bufferBuilder.setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);

        if (!bufferBuilder.build(chunk.buffer, &result)) {
            LUG_LOG.error("BufferPool::allocate: Can't create buffer: {}", result);
            return nullptr;
        }

        // Create chunk buffer memory
        {
            API::Builder::DeviceMemory deviceMemoryBuilder(_device);
            deviceMemoryBuilder.setMemoryFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            if (!deviceMemoryBuilder.addBuffer(chunk.buffer)) {
                LUG_LOG.error("BufferPool::allocate: Can't add buffer to device memory");
                return nullptr;
            }

            if (!deviceMemoryBuilder.build(chunk.bufferMemory, &result)) {
                LUG_LOG.error("BufferPool::allocate: Can't create device memory: {}", result);
                return nullptr;
            }
        }

        _chunks.push_back(std::move(chunk));
    }

    // Init new chunk sub-buffers
    {
        auto& chunk = _chunks.back();

        chunk.subBuffers.resize(_countPerChunk);
        chunk.subBuffersFree.resize(_countPerChunk);

        uint32_t offset = 0;

        for (uint32_t i = 0; i < _countPerChunk; ++i) {
            SubBuffer subBuffer{
                &chunk.buffer,
                offset,
                static_cast<uint32_t>(subBufferSizeAligned),
                &chunk
            };
            chunk.subBuffers[i] = std::move(subBuffer);

            // Warning: Do not resize chunk->subBuffers
            chunk.subBuffersFree[i] = &chunk.subBuffers[i];

            offset += static_cast<uint32_t>(subBufferSizeAligned);
        }
    }

    return allocate();
}

void BufferPool::free(SubBuffer* subBuffer) {
    subBuffer->free();
}

} // Render
} // Vulkan
} // Graphics
} // lug
