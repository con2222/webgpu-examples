#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <webgpu/webgpu_cpp.h>

#include <cstdlib>
#include <iostream>

struct WebGPUContext {
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
};

struct WindowData {
    GLFWwindow* window;

    wgpu::Surface surface;
    wgpu::SurfaceConfiguration currentConfig;
    wgpu::SurfaceConfiguration targetConfig;
};

void syncFromWindow(WindowData* data);

static WindowData data;
static WebGPUContext ctx;

WindowData addWindow(int width, int height, const char* label) {
    GLFWwindow* window =
        glfwCreateWindow(width, height, label, nullptr, nullptr);
    wgpu::Surface surface = glfwGetWGPUSurface(ctx.instance.Get(), window);
    wgpu::SurfaceCapabilities caps = {};
    surface.GetCapabilities(ctx.adapter, &caps);

    wgpu::SurfaceConfiguration config = {};
    config.height = 0;
    config.width = 0;
    config.alphaMode = caps.alphaModes[0];
    config.presentMode = caps.presentModes[0];
    config.device = ctx.device;
    config.format = caps.formats[0];
    config.usage = wgpu::TextureUsage::RenderAttachment;

    WindowData data = {};
    data.currentConfig = config;
    data.targetConfig = config;
    data.surface = surface;
    data.window = window;

    syncFromWindow(&data);

    return data;
}

void syncFromWindow(WindowData* data) {
    int width, height;
    glfwGetFramebufferSize(data->window, &width, &height);
    data->targetConfig.width = width;
    data->targetConfig.height = height;
}

bool IsSameConfig(wgpu::SurfaceConfiguration& a,
                  wgpu::SurfaceConfiguration& b) {
    return a.width == b.width && a.height == b.height && a.format == b.format &&
           a.device.Get() == b.device.Get() && a.presentMode == b.presentMode &&
           a.alphaMode == b.alphaMode &&
           a.viewFormatCount == b.viewFormatCount &&
           a.viewFormats == b.viewFormats && a.usage == b.usage;
}

int main() {
    glfwInit();

    static constexpr auto kTimedWaitAny =
        wgpu::InstanceFeatureName::TimedWaitAny;

    wgpu::InstanceDescriptor instanceDesc = {};
    instanceDesc.requiredFeatureCount = 1;
    instanceDesc.requiredFeatures = &kTimedWaitAny;

    ctx.instance = wgpu::CreateInstance(&instanceDesc);

    void* userdata = &ctx.adapter;
    wgpu::RequestAdapterOptions opts = {};

    auto callbackMode = wgpu::CallbackMode::WaitAnyOnly;
    auto adapterCallback = [](wgpu::RequestAdapterStatus status,
                              wgpu::Adapter adapter, wgpu::StringView message,
                              void* userdata) {
        if (status != wgpu::RequestAdapterStatus::Success) {
            //
        }
        *static_cast<wgpu::Adapter*>(userdata) = adapter;
    };

    wgpu::Future adapterFuture = ctx.instance.RequestAdapter(
        &opts, callbackMode, adapterCallback, userdata);
    ctx.instance.WaitAny(adapterFuture, UINT64_MAX);

    wgpu::DeviceDescriptor deviceDesc = {};
    deviceDesc.SetUncapturedErrorCallback([](const wgpu::Device& device,
                                             wgpu::ErrorType type,
                                             wgpu::StringView message) {
        //
    });

    auto deviceCallback = [](wgpu::RequestDeviceStatus status,
                             wgpu::Device device, wgpu::StringView message,
                             void* userdata) {
        if (status != wgpu::RequestDeviceStatus::Success) {
            ///
        }

        *static_cast<wgpu::Device*>(userdata) = device;
    };

    userdata = &ctx.device;
    wgpu::Future deviceFuture = ctx.adapter.RequestDevice(
        &deviceDesc, callbackMode, deviceCallback, userdata);
    ctx.instance.WaitAny(deviceFuture, UINT64_MAX);

    ctx.queue = ctx.device.GetQueue();

    data = addWindow(800, 600, "Main window");

    while (!glfwWindowShouldClose(data.window)) {
        glfwPollEvents();
        ctx.instance.ProcessEvents();

        if (!IsSameConfig(data.currentConfig, data.targetConfig)) {
            data.surface.Configure(&data.targetConfig);
            data.currentConfig = data.targetConfig;
        }
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();

    return EXIT_SUCCESS;
}
