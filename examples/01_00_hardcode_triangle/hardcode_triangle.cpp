#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

#include <cstdlib>
#include <iostream>

struct WindowData;

wgpu::ShaderModule CreateShaderModule(const wgpu::Device& device,
                                      const char* source);
wgpu::ShaderModule CreateShaderModule(const wgpu::Device& device,
                                      const std::string& source);
void SyncFromWindow(WindowData* data);
bool IsSameConfig(wgpu::SurfaceConfiguration& a, wgpu::SurfaceConfiguration& b);
void DoRender(WindowData* data, wgpu::Device device,
              wgpu::RenderPipeline pipeline, wgpu::Queue queue);
void printGpuSpecs(const wgpu::Adapter& adapter);
void printDeviceFeatures(const wgpu::Device& device);
void printAdapterFeatures(const wgpu::Adapter& adapter);
wgpu::Instance createInstance();
wgpu::Adapter createAdapter(const wgpu::Instance& instance);
wgpu::Device createDevice(const wgpu::Instance& instance,
                          const wgpu::Adapter& adapter);
wgpu::RenderPipeline createDefaultRenderpipeline(const WindowData& data,
                                                 const wgpu::Device& device);
WindowData addWindow(int width, int height, const char* label,
                     const wgpu::Instance& instance,
                     const wgpu::Adapter& adapter, const wgpu::Device& device);

const char* shader = R"(
struct VertexOutput {
    @builtin(position) position : vec4f,
    @location(0) color : vec4f,
};

@vertex
fn main_vs(@builtin(vertex_index) idx : u32) -> VertexOutput {
    var out : VertexOutput;
    if (idx == 0) {
        out.color = vec4f(1.0, 0.0, 0.0, 1.0);
        out.position = vec4f(1.0, 0.0, 0.0, 1.0);
    } else if (idx == 1) {
        out.color = vec4f(0.0, 1.0, 0.0, 1.0);
        out.position = vec4f(0.0, 1.0, 0.0, 1.0);
    } else {
        out.color = vec4f(0.0, 0.0, 1.0, 1.0);
        out.position = vec4f(0.0, 0.0, 1.0, 1.0);
    }
    return out;
}

@fragment
fn main_fs(vertexData : VertexOutput) -> @location(0)vec4f {
    return vertexData.color;
}
)";

struct WindowData {
    GLFWwindow* window;

    wgpu::Surface surface;
    wgpu::SurfaceConfiguration currentConfig;
    wgpu::SurfaceConfiguration targetConfig;
};

wgpu::ShaderModule CreateShaderModule(const wgpu::Device& device,
                                      const char* source) {
    wgpu::ShaderSourceWGSL wgslDesc;
    wgslDesc.code = source;
    wgpu::ShaderModuleDescriptor descriptor;
    descriptor.nextInChain = &wgslDesc;
    return device.CreateShaderModule(&descriptor);
}

wgpu::ShaderModule CreateShaderModule(const wgpu::Device& device,
                                      const std::string& source) {
    return CreateShaderModule(device, source.c_str());
}

void SyncFromWindow(WindowData* data) {
    int width, height;
    glfwGetFramebufferSize(data->window, &width, &height);
    data->targetConfig.width = std::max(1u, static_cast<uint32_t>(width));
    data->targetConfig.height = std::max(1u, static_cast<uint32_t>(height));
}

bool IsSameConfig(wgpu::SurfaceConfiguration& a,
                  wgpu::SurfaceConfiguration& b) {
    return a.device.Get() == b.device.Get() &&  //
           a.format == b.format &&              //
           a.usage == b.usage &&                //
           a.alphaMode == b.alphaMode &&        //
           a.width == b.width &&                //
           a.height == b.height &&              //
           a.presentMode == b.presentMode;
}

void DoRender(WindowData* data, wgpu::Device device,
              wgpu::RenderPipeline pipeline, wgpu::Queue queue) {
    // Acquire the texture that will be presented by the surface this frame.
    wgpu::SurfaceTexture surfaceTexture;
    data->surface.GetCurrentTexture(&surfaceTexture);

    // Render passes use a TextureView to access the texture as an attachment.
    wgpu::TextureView view = surfaceTexture.texture.CreateView();

    // Records GPU commands; commands are not executed yet.
    wgpu::CommandEncoder commandEncoder = device.CreateCommandEncoder();

    wgpu::RenderPassDescriptor desc = {};
    wgpu::RenderPassColorAttachment colorAttachment = {};

    // This TextureView is the color render target of this render pass.
    colorAttachment.view = view;

    // Clear the attachment before drawing and preserve the result afterwards.
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.clearValue = wgpu::Color{0.0, 0.0, 0.0, 1.0};

    desc.colorAttachments = &colorAttachment;
    desc.colorAttachmentCount = 1;

    wgpu::RenderPassEncoder renderPass = commandEncoder.BeginRenderPass(&desc);

    // Select the rules/shaders that will process subsequent draw calls.
    renderPass.SetPipeline(pipeline);

    // Generates 3 vertex shader invocations: vertex_index = 0, 1, 2.
    renderPass.Draw(3);

    renderPass.End();

    // Finish recording and submit the commands for GPU execution.
    wgpu::CommandBuffer cmdBuffer = commandEncoder.Finish();
    queue.Submit(1, &cmdBuffer);

    // Present the rendered surface texture to the window system.
    wgpu::Status presentStatus = data->surface.Present();

    if (presentStatus != wgpu::Status::Success) {
        std::cout << "Present status failed" << '\n';
    }
}

