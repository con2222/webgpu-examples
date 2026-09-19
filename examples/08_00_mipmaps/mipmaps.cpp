#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <context.hpp>
#include <iostream>
#include <math.hpp>
#include <vector>

#define IMAGE_NAME "utility/texture.jpg"

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
// CPU-side mip level
//
// Stores one generated mipmap before it is uploaded to the GPU.
//
// image  - RGBA8 pixel data
// width  - width of this mip level
// height - height of this mip level
// level  - destination mip level inside wgpu::Texture
// ---------------------------------------------------------
struct MipMapData {
    unsigned char* image;
    uint32_t width;
    uint32_t height;

    uint32_t level;
};

constexpr bool depthStencilToggle = true;

static WindowData data;
static WebGPUContext ctx;
static wgpu::Buffer sharedVertexBuffer;
static wgpu::Buffer indexBuffer;
static wgpu::Buffer uniformBuffer;

static uint32_t mipLevelCount;

const uint32_t UNIFORM_ALIGNMENT = 256;

// ---------------------------------------------------------
// Floor geometry
//
// A long plane on the XZ axis.
// The camera looks roughly along -Z, so the texture becomes
// progressively smaller toward the far end of the floor.
// ---------------------------------------------------------
std::vector<vec4> positions = {
    {-5.0f, -1.0f, -3.0f, 1.0f},   // 0 - near left
    {5.0f, -1.0f, -3.0f, 1.0f},    // 1 - near right
    {5.0f, -1.0f, -40.0f, 1.0f},   // 2 - far right
    {-5.0f, -1.0f, -40.0f, 1.0f},  // 3 - far left
};

// Repeat the texture many times across the floor.
// Large UV values are intentional: they make texture
// minification much easier to see in the distance.
std::vector<vec2> uvs = {
    {0.0f, 0.0f},    // 0
    {10.0f, 0.0f},   // 1
    {10.0f, 40.0f},  // 2
    {0.0f, 40.0f},   // 3
};

