#pragma once

#include <Renderer/BindlessDefines.h>

class CommandRingBuffer;

enum class RenderBufferInterfaceHandle : uint64_t { Invalid = (std::numeric_limits<uint64_t>::max)() };

// Manages a buffer, e.g. materials, lights, shadows, etc.
// todo (hbedard): find a better name
class RenderBufferInterface
{
public:
    virtual RenderBufferInterfaceHandle GetRenderBufferInterfaceHandle() = 0;

    virtual BufferHandle GetBufferHandle() = 0;

    virtual void Prepare() = 0;
    virtual void UploadToGPU(CommandRingBuffer& commandRingBuffer) = 0;
};