void printGpuSpecs(const wgpu::Adapter& adapter) {
    wgpu::AdapterInfo info;
    adapter.GetInfo(&info);

    std::cout << "VendorID: " << std::hex << info.vendorID << std::dec << "\n";
    std::cout << "Vendor: " << info.vendor << "\n";
    std::cout << "Architecture: " << info.architecture << "\n";
    std::cout << "DeviceID: " << std::hex << info.deviceID << std::dec << "\n";
    std::cout << "Name: " << info.device << "\n";
    std::cout << "Driver description: " << info.description << "\n";
}

void printDeviceFeatures(const wgpu::Device& device) {
    wgpu::SupportedFeatures supportedFeatures{};
    device.GetFeatures(&supportedFeatures);

    for (size_t i = 0; i < supportedFeatures.featureCount; ++i) {
        // supportedFeatures.features — array<wgpu::FeatureName>
        auto feature = supportedFeatures.features[i];

        // use number code, FeatureName type is enum
        std::cout << "Feature: " << static_cast<uint32_t>(feature) << '\n';
    }
}

void printAdapterFeatures(const wgpu::Adapter& adapter) {
    wgpu::SupportedFeatures supportedFeatures{};
    adapter.GetFeatures(&supportedFeatures);

    std::cout << "Total features supported: " << supportedFeatures.featureCount
              << '\n';

    for (size_t i = 0; i < supportedFeatures.featureCount; ++i) {
        wgpu::FeatureName feature = supportedFeatures.features[i];
        std::cout << "Feature ID: " << static_cast<uint32_t>(feature) << '\n';
    }
}

wgpu::Instance createInstance() {
    static constexpr auto kTimedWaitAny =
        wgpu::InstanceFeatureName::TimedWaitAny;  // Enable non-blocking
                                                  // asynchronous event waiting
                                                  // (timeouts) for the instance
    wgpu::InstanceDescriptor instanceDescriptor = {};
    instanceDescriptor.requiredFeatureCount = 1;
    instanceDescriptor.requiredFeatures = &kTimedWaitAny;

    return wgpu::CreateInstance(&instanceDescriptor);
}

wgpu::Adapter createAdapter(const wgpu::Instance& instance) {
    wgpu::Adapter adapter;
    wgpu::RequestAdapterOptions opts = {};
    void* userdata = nullptr;
    userdata = &adapter;

    auto callbackMode =
        wgpu::CallbackMode::WaitAnyOnly;  // using instance.WaitAny()

    // Called when the asynchronous adapter request completes.
    auto adapterCallbackFunc = [](wgpu::RequestAdapterStatus status,
                                  wgpu::Adapter adapter,
                                  wgpu::StringView message, void* userdata) {
        if (status != wgpu::RequestAdapterStatus::Success) {
            std::cerr << "Request adapter error: " << message << '\n';
            return;
        }
        *static_cast<wgpu::Adapter*>(userdata) = adapter;
    };

    wgpu::Future adapterFuture = instance.RequestAdapter(
        &opts, callbackMode, adapterCallbackFunc, userdata);

    instance.WaitAny(adapterFuture, UINT64_MAX);
    if (adapter == nullptr) {
        std::cerr << "Failed to request adapter" << '\n';
    }
    return adapter;
}

wgpu::Device createDevice(const wgpu::Instance& instance,
                          const wgpu::Adapter& adapter) {
    wgpu::Device device;
    void* userdata = &device;
    wgpu::DeviceDescriptor deviceDescriptor = {};

    auto callbackMode = wgpu::CallbackMode::WaitAnyOnly;
    deviceDescriptor.SetUncapturedErrorCallback([](const wgpu::Device& device,
                                                   wgpu::ErrorType errorType,
                                                   wgpu::StringView message) {
        std::cout << errorType << "Failed device: " << message;
    });
    auto deviceCallback = [](wgpu::RequestDeviceStatus status,
                             wgpu::Device device, wgpu::StringView message,
                             void* userdata) {
        if (status != wgpu::RequestDeviceStatus::Success) {
            std::cout << "Failed to request device: " << message << '\n';
            return;
        }
        *static_cast<wgpu::Device*>(userdata) = device;
    };

    wgpu::Future deviceFuture = adapter.RequestDevice(
        &deviceDescriptor, callbackMode, deviceCallback, userdata);

    instance.WaitAny(deviceFuture, UINT64_MAX);
    if (device == nullptr) {
        std::cerr << "Failed to request device" << '\n';
    }

    /* 2. Second way to get device
    adapter.CreateDevice(&deviceDescriptor);
    */

    return device;
}

