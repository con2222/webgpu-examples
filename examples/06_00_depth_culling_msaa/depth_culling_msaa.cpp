#include <context.hpp>
#include <iostream>
#include <math.hpp>
#include <vector>

struct VertexAttribute {
    vec4 position;
    vec4 color;
};

struct Uniforms {
    mat4 modelViewMatrix;
    mat4 perspectiveMatrix;
    float time;

    float _pad[3];
};

constexpr bool depthStencilToggle = true;

static WindowData data;
static WebGPUContext ctx;
static wgpu::Buffer sharedVertexBuffer;
static wgpu::Buffer indexBuffer;
static wgpu::Buffer uniformBuffer;

// ==========================================
// DEPTH BUFFER GLOBALS
// ==========================================
static wgpu::Texture depthTexture;
static wgpu::TextureView depthTextureView;
static constexpr wgpu::TextureFormat DEPTH_FORMAT =
    wgpu::TextureFormat::Depth24Plus;

// ==========================================
// MSAA GLOBALS
// ==========================================
static constexpr uint32_t SAMPLE_COUNT = 4;  // 4x MSAA
static wgpu::Texture msaaColorTexture;
static wgpu::TextureView msaaColorView;

const uint32_t UNIFORM_ALIGNMENT = 256;

std::vector<vec4> positions = {{-0.5, -0.5, 0.5, 1.0},  {0.5, -0.5, 0.5, 1.0},
                               {0.5, 0.5, 0.5, 1.0},    {-0.5, 0.5, 0.5, 1.0},
                               {-0.5, -0.5, -0.5, 1.0}, {0.5, -0.5, -0.5, 1.0},
                               {0.5, 0.5, -0.5, 1.0},   {-0.5, 0.5, -0.5, 1.0}};

std::vector<vec4> colors = {{1.0, 0.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0},
                            {0.0, 0.0, 1.0, 1.0}, {1.0, 1.0, 0.0, 1.0},
                            {1.0, 0.1, 0.5, 1.0}, {0.1, 1.0, 0.5, 1.0},
                            {0.5, 0.1, 1.0, 1.0}, {0.0, 1.0, 1.0, 1.0}};

std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0, 1, 5, 6, 6, 2, 1,
                                 5, 4, 7, 7, 6, 5, 4, 0, 3, 3, 7, 4,
                                 3, 2, 6, 6, 7, 3, 4, 5, 1, 1, 0, 4};

const uint64_t positionSize = positions.size() * sizeof(vec4);
const uint64_t colorsSize = colors.size() * sizeof(vec4);

const char* shader = R"(
struct Uniform {
    modelViewMatrix: mat4x4f,
    perspectiveMatrix: mat4x4f,
    time: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniform;

struct VertexInput {
    @location(0) position: vec4f,
    @location(1) color: vec4f,
};

struct VertexOutput {
    @builtin(position) position : vec4f, 
    @location(1) color : vec4f,
};

@vertex fn main_vs(in : VertexInput) -> VertexOutput {
    var out : VertexOutput;
    out.color = in.color;
    out.position = uniforms.perspectiveMatrix * uniforms.modelViewMatrix * in.position;
    return out;
}

@fragment fn main_fs(in : VertexOutput) -> @location(0) vec4f {
    var time: f32 = uniforms.time;
    var color : vec4f = vec4f(in.color.x * (sin(time + in.position.x * 0.01) * 0.5 + 0.5), in.color.y * (cos(time + in.position.y * 0.01) * 0.5 + 0.5), in.color.z * (cos(time + 1.0) * 0.5 + 0.5), 1.0);
    return color;
}
)";

