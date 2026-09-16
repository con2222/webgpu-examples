#include <context.hpp>
#include <iostream>
#include <math.hpp>
#include <random>
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

// ---------------------------------------------------------
// Per-instance data
//
// This data is different for every cube instance.
//
// position - where this particular cube is placed
// color    - color multiplier for this particular cube
//
// Unlike normal vertex data, this data advances once per
// INSTANCE, not once per vertex.
// ---------------------------------------------------------
struct InstanceData {
    vec4 position;
    vec4 color;
};

std::vector<InstanceData> instances;
uint32_t instance_count = 100000;

static WindowData data;
static WebGPUContext ctx;
static wgpu::Buffer sharedVertexBuffer;
static wgpu::Buffer indexBuffer;

std::vector<vec4> positions = {
    // -------------------------------------------------
    // Front (+Z)
    // -------------------------------------------------
    {-0.5f, -0.5f, 0.5f, 1.0f},  // 0
    {0.5f, -0.5f, 0.5f, 1.0f},   // 1
    {0.5f, 0.5f, 0.5f, 1.0f},    // 2
    {-0.5f, 0.5f, 0.5f, 1.0f},   // 3

    // -------------------------------------------------
    // Right (+X)
    // -------------------------------------------------
    {0.5f, -0.5f, 0.5f, 1.0f},   // 4
    {0.5f, -0.5f, -0.5f, 1.0f},  // 5
    {0.5f, 0.5f, -0.5f, 1.0f},   // 6
    {0.5f, 0.5f, 0.5f, 1.0f},    // 7

    // -------------------------------------------------
    // Back (-Z)
    // -------------------------------------------------
    {-0.5f, -0.5f, -0.5f, 1.0f},  // 8
    {-0.5f, 0.5f, -0.5f, 1.0f},   // 9
    {0.5f, 0.5f, -0.5f, 1.0f},    // 10
    {0.5f, -0.5f, -0.5f, 1.0f},   // 11

    // -------------------------------------------------
    // Left (-X)
    // -------------------------------------------------
    {-0.5f, -0.5f, -0.5f, 1.0f},  // 12
    {-0.5f, -0.5f, 0.5f, 1.0f},   // 13
    {-0.5f, 0.5f, 0.5f, 1.0f},    // 14
    {-0.5f, 0.5f, -0.5f, 1.0f},   // 15

    // -------------------------------------------------
    // Top (+Y)
    // -------------------------------------------------
    {-0.5f, 0.5f, 0.5f, 1.0f},   // 16
    {0.5f, 0.5f, 0.5f, 1.0f},    // 17
    {0.5f, 0.5f, -0.5f, 1.0f},   // 18
    {-0.5f, 0.5f, -0.5f, 1.0f},  // 19

    // -------------------------------------------------
    // Bottom (-Y)
    // -------------------------------------------------
    {-0.5f, -0.5f, -0.5f, 1.0f},  // 20
    {0.5f, -0.5f, -0.5f, 1.0f},   // 21
    {0.5f, -0.5f, 0.5f, 1.0f},    // 22
    {-0.5f, -0.5f, 0.5f, 1.0f},   // 23
};

std::vector<vec4> colors = {
    {1.0, 0.0, 0.0, 1.0}, {1.0, 0.0, 0.0, 1.0}, {1.0, 0.0, 0.0, 1.0},
    {1.0, 0.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0},
    {0.0, 1.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}, {0.0, 0.0, 1.0, 1.0},
    {0.0, 0.0, 1.0, 1.0}, {0.0, 0.0, 1.0, 1.0}, {0.0, 0.0, 1.0, 1.0},
    {1.0, 1.0, 0.0, 1.0}, {1.0, 1.0, 0.0, 1.0}, {1.0, 1.0, 0.0, 1.0},
    {1.0, 1.0, 0.0, 1.0}, {1.0, 0.0, 1.0, 1.0}, {1.0, 0.0, 1.0, 1.0},
    {1.0, 0.0, 1.0, 1.0}, {1.0, 0.0, 1.0, 1.0}, {0.0, 1.0, 1.0, 1.0},
    {0.0, 1.0, 1.0, 1.0}, {0.0, 1.0, 1.0, 1.0}, {0.0, 1.0, 1.0, 1.0}};

