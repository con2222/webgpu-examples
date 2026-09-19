#include <context.hpp>
#include <cstdint>
#include <iostream>

const char* shader = R"(

// Shared memory local to one workgroup.
//
// Every workgroup gets its own separate instance
// of this array.
//
// Workgroup 0 -> its own shared_data
// Workgroup 1 -> another shared_data
// Workgroup 2 -> another shared_data
var<workgroup> shared_data: array<u32, 256>;

@group(0) @binding(0)
var<storage, read_write> output: array<u32>;


// local_invocation_id:
// index of an invocation inside its workgroup.
// For this example: 0..255.
//
// workgroup_id:
// identifies which workgroup this invocation belongs to.
// With DispatchWorkgroups(3): 0, 1, or 2.
@compute @workgroup_size(256)
fn main(
    @builtin(local_invocation_id) local_id: vec3u,
    @builtin(workgroup_id) group_id: vec3u
) {
    let lid = local_id.x;
    let gid = group_id.x;

    // Give each workgroup a clearly distinguishable range:
    //
    // Group 0 ->   0..255
    // Group 1 -> 1000..1255
    // Group 2 -> 2000..2255
    //
    // Even though shared_data has one declaration in WGSL,
    // every workgroup writes into its own private instance.
    shared_data[lid] = gid * 1000u + lid;

    // Wait until every invocation in THIS workgroup
    // has finished writing to shared_data.
    //
    // The barrier does not synchronize different workgroups.
    workgroupBarrier();

    // Storage memory is shared across all workgroups,
    // so each workgroup must write into a different region.
    let output_index = gid * 256u + lid;


    // Read another invocation's value from the same
    // workgroup memory, reversing the group's 256 elements.
    output[output_index] =
        shared_data[255u - lid];
}
)";

static WebGPUContext ctx;
static WindowData data;

int main() {
    // Three independent workgroups.
    // Each workgroup contains 256 shader invocations.
    constexpr uint32_t groupCount = 3;
    constexpr uint32_t elementsPerGroup = 256;

    // Total shader invocations:
    //
    // 3 workgroups * 256 invocations = 768 invocations.
    constexpr uint32_t totalElements = groupCount * elementsPerGroup;
    constexpr uint64_t bufferSize = totalElements * sizeof(uint32_t);

    ctx.instance = createInstance();
    ctx.adapter = createAdapter(ctx.instance);
    ctx.device = createDevice(ctx.instance, ctx.adapter);
    ctx.queue = ctx.device.GetQueue();

    wgpu::BufferDescriptor bufferDescriptor = {};
    bufferDescriptor.size = bufferSize;

    // Global GPU buffer shared by all workgroups.
    //
    // Unlike var<workgroup>, this buffer is visible
    // to every invocation from every workgroup.
    bufferDescriptor.usage =
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer outputBuffer = ctx.device.CreateBuffer(&bufferDescriptor);

    // CPU-readable buffer used only to inspect
    // the compute shader result.
    bufferDescriptor.usage =
        wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer readbackBuffer = ctx.device.CreateBuffer(&bufferDescriptor);

    wgpu::ShaderModule shaderModule = CreateShaderModule(ctx.device, shader);

    wgpu::ComputePipelineDescriptor pipelineDescriptor = {};
    pipelineDescriptor.compute.module = shaderModule;
    pipelineDescriptor.compute.entryPoint = "main";
    wgpu::ComputePipeline pipeline =
        ctx.device.CreateComputePipeline(&pipelineDescriptor);

    wgpu::BindGroupEntry entries[1] = {};
    entries[0].binding = 0;
    entries[0].buffer = outputBuffer;
    entries[0].size = bufferSize;

    wgpu::BindGroupDescriptor bindGroupDescriptor{};
    bindGroupDescriptor.layout = pipeline.GetBindGroupLayout(0);
    bindGroupDescriptor.entryCount = 1;
    bindGroupDescriptor.entries = entries;
    wgpu::BindGroup bindGroup =
        ctx.device.CreateBindGroup(&bindGroupDescriptor);

    wgpu::CommandEncoder encoder = ctx.device.CreateCommandEncoder();
    wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
    pass.SetPipeline(pipeline);
    pass.SetBindGroup(0, bindGroup);

    // Launch three workgroups.
    //
    // Each workgroup contains 256 invocations because
    // the shader declares @workgroup_size(256).
    //
    // Total invocations:
    // 3 * 256 = 768.
    pass.DispatchWorkgroups(groupCount);
    pass.End();

    // Copy the GPU-only output into a CPU-readable buffer.
    encoder.CopyBufferToBuffer(outputBuffer, 0, readbackBuffer, 0, bufferSize);

    wgpu::CommandBuffer commands = encoder.Finish();
    ctx.queue.Submit(1, &commands);

    bool mapped = false;
    auto mapFuture = readbackBuffer.MapAsync(
        wgpu::MapMode::Read, 0, bufferSize, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
            mapped = status == wgpu::MapAsyncStatus::Success;
        });

    ctx.instance.WaitAny(mapFuture, UINT64_MAX);
    if (!mapped) return 1;

    const auto* result = static_cast<const uint32_t*>(
        readbackBuffer.GetConstMappedRange(0, bufferSize));

    for (uint32_t group = 0; group < groupCount; ++group) {
        std::cout << "\n=== Workgroup " << group << " ===\n";

        for (uint32_t i = 0; i < elementsPerGroup; ++i) {
            uint32_t index = group * elementsPerGroup + i;

            std::cout << result[index] << ' ';

            if ((i + 1) % 16 == 0) {
                std::cout << '\n';
            }
        }
    }

    readbackBuffer.Unmap();
    return 0;
}