void DoRender(WindowData* data, const wgpu::Device& device,
              const wgpu::Queue& queue, const wgpu::RenderPipeline& pipeline,
              const wgpu::Buffer& sharedVertexBuffer, uint64_t positionSize,
              uint64_t colorsSize, const wgpu::Buffer& indexBuffer,
              wgpu::BindGroup bindGroup, Uniforms& uniform) {
    wgpu::SurfaceTexture surfaceTexture;
    data->surface.GetCurrentTexture(&surfaceTexture);
    wgpu::TextureView surfaceView = surfaceTexture.texture.CreateView();

    wgpu::CommandEncoder cmdEncoder = device.CreateCommandEncoder();
    wgpu::RenderPassDescriptor renderPassDesc = {};

    // =========================================================
    // COLOR ATTACHMENT WITH MSAA SETUP
    // =========================================================
    wgpu::RenderPassColorAttachment colorAttachment = {};

    // view: The texture where we render our multi-sampled scene.
    // It contains multiple samples per pixel.
    colorAttachment.view = msaaColorView;

    // resolveTarget: The texture presented to the screen (1 sample per pixel).
    // The GPU automatically averages the samples from 'view' and writes the
    // final colors here.
    colorAttachment.resolveTarget = surfaceView;

    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    // storeOp = Discard: Since we already resolved the final image into
    // 'resolveTarget', we don't need to keep the heavy multi-sampled data in
    // VRAM. This saves memory bandwidth.
    colorAttachment.storeOp = wgpu::StoreOp::Discard;
    colorAttachment.clearValue = wgpu::Color{0.0, 0.35, 0.4, 1.0f};

    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    // =========================================================
    // DEPTH ATTACHMENT SETUP
    // =========================================================
    wgpu::RenderPassDepthStencilAttachment depthAttachment = {};

    if (depthStencilToggle) {
        depthAttachment.view = depthTextureView;

        // Clear the depth buffer to 1.0 (the furthest possible depth value) at
        // the start of each frame
        depthAttachment.depthLoadOp = wgpu::LoadOp::Clear;
        depthAttachment.depthClearValue = 1.0f;
        depthAttachment.depthStoreOp = wgpu::StoreOp::Store;
        depthAttachment.depthReadOnly = false;

        depthAttachment.stencilLoadOp = wgpu::LoadOp::Undefined;
        depthAttachment.stencilStoreOp = wgpu::StoreOp::Undefined;

        renderPassDesc.depthStencilAttachment = &depthAttachment;
    }

    int width, height;
    glfwGetWindowSize(data->window, &width, &height);

    mat4 proj = mat4::make_perspective(
        radians(45.0f), static_cast<float>(width) / static_cast<float>(height),
        0.1f, 100.0f);
    mat4 modelX = mat4::make_rotation_x(glfwGetTime());
    mat4 modelY = mat4::make_rotation_y(glfwGetTime() * 0.7f);
    mat4 model = modelX * modelY;

    mat4 view1 = mat4::make_translation(-.10f, 0.0f, -5.0f);
    uniform.modelViewMatrix = view1 * model;
    uniform.perspectiveMatrix = proj;
    uniform.time = glfwGetTime();
    queue.WriteBuffer(uniformBuffer, 0, &uniform, sizeof(Uniforms));

    mat4 view2 = mat4::make_translation(0.0f, 0.0f, -5.0f);
    uniform.modelViewMatrix = view2 * model;
    queue.WriteBuffer(uniformBuffer, 256, &uniform, sizeof(Uniforms));

    mat4 view3 = mat4::make_translation(1.5f, 0.0f, -5.0f);
    uniform.modelViewMatrix = view3 * model;
    queue.WriteBuffer(uniformBuffer, 512, &uniform, sizeof(Uniforms));

    wgpu::RenderPassEncoder renderPass =
        cmdEncoder.BeginRenderPass(&renderPassDesc);
    renderPass.SetPipeline(pipeline);
    renderPass.SetVertexBuffer(0, sharedVertexBuffer, 0, positionSize);
    renderPass.SetVertexBuffer(1, sharedVertexBuffer, positionSize, colorsSize);
    renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);

    uint32_t dynamicOffset = 0;
    renderPass.SetBindGroup(0, bindGroup, 1, &dynamicOffset);
    renderPass.DrawIndexed(36);

    dynamicOffset = 256;
    renderPass.SetBindGroup(0, bindGroup, 1, &dynamicOffset);
    renderPass.DrawIndexed(36);

    dynamicOffset = 512;
    renderPass.SetBindGroup(0, bindGroup, 1, &dynamicOffset);
    renderPass.DrawIndexed(36);

    renderPass.End();

    wgpu::CommandBuffer cmd = cmdEncoder.Finish();
    queue.Submit(1, &cmd);

    wgpu::Status presentStatus = data->surface.Present();
    if (presentStatus != wgpu::Status::Success) {
        std::cout << "Present status failed" << '\n';
    }
}

void createBuffers() {
    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.label = "Vertex Buffer";
    bufferDesc.mappedAtCreation = false;
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;

    bufferDesc.size = positionSize + colorsSize;
    sharedVertexBuffer = ctx.device.CreateBuffer(&bufferDesc);

    bufferDesc.label = "Index buffer";
    bufferDesc.mappedAtCreation = false;
    bufferDesc.size = indices.size() * sizeof(uint32_t);

    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index;
    indexBuffer = ctx.device.CreateBuffer(&bufferDesc);

    ctx.queue.WriteBuffer(sharedVertexBuffer, 0, positions.data(),
                          positionSize);

    ctx.queue.WriteBuffer(sharedVertexBuffer, positionSize, colors.data(),
                          colorsSize);

    ctx.queue.WriteBuffer(indexBuffer, 0, indices.data(),
                          indices.size() * sizeof(uint32_t));
}