std::vector<uint32_t> indices = {
    0,  1,  2,  2,  3,  0,   // Front
    4,  5,  6,  6,  7,  4,   // Right
    8,  9,  10, 10, 11, 8,   // Back
    12, 13, 14, 14, 15, 12,  // Left
    16, 17, 18, 18, 19, 16,  // Top
    20, 21, 22, 22, 23, 20   // Bottom
};

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
    // Per-vertex data.
    // Changes for every vertex of the cube.
    @location(0) position: vec4f,
    @location(1) color: vec4f,

    // Per-instance data.
    // Remains the same for all vertices of one cube,
    // then changes when the GPU starts the next instance.
    @location(2) instance_pos: vec4f,
    @location(3) instance_color: vec4f,

    // Built-in index of the current instance.
    //
    // For DrawIndexed(36, 100000):
    // first cube  -> instance_index = 0
    // second cube -> instance_index = 1
    // ...
    //
    // This value is generated automatically by the GPU.
    // It does NOT come from a vertex buffer.
    //
    // Useful for procedural animation, indexing arrays,
    // generating colors/positions, etc.
    @builtin(instance_index) instance_index: u32,
};

struct VertexOutput {
    @builtin(position) position : vec4f, 
    @location(1) color : vec4f,
};

@vertex fn main_vs(in : VertexInput) -> VertexOutput {
    var out : VertexOutput;

    // Combine the cube's vertex color with the color
    // assigned to this particular instance.
    out.color = in.color * in.instance_color;

    // Cube geometry is centered around the origin.
    // Adding instance_pos moves the same cube geometry
    // to a different location for every instance.
    var final_pos = in.position + in.instance_pos; // по сути позиция стандартная для куба + позиция самого инстанса и также с цветом
    final_pos.w = 1.0;

    out.position = uniforms.perspectiveMatrix * uniforms.modelViewMatrix * final_pos;
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
              const wgpu::Buffer& instanceBuffer, wgpu::BindGroup bindGroup) {
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

    renderPass.SetBindGroup(0, bindGroup, 0, nullptr);

    // Slot 0: per-vertex cube positions.
    renderPass.SetVertexBuffer(0, sharedVertexBuffer, 0, positionSize);

    // Slot 1: per-vertex cube colors.
    renderPass.SetVertexBuffer(1, sharedVertexBuffer, positionSize, colorsSize);

    // Slot 2: per-instance position + color.
    renderPass.SetVertexBuffer(2, instanceBuffer);

    // Same index buffer is reused for every instance.
    renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);

    // Render 36 indices per cube × 10,000 instances.
    //
    // IMPORTANT:
    // This is still ONE DrawIndexed call.
    renderPass.DrawIndexed(36, instance_count);
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
                                          wgpu::ShaderModule shaderModule) {
    wgpu::RenderPipelineDescriptor renderPipelineDesc = {};

    std::vector<wgpu::VertexBufferLayout> VBLayouts(3);
    std::vector<wgpu::VertexAttribute> vertexAttributes(3);

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

    // ---------------------------------------------------------
    // Instance vertex attributes
    //
    // InstanceData memory layout:
    //
    // [ position: vec4 ][ color: vec4 ]
    //
    // shader location 2 -> InstanceData::position
    // shader location 3 -> InstanceData::color
    // ---------------------------------------------------------
    std::vector<wgpu::VertexAttribute> instanceVertexAttributes(2);
    instanceVertexAttributes[0].format = wgpu::VertexFormat::Float32x4;
    instanceVertexAttributes[0].offset = 0;
    instanceVertexAttributes[0].shaderLocation = 2;

    instanceVertexAttributes[1].format = wgpu::VertexFormat::Float32x4;
    instanceVertexAttributes[1].offset = offsetof(InstanceData, color);
    instanceVertexAttributes[1].shaderLocation = 3;

    // One element in this buffer = one InstanceData structure.
    VBLayouts[2].arrayStride = sizeof(InstanceData);
    VBLayouts[2].attributeCount = 2;

    // Instance means:
    // advance to the next InstanceData only when the GPU
    // starts rendering the next cube instance.
    //
    // Vertex would advance after every vertex instead.
    VBLayouts[2].stepMode = wgpu::VertexStepMode::Instance;
    VBLayouts[2].attributes = instanceVertexAttributes.data();

    renderPipelineDesc.vertex.bufferCount = 3;
    renderPipelineDesc.vertex.buffers = VBLayouts.data();
    renderPipelineDesc.vertex.entryPoint = "main_vs";
    renderPipelineDesc.vertex.module = shaderModule;

    renderPipelineDesc.primitive.cullMode = wgpu::CullMode::Back;
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

    wgpu::RenderPipeline renderPipeline =
        ctx.device.CreateRenderPipeline(&renderPipelineDesc);

    return renderPipeline;
}

