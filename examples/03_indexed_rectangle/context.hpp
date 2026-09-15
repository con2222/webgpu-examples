#ifndef CONTEXT_HPP
#define CONTEXT_HPP

#include <GLFW/glfw3.h>
#include <webgpu/webgpu_cpp.h>
#include <webgpu/webgpu_cpp_print.h>

struct WindowData {
    GLFWwindow* window;

    wgpu::Surface surface;
    wgpu::SurfaceConfiguration currentConfig;
    wgpu::SurfaceConfiguration targetConfig;
};

struct WebGPUContext {
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
    wgpu::Queue queue;
};

struct vec4 {
    union {
        struct {
            float x, y, z, w;
        };
        float data[4];
    };
};

struct VertexAttribute {
    vec4 position;
    vec4 color;
};

wgpu::ShaderModule CreateShaderModule(const wgpu::Device& device,
                                      const char* source);
wgpu::ShaderModule CreateShaderModule(const wgpu::Device& device,
                                      const std::string& source);

void SyncFromWindow(WindowData* data);
bool IsSameConfig(wgpu::SurfaceConfiguration& a, wgpu::SurfaceConfiguration& b);

wgpu::Instance createInstance();
wgpu::Adapter createAdapter(const wgpu::Instance& instance);
wgpu::Device createDevice(const wgpu::Instance& instance,
                          const wgpu::Adapter& adapter);

WindowData addWindow(int width, int height, const char* label,
                     const wgpu::Instance& instance,
                     const wgpu::Adapter& adapter, const wgpu::Device& device);

#endif
