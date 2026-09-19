#include <context.hpp>
#include <iostream>
#include <math.hpp>
#include <vector>

struct VertexAttribute {
    vec4 position;
    vec4 color;
};

// CPU-side representation of the data stored in the uniform buffer.
// Its memory layout must be compatible with the Uniform struct in WGSL.
struct Uniforms {
    mat4 modelViewMatrix;
    mat4 perspectiveMatrix;
    float time;

    // Padding keeps the structure size/alignment compatible with
    // the WGSL uniform layout. Need 16 bytes allignment
    float _pad[3];
};

static WindowData data;
static WebGPUContext ctx;
static wgpu::Buffer sharedVertexBuffer;
static wgpu::Buffer indexBuffer;

// 8 вершин куба (от -0.5 до 0.5 по всем осям)
std::vector<vec4> positions = {// Передняя грань (Z = 0.5)
                               {-0.5, -0.5, 0.5, 1.0},
                               {0.5, -0.5, 0.5, 1.0},
                               {0.5, 0.5, 0.5, 1.0},
                               {-0.5, 0.5, 0.5, 1.0},
                               // Задняя грань (Z = -0.5)
                               {-0.5, -0.5, -0.5, 1.0},
                               {0.5, -0.5, -0.5, 1.0},
                               {0.5, 0.5, -0.5, 1.0},
                               {-0.5, 0.5, -0.5, 1.0}};

// 8 случайных цветов для каждой вершины
std::vector<vec4> colors = {{1.0, 0.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0},
                            {0.0, 0.0, 1.0, 1.0}, {1.0, 1.0, 0.0, 1.0},
                            {1.0, 0.1, 0.5, 1.0}, {0.1, 1.0, 0.5, 1.0},
                            {0.5, 0.1, 1.0, 1.0}, {0.0, 1.0, 1.0, 1.0}};

// 36 индексов (6 граней * 2 треугольника * 3 вершины)
std::vector<uint32_t> indices = {
    0, 1, 2, 2, 3, 0,  // Передняя
    1, 5, 6, 6, 2, 1,  // Правая
    5, 4, 7, 7, 6, 5,  // Задняя
    4, 0, 3, 3, 7, 4,  // Левая
    3, 2, 6, 6, 7, 3,  // Верхняя
    4, 5, 1, 1, 0, 4   // Нижняя
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

    // Bind resource group 0 for the following draw call.
    // The shader accesses its uniform buffer through
    // @group(0) @binding(0).
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
                                          wgpu::ShaderModule shaderModule) {
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
    wgpu::RenderPipeline renderPipeline =
        createRenderPipeline(ctx.device, shaderModule);

    // ---------------------------------------------------------
    // Uniform buffer setup
    //
    // CPU Uniforms
    //      ↓ WriteBuffer
    // GPU Uniform Buffer
    //      ↓ Bind Group
    // Shader @group(0) @binding(0)
    // ---------------------------------------------------------
    Uniforms uniform;

    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.mappedAtCreation = false;
    bufferDesc.label = "Uniform buffer";

    // Uniform: the shader may read this buffer as a uniform resource.
    // CopyDst: Queue::WriteBuffer can update it from CPU memory.
    bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    bufferDesc.size = sizeof(Uniforms);
    wgpu::Buffer uniformBuffer = ctx.device.CreateBuffer(&bufferDesc);

    std::vector<wgpu::BindGroupEntry> bgEntries(1);
    // This entry provides the actual resource for @binding(0).
    bgEntries[0].binding = 0;

    // The resource referenced by binding 0 is our uniform buffer.
    bgEntries[0].buffer = uniformBuffer;

    // Start reading from the beginning of the buffer.
    bgEntries[0].offset = 0;

    // Expose the Uniforms region of the buffer to the shader.
    bgEntries[0].size = sizeof(Uniforms);

    wgpu::BindGroupDescriptor bgDesc = {};

    // The pipeline layout was inferred from the shader.
    // Get the layout expected for @group(0).
    bgDesc.layout = renderPipeline.GetBindGroupLayout(0);
    bgDesc.entryCount = bgEntries.size();
    bgDesc.entries = bgEntries.data();

    // Create the concrete resource set that satisfies group 0's layout
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

        // Update the contents of the existing GPU uniform buffer.
        // The BindGroup does not need to be recreated because it still
        // references the same uniformBuffer object.
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