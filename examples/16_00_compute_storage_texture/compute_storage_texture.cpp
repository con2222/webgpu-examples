#include <context.hpp>
#include <cstdint>
#include <iostream>
#include <vector>

static WebGPUContext ctx;
static WindowData data;

const char* shader = R"(

// ---------------------------------------------------------
// One texture, two shader interfaces
//
// Compute writes through outputTexture; fragment samples sceneTexture.
// Binding 0 is reused because each entry point accesses only one
// declaration, and the pipelines use different bind group layouts.
// ---------------------------------------------------------
@group(0) @binding(0)
var outputTexture: texture_storage_2d<rgba8unorm, write>;

@group(0) @binding(0)
var sceneTexture: texture_2d<f32>;

@group(0) @binding(1)
var sceneSampler: sampler;

@compute @workgroup_size(8, 8)
fn main_cs(@builtin(global_invocation_id) gid: vec3u) {
    let size = textureDimensions(outputTexture);

    // Dispatch dimensions are rounded up to whole workgroups.
    // Skip invocations outside the texture.
    if (gid.x >= size.x || gid.y >= size.y) {
        return;
    }

    let uv = (vec2f(gid.xy) + vec2f(0.5)) / vec2f(size);
    let aspect = f32(size.x) / f32(size.y);
    let c = vec2f(
        (uv.x - 0.5) * 2.6 * aspect - 0.5,
        (0.5 - uv.y) * 2.6
    );

    var z = vec2f(0.0);
    var iteration = 0u;
    let maxIterations = 256u;

    while (iteration < maxIterations && dot(z, z) <= 4.0) {
        z = vec2f(z.x * z.x - z.y * z.y, 2.0 * z.x * z.y) + c;
        iteration += 1u;
    }

    var color = vec3f(0.0);

    if (iteration < maxIterations) {
        let smoothIteration = f32(iteration) + 1.0
            - log2(log2(length(z)));
        let t = smoothIteration * 0.025;
        color = vec3f(0.5) + vec3f(0.5)
            * cos(6.2831853 * (vec3f(t) + vec3f(0.0, 0.15, 0.3)));
    }

    // Write one RGBA texel at integer texture coordinates.
    // Storage writes do not use a sampler or filtering.
    textureStore(outputTexture, vec2i(gid.xy), vec4f(color, 1.0));
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

@vertex
fn scene_vs(@builtin(vertex_index) index: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f(3.0, -1.0),
        vec2f(-1.0, 3.0)
    );

    let position = positions[index];
    var out: VertexOutput;
    out.position = vec4f(position, 0.0, 1.0);
    out.uv = vec2f(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
    return out;
}

@fragment
fn scene_fs(in: VertexOutput) -> @location(0) vec4f {
    return textureSample(sceneTexture, sceneSampler, in.uv);
}
)";

