#include <context.hpp>
#include <iostream>
#include <math.hpp>
#include <random>
#include <vector>

static WebGPUContext ctx;
static WindowData data;

struct VertexAttributes {
    vec4 position;
    vec4 color;
};

// ---------------------------------------------------------
// Particle data
//
// Each particle stores its position and velocity.
//
// The same memory is used in two different ways:
//
// Compute:
//     reads and modifies ParticleData through a Storage Buffer.
//
// Render:
//     reads the same ParticleData as per-instance vertex data.
//
// No CPU readback is needed between Compute and Render.
// ---------------------------------------------------------
struct ParticleData {
    float posX, posY;
    float velX, velY;
};

// Storage buffer bindings require their starting offset
// to satisfy WebGPU's storage-buffer offset alignment.
//
// Particle data is stored after vertex and index data
// inside the same large GPU buffer, so its starting
// offset is rounded up to a 256-byte boundary.
uint32_t align_to_256(uint32_t x) { return (x + 255) & ~255; }

const char* shader = R"(
struct ParticleData {
    posX: f32,
    posY: f32,
    velX: f32,
    velY: f32,
};

struct VertexInput {
    @location(0) position: vec4f,
    @location(1) color: vec4f,

    @location(2) posX: f32,
    @location(3) posY: f32,
    @location(4) velX: f32,
    @location(5) velY: f32,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};


// Particle data is writable by the compute shader.
//
// On the C++ side this region belongs to the same buffer
// that is later bound as an instance vertex buffer.
@group(0) @binding(0) var<storage, read_write> particleData: array<ParticleData>;


// Each DrawIndexed instance represents one particle.
//
// Mesh vertices describe the small quad itself.
// ParticleData provides the per-instance offset.
@vertex
fn main_vs(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    // Move the same quad geometry to the current particle position.
    out.position = vec4f(in.position.x + in.posX, in.position.y + in.posY, 0.f, 1.f);
    out.color = in.color;

    return out;
}

@fragment
fn main_fs(in: VertexOutput) -> @location(0) vec4f {
    return in.color;
}



// ---------------------------------------------------------
// Particle simulation
//
// One invocation updates one particle.
//
// Compute writes new positions and velocities directly
// into the GPU particle buffer. The Render Pass later
// reads these updated values from the same memory.
// ---------------------------------------------------------
@compute @workgroup_size(64)
fn main_cm(@builtin(global_invocation_id) GlobalInvocationID: vec3<u32>) {
    let index = GlobalInvocationID.x;


    // Fixed timestep used only to keep this example simple.
    // A real application would normally provide the actual
    // frame delta time from the CPU.
    let deltaTime = 0.016f;


    // Dispatch is rounded up to a whole number of workgroups,
    // so some invocations may not correspond to real particles.
    if (index >= arrayLength(&particleData)) {
        return;
    }

    // Reflect horizontal velocity when the particle
    // reaches the left or right edge.
    if ( particleData[index].posX < -0.96f || particleData[index].posX > 0.96f) {
        particleData[index].velX *= -1.f;
    }
    particleData[index].posX += particleData[index].velX * deltaTime;


    // Reflect vertical velocity when the particle
    // reaches the top or bottom edge.
    if ( particleData[index].posY < -0.96f || particleData[index].posY > 0.96f) {
        particleData[index].velY *= -1.f;
    }
    particleData[index].posY += particleData[index].velY * deltaTime;
}
)";

std::vector<VertexAttributes> verticesAttributes{
    {{-0.01f, 0.01f, 0.0f, 1.0f}, {0.55f, 0.67f, 0.59f, 1.0f}},
    {{0.01f, 0.01f, 0.0f, 1.0f}, {0.25f, 0.74f, 0.34f, 1.0f}},
    {{0.01f, -0.01f, 0.0f, 1.0f}, {0.28f, 0.63f, 0.67f, 1.0f}},
    {{-0.01f, -0.01f, 0.0f, 1.0f}, {0.30f, 0.60f, 0.10f, 1.0f}}};

