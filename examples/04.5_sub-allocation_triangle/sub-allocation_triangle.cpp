#include <context.hpp>
#include <iostream>
#include <vector>

struct VertexAttribute;

static WindowData data;
static WebGPUContext ctx;

const char* shader = R"(struct VertexInput {
    @location(0) position: vec4f,
    @location(1) color: vec4f,
}
;

struct VertexOutput {
    @builtin(position) position : vec4f, @location(1) color : vec4f,
};

@vertex fn main_vs(in : VertexInput) -> VertexOutput {
    var out : VertexOutput;
    out.color = in.color;
    out.position = in.position;
    return out;
}

@fragment fn main_fs(in : VertexOutput) -> @location(0) vec4f {
    return in.color;
}
)";

void DoRender(WindowData* data, const wgpu::Device& device,
              const wgpu::Queue& queue, const wgpu::RenderPipeline& pipeline,
              const wgpu::Buffer& sharedVertexBuffer, uint64_t positionSize,
              uint64_t colorsSize, const wgpu::Buffer& indexBuffer) {
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

    // Bind the first region of sharedVertexBuffer to slot 0.
    //
    // Slot 0 sees only:
    //
    // [position0][position1][position2][position3]
    //
    // and provides its attribute to @location(0).
    renderPass.SetVertexBuffer(0, sharedVertexBuffer, 0, positionSize);

    // Bind the second region of the SAME buffer to slot 1.
    //
    // Slot 1 starts reading at positionSize, so from its point of view:
    //
    // [color0][color1][color2][color3]
    //
    // begins at offset 0.
    //
    // This slot provides its attribute to @location(1).
    renderPass.SetVertexBuffer(1, sharedVertexBuffer, positionSize, colorsSize);

    renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);

    renderPass.DrawIndexed(6);
    renderPass.End();

    wgpu::CommandBuffer cmd = cmdEncoder.Finish();

    queue.Submit(1, &cmd);

    wgpu::Status presentStatus = data->surface.Present();
    if (presentStatus != wgpu::Status::Success) {
        std::cout << "Present status failed" << '\n';
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

    WindowData data = addWindow(800, 600, "Main window", ctx.instance,
                                ctx.adapter, ctx.device);

    // Planar / SoA data.
    // Positions and colors are separate arrays on the CPU.
    std::vector<vec4> positions = {{-0.5, 0.5, 0.0, 1.0},
                                   {0.5, 0.5, 0.0, 1.0},
                                   {-0.5, -0.5, 0.0, 1.0},
                                   {0.5, -0.5, 0.0, 1.0}};

    std::vector<vec4> colors = {{0.55, 0.67, 0.59, 1.0},
                                {0.25, 0.74, 0.34, 1.0},
                                {0.28, 0.63, 0.67, 1.0},
                                {0.3, 0.6, 0.1, 1.0}};

    const uint64_t positionSize = positions.size() * sizeof(vec4);

    const uint64_t colorsSize = colors.size() * sizeof(vec4);

    std::vector<uint32_t> indices = {0, 2, 3, 1, 0, 3};

    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.label = "Vertex Buffer";
    bufferDesc.mappedAtCreation = false;
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;

    // One GPU buffer contains two planar regions:
    //
    // [ all positions ][ all colors ]
    //
    // The regions will later be bound to different vertex buffer slots.
    bufferDesc.size = positionSize + colorsSize;
    wgpu::Buffer sharedVertexBuffer = ctx.device.CreateBuffer(&bufferDesc);

    bufferDesc.label = "Index buffer";
    bufferDesc.mappedAtCreation = false;
    bufferDesc.size = indices.size() * sizeof(uint32_t);

    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index;
    wgpu::Buffer indexBuffer = ctx.device.CreateBuffer(&bufferDesc);

    // Write the positions at the beginning of the shared buffer.
    //
    // Buffer:
    // [positions........][................]
    ctx.queue.WriteBuffer(sharedVertexBuffer, 0, positions.data(),
                          positionSize);

    // Write the colors immediately after the positions.
    //
    // Buffer:
    // [positions........][colors..........]
    //                     ^
    //                     positionSize
    ctx.queue.WriteBuffer(sharedVertexBuffer, positionSize, colors.data(),
                          colorsSize);

    ctx.queue.WriteBuffer(indexBuffer, 0, indices.data(),
                          indices.size() * sizeof(uint32_t));

    wgpu::RenderPipelineDescriptor renderPipelineDesc = {};

    // ---------------------------------------------------------
    // Vertex attributes
    //
    // Each slot contains only one attribute.
    // Both attributes therefore start at offset 0 relative
    // to the beginning of their vertex buffer binding.
    // ---------------------------------------------------------
    std::vector<wgpu::VertexBufferLayout> VBLayouts(2);
    std::vector<wgpu::VertexAttribute> vertexAttributes(2);

    // Slot 0 data will be sent to @location(0).
    vertexAttributes[0].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[0].offset = 0;
    vertexAttributes[0].shaderLocation = 0;

    // Slot 1 data will be sent to @location(1).
    vertexAttributes[1].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[1].offset = 0;
    vertexAttributes[1].shaderLocation = 1;

    // ---------------------------------------------------------
    // Slot 0 layout — positions
    //
    // Reads:
    // [position0][position1][position2][position3]
    // ---------------------------------------------------------
    VBLayouts[0].arrayStride = sizeof(vec4);
    VBLayouts[0].attributeCount = 1;
    VBLayouts[0].attributes = &vertexAttributes[0];
    VBLayouts[0].stepMode = wgpu::VertexStepMode::Vertex;

    // ---------------------------------------------------------
    // Slot 1 layout — colors
    //
    // Reads:
    // [color0][color1][color2][color3]
    // ---------------------------------------------------------
    VBLayouts[1].arrayStride = sizeof(vec4);
    VBLayouts[1].attributeCount = 1;
    VBLayouts[1].attributes = &vertexAttributes[1];
    VBLayouts[1].stepMode = wgpu::VertexStepMode::Vertex;

    // The pipeline expects two vertex buffer slots.
    // They may refer to different buffers or to different regions
    // of the same buffer.
    renderPipelineDesc.vertex.bufferCount = 2;
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

    while (!glfwWindowShouldClose(data.window)) {
        ctx.instance.ProcessEvents();
        glfwPollEvents();

        SyncFromWindow(&data);
        if (!IsSameConfig(data.currentConfig, data.targetConfig)) {
            data.surface.Configure(&data.targetConfig);
            data.currentConfig = data.targetConfig;
        }

        DoRender(&data, ctx.device, ctx.queue, renderPipeline,
                 sharedVertexBuffer, positionSize, colorsSize, indexBuffer);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();
}