wgpu::RenderPipeline createDefaultRenderpipeline(const WindowData& data,
                                                 const wgpu::Device& device) {
    wgpu::ShaderModule shaderModule = CreateShaderModule(device, shader);
    wgpu::RenderPipelineDescriptor pipelineDescriptor = {};

    // No bind groups/resources are used by this pipeline.
    // nullptr lets WebGPU infer an empty pipeline layout from the shaders.
    pipelineDescriptor.layout = nullptr;

    // Vertex data is generated inside the vertex shader using vertex_index.
    pipelineDescriptor.vertex.buffers = nullptr;
    pipelineDescriptor.vertex.bufferCount = 0;
    pipelineDescriptor.vertex.module = shaderModule;
    pipelineDescriptor.vertex.entryPoint = "main_vs";

    // Every group of 3 vertices forms one independent triangle.
    pipelineDescriptor.primitive.topology =
        wgpu::PrimitiveTopology::TriangleList;

    // Render both front-facing and back-facing triangles.
    pipelineDescriptor.primitive.cullMode = wgpu::CullMode::None;

    // Index format is only relevant for strip topologies.
    pipelineDescriptor.primitive.stripIndexFormat =
        wgpu::IndexFormat::Undefined;

    pipelineDescriptor.primitive.frontFace = wgpu::FrontFace::CCW;

    // No depth or stencil buffer in this example.
    pipelineDescriptor.depthStencil = nullptr;

    // One sample per pixel means MSAA is disabled.
    pipelineDescriptor.multisample.count = 1;

    // Enable writes to all available samples.
    pipelineDescriptor.multisample.mask = 0xFFFFFFFF;

    // Do not convert fragment alpha into MSAA sample coverage.
    pipelineDescriptor.multisample.alphaToCoverageEnabled = false;

    wgpu::FragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "main_fs";
    fragmentState.targetCount = 1;

    wgpu::ColorTargetState colorTarget = {};

    // Must match the format of the color attachment used by the render pass.
    colorTarget.format = data.currentConfig.format;

    // Fragment shader @location(0) writes to color target 0.
    fragmentState.targets = &colorTarget;

    pipelineDescriptor.fragment = &fragmentState;

    return device.CreateRenderPipeline(&pipelineDescriptor);
}

[[nodiscard]] WindowData addWindow(int width, int height, const char* label,
                                   const wgpu::Instance& instance,
                                   const wgpu::Adapter& adapter,
                                   const wgpu::Device& device) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window =
        glfwCreateWindow(width, height, label, nullptr, nullptr);

    wgpu::Surface surface = glfwGetWGPUSurface(
        instance.Get(),
        window);  // using utility library to get surface from glfw

    wgpu::SurfaceCapabilities capabilities = {};
    surface.GetCapabilities(adapter, &capabilities);

    wgpu::SurfaceConfiguration config{};
    config.device = device;
    // Surface textures will be used as render-pass color attachments.
    config.usage = wgpu::TextureUsage::RenderAttachment;

    // Choose a texture format supported by this surface.
    config.format = capabilities.formats[0];

    // Controls how the surface alpha channel is composed with the window
    // system.
    config.alphaMode = capabilities.alphaModes[0];

    // Controls when rendered frames are presented to the display.
    config.presentMode = capabilities.presentModes[0];
    config.width = 0;
    config.height = 0;

    WindowData data = {};
    // Using two surface to regular window resize
    data.currentConfig = config;
    data.targetConfig = config;
    data.window = window;
    data.surface = surface;
    SyncFromWindow(&data);

    return data;
}

int main(int argc, char* argv[]) {
    // GLFW setup
    glfwSetErrorCallback([](int code, const char* message) {
        std::cerr << "GLFW error " << code << " " << message;
    });
    glfwInit();

    /* SETUP STATIC WEBGPU CONTEXT(INSTANCE -> ADAPTER -> DEVICE -> QUEUE)*/
    wgpu::Instance instance = createInstance();
    wgpu::Adapter adapter = createAdapter(instance);
    wgpu::Device device = createDevice(instance, adapter);
    wgpu::Queue queue = device.GetQueue();
    /* END SETUP GPU CONTEXT*/

    /* SURFACE SETUP AND GLFW WINDOW */
    WindowData data =
        addWindow(800, 600, "Hello window", instance, adapter, device);
    /* END SURFACE SETUP */

    wgpu::RenderPipeline renderPipeline =
        createDefaultRenderpipeline(data, device);

    while (!glfwWindowShouldClose(data.window)) {
        instance.ProcessEvents();
        glfwPollEvents();

        SyncFromWindow(&data);

        if (!IsSameConfig(data.currentConfig, data.targetConfig)) {
            data.surface.Configure(&data.targetConfig);
            data.currentConfig = data.targetConfig;
        }

        DoRender(&data, device, renderPipeline, queue);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();

    return EXIT_SUCCESS;
}
