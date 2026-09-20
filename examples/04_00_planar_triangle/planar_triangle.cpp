#include <context.hpp>
#include <iostream>
#include <math.hpp>
#include <vector>

struct VertexAttributes {
    vec4 position;
    vec4 color;
};

static WindowData data;
static WebGPUContext ctx;

const char* shader = R"(
struct VertexInput {
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
              const wgpu::Buffer& vertexPositionsBuffer,
              const wgpu::Buffer& vertexColorsBuffer,
              const wgpu::Buffer& indexBuffer) {
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

    // Slot 0 contains vertex positions.
    // This slot uses VBLayouts[0] and provides data for @location(0).
    renderPass.SetVertexBuffer(0, vertexPositionsBuffer);

    // Slot 1 contains vertex colors.
    // This slot uses VBLayouts[1] and provides data for @location(1).
    renderPass.SetVertexBuffer(1, vertexColorsBuffer);

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

    // Planar / SoA layout:
    // positions and colors are stored in separate CPU arrays.
    std::vector<vec4> positions = {{-0.5, 0.5, 0.0, 1.0},
                                   {0.5, 0.5, 0.0, 1.0},
                                   {-0.5, -0.5, 0.0, 1.0},
                                   {0.5, -0.5, 0.0, 1.0}};

    std::vector<vec4> colors = {{0.55, 0.67, 0.59, 1.0},
                                {0.25, 0.74, 0.34, 1.0},
                                {0.28, 0.63, 0.67, 1.0},
                                {0.3, 0.6, 0.1, 1.0}};

    std::vector<uint32_t> indices = {0, 2, 3, 1, 0, 3};

    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.label = "Positions Vertex Buffer";
    bufferDesc.mappedAtCreation = false;
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;
    bufferDesc.size = positions.size() * sizeof(vec4);

    // This buffer contains only positions:
    // [position][position][position][position]
    wgpu::Buffer vertexPositionsBuffer = ctx.device.CreateBuffer(&bufferDesc);

    // Reuse the descriptor to create a second vertex buffer.
    // This buffer contains only colors:
    // [color][color][color][color]
    bufferDesc.label = "Colors Vertex Buffer";
    bufferDesc.size = colors.size() * sizeof(vec4);
    wgpu::Buffer vertexColorsBuffer = ctx.device.CreateBuffer(&bufferDesc);

    bufferDesc.label = "Index buffer";
    bufferDesc.mappedAtCreation = false;
    bufferDesc.size = indices.size() * sizeof(uint32_t);

    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index;
    wgpu::Buffer indexBuffer = ctx.device.CreateBuffer(&bufferDesc);

    // Upload positions to vertex buffer slot 0's data source.
    ctx.queue.WriteBuffer(vertexPositionsBuffer, 0, positions.data(),
                          positions.size() * sizeof(vec4));

    // Upload colors to vertex buffer slot 1's data source.
    ctx.queue.WriteBuffer(vertexColorsBuffer, 0, colors.data(),
                          colors.size() * sizeof(vec4));

    ctx.queue.WriteBuffer(indexBuffer, 0, indices.data(),
                          indices.size() * sizeof(uint32_t));

    wgpu::RenderPipelineDescriptor renderPipelineDesc = {};

    // ---------------------------------------------------------
    // Vertex input layouts
    //
    // Unlike the interleaved version, positions and colors are
    // stored in two different vertex buffers.
    // Therefore the pipeline uses two vertex buffer layouts.
    // ---------------------------------------------------------
    std::vector<wgpu::VertexBufferLayout> VBLayouts(2);
    std::vector<wgpu::VertexAttribute> vertexAttributes(2);

    // Position attribute:
    // read one vec4 from slot 0 and send it to shader @location(0).
    vertexAttributes[0].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[0].offset = 0;
    vertexAttributes[0].shaderLocation = 0;

    // Color attribute:
    // read one vec4 from slot 1 and send it to shader @location(1).
    vertexAttributes[1].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[1].offset = 0;
    vertexAttributes[1].shaderLocation = 1;

    // ---------------------------------------------------------
    // Slot 0 layout — positions
    //
    // Buffer memory:
    // [position0][position1][position2][position3]
    //
    // Each element contains only one vec4, so:
    // stride = sizeof(vec4)
    // offset = 0
    // ---------------------------------------------------------
    VBLayouts[0].arrayStride = sizeof(vec4);
    VBLayouts[0].attributeCount = 1;
    VBLayouts[0].attributes = &vertexAttributes[0];
    VBLayouts[0].stepMode = wgpu::VertexStepMode::Vertex;

    // ---------------------------------------------------------
    // Slot 1 layout — colors
    //
    // Buffer memory:
    // [color0][color1][color2][color3]
    //
    // Colors are stored in their own buffer, so this layout
    // also advances by one vec4 per vertex.
    // ---------------------------------------------------------
    VBLayouts[1].arrayStride = sizeof(vec4);
    VBLayouts[1].attributeCount = 1;
    VBLayouts[1].attributes = &vertexAttributes[1];
    VBLayouts[1].stepMode = wgpu::VertexStepMode::Vertex;

    // The pipeline expects two vertex buffer slots:
    //
    // slot 0 -> positions -> @location(0)
    // slot 1 -> colors    -> @location(1)
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
                 vertexPositionsBuffer, vertexColorsBuffer, indexBuffer);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();
}