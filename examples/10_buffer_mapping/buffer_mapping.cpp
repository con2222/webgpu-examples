#include <context.hpp>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

WebGPUContext ctx;

std::vector<float> generateRandomNumbers(size_t count, float minValue = 0.0f,
                                         float maxValue = 1.0f) {
    std::vector<float> result;
    result.reserve(count);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(minValue, maxValue);

    for (size_t i = 0; i < count; ++i) {
        result.push_back(dist(gen));
    }

    return result;
}

int main() {
    // ---------------------------------------------------------
    // WebGPU initialization
    //
    // No window or Surface is needed because this example
    // performs only buffer operations.
    // ---------------------------------------------------------

    ctx.instance = createInstance();
    ctx.adapter = createAdapter(ctx.instance);
    ctx.device = createDevice(ctx.instance, ctx.adapter);
    ctx.queue = ctx.device.GetQueue();

    std::vector<float> numbers = generateRandomNumbers(20);

    const uint64_t bufferSize = numbers.size() * sizeof(float);

    // ---------------------------------------------------------
    // CPU -> Buffer using mappedAtCreation
    //
    // mappedAtCreation = true means the buffer is immediately
    // accessible from the CPU after CreateBuffer().
    //
    // MapWrite is NOT required here because we are not using
    // MapAsync(Write). This is the initial mapping provided
    // directly during buffer creation.
    //
    // CopySrc is required because this buffer will later be
    // the SOURCE of CopyBufferToBuffer().
    // ---------------------------------------------------------

    wgpu::BufferDescriptor sourceBufferDesc = {};

    sourceBufferDesc.label = "Mapped source buffer";
    sourceBufferDesc.size = bufferSize;
    sourceBufferDesc.usage = wgpu::BufferUsage::CopySrc;
    sourceBufferDesc.mappedAtCreation = true;

    wgpu::Buffer sourceBuffer = ctx.device.CreateBuffer(&sourceBufferDesc);

    // Get a CPU-accessible pointer to the mapped buffer memory.
    void* mappedMemory = sourceBuffer.GetMappedRange(0, bufferSize);

    // Copy the std::vector data into the WebGPU buffer.
    std::memcpy(mappedMemory, numbers.data(), bufferSize);

    // Finish CPU access.
    //
    // After Unmap(), the CPU pointer is no longer valid and
    // the buffer can be used by GPU commands.
    sourceBuffer.Unmap();

    // ---------------------------------------------------------
    // Readback / staging buffer
    //
    // CopyDst:
    //     GPU commands are allowed to copy data INTO this buffer.
    //
    // MapRead:
    //     CPU is allowed to later map this buffer using
    //     MapAsync(MapMode::Read) and read its contents.
    //
    // A buffer used for reading GPU results back on the CPU
    // is commonly called a readback or staging buffer.
    // ---------------------------------------------------------

    wgpu::BufferDescriptor readbackBufferDesc = {};

    readbackBufferDesc.label = "Readback buffer";
    readbackBufferDesc.size = bufferSize;

    readbackBufferDesc.usage =
        wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;

    readbackBufferDesc.mappedAtCreation = false;

    wgpu::Buffer readbackBuffer = ctx.device.CreateBuffer(&readbackBufferDesc);

    // ---------------------------------------------------------
    // GPU copy
    //
    // sourceBuffer:
    //     CopySrc
    //
    // readbackBuffer:
    //     CopyDst
    //
    // CopyBufferToBuffer records the copy command.
    // The copy is not executed until Queue::Submit().
    // ---------------------------------------------------------

    wgpu::CommandEncoder encoder = ctx.device.CreateCommandEncoder();

    encoder.CopyBufferToBuffer(sourceBuffer, 0, readbackBuffer, 0, bufferSize);

    wgpu::CommandBuffer command = encoder.Finish();

    // Send the recorded copy command to the GPU.
    ctx.queue.Submit(1, &command);

    // ---------------------------------------------------------
    // GPU -> CPU using MapAsync(Read)
    //
    // Mapping is asynchronous because the GPU may still be
    // using/writing the buffer when MapAsync() is called.
    //
    // WebGPU calls the callback when CPU access becomes possible.
    // ---------------------------------------------------------

    bool isReady = false;

    auto onBufferMapped = [](wgpu::MapAsyncStatus status,
                             wgpu::StringView message, void* userdata) {
        bool* readyFlag = static_cast<bool*>(userdata);

        *readyFlag = true;

        std::cout << "Readback buffer mapped with status "
                  << static_cast<uint32_t>(status) << '\n';
    };

    readbackBuffer.MapAsync(wgpu::MapMode::Read, 0, bufferSize,
                            wgpu::CallbackMode::AllowProcessEvents,
                            onBufferMapped, &isReady);

    // Process WebGPU callbacks until MapAsync finishes.
    while (!isReady) {
        ctx.instance.ProcessEvents();  // Can use WaitAny like other examples
    }

    // ---------------------------------------------------------
    // Read mapped data
    //
    // The buffer contains float values, therefore we interpret
    // the mapped bytes as an array of float.
    // ---------------------------------------------------------

    const float* bufferData = static_cast<const float*>(
        readbackBuffer.GetConstMappedRange(0, bufferSize));

    if (bufferData != nullptr) {
        std::cout << "bufferData = [";

        for (size_t i = 0; i < numbers.size(); ++i) {
            if (i > 0) {
                std::cout << ", ";
            }

            std::cout << bufferData[i];
        }

        std::cout << "]\n";
    }

    // Finish CPU access to the readback buffer.
    readbackBuffer.Unmap();

    return 0;
}