// ---------------------------------------------------------
// Generate per-instance data
//
// Geometry itself is NOT duplicated.
// We only generate position and color for every cube.
// ---------------------------------------------------------
void generateInstances(int count) {
    instances.reserve(count);

    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_real_distribution<float> posDist(-20.0f, 20.0f);

    std::uniform_real_distribution<float> colorDist(0.0f, 1.0f);

    for (int i = 0; i < count; ++i) {
        InstanceData instance;

        instance.position = {posDist(gen), posDist(gen), posDist(gen), 1.0f};

        instance.color = {colorDist(gen), colorDist(gen), colorDist(gen), 1.0f};

        instances.push_back(instance);
    }
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

    generateInstances(instance_count);

    // ---------------------------------------------------------
    // Instance buffer
    //
    // Contains 10,000 InstanceData structures.
    //
    // Vertex usage is required because the pipeline reads this
    // buffer as vertex/instance input.
    //
    // CopyDst allows Queue::WriteBuffer to upload CPU data.
    // ---------------------------------------------------------
    wgpu::BufferDescriptor instanceBufferDesc = {};
    instanceBufferDesc.label = "Instance buffer";
    instanceBufferDesc.mappedAtCreation = false;
    instanceBufferDesc.size = instances.size() * sizeof(InstanceData);
    instanceBufferDesc.usage =
        wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;
    wgpu::Buffer instanceBuffer = ctx.device.CreateBuffer(&instanceBufferDesc);
    ctx.queue.WriteBuffer(instanceBuffer, 0, instances.data(),
                          sizeof(InstanceData) * instance_count);

    wgpu::RenderPipeline renderPipeline =
        createRenderPipeline(ctx.device, shaderModule);

    Uniforms uniform;

    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.mappedAtCreation = false;
    bufferDesc.label = "Uniform buffer";

    bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    bufferDesc.size = sizeof(Uniforms);
    wgpu::Buffer uniformBuffer = ctx.device.CreateBuffer(&bufferDesc);

    std::vector<wgpu::BindGroupEntry> bgEntries(1);
    bgEntries[0].binding = 0;
    bgEntries[0].buffer = uniformBuffer;
    bgEntries[0].offset = 0;
    bgEntries[0].size = sizeof(Uniforms);

    wgpu::BindGroupDescriptor bgDesc = {};

    bgDesc.layout = renderPipeline.GetBindGroupLayout(0);
    bgDesc.entryCount = bgEntries.size();
    bgDesc.entries = bgEntries.data();

    wgpu::BindGroup bindGroup = ctx.device.CreateBindGroup(&bgDesc);

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
                 instanceBuffer, bindGroup);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();
}