// Color is not currently used by the fragment shader.
// Kept only so the existing vertex layout does not need to change.
std::vector<vec4> colors = {
    {1.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
    {1.0f, 1.0f, 1.0f, 1.0f},
};

std::vector<uint32_t> indices = {
    0, 1, 2, 2, 3, 0,
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
    mat4 modelView = mat4::make_translation(0.0f, 0.0f, 0.0f);

    uniform.modelViewMatrix = modelView;
    uniform.perspectiveMatrix = proj;
    uniform.time = glfwGetTime();
    queue.WriteBuffer(uniformBuffer, 0, &uniform, sizeof(Uniforms));

    wgpu::RenderPassEncoder renderPass =
        cmdEncoder.BeginRenderPass(&renderPassDesc);
    renderPass.SetPipeline(pipeline);
    renderPass.SetVertexBuffer(0, sharedVertexBuffer, 0, positionSize);
    renderPass.SetVertexBuffer(1, sharedVertexBuffer, positionSize, colorsSize);

    renderPass.SetVertexBuffer(2, sharedVertexBuffer, positionSize + colorsSize,
                               uvSize);
    renderPass.SetIndexBuffer(indexBuffer, wgpu::IndexFormat::Uint32);

    uint32_t dynamicOffset = 0;
    renderPass.SetBindGroup(0, bindGroup, 1, &dynamicOffset);
    renderPass.DrawIndexed(6);

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

// ---------------------------------------------------------
// Generate mipmaps on the CPU
//
// Each next mip level is approximately 2x smaller:
//
// mip 0: 1024 x 1024  <- original image
// mip 1:  512 x 512
// mip 2:  256 x 256
// ...
// mip N:    1 x 1
//
// Each new pixel is calculated by averaging a 2x2 block
// of pixels from the previous mip level.
// ---------------------------------------------------------
std::vector<MipMapData> createMipMaps(uint32_t mipLevelCount, uint32_t width,
                                      uint32_t height, unsigned char* image) {
    std::vector<MipMapData> mipmaps;

    // mip 0 is the original image, so we only generate
    // mip levels 1 .. mipLevelCount - 1.
    mipmaps.reserve(mipLevelCount - 1);

    uint32_t oldWidth = width;
    uint32_t oldHeight = height;
    unsigned char* oldImage = image;

    for (uint32_t level = 0; level < mipLevelCount - 1; level++) {
        // Every mip is half the size of the previous one.
        // max(1) prevents dimensions from becoming zero.
        uint32_t newWidth = std::max(1u, oldWidth / 2);
        uint32_t newHeight = std::max(1u, oldHeight / 2);

        // RGBA8 = 4 bytes per pixel.
        unsigned char* newImage = new unsigned char[newWidth * newHeight * 4];

        for (uint32_t y = 0; y < newHeight; y++) {
            for (uint32_t x = 0; x < newWidth; x++) {
                // uint32_t is used because the sum of four
                // 8-bit channels may be as large as 4 * 255 = 1020.
                uint32_t red = 0;
                uint32_t green = 0;
                uint32_t blue = 0;
                uint32_t alpha = 0;

                // One new pixel corresponds to a 2x2 block
                // in the previous mip level.
                for (uint32_t i = 2 * y; i <= 2 * y + 1; i++) {
                    for (uint32_t j = 2 * x; j <= x * 2 + 1; j++) {
                        // Clamp coordinates for images with odd dimensions.
                        uint32_t safe_i = std::min(i, oldHeight - 1);
                        uint32_t safe_j = std::min(j, oldWidth - 1);

                        // Convert (x, y) pixel coordinates into an
                        // index inside the flat RGBA byte array.
                        uint32_t idx = (safe_i * oldWidth + safe_j) * 4;

                        red += oldImage[idx];
                        green += oldImage[idx + 1];
                        blue += oldImage[idx + 2];
                        alpha += oldImage[idx + 3];
                    }
                }

                uint32_t idx = (y * newWidth + x) * 4;

                // Average the four source pixels.
                newImage[idx] = red / 4;
                newImage[idx + 1] = green / 4;
                newImage[idx + 2] = blue / 4;
                newImage[idx + 3] = alpha / 4;
            }
        }

        // The generated mip becomes the source for the next mip.
        oldImage = newImage;
        oldHeight = newHeight;
        oldWidth = newWidth;
        mipmaps.emplace_back(newImage, newWidth, newHeight, level + 1);
    }

    return mipmaps;
}

int main() {
    glfwSetErrorCallback([](int code, const char* message) {
        std::cerr << "GLFW error " << code << " " << message;
    });
    glfwInit();

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

    // Full mip chain:
    //
    // mipLevelCount = floor(log2(max(width, height))) + 1
    //
    // Example for 1024x1024:
    //
    // mip 0  -> 1024x1024
    // mip 1  -> 512x512
    // mip 2  -> 256x256
    // ...
    // mip 10 -> 1x1
    mipLevelCount =
        std::floor(std::log2(std::max(image_width, image_height))) + 1;

    wgpu::TextureDescriptor imageTextureDescriptor = {};

    // Allocate storage for the complete mip chain.
    //
    // IMPORTANT:
    // This does NOT generate mipmaps.
    // It only creates space for them inside the GPU texture.
    imageTextureDescriptor.mipLevelCount = mipLevelCount;
    imageTextureDescriptor.size.depthOrArrayLayers = 1;
    imageTextureDescriptor.size.width = image_width;
    imageTextureDescriptor.size.height = image_height;
    imageTextureDescriptor.format = wgpu::TextureFormat::RGBA8Unorm;
    imageTextureDescriptor.label = "Image Texture";
    imageTextureDescriptor.sampleCount = 1;

    imageTextureDescriptor.usage =
        wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;

    wgpu::Texture imageTexture =
        ctx.device.CreateTexture(&imageTextureDescriptor);

    wgpu::TexelCopyTextureInfo destination;
    destination.texture = imageTexture;
    destination.origin = wgpu::Origin3D{0, 0, 0};
    destination.aspect = wgpu::TextureAspect::All;

    wgpu::TexelCopyBufferLayout sourceLayout = {};

    sourceLayout.offset = 0;
    sourceLayout.bytesPerRow = image_width * 4;
    sourceLayout.rowsPerImage = image_height;

    wgpu::Extent3D copySize = {(uint32_t)image_width, (uint32_t)image_height,
                               1};

    auto mipmaps =
        createMipMaps(mipLevelCount, image_width, image_height, imageData);

    // ---------------------------------------------------------
    // Upload mip chain to the GPU
    //
    // mip 0 is the original stb_image image.
    // All other levels were generated by createMipMaps().
    // ---------------------------------------------------------

    // Upload mip 0.
    destination.mipLevel = 0;
    ctx.queue.WriteTexture(&destination, imageData,
                           image_height * image_width * 4, &sourceLayout,
                           &copySize);

    // Upload generated mip levels.
    for (auto& mipmap : mipmaps) {
        // Select which mip level inside the GPU texture receives data.
        destination.mipLevel = mipmap.level;

        // Source memory layout changes because every mip
        // has a different width and height.
        sourceLayout.bytesPerRow = mipmap.width * 4;
        sourceLayout.rowsPerImage = mipmap.height;
        copySize = {(uint32_t)mipmap.width, (uint32_t)mipmap.height, 1};

        ctx.queue.WriteTexture(&destination, mipmap.image,
                               mipmap.height * mipmap.width * 4, &sourceLayout,
                               &copySize);

        // CPU copy is no longer needed after the upload.
        delete[] mipmap.image;
    }

    stbi_image_free(imageData);

    wgpu::TextureView imageTextureView = imageTexture.CreateView();

    // ---------------------------------------------------------
    // Sampler
    //
    // Controls HOW a texture is sampled:
    //
    // addressMode*  - behavior for UV coordinates outside [0, 1]
    // magFilter     - filtering when texture is magnified
    // minFilter     - filtering when texture is minified
    // mipmapFilter  - filtering between mip levels
    // lodMinClamp   - minimum allowed LOD
    // lodMaxClamp   - maximum allowed LOD
    // maxAnisotropy - improves quality at oblique viewing angles
    // ---------------------------------------------------------
    wgpu::SamplerDescriptor samplerDescriptor = {};

    // Magnification filter.
    //
    // Used when the texture is displayed larger than its texel resolution.
    //
    // Nearest:
    //     chooses one nearest texel -> sharp / pixelated.
    //
    // Linear:
    //     interpolates neighboring texels -> smoother.
    samplerDescriptor.magFilter = wgpu::FilterMode::Linear;

    // Minification filter.
    //
    // Used when many texture texels have to fit into fewer screen pixels.
    //
    // Nearest:
    //     nearest texel.
    //
    // Linear:
    //     interpolate neighboring texels inside the selected mip level.
    samplerDescriptor.minFilter = wgpu::FilterMode::Linear;

    // Controls filtering BETWEEN mip levels.
    //
    // Nearest:
    //     choose one closest mip level.
    //
    // Linear:
    //     interpolate between two neighboring mip levels.
    //     With linear minification this gives trilinear filtering.
    samplerDescriptor.mipmapFilter = wgpu::MipmapFilterMode::Nearest;

    // ---------------------------------------------------------
    // Texture addressing modes
    //
    // These modes define what happens when UV coordinates go
    // outside the normal [0, 1] range.
    //
    // ClampToEdge:
    //     UV values are clamped to the nearest edge.
    //     Example:
    //         -0.3 -> 0.0
    //          1.7 -> 1.0
    //
    //     The border texels are stretched outside the texture.
    //     Useful for images that should NOT repeat.
    //
    // Repeat:
    //     The texture repeats every 1.0 UV unit.
    //     Example:
    //          0.2 -> 0.2
    //          1.2 -> 0.2
    //          2.2 -> 0.2
    //
    //     Useful for tiled textures such as floors, walls,
    //     terrain, bricks, etc.
    //
    // MirrorRepeat:
    //     Same as Repeat, but every second copy is mirrored.
    //
    //     Example:
    //         0..1  -> normal
    //         1..2  -> mirrored
    //         2..3  -> normal
    //         3..4  -> mirrored
    //
    //     Useful when regular repetition produces visible seams
    //     or an obvious repeating pattern.
    //
    // U = horizontal texture coordinate
    // V = vertical texture coordinate
    // W = third coordinate, mainly used for 3D textures
    // ---------------------------------------------------------
    samplerDescriptor.addressModeU = wgpu::AddressMode::Repeat;
    samplerDescriptor.addressModeV = wgpu::AddressMode::Repeat;
    samplerDescriptor.addressModeW = wgpu::AddressMode::Repeat;

    // Restrict which mip LODs sampler is allowed to use.
    //
    // Useful for debugging mipmaps:
    // lodMinClamp = 3 would prevent levels 0,1,2 from being selected.
    // samplerDescriptor.lodMinClamp = 0.0f;
    // samplerDescriptor.lodMaxClamp = 32.0f;

    // Anisotropic filtering.
    //
    // Improves textures viewed at sharp/oblique angles,
    // exactly like our long floor.
    //
    // 1 = disabled.
    // Larger values improve quality but cost more sampling work.
    //
    // If maxAnisotropy > 1, WebGPU requires mag/min/mipmap
    // filters to all use Linear.
    // samplerDescriptor.maxAnisotropy = 1;

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

    bindingGroupLayoutEntries[1].binding = 1;
    bindingGroupLayoutEntries[1].texture.sampleType =
        wgpu::TextureSampleType::Float;
    bindingGroupLayoutEntries[1].texture.viewDimension =
        wgpu::TextureViewDimension::e2D;
    bindingGroupLayoutEntries[1].visibility = wgpu::ShaderStage::Fragment;

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

    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = uniformBuffer;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = sizeof(Uniforms);

    bindGroupEntries[1].binding = 1;

    bindGroupEntries[1].textureView = imageTextureView;

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