#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

#include <cstdlib>
#include <iostream>

struct WindowData {
    GLFWwindow* window;

    wgpu::Surface surface;
    wgpu::SurfaceConfiguration currentConfig;
    wgpu::SurfaceConfiguration targetConfig;
};

static WindowData* data{};

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

    /* 2
    adapter.CreateDevice(&deviceDescriptor);
    */

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

    data->currentConfig = config;
    data->targetConfig = config;
    data->window = window;
    data->surface = surface;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();

    return EXIT_SUCCESS;
}