std::vector<uint32_t> indicesArray = {0, 1, 2, 0, 2, 3};

const uint32_t instanceCount = 10000;

const uint32_t verticesAttributesSize =
    verticesAttributes.size() * sizeof(VertexAttributes);
const uint32_t indicesArraySize = indicesArray.size() * sizeof(uint32_t);

void DoRender(WindowData* data, const wgpu::Device& device,
              const wgpu::Queue& queue, const wgpu::RenderPipeline& pipeline,
              const wgpu::ComputePipeline& computePipeline,
              const wgpu::BindGroup& bindGroup,
              const wgpu::Buffer& sharedBuffer, uint32_t particlesDataSize,
              uint32_t particleOffset) {
    wgpu::SurfaceTexture surfaceTexture;
    data->surface.GetCurrentTexture(&surfaceTexture);
    wgpu::TextureView surfaceView = surfaceTexture.texture.CreateView();

    wgpu::ComputePassDescriptor computePassDesc = {};

    wgpu::RenderPassDescriptor renderPassDesc = {};

    wgpu::RenderPassColorAttachment colorAttachment = {};
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.view = surfaceView;
    colorAttachment.clearValue = wgpu::Color{0.0, 0.35, 0.4, 1.0f};

    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    // ---------------------------------------------------------
    // Compute -> Render command sequence
    //
    // Both passes are recorded into the SAME CommandEncoder.
    //
    // 1. Compute Pass updates particle positions.
    // 2. Render Pass reads those updated positions.
    //
    // Because both operations are recorded in this order,
    // WebGPU preserves the required dependency between them.
    //
    // No CPU synchronization or readback is needed.
    // ---------------------------------------------------------
    wgpu::CommandEncoder cmdEncoder = ctx.device.CreateCommandEncoder();

    // Update all particles on the GPU.
    wgpu::ComputePassEncoder computePass =
        cmdEncoder.BeginComputePass(&computePassDesc);

    computePass.SetPipeline(computePipeline);
    computePass.SetBindGroup(0, bindGroup);

    // One workgroup contains 64 invocations.
    // Round up so there are enough invocations for every particle.
    // Extra invocations are rejected by the bounds check in WGSL.
    uint32_t workgroupCount = (instanceCount + 63) / 64;
    computePass.DispatchWorkgroups(workgroupCount);
    computePass.End();

    // Compute Pass has finished recording.
    //
    // The following Render Pass uses the particle data
    // that Compute just modified.
    wgpu::RenderPassEncoder renderPass =
        cmdEncoder.BeginRenderPass(&renderPassDesc);

    renderPass.SetPipeline(pipeline);

    // Slot 0:
    // static quad geometry, advanced once per vertex.
    renderPass.SetVertexBuffer(0, sharedBuffer, 0, verticesAttributesSize);

    // Index data for the quad.
    renderPass.SetIndexBuffer(sharedBuffer, wgpu::IndexFormat::Uint32,
                              verticesAttributesSize, indicesArraySize);

    // Slot 1:
    // particle data updated by the Compute Pass.
    //
    // The same region was previously exposed to the
    // compute shader as a Storage Buffer.
    //
    // Because this slot uses VertexStepMode::Instance,
    // one ParticleData element is selected per particle.
    renderPass.SetVertexBuffer(1, sharedBuffer, particleOffset,
                               particlesDataSize);
    renderPass.DrawIndexed(indicesArray.size(), instanceCount);
    renderPass.End();

    wgpu::CommandBuffer cmdBuffer = cmdEncoder.Finish();
    ctx.queue.Submit(1, &cmdBuffer);

    data->surface.Present();
}

std::vector<ParticleData> generateParticles(uint32_t count) {
    std::vector<ParticleData> particles(count);

    std::random_device rd;
    std::mt19937 gen(rd());

    std::uniform_real_distribution<float> posDist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> velDist(-0.01f, 0.01f);

    for (auto& particle : particles) {
        particle.posX = posDist(gen);
        particle.posY = posDist(gen);

        particle.velX = velDist(gen);
        particle.velY = velDist(gen);
    }

    return particles;
}

