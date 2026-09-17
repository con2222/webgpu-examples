#include <context.hpp>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

constexpr uint32_t WIDTH = 80;
constexpr uint32_t HEIGHT = 40;
constexpr uint64_t BUFFER_SIZE = WIDTH * HEIGHT * sizeof(uint32_t);

WebGPUContext ctx;

const char* shader = R"(
// ---------------------------------------------------------
// Pipeline override constants
//
// Values are provided from C++ when ComputePipeline is created.
// Unlike uniforms, override constants are fixed for a particular
// pipeline and cannot be changed without creating a new pipeline.
// ---------------------------------------------------------
override WIDTH: u32;
override HEIGHT: u32;

// Each element stores the number of Mandelbrot iterations
// for one output cell.
@group(0) @binding(0)
var<storage, read_write> computeData: array<u32>;

// ---------------------------------------------------------
// Compute shader
//
// One workgroup contains 8 x 8 = 64 invocations.
// Each invocation calculates one point of the fractal.
// ---------------------------------------------------------
@compute @workgroup_size(8, 8, 1)
fn compute_main(
    @builtin(global_invocation_id) id: vec3<u32>
) {
    // Dispatch may create extra invocations when WIDTH or HEIGHT
    // is not divisible by 8.
    if (id.x >= WIDTH || id.y >= HEIGHT) {
        return;
    }

    // Convert 2D coordinate (x, y) into a 1D buffer index.
    let index = id.y * WIDTH + id.x;

    // Normalize pixel coordinates into [0, 1].
    let uv = vec2f(
        f32(id.x) / f32(WIDTH),
        f32(id.y) / f32(HEIGHT)
    );

    // Map normalized coordinates into the Mandelbrot plane:
    //
    // X: [-2, 1]
    // Y: [-1, 1]
    let c = vec2f(
        uv.x * 3.0 - 2.0,
        uv.y * 2.0 - 1.0
    );

    // Mandelbrot iteration starts at z = 0.
    var z = vec2f(0.0, 0.0);

    var iterations: u32 = 0u;
    let max_iterations: u32 = 100u;

    while (iterations < max_iterations) {

        // |z|² > 4 means the point escaped.
        if (z.x * z.x + z.y * z.y > 4.0) {
            break;
        }

        // Complex-number formula:
        //
        // z = z² + c
        let z_new = vec2f(
            z.x * z.x - z.y * z.y,
            2.0 * z.x * z.y
        ) + c;

        z = z_new;
        iterations++;
    }

    // Store the result produced by this invocation.
    computeData[index] = iterations;
}
)";

