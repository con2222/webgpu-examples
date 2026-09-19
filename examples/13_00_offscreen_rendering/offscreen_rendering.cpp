#include <context.hpp>
#include <iostream>
#include <math.hpp>
#include <random>
#include <vector>

static WebGPUContext ctx;
static WindowData data;

const char* shader = R"(

struct PostProcessVSOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

struct SceneVSOutput {
    @builtin(position) position : vec4f,
};


// The result of the Scene Pass is exposed to the
// Post-Processing Pass as a regular sampled texture.
@group(0) @binding(1)
var sceneSampler: sampler;

@group(0) @binding(0)
var sceneTexture: texture_2d<f32>;


// ---------------------------------------------------------
// Scene pass
//
// A fullscreen triangle is generated directly from vertex_index.
// No vertex buffer is needed.
//
// The triangle is intentionally larger than the viewport,
// so after clipping it covers the entire render target.
// ---------------------------------------------------------
@vertex
fn scene_vs(@builtin(vertex_index) idx: u32) -> SceneVSOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );

    var out: SceneVSOutput;
    out.position = vec4f(positions[idx], 0.0, 1.0);
    return out;
}

// Generate a procedural color for every fragment.
//
// @builtin(position) in a fragment shader contains the
// fragment position in framebuffer coordinates.
//
// Dividing by the render target resolution converts
// pixel coordinates into normalized coordinates [0, 1].
@fragment
fn scene_fs(
    @builtin(position) fragCoord: vec4f
) -> @location(0) vec4f {

    let resolution = vec2f(800.0, 600.0);

    let uv = fragCoord.xy / resolution;

    return vec4f(
        uv.x,
        uv.y,
        0.0f,
        1.0
    );
}


// ---------------------------------------------------------
// Post-processing pass
//
// Another fullscreen triangle is used to display the
// offscreen texture over the entire surface.
//
// UV coordinates extend to 2 because the triangle itself
// extends beyond clip-space [-1, 1]. After clipping,
// the visible part interpolates approximately over [0, 1].
// ---------------------------------------------------------
@vertex
fn post_process_vs(@builtin(vertex_index) idx: u32) -> PostProcessVSOutput {
    var out: PostProcessVSOutput;

    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );

    var uvs = array<vec2f, 3>(
        vec2f(0.0, 0.0),
        vec2f(2.0, 0.0),
        vec2f(0.0, 2.0)
    );

    out.position = vec4f(positions[idx], 0.0, 1.0);
    out.uv = uvs[idx];

    return out;
}

@fragment
fn post_process_fs(in: PostProcessVSOutput) -> @location(0) vec4f {
    // Read the color produced by the Scene Pass.
    let color = textureSample(sceneTexture, sceneSampler, in.uv);

    // Measure how far this fragment is from the screen center.
    let distanceFromCenter =
        length(in.uv - vec2f(0.5, 0.5));

    // Keep the center bright and gradually darken the edges.
    let vignette =
        1.0 - smoothstep(0.2, 0.8, distanceFromCenter);

    return vec4f(color.rgb * vignette, color.a);
}
)";

void DoRender(WindowData* data, const wgpu::Device& device,
              const wgpu::Queue& queue,
              const wgpu::RenderPipeline& postProcessPipeline,
              const wgpu::RenderPipeline& scenePipeline,
              wgpu::TextureView offscreenView,
              const wgpu::BindGroup& postProcessBindGroup) {
    wgpu::SurfaceTexture surfaceTexture = {};
    data->surface.GetCurrentTexture(&surfaceTexture);
    wgpu::TextureView surfaceView = surfaceTexture.texture.CreateView();

    wgpu::RenderPassDescriptor scenePassDesc = {};
    wgpu::RenderPassColorAttachment sceneColorAttachment = {};
    sceneColorAttachment.view =
        offscreenView;  // Pass 1 renders into an intermediate GPU texture.
    sceneColorAttachment.loadOp = wgpu::LoadOp::Clear;
    sceneColorAttachment.storeOp = wgpu::StoreOp::Store;
    sceneColorAttachment.clearValue = wgpu::Color{0.0, 0.0, 0.0, 1.0};
    scenePassDesc.colorAttachmentCount = 1;
    scenePassDesc.colorAttachments = &sceneColorAttachment;

    wgpu::RenderPassDescriptor postProcessPassDesc = {};
    wgpu::RenderPassColorAttachment surfaceColorAttachment = {};
    surfaceColorAttachment.view =
        surfaceView;  // Pass 2 renders the final processed image to the window.
    surfaceColorAttachment.loadOp = wgpu::LoadOp::Clear;
    surfaceColorAttachment.storeOp = wgpu::StoreOp::Store;
    surfaceColorAttachment.clearValue = wgpu::Color{0.0, 0.0, 0.0, 1.0};
    postProcessPassDesc.colorAttachmentCount = 1;
    postProcessPassDesc.colorAttachments = &surfaceColorAttachment;

    wgpu::CommandEncoder commandEncoder = device.CreateCommandEncoder();

    // ---------------------------------------------------------
    // Pass 1: Scene -> Offscreen Texture
    //
    // The render target is offscreenView instead of surfaceView.
    // The resulting image stays entirely on the GPU.
    // ---------------------------------------------------------
    wgpu::RenderPassEncoder scenePass =
        commandEncoder.BeginRenderPass(&scenePassDesc);
    scenePass.SetPipeline(scenePipeline);
    scenePass.Draw(3);
    scenePass.End();

    // ---------------------------------------------------------
    // Pass 2: Offscreen Texture -> Surface
    //
    // The post-processing shader samples the image produced
    // by Pass 1 and writes the processed result to the surface.
    //
    // Both passes are recorded into the same CommandEncoder.
    // ---------------------------------------------------------
    wgpu::RenderPassEncoder postProcessPass =
        commandEncoder.BeginRenderPass(&postProcessPassDesc);

    postProcessPass.SetPipeline(postProcessPipeline);
    postProcessPass.SetBindGroup(0, postProcessBindGroup);
    postProcessPass.Draw(3);

    postProcessPass.End();

    wgpu::CommandBuffer cmdBuffer = commandEncoder.Finish();
    queue.Submit(1, &cmdBuffer);

    wgpu::Status presentStatus = data->surface.Present();

    if (presentStatus != wgpu::Status::Success) {
        std::cout << "Present status failed" << '\n';
    }
}

