#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <context.hpp>
#include <iostream>
#include <math.hpp>
#include <vector>

#define IMAGE_NAME "utility/kiana.jpg"

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

const uint32_t UNIFORM_ALIGNMENT = 256;

// ---------------------------------------------------------
// Textured cube geometry
//
// A cube has only 8 unique geometric corners, but a textured
// cube usually needs 24 vertices: 4 independent vertices per face.
//
// The same 3D corner may need different UV coordinates on
// different faces, so its position has to be duplicated.
// ---------------------------------------------------------
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

// ---------------------------------------------------------
// Texture coordinates
//
// Each face maps the complete texture from (0, 0) to (1, 1).
// Because every face owns its own four vertices, each face can
// have independent UV coordinates.
// ---------------------------------------------------------
std::vector<vec2> uvs = {
    // Front
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 1.0f},

    // Right
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 1.0f},

    // Back
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 1.0f},

    // Left
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 1.0f},

    // Top
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 1.0f},

    // Bottom
    {0.0f, 0.0f},
    {1.0f, 0.0f},
    {1.0f, 1.0f},
    {0.0f, 1.0f},
};

std::vector<vec4> colors = {{1.0, 0.0, 0.0, 1.0}, {0.0, 1.0, 0.0, 1.0},
                            {0.0, 0.0, 1.0, 1.0}, {1.0, 1.0, 0.0, 1.0},
                            {1.0, 0.1, 0.5, 1.0}, {0.1, 1.0, 0.5, 1.0},
                            {0.5, 0.1, 1.0, 1.0}, {0.0, 1.0, 1.0, 1.0}};

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
const uint64_t uvSize = uvs.size() * sizeof(vec2);

const char* shader = R"(
struct Uniform {
    modelViewMatrix: mat4x4f,
    perspectiveMatrix: mat4x4f,
    time: f32,
};

// Uniform data shared by the draw calls.
@group(0) @binding(0) var<uniform> uniforms: Uniform;

// TextureView is exposed to the shader through this binding.
// It provides access to the texture texels.
@group(0) @binding(1) var imageTexture: texture_2d<f32>;

// Sampler describes HOW the texture should be sampled:
// filtering, addressing, mipmap filtering, etc.
@group(0) @binding(2) var imageSampler: sampler;

struct VertexInput {
    @location(0) position: vec4f,
    @location(1) color: vec4f,
    @location(2) uv : vec2f,
};

struct VertexOutput {
    @builtin(position) position : vec4f, 
    @location(1) color : vec4f,
    @location(2) uv : vec2f,
};

@vertex fn main_vs(in : VertexInput) -> VertexOutput {
    var out : VertexOutput;
    out.color = in.color;
    out.position = uniforms.perspectiveMatrix * uniforms.modelViewMatrix * in.position;
    out.uv = in.uv;
    return out;
}

