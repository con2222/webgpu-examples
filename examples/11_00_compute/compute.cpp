#include <context.hpp>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

WebGPUContext ctx;

// ---------------------------------------------------------
// Compute shader
//
// Each GPU invocation processes one element of computeData.
//
// CPU dispatches enough workgroups to cover the whole array.
// global_invocation_id.x gives the global index of the current
// invocation.
//
// computeData is a read-write storage buffer, so the shader
// can both read and modify its elements.
// ---------------------------------------------------------
const char* shader = R"(
@group(0) @binding(0) var<storage, read_write> computeData: array<f32>;


@compute @workgroup_size(64)
fn compute_main(@builtin(global_invocation_id) GlobalInvocationID: vec3<u32>) {

    // One invocation processes one array element.
    let index = GlobalInvocationID.x;

    // The last workgroup may contain more invocations than
    // there are elements in the array.
    if (index >= arrayLength(&computeData)) { return; }

    let x = computeData[index];

    // Perform computation directly on the GPU.
    computeData[index] = sin(x) * x * x;
}
)";

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
    ctx.instance = createInstance();
    ctx.adapter = createAdapter(ctx.instance);
    ctx.device = createDevice(ctx.instance, ctx.adapter);
    ctx.queue = ctx.device.GetQueue();

    wgpu::ShaderModule shaderModule = CreateShaderModule(ctx.device, shader);

    std::vector<float> nums = generateRandomNumbers(1000000);

    // ---------------------------------------------------------
    // Storage buffer
    //
    // Contains the array processed by the compute shader.
    //
    // Storage:
    //     shader can access this buffer as a storage resource.
    //
    // CopyDst:
    //     allows Queue::WriteBuffer to upload CPU data.
    //
    // CopySrc:
    //     allows copying the GPU result into a readback buffer.
    // ---------------------------------------------------------
    wgpu::BufferDescriptor storageBufferDesc = {};
    storageBufferDesc.label = "Storage Buffer";
    storageBufferDesc.mappedAtCreation = false;
    storageBufferDesc.size = sizeof(float) * nums.size();
    storageBufferDesc.usage = wgpu::BufferUsage::Storage |
                              wgpu::BufferUsage::CopySrc |
                              wgpu::BufferUsage::CopyDst;
    wgpu::Buffer storageBuffer = ctx.device.CreateBuffer(&storageBufferDesc);

    // Upload the initial CPU array into the GPU storage buffer.
    ctx.queue.WriteBuffer(storageBuffer, 0, nums.data(),
                          nums.size() * sizeof(float));

    // ---------------------------------------------------------
    // Readback buffer
    //
    // Compute results cannot be mapped directly from our storage
    // buffer because it was not created with MapRead.
    //
    // Instead:
    //
    // storageBuffer
    //      ↓ CopyBufferToBuffer
    // readStorageBuffer
    //      ↓ MapAsync(Read)
    // CPU
    //
    // CopyDst:
    //     GPU can copy results into this buffer.
    //
    // MapRead:
    //     CPU can map and read the results afterwards.
    // ---------------------------------------------------------
    storageBufferDesc.usage =
        wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    wgpu::Buffer readStorageBuffer =
        ctx.device.CreateBuffer(&storageBufferDesc);

    std::vector<wgpu::BindGroupLayoutEntry> BindLayoutEntries(1);

    // ---------------------------------------------------------
    // Compute resource layout
    //
    // Shader expects:
    //
    // @group(0) @binding(0)
    //     read-write storage buffer
    //
    // Visibility is Compute because this resource is used only
    // by the compute shader.
    // ---------------------------------------------------------
    BindLayoutEntries[0].binding = 0;
    BindLayoutEntries[0].buffer.type = wgpu::BufferBindingType::Storage;
    BindLayoutEntries[0].buffer.minBindingSize = sizeof(float) * nums.size();
    BindLayoutEntries[0].buffer.hasDynamicOffset = false;
    BindLayoutEntries[0].visibility = wgpu::ShaderStage::Compute;

    wgpu::BindGroupLayoutDescriptor BGLayoutDesc = {};
    BGLayoutDesc.entries = BindLayoutEntries.data();
    BGLayoutDesc.entryCount = BindLayoutEntries.size();
    wgpu::BindGroupLayout BGLayout =
        ctx.device.CreateBindGroupLayout(&BGLayoutDesc);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
    pipelineLayoutDescriptor.bindGroupLayouts = &BGLayout;
    wgpu::PipelineLayout pipelineLayout =
        ctx.device.CreatePipelineLayout(&pipelineLayoutDescriptor);

    std::vector<wgpu::BindGroupEntry> bindGroupEntries(1);

    // Bind the actual storageBuffer to @group(0) @binding(0).
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = storageBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = nums.size() * sizeof(float);

    wgpu::BindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.entries = bindGroupEntries.data();
    bindGroupDesc.entryCount = bindGroupEntries.size();
    bindGroupDesc.layout = BGLayout;
    wgpu::BindGroup bindGroup = ctx.device.CreateBindGroup(&bindGroupDesc);

    // ---------------------------------------------------------
    // Compute pipeline
    //
    // Unlike RenderPipeline, there are no:
    // - vertex buffers
    // - fragment shaders
    // - color/depth attachments
    //
    // The pipeline only needs the compute shader and its
    // resource layout.
    // ---------------------------------------------------------
    wgpu::ComputePipelineDescriptor computePipelineDesc = {};
    computePipelineDesc.compute.constantCount = 0;
    computePipelineDesc.compute.constants = nullptr;
    computePipelineDesc.compute.entryPoint = "compute_main";
    computePipelineDesc.compute.module = shaderModule;
    computePipelineDesc.label = "Compute Pipeline";
    computePipelineDesc.layout = pipelineLayout;

    wgpu::ComputePipeline computePipeline =
        ctx.device.CreateComputePipeline(&computePipelineDesc);

    wgpu::CommandEncoder cmdEncoder = ctx.device.CreateCommandEncoder();

    // ---------------------------------------------------------
    // Compute pass
    //
    // @workgroup_size(64) means one workgroup contains
    // 64 shader invocations along the X axis.
    //
    // We need enough workgroups to cover every array element.
    //
    // Example:
    //
    // 1,000,000 elements
    //      ↓
    // ceil(1,000,000 / 64)
    //      ↓
    // 15,625 workgroups
    //
    // The bounds check inside the shader protects against extra
    // invocations when the array size is not divisible by 64.
    // ---------------------------------------------------------
    wgpu::ComputePassEncoder computePass = cmdEncoder.BeginComputePass();
    computePass.SetPipeline(computePipeline);
    computePass.SetBindGroup(0, bindGroup);

    uint32_t workgroupCount = (nums.size() + 63) / 64;
    computePass.DispatchWorkgroups(workgroupCount);
    computePass.End();

    // Copy the computed GPU data into a CPU-readable buffer.
    //
    // This command is recorded after the compute pass, so the copy
    // sees the results produced by the compute shader.
    cmdEncoder.CopyBufferToBuffer(storageBuffer, 0, readStorageBuffer, 0,
                                  nums.size() * sizeof(float));

    wgpu::CommandBuffer cmdBuffer = cmdEncoder.Finish();
    ctx.queue.Submit(1, &cmdBuffer);

    auto onBufferMapped = [](wgpu::MapAsyncStatus status,
                             wgpu::StringView message) {
        if (status != wgpu::MapAsyncStatus::Success) {
            std::cerr << "Map failed\n";
        }
    };

    // ---------------------------------------------------------
    // Asynchronous GPU -> CPU readback
    //
    // MapAsync requests CPU access to the readback buffer.
    //
    // WaitAny waits until that asynchronous mapping operation
    // finishes. After that GetConstMappedRange can safely return
    // a CPU pointer to the result.
    // ---------------------------------------------------------
    wgpu::Future bufferAsyncReadFuture = readStorageBuffer.MapAsync(
        wgpu::MapMode::Read, 0, nums.size() * sizeof(float),
        wgpu::CallbackMode::WaitAnyOnly, onBufferMapped);

    // Wait until the mapping request completes.
    ctx.instance.WaitAny(bufferAsyncReadFuture, UINT64_MAX);

    // Interpret the mapped bytes as an array of float values.
    const float* bufferdata = static_cast<const float*>(
        readStorageBuffer.GetConstMappedRange(0, sizeof(float) * nums.size()));

    if (bufferdata != nullptr) {
        std::cout << "bufferData = [";

        for (size_t i = 0; i < std::min(nums.size(), (size_t)40); ++i) {
            if (i > 0) {
                std::cout << ", ";
            }

            std::cout << bufferdata[i];
        }

        std::cout << "]\n";
    }

    readStorageBuffer.Unmap();

    return 0;
}