wgpu::RenderPipeline createRenderPipeline(const wgpu::Device& device,
                                          wgpu::ShaderModule shaderModule,
                                          wgpu::PipelineLayout pipelineLayout) {
    wgpu::RenderPipelineDescriptor renderPipelineDesc = {};

    std::vector<wgpu::VertexBufferLayout> VBLayouts(2);
    std::vector<wgpu::VertexAttribute> vertexAttributes(2);

    vertexAttributes[0].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[0].offset = 0;
    vertexAttributes[0].shaderLocation = 0;

    vertexAttributes[1].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[1].offset = 0;
    vertexAttributes[1].shaderLocation = 1;

    VBLayouts[0].arrayStride = sizeof(vec4);
    VBLayouts[0].attributeCount = 1;
    VBLayouts[0].attributes = &vertexAttributes[0];
    VBLayouts[0].stepMode = wgpu::VertexStepMode::Vertex;

    VBLayouts[1].arrayStride = sizeof(vec4);
    VBLayouts[1].attributeCount = 1;
    VBLayouts[1].attributes = &vertexAttributes[1];
    VBLayouts[1].stepMode = wgpu::VertexStepMode::Vertex;

    renderPipelineDesc.vertex.bufferCount = 2;
    renderPipelineDesc.vertex.buffers = VBLayouts.data();
    renderPipelineDesc.vertex.entryPoint = "main_vs";
    renderPipelineDesc.vertex.module = shaderModule;

    // =========================================================
    // CULLING SETTINGS (Backface Culling)
    // =========================================================
    // cullMode = Back: Discards triangles facing away from the camera.
    // This saves GPU performance by not rendering the inside/back of 3D
    // objects.
    renderPipelineDesc.primitive.cullMode = wgpu::CullMode::Back;

    // frontFace = CCW: Defines that vertices defined in Counter-Clockwise
    // order relative to the camera are considered the "front" of the triangle.
    renderPipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;

    renderPipelineDesc.primitive.topology =
        wgpu::PrimitiveTopology::TriangleList;
    renderPipelineDesc.primitive.stripIndexFormat =
        wgpu::IndexFormat::Undefined;

    wgpu::FragmentState fragmentState = {};
    fragmentState.constantCount = 0;
    fragmentState.constants = nullptr;
    fragmentState.entryPoint = "main_fs";
    fragmentState.module = shaderModule;
    fragmentState.targetCount = 1;

    wgpu::ColorTargetState colorTargetState = {};
    colorTargetState.format = data.currentConfig.format;
    fragmentState.targets = &colorTargetState;

    renderPipelineDesc.fragment = &fragmentState;

    // =========================================================
    // MSAA PIPELINE SETTINGS
    // =========================================================
    // count: Defines how many samples are taken per pixel.
    // Must exactly match the sampleCount of our MSAA and Depth textures.
    renderPipelineDesc.multisample.count = SAMPLE_COUNT;
    renderPipelineDesc.multisample.mask = 0xFFFFFFFF;
    renderPipelineDesc.multisample.alphaToCoverageEnabled = false;

    renderPipelineDesc.layout = pipelineLayout;

    wgpu::DepthStencilState depthStencilState = {};
    if (depthStencilToggle) {
        // =========================================================
        // DEPTH BUFFER (Z-BUFFER) PIPELINE SETTINGS
        // =========================================================
        depthStencilState.format = DEPTH_FORMAT;

        // depthWriteEnabled = True: Fragments that pass the depth test will
        // update the depth buffer with their own depth value.
        depthStencilState.depthWriteEnabled = wgpu::OptionalBool::True;

        // depthCompare = Less: A new fragment is only drawn if its depth value
        // is strictly less (closer to the camera) than the value already in the
        // buffer.
        depthStencilState.depthCompare = wgpu::CompareFunction::Less;

        renderPipelineDesc.depthStencil = &depthStencilState;
    }

    wgpu::RenderPipeline renderPipeline =
        ctx.device.CreateRenderPipeline(&renderPipelineDesc);

    return renderPipeline;
}

void createDepthBuffer(uint32_t width, uint32_t height) {
    wgpu::TextureDescriptor depthDescriptor = {};

    depthDescriptor.label = "Depth buffer";
    depthDescriptor.dimension = wgpu::TextureDimension::e2D;

    depthDescriptor.size.width = width;
    depthDescriptor.size.height = height;
    depthDescriptor.size.depthOrArrayLayers = 1;

    depthDescriptor.mipLevelCount = 1;

    // The depth buffer must have the exact same sample count as our MSAA color
    // texture
    depthDescriptor.sampleCount = SAMPLE_COUNT;

    depthDescriptor.format = DEPTH_FORMAT;
    depthDescriptor.usage = wgpu::TextureUsage::RenderAttachment;

    depthTexture = ctx.device.CreateTexture(&depthDescriptor);
    depthTextureView = depthTexture.CreateView();
}

