#include <glfw/glfw3.h>
#include <glfw3webgpu.h>

#include <context.hpp>
#include <iostream>

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

wgpu::Instance createInstance() {
    static constexpr auto kTimedWaitAny =
        wgpu::InstanceFeatureName::TimedWaitAny;
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

    auto callbackMode = wgpu::CallbackMode::WaitAnyOnly;

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

    return device;
}

[[nodiscard]] WindowData addWindow(int width, int height, const char* label,
                                   const wgpu::Instance& instance,
                                   const wgpu::Adapter& adapter,
                                   const wgpu::Device& device) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window =
        glfwCreateWindow(width, height, label, nullptr, nullptr);

    wgpu::Surface surface = glfwGetWGPUSurface(instance.Get(), window);

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

    return data;
}