int main() {
    if (!glfwInit()) {
        return 1;
    }

    ctx.instance = createInstance();
    ctx.adapter = createAdapter(ctx.instance);
    ctx.device = createDevice(ctx.instance, ctx.adapter);
    ctx.queue = ctx.device.GetQueue();

    data = addWindow(800, 600, "Mandelbrot", ctx.instance, ctx.adapter,
                     ctx.device);

    wgpu::ShaderModule shaderModule = CreateShaderModule(ctx.device, shader);

    wgpu::SamplerDescriptor sceneSamplerDesc = {};
    sceneSamplerDesc.minFilter = wgpu::FilterMode::Linear;
    sceneSamplerDesc.magFilter = wgpu::FilterMode::Linear;
    sceneSamplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
    sceneSamplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
    sceneSamplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;
    wgpu::Sampler sceneSampler = ctx.device.CreateSampler(&sceneSamplerDesc);

    std::vector<wgpu::BindGroupLayoutEntry> BGLayoutEntries(2);
    BGLayoutEntries[0].binding = 0;
    BGLayoutEntries[0].visibility = wgpu::ShaderStage::Fragment;
    BGLayoutEntries[0].texture.sampleType = wgpu::TextureSampleType::Float;
    BGLayoutEntries[0].texture.viewDimension = wgpu::TextureViewDimension::e2D;
    BGLayoutEntries[0].texture.multisampled = false;

    BGLayoutEntries[1].binding = 1;
    BGLayoutEntries[1].visibility = wgpu::ShaderStage::Fragment;
    BGLayoutEntries[1].sampler.type = wgpu::SamplerBindingType::Filtering;

    wgpu::BindGroupLayoutDescriptor postProcessBindGroupLayoutDesc = {};
    postProcessBindGroupLayoutDesc.entries = BGLayoutEntries.data();
    postProcessBindGroupLayoutDesc.entryCount = BGLayoutEntries.size();
    wgpu::BindGroupLayout postProcessBindGroupLayout =
        ctx.device.CreateBindGroupLayout(&postProcessBindGroupLayoutDesc);

    wgpu::PipelineLayoutDescriptor postProcessPipelineLayoutDesc = {};
    postProcessPipelineLayoutDesc.bindGroupLayoutCount = 1;
    postProcessPipelineLayoutDesc.bindGroupLayouts =
        &postProcessBindGroupLayout;
    wgpu::PipelineLayout postProcessPipelineLayout =
        ctx.device.CreatePipelineLayout(&postProcessPipelineLayoutDesc);

    // Describe the write-only storage texture expected by the compute shader.
    // Format and view dimension must match the bound view and WGSL declaration.
    wgpu::BindGroupLayoutEntry computeBGLayoutEntry = {};
    computeBGLayoutEntry.binding = 0;
    computeBGLayoutEntry.visibility = wgpu::ShaderStage::Compute;
    computeBGLayoutEntry.storageTexture.access =
        wgpu::StorageTextureAccess::WriteOnly;
    computeBGLayoutEntry.storageTexture.format =
        wgpu::TextureFormat::RGBA8Unorm;
    computeBGLayoutEntry.storageTexture.viewDimension =
        wgpu::TextureViewDimension::e2D;

    wgpu::BindGroupLayoutDescriptor computeBGLayoutDesc = {};
    computeBGLayoutDesc.entries = &computeBGLayoutEntry;
    computeBGLayoutDesc.entryCount = 1;
    wgpu::BindGroupLayout computeBGLayout =
        ctx.device.CreateBindGroupLayout(&computeBGLayoutDesc);

    wgpu::PipelineLayoutDescriptor computePipelineLayoutDesc = {};
    computePipelineLayoutDesc.bindGroupLayoutCount = 1;
    computePipelineLayoutDesc.bindGroupLayouts = &computeBGLayout;
    wgpu::PipelineLayout computePipelineLayout =
        ctx.device.CreatePipelineLayout(&computePipelineLayoutDesc);

    wgpu::ComputePipelineDescriptor computePipelineDesc = {};
    computePipelineDesc.layout = computePipelineLayout;
    computePipelineDesc.compute.module = shaderModule;
    computePipelineDesc.compute.entryPoint = "main_cs";
    wgpu::ComputePipeline computePipeline =
        ctx.device.CreateComputePipeline(&computePipelineDesc);

    wgpu::ColorTargetState sceneColorTarget = {};
    sceneColorTarget.format = data.currentConfig.format;
    sceneColorTarget.writeMask = wgpu::ColorWriteMask::All;

    wgpu::FragmentState sceneFragmentState = {};
    sceneFragmentState.module = shaderModule;
    sceneFragmentState.entryPoint = "scene_fs";
    sceneFragmentState.targetCount = 1;
    sceneFragmentState.targets = &sceneColorTarget;

    wgpu::RenderPipelineDescriptor scenePipelineDesc = {};
    scenePipelineDesc.layout = postProcessPipelineLayout;
    scenePipelineDesc.vertex.module = shaderModule;
    scenePipelineDesc.vertex.entryPoint = "scene_vs";
    scenePipelineDesc.primitive.topology =
        wgpu::PrimitiveTopology::TriangleList;
    scenePipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    scenePipelineDesc.multisample.count = 1;
    scenePipelineDesc.fragment = &sceneFragmentState;
    wgpu::RenderPipeline scenePipeline =
        ctx.device.CreateRenderPipeline(&scenePipelineDesc);

    wgpu::Texture texture;
    wgpu::TextureView textureView;
    wgpu::BindGroup computeBindGroup;
    wgpu::BindGroup postProcessBindGroup;
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;

    // ---------------------------------------------------------
    // Compute output texture
    //
    // StorageBinding allows storage access; TextureBinding allows sampling.
    // Both bind groups reference the same texture view; no image copy is
    // needed. After resizing, rebuild both bind groups because existing bind
    // groups retain their original resources.
    // ---------------------------------------------------------
    auto recreateTexture = [&](uint32_t width, uint32_t height) {
        wgpu::TextureDescriptor textureDesc = {};
        textureDesc.dimension = wgpu::TextureDimension::e2D;
        textureDesc.size = {width, height, 1};
        textureDesc.format = wgpu::TextureFormat::RGBA8Unorm;
        textureDesc.mipLevelCount = 1;
        textureDesc.sampleCount = 1;
        textureDesc.usage = wgpu::TextureUsage::StorageBinding |
                            wgpu::TextureUsage::TextureBinding;

        texture = ctx.device.CreateTexture(&textureDesc);
        textureView = texture.CreateView();

        wgpu::BindGroupEntry computeEntry = {};
        computeEntry.binding = 0;
        computeEntry.textureView = textureView;

        wgpu::BindGroupDescriptor computeBindGroupDesc = {};
        computeBindGroupDesc.layout = computeBGLayout;
        computeBindGroupDesc.entryCount = 1;
        computeBindGroupDesc.entries = &computeEntry;
        computeBindGroup = ctx.device.CreateBindGroup(&computeBindGroupDesc);

        wgpu::BindGroupEntry renderEntries[2] = {};
        renderEntries[0].binding = 0;
        renderEntries[0].textureView = textureView;
        renderEntries[1].binding = 1;
        renderEntries[1].sampler = sceneSampler;

        wgpu::BindGroupDescriptor postProcessBindGroupDesc = {};
        postProcessBindGroupDesc.layout = postProcessBindGroupLayout;
        postProcessBindGroupDesc.entryCount = 2;
        postProcessBindGroupDesc.entries = renderEntries;
        postProcessBindGroup =
            ctx.device.CreateBindGroup(&postProcessBindGroupDesc);

        textureWidth = width;
        textureHeight = height;
    };

    while (!glfwWindowShouldClose(data.window)) {
        glfwPollEvents();
        ctx.instance.ProcessEvents();
        SyncFromWindow(&data);

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(data.window, &width, &height);

        if (width <= 0 || height <= 0) {
            glfwWaitEventsTimeout(0.05);
            continue;
        }

        if (!IsSameConfig(data.currentConfig, data.targetConfig)) {
            data.surface.Configure(&data.targetConfig);
            data.currentConfig = data.targetConfig;
        }

        if (textureWidth != data.currentConfig.width ||
            textureHeight != data.currentConfig.height) {
            recreateTexture(data.currentConfig.width,
                            data.currentConfig.height);
        }

        wgpu::SurfaceTexture surfaceTexture = {};
        data.surface.GetCurrentTexture(&surfaceTexture);

        if (!surfaceTexture.texture) {
            data.surface.Configure(&data.currentConfig);
            continue;
        }

        wgpu::TextureView surfaceView = surfaceTexture.texture.CreateView();
        wgpu::CommandEncoder encoder = ctx.device.CreateCommandEncoder();

        // ---------------------------------------------------------
        // Generate the image, then display it
        //
        // Encode compute before rendering so the render pass reads this frame's
        // image. WebGPU handles the required synchronization between these
        // passes. No CPU readback or explicit CPU wait is needed.
        // ---------------------------------------------------------
        wgpu::ComputePassEncoder computePass = encoder.BeginComputePass();
        computePass.SetPipeline(computePipeline);
        computePass.SetBindGroup(0, computeBindGroup);

        // Each workgroup covers an 8x8 region. Round up to cover the full
        // texture.
        computePass.DispatchWorkgroups((textureWidth + 7) / 8,
                                       (textureHeight + 7) / 8);
        computePass.End();

        wgpu::RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = surfaceView;
        colorAttachment.loadOp = wgpu::LoadOp::Clear;
        colorAttachment.storeOp = wgpu::StoreOp::Store;
        colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

        wgpu::RenderPassDescriptor renderPassDesc = {};
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &colorAttachment;

        wgpu::RenderPassEncoder renderPass =
            encoder.BeginRenderPass(&renderPassDesc);
        renderPass.SetPipeline(scenePipeline);
        renderPass.SetBindGroup(0, postProcessBindGroup);
        renderPass.Draw(3);
        renderPass.End();

        wgpu::CommandBuffer commands = encoder.Finish();
        ctx.queue.Submit(1, &commands);

        if (data.surface.Present() != wgpu::Status::Success) {
            std::cerr << "Present failed\n";
            break;
        }
    }

    data.surface.Unconfigure();
    glfwDestroyWindow(data.window);
    glfwTerminate();
    return 0;
}
