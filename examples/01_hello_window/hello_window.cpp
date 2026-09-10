#include <GLFW/glfw3.h>
#include <endian.h>
#include <glfw3webgpu.h>
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

#include <cstdlib>
#include <iostream>

const char* shader = R"(
struct VertexOutput {
    @builtin(position) position : vec4f,
    @location(0) pos : vec4f,
};

@vertex
fn main_vs(@builtin(vertex_index) idx : u32) -> VertexOutput {
    var out : VertexOutput;
    if (idx == 0) {
        out.pos = vec4f(1.0, 0.0, 0.0, 1.0);
        out.position = vec4f(1.0, 0.0, 0.0, 1.0);
    } else if (idx == 1) {
        out.pos = vec4f(0.0, 1.0, 0.0, 1.0);
        out.position = vec4f(0.0, 1.0, 0.0, 1.0);
    } else {
        out.pos = vec4f(0.0, 0.0, 1.0, 1.0);
        out.position = vec4f(0.0, 0.0, 1.0, 1.0);
    }
    return out;
}

@fragment
fn main_fs(vertexData : VertexOutput) -> @location(0)vec4f {
    return vertexData.pos;
}
)";

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

struct WindowData {
    GLFWwindow* window;

    wgpu::Surface surface;
    wgpu::SurfaceConfiguration currentConfig;
    wgpu::SurfaceConfiguration targetConfig;
};

void SyncFromWindow(WindowData* data) {
    int height, width;
    glfwGetFramebufferSize(data->window, &height, &width);
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
    wgpu::SurfaceTexture surfaceTexture;
    data->surface.GetCurrentTexture(&surfaceTexture);
    wgpu::TextureView view = surfaceTexture.texture.CreateView();

    wgpu::CommandEncoder commandEncoder = device.CreateCommandEncoder();
    wgpu::RenderPassDescriptor desc = {};
    wgpu::RenderPassColorAttachment colorAttachment = {};

    colorAttachment.view = view;
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    colorAttachment.clearValue = wgpu::Color{0.0, 0.0, 0.0, 0.0};

    desc.colorAttachments = &colorAttachment;
    desc.colorAttachmentCount = 1;

    wgpu::RenderPassEncoder renderPass = commandEncoder.BeginRenderPass(&desc);
    renderPass.SetPipeline(pipeline);
    renderPass.Draw(3);
    renderPass.End();

    wgpu::CommandBuffer cmdBuffer = commandEncoder.Finish();
    queue.Submit(1, &cmdBuffer);

    wgpu::Status presentStatus = data->surface.Present();
    if (presentStatus != wgpu::Status::Success) {
        std::cout << "Present status failed" << '\n';
    }
}

