#include <context.hpp>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

constexpr uint32_t WIDTH = 80;
constexpr uint32_t HEIGHT = 40;
constexpr uint64_t BUFFER_SIZE = WIDTH * HEIGHT * sizeof(uint32_t);

WebGPUContext ctx;

// ---------------------------------------------------------
// Fractal camera
//
// centerX / centerY:
//     point in the Mandelbrot plane that stays in the
//     center of the screen.
//
// zoom:
//     size of the visible area around the center.
//     Smaller value = stronger zoom.
//
// _pad:
//     keeps the C++ struct layout compatible with the
//     WGSL uniform layout.
// ---------------------------------------------------------
struct Uniforms {
    float centerX;
    float centerY;
    float zoom;
    float _pad;
};

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

struct Uniform {
    centerX: f32,
    centerY: f32,
    zoom: f32,
    _pad: f32,
};

// Each element stores the number of Mandelbrot iterations
// for one output cell.
@group(0) @binding(0)
var<storage, read_write> computeData: array<u32>;


// Dynamic camera parameters.
//
// Unlike override constants, uniform values can be changed
// every frame without recreating the pipeline.
@group(0) @binding(1)
var<uniform> camera: Uniform;

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


    // Preserve the aspect ratio of the terminal output.
    // WIDTH is twice HEIGHT, so without this correction
    // the Mandelbrot set would appear stretched vertically.
    let aspect = f32(HEIGHT) / f32(WIDTH);

    // Move UV coordinates so that (0, 0) is the center
    // of the screen instead of the top-left corner.
    //
    // Then scale them by camera.zoom.
    // Smaller zoom means a smaller visible region,
    // therefore the image appears magnified.
    let centered_uv = vec2f((uv.x - 0.5) * camera.zoom, (uv.y - 0.5) * camera.zoom * aspect);


    // Move the visible region to the selected point
    // in the Mandelbrot coordinate system.
    let c = centered_uv + vec2f(camera.centerX, camera.centerY);

    // Mandelbrot iteration starts at z = 0.
    var z = vec2f(0.0, 0.0);

    var iterations: u32 = 0u;
    let max_iterations: u32 = 500u;

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
    storageBufferDesc.usage =
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
    wgpu::Buffer storageBuffer = ctx.device.CreateBuffer(&storageBufferDesc);

    storageBufferDesc.usage =
        wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    wgpu::Buffer readStorageBuffer =
        ctx.device.CreateBuffer(&storageBufferDesc);

    // ---------------------------------------------------------
    // Camera uniform buffer
    //
    // This buffer contains parameters that change while the
    // program is running.
    //
    // CopyDst is required because Queue::WriteBuffer()
    // updates the camera data every frame.
    // ---------------------------------------------------------
    wgpu::BufferDescriptor uniformsBufferDesc = {};
    uniformsBufferDesc.label = "Uniform buffer";
    uniformsBufferDesc.mappedAtCreation = false;
    uniformsBufferDesc.size = sizeof(Uniforms);
    uniformsBufferDesc.usage =
        wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer uniformBuffer = ctx.device.CreateBuffer(&uniformsBufferDesc);

    std::vector<wgpu::BindGroupLayoutEntry> bindLayoutEntries(2);

    bindLayoutEntries[0].binding = 0;
    bindLayoutEntries[0].buffer.type = wgpu::BufferBindingType::Storage;
    bindLayoutEntries[0].buffer.minBindingSize = BUFFER_SIZE;
    bindLayoutEntries[0].buffer.hasDynamicOffset = false;
    bindLayoutEntries[0].visibility = wgpu::ShaderStage::Compute;

    bindLayoutEntries[1].binding = 1;
    bindLayoutEntries[1].buffer.type = wgpu::BufferBindingType::Uniform;
    bindLayoutEntries[1].buffer.minBindingSize = sizeof(Uniforms);
    bindLayoutEntries[1].buffer.hasDynamicOffset = false;
    bindLayoutEntries[1].visibility = wgpu::ShaderStage::Compute;

    wgpu::BindGroupLayoutDescriptor BGLayoutDesc = {};
    BGLayoutDesc.entries = bindLayoutEntries.data();
    BGLayoutDesc.entryCount = bindLayoutEntries.size();
    wgpu::BindGroupLayout BGLayout =
        ctx.device.CreateBindGroupLayout(&BGLayoutDesc);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
    pipelineLayoutDescriptor.bindGroupLayouts = &BGLayout;
    wgpu::PipelineLayout pipelineLayout =
        ctx.device.CreatePipelineLayout(&pipelineLayoutDescriptor);

    std::vector<wgpu::BindGroupEntry> bindGroupEntries(2);
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = storageBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = BUFFER_SIZE;

    bindGroupEntries[1].binding = 1;
    bindGroupEntries[1].buffer = uniformBuffer;
    bindGroupEntries[1].offset = 0;
    bindGroupEntries[1].size = sizeof(Uniforms);

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

    // Start near an interesting boundary region of the
    // Mandelbrot set.
    //
    // Zooming into the boundary reveals increasingly
    // detailed fractal structures.
    Uniforms camera = {};
    camera.centerX = -0.743643887f;
    camera.centerY = 0.131825904f;
    camera.zoom = 0.1f;

    // Clear the terminal once before the animation starts.
    std::cout << "\x1b[2J";

    while (true) {
        // Upload the current camera state to the GPU.
        //
        // The pipeline and bind group stay the same.
        // Only the uniform data changes between frames.
        ctx.queue.WriteBuffer(uniformBuffer, 0, &camera, sizeof(Uniforms));

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
            wgpu::MapMode::Read, 0, BUFFER_SIZE,
            wgpu::CallbackMode::WaitAnyOnly, onBufferMapped);

        ctx.instance.WaitAny(bufferAsyncReadFuture, UINT64_MAX);

        const uint32_t* bufferdata = static_cast<const uint32_t*>(
            readStorageBuffer.GetConstMappedRange(0, BUFFER_SIZE));

        // Move the cursor back to the top-left corner.
        // The next frame overwrites the previous one instead
        // of being printed below it.
        std::cout << "\x1b[H";

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

                if (iters == 500)
                    std::cout << "#";
                else if (iters > 100)
                    std::cout << "*";
                else if (iters > 30)
                    std::cout << ":";
                else if (iters > 10)
                    std::cout << ".";
                else
                    std::cout << " ";
            }
            std::cout << "\n";
        }

        readStorageBuffer.Unmap();

        // Reduce the visible area slightly every frame.
        //
        // Smaller zoom -> smaller region of the Mandelbrot plane
        // -> stronger visual magnification.
        camera.zoom *= 0.995f;
        std::cout << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}