@fragment fn main_fs(in : VertexOutput) -> @location(0) vec4f {
    var time: f32 = uniforms.time;
    
    // Sample the texture at the interpolated UV coordinate.
    // Texture provides the data, Sampler provides the sampling rules.
    var color = textureSample(imageTexture, imageSampler, in.uv);
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

    wgpu::RenderPassColorAttachment colorAttachment = {};

    colorAttachment.view = surfaceView;

    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.clearValue = wgpu::Color{0.0, 0.35, 0.4, 1.0f};

    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    wgpu::RenderPassDepthStencilAttachment depthAttachment = {};

    int width, height;
    glfwGetWindowSize(data->window, &width, &height);

    mat4 proj = mat4::make_perspective(
        radians(45.0f), static_cast<float>(width) / static_cast<float>(height),
        0.1f, 100.0f);
    mat4 modelX = mat4::make_rotation_x(glfwGetTime());
    mat4 modelY = mat4::make_rotation_y(glfwGetTime() * 0.7f);
    mat4 model = modelX * modelY;

    mat4 view1 = mat4::make_translation(0.0f, 0.0f, -2.5f);
    uniform.modelViewMatrix = view1 * model;
    uniform.perspectiveMatrix = proj;
    uniform.time = glfwGetTime();
    queue.WriteBuffer(uniformBuffer, 0, &uniform, sizeof(Uniforms));

    wgpu::RenderPassEncoder renderPass =
        cmdEncoder.BeginRenderPass(&renderPassDesc);
    renderPass.SetPipeline(pipeline);
    renderPass.SetVertexBuffer(0, sharedVertexBuffer, 0, positionSize);
    renderPass.SetVertexBuffer(1, sharedVertexBuffer, positionSize, colorsSize);

    // Bind the UV region of the shared vertex buffer to slot 2.
    // The pipeline maps this slot to shader @location(2).
    renderPass.SetVertexBuffer(2, sharedVertexBuffer, positionSize + colorsSize,
                               uvSize);
    renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);

    uint32_t dynamicOffset = 0;
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

    // Shared vertex buffer memory layout:
    //
    // [ positions ][ colors ][ UVs ]
    //
    // Each region is later bound as a separate vertex-buffer slot.
    bufferDesc.size = positionSize + colorsSize + uvSize;
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

    // Upload UV coordinates after the position and color regions.
    ctx.queue.WriteBuffer(sharedVertexBuffer, positionSize + colorsSize,
                          uvs.data(), uvSize);

    ctx.queue.WriteBuffer(indexBuffer, 0, indices.data(),
                          indices.size() * sizeof(uint32_t));
}

wgpu::RenderPipeline createRenderPipeline(const wgpu::Device& device,
                                          wgpu::ShaderModule shaderModule,
                                          wgpu::PipelineLayout pipelineLayout) {
    wgpu::RenderPipelineDescriptor renderPipelineDesc = {};

    std::vector<wgpu::VertexBufferLayout> VBLayouts(3);
    std::vector<wgpu::VertexAttribute> vertexAttributes(3);

    vertexAttributes[0].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[0].offset = 0;
    vertexAttributes[0].shaderLocation = 0;

    vertexAttributes[1].format = wgpu::VertexFormat::Float32x4;
    vertexAttributes[1].offset = 0;
    vertexAttributes[1].shaderLocation = 1;

    // UV attribute: two 32-bit floats passed to shader @location(2).
    vertexAttributes[2].format = wgpu::VertexFormat::Float32x2;
    vertexAttributes[2].offset = 0;
    vertexAttributes[2].shaderLocation = 2;

    VBLayouts[0].arrayStride = sizeof(vec4);
    VBLayouts[0].attributeCount = 1;
    VBLayouts[0].attributes = &vertexAttributes[0];
    VBLayouts[0].stepMode = wgpu::VertexStepMode::Vertex;

    VBLayouts[1].arrayStride = sizeof(vec4);
    VBLayouts[1].attributeCount = 1;
    VBLayouts[1].attributes = &vertexAttributes[1];
    VBLayouts[1].stepMode = wgpu::VertexStepMode::Vertex;

    VBLayouts[2].arrayStride = sizeof(vec2);
    VBLayouts[2].attributeCount = 1;
    VBLayouts[2].attributes = &vertexAttributes[2];
    VBLayouts[2].stepMode = wgpu::VertexStepMode::Vertex;

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

    renderPipelineDesc.layout = pipelineLayout;

    wgpu::DepthStencilState depthStencilState = {};

    wgpu::RenderPipeline renderPipeline =
        ctx.device.CreateRenderPipeline(&renderPipelineDesc);

    return renderPipeline;
}