int main(int argc, char* argv[]) {
    static constexpr auto kTimedWaitAny =
        wgpu::InstanceFeatureName::TimedWaitAny;  // Enable non-blocking
                                                  // asynchronous event waiting
                                                  // (timeouts) for the instance
    wgpu::InstanceDescriptor instanceDescriptor;
    instanceDescriptor.requiredFeatureCount = 1;
    instanceDescriptor.requiredFeatures = &kTimedWaitAny;

    wgpu::Instance instance = wgpu::CreateInstance(&instanceDescriptor);

    wgpu::Adapter adapter = {};
    void* userData = &adapter;
    wgpu::CallbackMode callbackMode = wgpu::CallbackMode::WaitAnyOnly;

    auto adapterCallback = [](wgpu::RequestAdapterStatus status,
                              wgpu::Adapter adapter_, wgpu::StringView message,
                              void* userData) {
        if (status != wgpu::RequestAdapterStatus::Success) {
            std::cerr << "Can't request adapter: " << message << '\n';
        } else {
            *static_cast<wgpu::Adapter*>(userData) = adapter_;
        }
    };
    wgpu::RequestAdapterOptions adapterOptions = {};

    wgpu::Future futureAdapter = instance.RequestAdapter(
        &adapterOptions, callbackMode, adapterCallback, userData);

    instance.WaitAny(futureAdapter, UINT64_MAX);
    if (adapter == nullptr) {
        std::cerr << "RequestAdapter failed" << '\n';
        return 1;
    }

    // Print GPU's specs
    wgpu::AdapterInfo info;
    adapter.GetInfo(&info);

    std::cout << "VendorID: " << std::hex << info.vendorID << std::dec << "\n";
    std::cout << "Vendor: " << info.vendor << "\n";
    std::cout << "Architecture: " << info.architecture << "\n";
    std::cout << "DeviceID: " << std::hex << info.deviceID << std::dec << "\n";
    std::cout << "Name: " << info.device << "\n";
    std::cout << "Driver description: " << info.description << "\n";

    wgpu::DeviceDescriptor deviceDescriptor{};
    deviceDescriptor.SetUncapturedErrorCallback([](const wgpu::Device&,
                                                   wgpu::ErrorType errorType,
                                                   wgpu::StringView message) {
        std::cout << errorType << " error: " << message << '\n';
    });

    wgpu::Device device = {};
    userData = &device;
    // Can request device in two pathes

    /* 1 */
    auto deviceCallback = [](wgpu::RequestDeviceStatus status,
                             wgpu::Device device, wgpu::StringView message,
                             void* userData) {
        if (status != wgpu::RequestDeviceStatus::Success) {
            std::cerr << "Failed to get a device: " << message;
            return;
        } else {
            *static_cast<wgpu::Device*>(userData) = device;
        }
    };
    wgpu::Future deviceFuture = adapter.RequestDevice(
        &deviceDescriptor, wgpu::CallbackMode::WaitAnyOnly, deviceCallback,
        userData);
    instance.WaitAny(deviceFuture, UINT64_MAX);
    if (device == nullptr) {
        std::cerr << "RequsetDevice failed" << '\n';
        return EXIT_FAILURE;
    }

    /* 2. Second way to get device
    adapter.CreateDevice(&deviceDescriptor);
    */

    // Get queue
    wgpu::Queue queue = device.GetQueue();

    // GLFW setup
    glfwSetErrorCallback([](int code, const char* message) {
        std::cerr << "GLFW error " << code << " " << message;
    });
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window =
        glfwCreateWindow(800, 600, "Hello Window", nullptr, nullptr);

    // Surface setup
    wgpu::Surface surface = glfwGetWGPUSurface(
        instance.Get(),
        window);  // using utility library to get surface from glfw

    wgpu::SurfaceCapabilities capabilities = {};
    surface.GetCapabilities(adapter, &capabilities);

    wgpu::SurfaceConfiguration config{};
    config.device = device;
    config.usage = wgpu::TextureUsage::RenderAttachment;
    config.format = capabilities.formats[0];
    config.alphaMode = capabilities.alphaModes[0];
    config.presentMode = capabilities.presentModes[0];
    config.width = 0;
    config.height = 0;

    WindowData data = {};
    data.currentConfig = config;
    data.targetConfig = config;
    data.window = window;
    data.surface = surface;
    SyncFromWindow(&data);

    // Create renderPipeline
    wgpu::ShaderModule shaderModule = CreateShaderModule(device, shader);
    wgpu::RenderPipelineDescriptor pipelineDescriptor = {};

    pipelineDescriptor.layout = nullptr;
    pipelineDescriptor.vertex.buffers = nullptr;
    pipelineDescriptor.vertex.bufferCount = 0;
    pipelineDescriptor.vertex.module = shaderModule;
    pipelineDescriptor.vertex.entryPoint = "main_vs";

    pipelineDescriptor.primitive.topology =
        wgpu::PrimitiveTopology::TriangleList;
    pipelineDescriptor.primitive.cullMode = wgpu::CullMode::None;
    pipelineDescriptor.primitive.stripIndexFormat =
        wgpu::IndexFormat::Undefined;
    pipelineDescriptor.primitive.frontFace = wgpu::FrontFace::CCW;
    pipelineDescriptor.primitive.cullMode = wgpu::CullMode::None;

    pipelineDescriptor.depthStencil = nullptr;

    pipelineDescriptor.multisample.mask = 0xFFFFFFFF;
    pipelineDescriptor.multisample.count = 1;
    pipelineDescriptor.multisample.alphaToCoverageEnabled = false;

    wgpu::FragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "main_fs";
    fragmentState.targetCount = 1;

    wgpu::ColorTargetState colorTarget = {};
    colorTarget.format = data.currentConfig.format;
    fragmentState.targets = &colorTarget;

    pipelineDescriptor.fragment = &fragmentState;

    wgpu::BlendComponent blendComponent = {};
    blendComponent.srcFactor = wgpu::BlendFactor::One;
    blendComponent.dstFactor = wgpu::BlendFactor::Zero;
    blendComponent.operation = wgpu::BlendOperation::Add;

    wgpu::BlendState blendState;
    blendState.color = blendComponent;
    blendState.alpha = blendComponent;

    wgpu::RenderPipeline renderPipeline =
        device.CreateRenderPipeline(&pipelineDescriptor);

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

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}
