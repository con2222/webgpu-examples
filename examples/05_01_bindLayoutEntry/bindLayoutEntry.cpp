#include <context.hpp>
#include <iostream>
#include <math.hpp>
#include <vector>

// ---------------------------------------------------------
// Resource binding hierarchy:
//
// BindGroupLayoutEntry
//      ↓
// BindGroupLayout          describes one @group(...)
//      ↓
// PipelineLayout           describes all groups used by pipeline
//      ↓
// RenderPipeline
//
// Uniform Buffer
//      ↓
// BindGroupEntry           actual resource for @binding(...)
//      ↓
// BindGroup
//      ↓
// SetBindGroup()
// ---------------------------------------------------------

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

static WindowData data;
static WebGPUContext ctx;
static wgpu::Buffer sharedVertexBuffer;
static wgpu::Buffer indexBuffer;

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


// group(0), binding(0) identifies where this shader resource
// is expected in the pipeline's resource layout.

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
              wgpu::BindGroup bindGroup) {
    wgpu::SurfaceTexture surfaceTexture;
    data->surface.GetCurrentTexture(&surfaceTexture);
    wgpu::TextureView view = surfaceTexture.texture.CreateView();

    wgpu::CommandEncoder cmdEncoder = device.CreateCommandEncoder();

    wgpu::RenderPassDescriptor renderPassDesc = {};
    wgpu::RenderPassColorAttachment colorAttachment = {};
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.view = view;
    colorAttachment.clearValue = wgpu::Color{0.0, 0.35, 0.4, 1.0f};

    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    wgpu::RenderPassEncoder renderPass =
        cmdEncoder.BeginRenderPass(&renderPassDesc);

    renderPass.SetPipeline(pipeline);

    // Bind resource group 0.
    // This must match pipelineLayout.bindGroupLayouts[0]
    // and WGSL @group(0).
    renderPass.SetBindGroup(0, bindGroup, 0, nullptr);

    renderPass.SetVertexBuffer(0, sharedVertexBuffer, 0, positionSize);

    renderPass.SetVertexBuffer(1, sharedVertexBuffer, positionSize, colorsSize);

    renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);

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

    renderPipelineDesc.primitive.cullMode = wgpu::CullMode::None;
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

    renderPipelineDesc.multisample.count = 1;
    renderPipelineDesc.multisample.mask = 0xFFFFFFFF;
    renderPipelineDesc.multisample.alphaToCoverageEnabled = false;

    renderPipelineDesc.depthStencil = nullptr;

    // Use our explicitly created PipelineLayout instead of
    // letting WebGPU infer the layout from the shader.
    renderPipelineDesc.layout = pipelineLayout;

    wgpu::RenderPipeline renderPipeline =
        ctx.device.CreateRenderPipeline(&renderPipelineDesc);

    return renderPipeline;
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
    data = addWindow(800, 600, "Main window", ctx.instance, ctx.adapter,
                     ctx.device);
    createBuffers();

    Uniforms uniform;

    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.mappedAtCreation = false;
    bufferDesc.label = "Uniform buffer";

    bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    bufferDesc.size = sizeof(Uniforms);
    wgpu::Buffer uniformBuffer = ctx.device.CreateBuffer(&bufferDesc);

    // ---------------------------------------------------------
    // Explicit bind group layout setup
    //
    // In the previous example WebGPU inferred the resource layout
    // automatically from the shader.
    //
    // Here we describe the resource contract explicitly:
    // group(0), binding(0) must contain a uniform buffer.
    // ---------------------------------------------------------

    std::vector<wgpu::BindGroupLayoutEntry> bindingGroupLayoutEntries(1);

    // Describes what kind of resource is expected at @binding(0).
    bindingGroupLayoutEntries[0].binding = 0;

    // @binding(0) must contain a uniform buffer.
    bindingGroupLayoutEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;

    // Minimum buffer region size that may be bound to this binding.
    bindingGroupLayoutEntries[0].buffer.minBindingSize = sizeof(Uniforms);

    // The buffer offset will not be changed dynamically at SetBindGroup().
    bindingGroupLayoutEntries[0].buffer.hasDynamicOffset = false;

    // This uniform is accessible from both vertex and fragment shaders.
    bindingGroupLayoutEntries[0].visibility =
        wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex;

    // ---------------------------------------------------------
    // BindGroupLayout
    //
    // BindGroupLayout is the contract for one resource group.
    //
    // group(0):
    //     binding(0) -> Uniform Buffer
    // ---------------------------------------------------------
    std::vector<wgpu::BindGroupLayout> BGLayouts(1);
    wgpu::BindGroupLayoutDescriptor BGLayoutDescriptor = {};
    BGLayoutDescriptor.entries = bindingGroupLayoutEntries.data();
    BGLayoutDescriptor.entryCount = 1;
    BGLayouts[0] = ctx.device.CreateBindGroupLayout(&BGLayoutDescriptor);

    // ---------------------------------------------------------
    // Pipeline layout
    //
    // PipelineLayout describes which bind group layouts
    // the render pipeline expects.
    //
    // bindGroupLayouts[0] corresponds to WGSL @group(0).
    // ---------------------------------------------------------
    wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
    pipelineLayoutDescriptor.bindGroupLayouts = BGLayouts.data();
    wgpu::PipelineLayout pipelineLayout =
        ctx.device.CreatePipelineLayout(&pipelineLayoutDescriptor);

    // ---------------------------------------------------------
    // Bind group entries
    //
    // BindGroupLayoutEntry described WHAT binding(0) must be.
    //
    // BindGroupEntry provides the ACTUAL resource that will
    // be placed into binding(0).
    // ---------------------------------------------------------
    std::vector<wgpu::BindGroupEntry> bindGroupEntries(1);

    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = uniformBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = sizeof(Uniforms);

    // ---------------------------------------------------------
    // Create the concrete bind group
    //
    // The BindGroup must satisfy the contract described by
    // BGLayouts[0].
    // ---------------------------------------------------------
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

        mat4 proj = mat4::make_perspective(radians(45.0f), 800.0f / 600.0f,
                                           0.1f, 100.0f);
        mat4 view = mat4::make_translation(0.0f, 0.0f, -3.0f);
        mat4 modelX = mat4::make_rotation_x(glfwGetTime());
        mat4 modelY = mat4::make_rotation_y(glfwGetTime() * 0.7f);
        mat4 model = modelX * modelY;

        uniform.modelViewMatrix = view * model;
        uniform.perspectiveMatrix = proj;
        uniform.time = glfwGetTime();

        ctx.queue.WriteBuffer(uniformBuffer, 0, &uniform, sizeof(Uniforms));

        SyncFromWindow(&data);
        if (!IsSameConfig(data.currentConfig, data.targetConfig)) {
            data.surface.Configure(&data.targetConfig);
            data.currentConfig = data.targetConfig;
        }

        DoRender(&data, ctx.device, ctx.queue, renderPipeline,
                 sharedVertexBuffer, positionSize, colorsSize, indexBuffer,
                 bindGroup);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();
}