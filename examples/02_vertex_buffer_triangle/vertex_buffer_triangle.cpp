/* All code from previous examples is contained in context.hpp */

#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

#include <context.hpp>
#include <iostream>
#include <vector>

static WebGPUContext ctx;
static WindowData data;

struct vec3 {
    union {
        struct {
            float x, y, z;
        };
        float data[3];
    };
};

struct vec4 {
    union {
        struct {
            float x, y, z, w;
        };
        float data[4];
    };
};

unsigned int round_up_16(unsigned int n) { return (n + 15) & ~15; }

const char* shader = R"(

struct VertexInput {
    @location(0) position: vec4<f32>,
    @location(1) color: vec4<f32>,
};

struct VertexOutput {
    @builtin(position) position : vec4f,
    @location(1) color : vec4f,
};

@vertex
fn main_vs(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    out.position = in.position;
    out.color = in.color;

    return out;
}

@fragment
fn main_fs(in: VertexOutput) -> @location(0) vec4<f32> {
    return in.color;
}

)";

struct VertexAttributes {
    vec4 position;
    vec4 color;
};

void DoRender(WindowData* data, wgpu::Device device, wgpu::Queue queue,
              wgpu::RenderPipeline pipeline, wgpu::Buffer vertexBuffer) {
    wgpu::SurfaceTexture surfaceTexture;
    data->surface.GetCurrentTexture(&surfaceTexture);

    wgpu::TextureView surfaceView = surfaceTexture.texture.CreateView();

    wgpu::CommandEncoder commandEncoder = device.CreateCommandEncoder();

    wgpu::RenderPassDescriptor desc = {};
    wgpu::RenderPassColorAttachment colorAttachment = {};

    colorAttachment.view = surfaceView;
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.clearValue = wgpu::Color{0.0, 0.0, 0.0, 1.0};

    desc.colorAttachments = &colorAttachment;
    desc.colorAttachmentCount = 1;

    wgpu::RenderPassEncoder renderPass = commandEncoder.BeginRenderPass(&desc);

    renderPass.SetPipeline(pipeline);

    // Bind vertexBuffer to vertex buffer slot 0.
    // This slot corresponds to pipelineDesc.vertex.buffers[0].
    renderPass.SetVertexBuffer(0, vertexBuffer);

    // Three vertices are fetched from the vertex buffer and processed
    // by three vertex shader invocations.
    renderPass.Draw(3);

    renderPass.End();

    wgpu::CommandBuffer cmd = commandEncoder.Finish();
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

    // Interleaved vertex data:
    // [position][color][position][color][position][color]
    std::vector<VertexAttributes> model = {
        {{1.0, 0.0, 0.0, 1.0}, {1.0, 0.0, 0.0, 1.0}},
        {{0.0, 1.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0}},
        {{0.0, 0.0, 1.0, 1.0}, {0.0, 0.0, 1.0, 1.0}}};

    uint64_t modelSize = model.size() * sizeof(VertexAttributes);

    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.label = "Vertex buffer";
    bufferDesc.mappedAtCreation = false;
    bufferDesc.size = round_up_16(modelSize);

    // Vertex: the buffer can be used as a vertex buffer.
    // CopyDst: Queue::WriteBuffer writes data into this buffer.
    bufferDesc.usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;

    wgpu::Buffer vertexBuffer = ctx.device.CreateBuffer(&bufferDesc);

    // Copy vertex data from CPU memory into the GPU buffer.
    ctx.queue.WriteBuffer(vertexBuffer, 0, model.data(), modelSize);

    data = addWindow(800, 600, "Main window", ctx.instance, ctx.adapter,
                     ctx.device);

    // ---------------------------------------------------------
    // Render pipeline setup
    // ---------------------------------------------------------

    wgpu::RenderPipelineDescriptor pipelineDesc = {};

    // ---------------------------------------------------------
    // Vertex input layout
    //
    // Describes how raw bytes from the vertex buffer are
    // interpreted and mapped to vertex shader @location inputs.
    // ---------------------------------------------------------

    wgpu::VertexBufferLayout VBLayout = {};

    // This vertex contains two attributes:
    //   @location(0) -> position
    //   @location(1) -> color
    std::vector<wgpu::VertexAttribute> vertexAttributes(2);

    // Position starts at the 'position' field inside VertexAttributes.
    vertexAttributes[0].shaderLocation = 0;
    vertexAttributes[0].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[0].offset = offsetof(VertexAttributes, position);

    // Color starts at the 'color' field inside VertexAttributes.
    vertexAttributes[1].shaderLocation = 1;
    vertexAttributes[1].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[1].offset = offsetof(VertexAttributes, color);

    // Number of attributes described by this vertex buffer layout.
    VBLayout.attributeCount = 2;
    VBLayout.attributes = vertexAttributes.data();

    // Distance in bytes from the beginning of one vertex
    // to the beginning of the next vertex.
    VBLayout.arrayStride = sizeof(VertexAttributes);

    // Advance to the next vertex after each vertex shader invocation.
    VBLayout.stepMode = wgpu::VertexStepMode::Vertex;

    // ---------------------------------------------------------
    // Vertex stage
    // ---------------------------------------------------------

    // One vertex buffer slot is expected by this pipeline.
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &VBLayout;

    pipelineDesc.vertex.entryPoint = "main_vs";
    pipelineDesc.vertex.module = shaderModule;

    // ---------------------------------------------------------
    // Primitive assembly / rasterization
    // ---------------------------------------------------------

    pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;

    // No strip topology is used, so no strip index format is required.
    pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;

    // Every group of three vertices forms one independent triangle.
    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;

    // ---------------------------------------------------------
    // Fragment stage
    // ---------------------------------------------------------

    wgpu::FragmentState fragmentState = {};

    fragmentState.entryPoint = "main_fs";
    fragmentState.module = shaderModule;

    // This example uses one fragment shader color output:
    // @location(0).
    fragmentState.targetCount = 1;

    wgpu::ColorTargetState colorTargetState = {};

    // The pipeline expects color attachment 0 to use the same
    // format as the surface texture.
    colorTargetState.format = data.currentConfig.format;

    fragmentState.targets = &colorTargetState;
    pipelineDesc.fragment = &fragmentState;

    // ---------------------------------------------------------
    // Create the final render pipeline
    //
    // The pipeline now knows:
    // - how vertex buffer bytes are interpreted;
    // - which shaders are executed;
    // - how vertices form primitives;
    // - what type of color target the fragment stage writes to.
    // ---------------------------------------------------------

    wgpu::RenderPipeline renderPipeline =
        ctx.device.CreateRenderPipeline(&pipelineDesc);

    while (!glfwWindowShouldClose(data.window)) {
        ctx.instance.ProcessEvents();
        glfwPollEvents();

        SyncFromWindow(&data);

        if (!IsSameConfig(data.currentConfig, data.targetConfig)) {
            data.surface.Configure(&data.targetConfig);
            data.currentConfig = data.targetConfig;
        }

        DoRender(&data, ctx.device, ctx.queue, renderPipeline, vertexBuffer);
    }
}