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
              const wgpu::Buffer& vertexBuffer,
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
    // Bind the vertex buffer to vertex buffer slot 0.
    renderPass.SetVertexBuffer(0, vertexBuffer);

    // Bind the index buffer.
    // Uint32 tells WebGPU how each index inside the buffer must be interpreted.
    renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);

    // Read 6 indices from the index buffer.
    // Each index selects a vertex from the currently bound vertex buffer.
    // With TriangleList, every 3 indices form one triangle.
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

    std::vector<VertexAttribute> vertices = {
        {{-0.5, 0.5, 0.0, 1.0}, {0.55, 0.67, 0.59, 1.0}},
        {{0.5, 0.5, 0.0, 1.0}, {0.25, 0.74, 0.34, 1.0}},
        {{-0.5, -0.5, 0.0, 1.0}, {0.28, 0.63, 0.67, 1.0}},
        {{0.5, -0.5, 0.0, 1.0}, {0.3, 0.6, 0.1, 1.0}}};

    // Indices describe which vertices are used to build each triangle.
    // Triangle 1: vertices 0, 2, 3
    // Triangle 2: vertices 1, 0, 3
    std::vector<uint32_t> indices = {0, 2, 3, 1, 0, 3};

    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.label = "Vertex Buffer";
    bufferDesc.mappedAtCreation = false;
    bufferDesc.size = vertices.size() * sizeof(VertexAttribute);

    // Vertex: this buffer can be used as a vertex buffer.
    // CopyDst: Queue::WriteBuffer can copy CPU data into it.
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Vertex;
    wgpu::Buffer vertexBuffer = ctx.device.CreateBuffer(&bufferDesc);

    bufferDesc.label = "Index buffer";
    bufferDesc.mappedAtCreation = false;
    bufferDesc.size = indices.size() * sizeof(uint32_t);

    // Index: this buffer contains indices used by indexed draw calls.
    // CopyDst: CPU index data will be uploaded with Queue::WriteBuffer.
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index;
    wgpu::Buffer indexBuffer = ctx.device.CreateBuffer(&bufferDesc);

    // Upload vertex data from CPU memory to the vertex buffer.
    ctx.queue.WriteBuffer(vertexBuffer, 0, vertices.data(),
                          vertices.size() * sizeof(VertexAttribute));

    // Upload index data from CPU memory to the index buffer.
    ctx.queue.WriteBuffer(indexBuffer, 0, indices.data(),
                          indices.size() * sizeof(uint32_t));

    // ---------------------------------------------------------
    // Render pipeline setup
    // ---------------------------------------------------------

    wgpu::RenderPipelineDescriptor renderPipelineDesc = {};

    // ---------------------------------------------------------
    // Vertices Setup
    // ---------------------------------------------------------

    wgpu::VertexBufferLayout VBLayout = {};

    std::vector<wgpu::VertexAttribute> vertexAttributes(2);

    vertexAttributes[0].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[0].offset = offsetof(VertexAttribute, position);
    vertexAttributes[0].shaderLocation = 0;

    vertexAttributes[1].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[1].offset = offsetof(VertexAttribute, color);
    vertexAttributes[1].shaderLocation = 1;

    VBLayout.arrayStride = sizeof(VertexAttribute);
    VBLayout.attributeCount = 2;
    VBLayout.attributes = vertexAttributes.data();
    VBLayout.stepMode = wgpu::VertexStepMode::Vertex;

    renderPipelineDesc.vertex.bufferCount = 1;
    renderPipelineDesc.vertex.buffers = &VBLayout;
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

        DoRender(&data, ctx.device, ctx.queue, renderPipeline, vertexBuffer,
                 indexBuffer);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();
}