int main() {
    ctx.instance = createInstance();
    ctx.adapter = createAdapter(ctx.instance);
    ctx.device = createDevice(ctx.instance, ctx.adapter);
    ctx.queue = ctx.device.GetQueue();

    wgpu::ShaderModule shaderModule = CreateShaderModule(ctx.device, shader);

    // ---------------------------------------------------------
    // GPU storage buffer
    //
    // One uint32_t for every fractal cell:
    //
    // WIDTH * HEIGHT * sizeof(uint32_t)
    //
    // Storage:
    //     compute shader can read/write this buffer.
    //
    // CopySrc:
    //     after computation, results can be copied into
    //     the CPU-readable readback buffer.
    // ---------------------------------------------------------
    wgpu::BufferDescriptor storageBufferDesc = {};
    storageBufferDesc.label = "Storage Buffer";
    storageBufferDesc.mappedAtCreation = false;
    storageBufferDesc.size = BUFFER_SIZE;

    // ---------------------------------------------------------
    // Readback buffer
    //
    // CopyDst:
    //     receives the GPU result.
    //
    // MapRead:
    //     allows CPU to map and read the result afterwards.
    // ---------------------------------------------------------
    storageBufferDesc.usage = wgpu::BufferUsage::Storage |
                              wgpu::BufferUsage::CopySrc |
                              wgpu::BufferUsage::CopyDst;
    wgpu::Buffer storageBuffer = ctx.device.CreateBuffer(&storageBufferDesc);

    storageBufferDesc.usage =
        wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    wgpu::Buffer readStorageBuffer =
        ctx.device.CreateBuffer(&storageBufferDesc);

    std::vector<wgpu::BindGroupLayoutEntry> BindLayoutEntries(1);

    BindLayoutEntries[0].binding = 0;
    BindLayoutEntries[0].buffer.type = wgpu::BufferBindingType::Storage;
    BindLayoutEntries[0].buffer.minBindingSize = BUFFER_SIZE;
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
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = storageBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = BUFFER_SIZE;

    wgpu::BindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.entries = bindGroupEntries.data();
    bindGroupDesc.entryCount = bindGroupEntries.size();
    bindGroupDesc.layout = BGLayout;
    wgpu::BindGroup bindGroup = ctx.device.CreateBindGroup(&bindGroupDesc);

    // ---------------------------------------------------------
    // Override constants
    //
    // Match the WGSL:
    //
    // override WIDTH: u32;
    // override HEIGHT: u32;
    //
    // These values become fixed when ComputePipeline is created.
    // ---------------------------------------------------------
    std::vector<wgpu::ConstantEntry> constants(2);

    /*
    Alternative explicit IDs in WGSL:

    @id(0) override WIDTH: u32;
    @id(1) override HEIGHT: u32;

    Then C++ can use:

    constants[0].key = "0";
    constants[1].key = "1";
    */
    constants[0].key = "WIDTH";
    constants[0].value = static_cast<double>(WIDTH);

    constants[1].key = "HEIGHT";
    constants[1].value = static_cast<double>(HEIGHT);

    wgpu::ComputePipelineDescriptor computePipelineDesc = {};
    computePipelineDesc.compute.constantCount = constants.size();
    computePipelineDesc.compute.constants = constants.data();
    computePipelineDesc.compute.entryPoint = "compute_main";
    computePipelineDesc.compute.module = shaderModule;
    computePipelineDesc.label = "Compute Pipeline";
    computePipelineDesc.layout = pipelineLayout;

    wgpu::ComputePipeline computePipeline =
        ctx.device.CreateComputePipeline(&computePipelineDesc);

    wgpu::CommandEncoder cmdEncoder = ctx.device.CreateCommandEncoder();

    wgpu::ComputePassEncoder computePass = cmdEncoder.BeginComputePass();
    computePass.SetPipeline(computePipeline);
    computePass.SetBindGroup(0, bindGroup);

    // ---------------------------------------------------------
    // Dispatch 2D workgroup grid
    //
    // Shader uses:
    //     @workgroup_size(8, 8, 1)
    //
    // Therefore one workgroup covers an 8x8 block.
    //
    // For WIDTH = 80:
    //     80 / 8 = 10 workgroups in X
    //
    // For HEIGHT = 40:
    //     40 / 8 = 5 workgroups in Y
    //
    // Total:
    //     10 * 5 workgroups
    //     80 * 40 shader invocations
    //
    // The rounded-up formula also works when the dimensions
    // are not exactly divisible by 8.
    // ---------------------------------------------------------
    uint32_t groupsX = (WIDTH + 7) / 8;
    uint32_t groupsY = (HEIGHT + 7) / 8;
    computePass.DispatchWorkgroups(groupsX, groupsY, 1);
    computePass.End();

    // Compute shader writes into storageBuffer.
    //
    // After the compute pass finishes, copy the results into
    // the CPU-readable readback buffer.
    cmdEncoder.CopyBufferToBuffer(storageBuffer, 0, readStorageBuffer, 0,
                                  BUFFER_SIZE);

    wgpu::CommandBuffer cmdBuffer = cmdEncoder.Finish();
    ctx.queue.Submit(1, &cmdBuffer);

    auto onBufferMapped = [](wgpu::MapAsyncStatus status,
                             wgpu::StringView message) {
        if (status != wgpu::MapAsyncStatus::Success) {
            std::cerr << "Map failed\n";
        }
    };

    wgpu::Future bufferAsyncReadFuture = readStorageBuffer.MapAsync(
        wgpu::MapMode::Read, 0, BUFFER_SIZE, wgpu::CallbackMode::WaitAnyOnly,
        onBufferMapped);

    ctx.instance.WaitAny(bufferAsyncReadFuture, UINT64_MAX);

    const uint32_t* bufferdata = static_cast<const uint32_t*>(
        readStorageBuffer.GetConstMappedRange(0, BUFFER_SIZE));

    for (uint32_t y = 0; y < HEIGHT; ++y) {
        for (uint32_t x = 0; x < WIDTH; ++x) {
            // Each buffer element represents one screen/fractal cell.
            //
            // Convert:
            //     (x, y) -> linear buffer index
            //
            //     index = y * WIDTH + x
            //
            // Larger iteration count means the point stayed inside
            // the Mandelbrot iteration longer.
            uint32_t iters = bufferdata[y * WIDTH + x];

            if (iters == 100)
                std::cout << "#";
            else if (iters > 50)
                std::cout << "*";
            else if (iters > 20)
                std::cout << ":";
            else if (iters > 5)
                std::cout << ".";
            else
                std::cout << " ";
        }
        std::cout << "\n";
    }

    readStorageBuffer.Unmap();

    return 0;
}