int main() {
    glfwInit();

    ctx.instance = createInstance();
    ctx.adapter = createAdapter(ctx.instance);
    ctx.device = createDevice(ctx.instance, ctx.adapter);
    ctx.queue = ctx.device.GetQueue();

    data = addWindow(800, 600, "Main Window", ctx.instance, ctx.adapter,
                     ctx.device);

    wgpu::ShaderModule shaderModule = CreateShaderModule(ctx.device, shader);

    // ---------------------------------------------------------
    // Offscreen render target
    //
    // RenderAttachment:
    //     allows the Scene Pass to render into this texture.
    //
    // TextureBinding:
    //     allows the Post-Processing Pass to sample the
    //     rendered image in its fragment shader.
    //
    // Data flow:
    //
    // Scene Pass -> offscreenTexture -> Post Process -> Surface
    // ---------------------------------------------------------
    wgpu::TextureDescriptor offscreenTextureDesc = {};
    offscreenTextureDesc.size = {data.targetConfig.width,
                                 data.targetConfig.height, 1};
    offscreenTextureDesc.format = data.currentConfig.format;
    offscreenTextureDesc.usage = wgpu::TextureUsage::RenderAttachment |
                                 wgpu::TextureUsage::TextureBinding;

    wgpu::Texture offscreenTexture =
        ctx.device.CreateTexture(&offscreenTextureDesc);

    wgpu::TextureView offscreenView = offscreenTexture.CreateView();
    wgpu::SamplerDescriptor sceneSamplerDesc = {};
    sceneSamplerDesc.minFilter = wgpu::FilterMode::Linear;
    sceneSamplerDesc.magFilter = wgpu::FilterMode::Linear;
    sceneSamplerDesc.addressModeU = wgpu::AddressMode::ClampToEdge;
    sceneSamplerDesc.addressModeV = wgpu::AddressMode::ClampToEdge;
    sceneSamplerDesc.addressModeW = wgpu::AddressMode::ClampToEdge;

    wgpu::Sampler sceneSampler = ctx.device.CreateSampler(&sceneSamplerDesc);

    std::vector<wgpu::BindGroupLayoutEntry> postProcessLayoutEntries(2);
    postProcessLayoutEntries[0].binding = 0;
    postProcessLayoutEntries[0].texture.sampleType =
        wgpu::TextureSampleType::Float;
    postProcessLayoutEntries[0].texture.viewDimension =
        wgpu::TextureViewDimension::e2D;
    postProcessLayoutEntries[0].visibility =
        wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;

    postProcessLayoutEntries[1].binding = 1;
    postProcessLayoutEntries[1].visibility =
        wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex;
    postProcessLayoutEntries[1].sampler.type =
        wgpu::SamplerBindingType::Filtering;

    wgpu::BindGroupLayoutDescriptor postProcessBindGroupLayoutDesc = {};
    postProcessBindGroupLayoutDesc.entries = postProcessLayoutEntries.data();
    postProcessBindGroupLayoutDesc.entryCount = 2;
    wgpu::BindGroupLayout postProcessBindGroupLayout =
        ctx.device.CreateBindGroupLayout(&postProcessBindGroupLayoutDesc);

    wgpu::PipelineLayoutDescriptor postProcessPipelineLayoutDesc = {};
    postProcessPipelineLayoutDesc.bindGroupLayoutCount = 1;
    postProcessPipelineLayoutDesc.bindGroupLayouts =
        &postProcessBindGroupLayout;
    wgpu::PipelineLayout postProcessPipelineLayout =
        ctx.device.CreatePipelineLayout(&postProcessPipelineLayoutDesc);

    // Expose the offscreen texture and sampler to the
    // post-processing fragment shader.
    std::vector<wgpu::BindGroupEntry> postProcessBindGroupEntries(2);

    postProcessBindGroupEntries[0].binding = 0;
    postProcessBindGroupEntries[0].textureView = offscreenView;

    postProcessBindGroupEntries[1].binding = 1;
    postProcessBindGroupEntries[1].sampler = sceneSampler;

    wgpu::BindGroupDescriptor postProcessBindGroupDesc = {};
    postProcessBindGroupDesc.entries = postProcessBindGroupEntries.data();
    postProcessBindGroupDesc.entryCount = postProcessBindGroupEntries.size();
    postProcessBindGroupDesc.layout = postProcessBindGroupLayout;
    wgpu::BindGroup postProcessBindGroup =
        ctx.device.CreateBindGroup(&postProcessBindGroupDesc);

    wgpu::RenderPipelineDescriptor scenePipelineDesc = {};
    scenePipelineDesc.depthStencil = nullptr;

    scenePipelineDesc.vertex.buffers = nullptr;
    scenePipelineDesc.vertex.bufferCount = 0;
    scenePipelineDesc.vertex.module = shaderModule;
    scenePipelineDesc.vertex.entryPoint = "scene_vs";

    scenePipelineDesc.primitive.topology =
        wgpu::PrimitiveTopology::TriangleList;
    scenePipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    scenePipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;

    scenePipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    scenePipelineDesc.depthStencil = nullptr;
    scenePipelineDesc.multisample.count = 1;
    scenePipelineDesc.multisample.mask = 0xFFFFFFFF;
    scenePipelineDesc.multisample.alphaToCoverageEnabled = false;

    wgpu::FragmentState sceneFragmentState = {};
    sceneFragmentState.module = shaderModule;
    sceneFragmentState.entryPoint = "scene_fs";
    sceneFragmentState.targetCount = 1;
    wgpu::ColorTargetState sceneColorTarget = {};
    sceneColorTarget.format = data.currentConfig.format;
    sceneFragmentState.targets = &sceneColorTarget;
    scenePipelineDesc.fragment = &sceneFragmentState;

    wgpu::RenderPipeline scenePipeline =
        ctx.device.CreateRenderPipeline(&scenePipelineDesc);

    wgpu::RenderPipelineDescriptor postProcessPipelineDesc = {};
    postProcessPipelineDesc.depthStencil = nullptr;
    postProcessPipelineDesc.layout = postProcessPipelineLayout;

    postProcessPipelineDesc.vertex.buffers = nullptr;
    postProcessPipelineDesc.vertex.bufferCount = 0;
    postProcessPipelineDesc.vertex.module = shaderModule;
    postProcessPipelineDesc.vertex.entryPoint = "post_process_vs";

    postProcessPipelineDesc.primitive.topology =
        wgpu::PrimitiveTopology::TriangleList;
    postProcessPipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    postProcessPipelineDesc.primitive.stripIndexFormat =
        wgpu::IndexFormat::Undefined;

    postProcessPipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    postProcessPipelineDesc.depthStencil = nullptr;
    postProcessPipelineDesc.multisample.count = 1;
    postProcessPipelineDesc.multisample.mask = 0xFFFFFFFF;
    postProcessPipelineDesc.multisample.alphaToCoverageEnabled = false;

    wgpu::FragmentState postProcessFragmentState = {};
    postProcessFragmentState.module = shaderModule;
    postProcessFragmentState.entryPoint = "post_process_fs";
    postProcessFragmentState.targetCount = 1;
    wgpu::ColorTargetState postProcessColorTarget = {};
    postProcessColorTarget.format = data.currentConfig.format;
    postProcessFragmentState.targets = &postProcessColorTarget;
    postProcessPipelineDesc.fragment = &postProcessFragmentState;

    wgpu::RenderPipeline postProcessPipeline =
        ctx.device.CreateRenderPipeline(&postProcessPipelineDesc);

    while (!glfwWindowShouldClose(data.window)) {
        glfwPollEvents();
        ctx.instance.ProcessEvents();
        SyncFromWindow(&data);

        if (!IsSameConfig(data.currentConfig, data.targetConfig)) {
            data.surface.Configure(&data.targetConfig);
            data.currentConfig = data.targetConfig;
        }

        DoRender(&data, ctx.device, ctx.queue, postProcessPipeline,
                 scenePipeline, offscreenView, postProcessBindGroup);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();
}