void createMSAABuffer(uint32_t width, uint32_t height) {
    wgpu::TextureDescriptor msaaDescriptor = {};

    msaaDescriptor.label = "MSAA buffer";
    msaaDescriptor.dimension = wgpu::TextureDimension::e2D;

    msaaDescriptor.size.width = width;
    msaaDescriptor.size.height = height;

    // depthOrArrayLayers = 1: We just need a single standard 2D image layer,
    // not a 3D texture or an array of textures.
    msaaDescriptor.size.depthOrArrayLayers = 1;

    msaaDescriptor.mipLevelCount = 1;
    msaaDescriptor.sampleCount = SAMPLE_COUNT;

    msaaDescriptor.format = data.currentConfig.format;
    msaaDescriptor.usage = wgpu::TextureUsage::RenderAttachment;

    msaaColorTexture = ctx.device.CreateTexture(&msaaDescriptor);
    msaaColorView = msaaColorTexture.CreateView();
}

int main() {
    glfwSetErrorCallback([](int code, const char* message) {
        std::cerr << "GLFW error " << code << " " << message;
    });
    glfwInit();

    ctx.instance = createInstance();
    ctx.adapter = createAdapter(ctx.instance);
    ctx.device = createDevice(ctx.instance, ctx.adapter);
    ctx.queue = ctx.device.GetQueue();
    wgpu::ShaderModule shaderModule = CreateShaderModule(ctx.device, shader);
    data = addWindow(1920, 1080, "Main window", ctx.instance, ctx.adapter,
                     ctx.device);

    createBuffers();

    Uniforms uniform;

    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.mappedAtCreation = false;
    bufferDesc.label = "Uniform buffer";

    bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    bufferDesc.size = UNIFORM_ALIGNMENT * 3;
    uniformBuffer = ctx.device.CreateBuffer(&bufferDesc);

    std::vector<wgpu::BindGroupLayoutEntry> bindingGroupLayoutEntries(1);
    bindingGroupLayoutEntries[0].binding = 0;
    bindingGroupLayoutEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    bindingGroupLayoutEntries[0].buffer.minBindingSize = sizeof(Uniforms);
    bindingGroupLayoutEntries[0].buffer.hasDynamicOffset = true;
    bindingGroupLayoutEntries[0].visibility =
        wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex;

    std::vector<wgpu::BindGroupLayout> BGLayouts(1);
    wgpu::BindGroupLayoutDescriptor BGLayoutDescriptor = {};
    BGLayoutDescriptor.entries = bindingGroupLayoutEntries.data();
    BGLayoutDescriptor.entryCount = 1;
    BGLayouts[0] = ctx.device.CreateBindGroupLayout(&BGLayoutDescriptor);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
    pipelineLayoutDescriptor.bindGroupLayouts = BGLayouts.data();
    wgpu::PipelineLayout pipelineLayout =
        ctx.device.CreatePipelineLayout(&pipelineLayoutDescriptor);

    std::vector<wgpu::BindGroupEntry> bindGroupEntries(1);
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = uniformBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = sizeof(Uniforms);

    wgpu::BindGroupDescriptor BGDesc = {};
    BGDesc.entries = bindGroupEntries.data();
    BGDesc.entryCount = 1;
    BGDesc.layout = BGLayouts[0];

    wgpu::BindGroup bindGroup = ctx.device.CreateBindGroup(&BGDesc);

    wgpu::RenderPipeline renderPipeline =
        createRenderPipeline(ctx.device, shaderModule, pipelineLayout);

    while (!glfwWindowShouldClose(data.window)) {
        ctx.instance.ProcessEvents();
        glfwPollEvents();

        SyncFromWindow(&data);
        if (!IsSameConfig(data.currentConfig, data.targetConfig)) {
            data.surface.Configure(&data.targetConfig);
            data.currentConfig = data.targetConfig;

            if (depthStencilToggle) {
                createDepthBuffer(data.currentConfig.width,
                                  data.currentConfig.height);
                createMSAABuffer(data.currentConfig.width,
                                 data.currentConfig.height);
            }
        }

        DoRender(&data, ctx.device, ctx.queue, renderPipeline,
                 sharedVertexBuffer, positionSize, colorsSize, indexBuffer,
                 bindGroup, uniform);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();
}