int main() {
    glfwSetErrorCallback([](int code, const char* message) {
        std::cerr << "GLFW error " << code << " " << message;
    });
    glfwInit();

    // ---------------------------------------------------------
    // Load image into CPU memory
    //
    // stb_image decodes the image into raw RGBA8 pixels.
    // At this point the image still exists only in CPU memory.
    // ---------------------------------------------------------
    stbi_set_flip_vertically_on_load(true);

    int image_width, image_height, nrChannels;
    unsigned char* imageData = stbi_load(
        IMAGE_NAME, &image_width, &image_height, &nrChannels, STBI_rgb_alpha);

    if (!imageData) {
        std::cerr << "Failed to load image!" << std::endl;
    }

    ctx.instance = createInstance();
    ctx.adapter = createAdapter(ctx.instance);
    ctx.device = createDevice(ctx.instance, ctx.adapter);
    ctx.queue = ctx.device.GetQueue();
    wgpu::ShaderModule shaderModule = CreateShaderModule(ctx.device, shader);
    data = addWindow(1920, 1080, "Main window", ctx.instance, ctx.adapter,
                     ctx.device);

    createBuffers();

    // ---------------------------------------------------------
    // GPU texture creation
    //
    // Allocate GPU storage that will receive the decoded RGBA pixels.
    // ---------------------------------------------------------
    wgpu::TextureDescriptor imageTextureDescriptor = {};

    imageTextureDescriptor.mipLevelCount = 1;
    imageTextureDescriptor.size.depthOrArrayLayers = 1;
    imageTextureDescriptor.size.width = image_width;
    imageTextureDescriptor.size.height = image_height;
    imageTextureDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    imageTextureDescriptor.label = "Image Texture";
    imageTextureDescriptor.sampleCount = 1;

    // TextureBinding:
    //     the texture can be read by shaders.
    //
    // CopyDst:
    //     CPU-side image data can be uploaded into the texture.
    imageTextureDescriptor.usage =
        wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;

    wgpu::Texture imageTexture =
        ctx.device.CreateTexture(&imageTextureDescriptor);

    // Describes the destination of the texture upload:
    // which texture, mip level, aspect and origin receive the pixels.
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = imageTexture;

    // Start writing at the top-left origin of mip level 0.
    destination.origin = wgpu::Origin3D{0, 0, 0};
    destination.mipLevel = 0;
    destination.aspect = wgpu::TextureAspect::All;

    // Describes how the source pixels are laid out in CPU memory.
    wgpu::TexelCopyBufferLayout sourceLayout = {};

    sourceLayout.offset = 0;
    sourceLayout.bytesPerRow = image_width * 4;  // RGBA8 = 4 bytes per pixel.
    sourceLayout.rowsPerImage = image_height;

    // Size of the texture region that will receive the upload.
    wgpu::Extent3D copySize = {(uint32_t)image_width, (uint32_t)image_height,
                               1};

    // Upload decoded RGBA pixels from CPU memory into the GPU texture.
    ctx.queue.WriteTexture(&destination, imageData,
                           image_width * image_height * 4, &sourceLayout,
                           &copySize);

    // CPU-side decoded pixels are no longer needed.
    // The GPU texture now owns its own copy of the image data.
    stbi_image_free(imageData);

    // ---------------------------------------------------------
    // Texture view and sampler
    //
    // TextureView exposes the texture as a shader resource.
    //
    // Sampler does not contain texture data.
    // It only describes how texture sampling should behave.
    // ---------------------------------------------------------
    wgpu::TextureView imageTextureView = imageTexture.CreateView();
    wgpu::SamplerDescriptor samplerDescriptor = {};

    // Linear filtering interpolates between neighboring texels.
    samplerDescriptor.magFilter = wgpu::FilterMode::Linear;
    samplerDescriptor.minFilter = wgpu::FilterMode::Linear;

    // UV coordinates outside [0, 1] are clamped to the texture edge.
    samplerDescriptor.addressModeU = wgpu::AddressMode::ClampToEdge;
    samplerDescriptor.addressModeV = wgpu::AddressMode::ClampToEdge;
    samplerDescriptor.addressModeW = wgpu::AddressMode::ClampToEdge;

    wgpu::Sampler sampler = ctx.device.CreateSampler(&samplerDescriptor);

    Uniforms uniform;

    wgpu::BufferDescriptor bufferDesc = {};
    bufferDesc.mappedAtCreation = false;
    bufferDesc.label = "Uniform buffer";

    bufferDesc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    bufferDesc.size = sizeof(Uniforms);
    uniformBuffer = ctx.device.CreateBuffer(&bufferDesc);

    std::vector<wgpu::BindGroupLayoutEntry> bindingGroupLayoutEntries(3);
    bindingGroupLayoutEntries[0].binding = 0;
    bindingGroupLayoutEntries[0].buffer.type = wgpu::BufferBindingType::Uniform;
    bindingGroupLayoutEntries[0].buffer.minBindingSize = sizeof(Uniforms);
    bindingGroupLayoutEntries[0].buffer.hasDynamicOffset = true;
    bindingGroupLayoutEntries[0].visibility =
        wgpu::ShaderStage::Fragment | wgpu::ShaderStage::Vertex;

    // binding(1) must contain a filterable 2D floating-point texture.
    bindingGroupLayoutEntries[1].binding = 1;
    bindingGroupLayoutEntries[1].texture.sampleType =
        wgpu::TextureSampleType::Float;
    bindingGroupLayoutEntries[1].texture.viewDimension =
        wgpu::TextureViewDimension::e2D;
    bindingGroupLayoutEntries[1].visibility = wgpu::ShaderStage::Fragment;

    // binding(2) must contain a filtering sampler.
    // Filtering is required because Linear filtering is used.
    bindingGroupLayoutEntries[2].binding = 2;
    bindingGroupLayoutEntries[2].sampler.type =
        wgpu::SamplerBindingType::Filtering;
    bindingGroupLayoutEntries[2].visibility = wgpu::ShaderStage::Fragment;

    std::vector<wgpu::BindGroupLayout> BGLayouts(1);
    wgpu::BindGroupLayoutDescriptor BGLayoutDescriptor = {};
    BGLayoutDescriptor.entries = bindingGroupLayoutEntries.data();
    BGLayoutDescriptor.entryCount = bindingGroupLayoutEntries.size();
    BGLayouts[0] = ctx.device.CreateBindGroupLayout(&BGLayoutDescriptor);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDescriptor = {};
    pipelineLayoutDescriptor.bindGroupLayoutCount = BGLayouts.size();
    pipelineLayoutDescriptor.bindGroupLayouts = BGLayouts.data();
    wgpu::PipelineLayout pipelineLayout =
        ctx.device.CreatePipelineLayout(&pipelineLayoutDescriptor);

    std::vector<wgpu::BindGroupEntry> bindGroupEntries(3);

    // ---------------------------------------------------------
    // Concrete resources for group(0)
    //
    // binding(0) -> Uniform Buffer
    // binding(1) -> TextureView
    // binding(2) -> Sampler
    // ---------------------------------------------------------
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = uniformBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = sizeof(Uniforms);

    bindGroupEntries[1].binding = 1;

    // Texture bindings use TextureView rather than Texture directly.
    bindGroupEntries[1].textureView = imageTextureView;

    // Buffer bindings use offset/size.
    // Texture and sampler bindings use textureView/sampler instead.
    // bindGroupEntries[1].offset = 0;
    // bindGroupEntries[1].size = image_width * image_height;

    bindGroupEntries[2].binding = 2;
    bindGroupEntries[2].sampler = sampler;

    wgpu::BindGroupDescriptor BGDesc = {};
    BGDesc.entries = bindGroupEntries.data();
    BGDesc.entryCount = bindGroupEntries.size();
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
        }

        DoRender(&data, ctx.device, ctx.queue, renderPipeline,
                 sharedVertexBuffer, positionSize, colorsSize, indexBuffer,
                 bindGroup, uniform);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();
}