int main() {
    glfwInit();

    ctx.instance = createInstance();
    ctx.adapter = createAdapter(ctx.instance);
    ctx.device = createDevice(ctx.instance, ctx.adapter);
    ctx.queue = ctx.device.GetQueue();

    data = addWindow(800, 600, "Main Window", ctx.instance, ctx.adapter,
                     ctx.device);

    std::vector<ParticleData> particlesData = generateParticles(instanceCount);

    uint32_t particlesDataSize = particlesData.size() * sizeof(ParticleData);

    // ---------------------------------------------------------
    // Shared GPU buffer layout
    //
    // [ vertex data ]
    // [ index data  ]
    // [ padding     ] <- required for Storage binding alignment
    // [ particles   ]
    //
    // The particle region has two roles:
    //
    // Compute -> Storage Buffer
    // Render  -> Instance Vertex Buffer
    // ---------------------------------------------------------
    uint32_t particleOffset =
        align_to_256(verticesAttributesSize + indicesArraySize);

    wgpu::ShaderModule shaderModule = CreateShaderModule(ctx.device, shader);

    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.mappedAtCreation = false;
    bufferDesc.label = "Shared Buffer";
    bufferDesc.size = particleOffset + particlesDataSize;

    // This single buffer supports several roles:
    //
    // CopyDst:
    //     initial CPU upload.
    //
    // Index:
    //     index data region.
    //
    // Vertex:
    //     quad vertices + particle instance data.
    //
    // Storage:
    //     compute shader can modify particle data.
    bufferDesc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::Index |
                       wgpu::BufferUsage::Vertex | wgpu::BufferUsage::Storage;
    wgpu::Buffer sharedBuffer = ctx.device.CreateBuffer(&bufferDesc);

    ctx.queue.WriteBuffer(sharedBuffer, 0, verticesAttributes.data(),
                          verticesAttributesSize);
    ctx.queue.WriteBuffer(sharedBuffer, verticesAttributesSize,
                          indicesArray.data(), indicesArraySize);
    ctx.queue.WriteBuffer(sharedBuffer, particleOffset, particlesData.data(),
                          particlesDataSize);

    std::vector<wgpu::BindGroupLayoutEntry> BGLayoutEntry(1);

    BGLayoutEntry[0].binding = 0;
    BGLayoutEntry[0].buffer.type = wgpu::BufferBindingType::Storage;
    BGLayoutEntry[0].buffer.minBindingSize = particlesDataSize;
    BGLayoutEntry[0].buffer.hasDynamicOffset = false;
    BGLayoutEntry[0].visibility = wgpu::ShaderStage::Compute;

    wgpu::BindGroupLayoutDescriptor BGLayoutDesc = {};
    BGLayoutDesc.entries = BGLayoutEntry.data();
    BGLayoutDesc.entryCount = BGLayoutEntry.size();
    wgpu::BindGroupLayout BGLayout =
        ctx.device.CreateBindGroupLayout(&BGLayoutDesc);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &BGLayout;
    wgpu::PipelineLayout pipelineLayout =
        ctx.device.CreatePipelineLayout(&pipelineLayoutDesc);

    std::vector<wgpu::BindGroupEntry> bindGroupEntries(1);

    // Expose ONLY the particle region of sharedBuffer
    // to the compute shader.
    //
    // From WGSL's point of view:
    //
    // particleData[0]
    //
    // starts exactly at particleOffset, not at the
    // beginning of the physical GPU buffer.
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = sharedBuffer;
    bindGroupEntries[0].offset = particleOffset;
    bindGroupEntries[0].size = particlesDataSize;

    wgpu::BindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.entries = bindGroupEntries.data();
    bindGroupDesc.entryCount = bindGroupEntries.size();
    bindGroupDesc.layout = BGLayout;
    wgpu::BindGroup bindGroup = ctx.device.CreateBindGroup(&bindGroupDesc);

    wgpu::RenderPipelineDescriptor renderPipelineDesc = {};
    renderPipelineDesc.depthStencil = nullptr;
    renderPipelineDesc.layout = nullptr;

    std::vector<wgpu::VertexBufferLayout> VBLayout(2);

    std::vector<wgpu::VertexAttribute> vertexAttributes(2);

    vertexAttributes[0].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[0].offset = offsetof(VertexAttributes, position);
    vertexAttributes[0].shaderLocation = 0;

    vertexAttributes[1].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[1].offset = offsetof(VertexAttributes, color);
    vertexAttributes[1].shaderLocation = 1;

    VBLayout[0].arrayStride = sizeof(VertexAttributes);
    VBLayout[0].attributeCount = vertexAttributes.size();
    VBLayout[0].attributes = vertexAttributes.data();
    VBLayout[0].stepMode = wgpu::VertexStepMode::Vertex;

    std::vector<wgpu::VertexAttribute> instanceAttributes(4);

    instanceAttributes[0].format = wgpu::VertexFormat::Float32;
    instanceAttributes[0].offset = offsetof(ParticleData, posX);
    instanceAttributes[0].shaderLocation = 2;

    instanceAttributes[1].format = wgpu::VertexFormat::Float32;
    instanceAttributes[1].offset = offsetof(ParticleData, posY);
    instanceAttributes[1].shaderLocation = 3;

    instanceAttributes[2].format = wgpu::VertexFormat::Float32;
    instanceAttributes[2].offset = offsetof(ParticleData, velX);
    instanceAttributes[2].shaderLocation = 4;

    instanceAttributes[3].format = wgpu::VertexFormat::Float32;
    instanceAttributes[3].offset = offsetof(ParticleData, velY);
    instanceAttributes[3].shaderLocation = 5;

    // ---------------------------------------------------------
    // Per-instance particle layout
    //
    // One ParticleData element is consumed for each instance:
    //
    // instance 0 -> particleData[0]
    // instance 1 -> particleData[1]
    // instance 2 -> particleData[2]
    // ...
    //
    // This is the same data that the compute shader modifies.
    // ---------------------------------------------------------
    VBLayout[1].arrayStride = sizeof(ParticleData);
    VBLayout[1].attributeCount = 4;
    VBLayout[1].attributes = instanceAttributes.data();

    // Advance to the next ParticleData once per rendered instance,
    // not once per quad vertex.
    VBLayout[1].stepMode = wgpu::VertexStepMode::Instance;

    renderPipelineDesc.vertex.bufferCount = 2;
    renderPipelineDesc.vertex.buffers = VBLayout.data();
    renderPipelineDesc.vertex.module = shaderModule;
    renderPipelineDesc.vertex.entryPoint = "main_vs";

    renderPipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    renderPipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;
    renderPipelineDesc.primitive.stripIndexFormat =
        wgpu::IndexFormat::Undefined;
    renderPipelineDesc.primitive.topology =
        wgpu::PrimitiveTopology::TriangleList;

    wgpu::FragmentState fragmentState = {};
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

    wgpu::RenderPipeline renderPipeline =
        ctx.device.CreateRenderPipeline(&renderPipelineDesc);

    wgpu::ComputePipelineDescriptor computePipelineDesc = {};
    computePipelineDesc.compute.entryPoint = "main_cm";
    computePipelineDesc.compute.module = shaderModule;
    computePipelineDesc.layout = pipelineLayout;

    wgpu::ComputePipeline computePipeline =
        ctx.device.CreateComputePipeline(&computePipelineDesc);

    while (!glfwWindowShouldClose(data.window)) {
        glfwPollEvents();
        ctx.instance.ProcessEvents();
        SyncFromWindow(&data);

        if (!IsSameConfig(data.currentConfig, data.targetConfig)) {
            data.surface.Configure(&data.targetConfig);
            data.currentConfig = data.targetConfig;
        }

        DoRender(&data, ctx.device, ctx.queue, renderPipeline, computePipeline,
                 bindGroup, sharedBuffer, particlesDataSize, particleOffset);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();
}