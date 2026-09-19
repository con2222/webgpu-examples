#include <context.hpp>
#include <iostream>
#include <math.hpp>
#include <vector>

static WebGPUContext ctx;
static WindowData data;

const char* shader = R"(
struct VertexOutput {
    @builtin(position) position : vec4f,
    @location(0) color : vec4f,
};

@vertex
fn main_vs(@builtin(vertex_index) v_idx: u32, @builtin(instance_index) i_idx: u32) -> VertexOutput {
    var pos = array<vec2f, 6>(
        vec2f(-0.5, -0.5), vec2f( 0.5, -0.5), vec2f( 0.5,  0.5),
        vec2f(-0.5, -0.5), vec2f( 0.5,  0.5), vec2f(-0.5,  0.5)
    );

    var offsets = array<vec2f, 2>(
        vec2f(-0.2, -0.2),
        vec2f( 0.2,  0.2)
    );

    // Straight (non-premultiplied) colors: RGB is not multiplied by alpha here.
    // Alpha = 0.5 controls the source contribution with our blend settings.
    // Alpha alone does not enable transparency; the pipeline must enable blending.
    var colors = array<vec4f, 2>(
        vec4f(1.0, 0.0, 0.0, 0.5),
        vec4f(0.0, 0.0, 1.0, 0.5)
    );

    var out: VertexOutput;
    out.position = vec4f(pos[v_idx] + offsets[i_idx], 0.0, 1.0);
    out.color = colors[i_idx];
    return out;
}

@fragment
fn main_fs(in: VertexOutput) -> @location(0) vec4f {
    // This is the source color. The pipeline blends it with the destination
    // already stored at this location in the color attachment.
    return in.color;
}
)";

void DoRender(WindowData* data, const wgpu::Device& device,
              const wgpu::Queue& queue, const wgpu::RenderPipeline& pipeline) {
    wgpu::SurfaceTexture surfaceTexture = {};
    data->surface.GetCurrentTexture(&surfaceTexture);
    wgpu::TextureView surfaceView = surfaceTexture.texture.CreateView();

    wgpu::RenderPassDescriptor passDesc = {};
    wgpu::RenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = surfaceView;
    colorAttachment.loadOp = wgpu::LoadOp::Clear;
    colorAttachment.storeOp = wgpu::StoreOp::Store;
    // Opaque black is the initial destination for blending each frame.
    colorAttachment.clearValue = wgpu::Color{0.0, 0.0, 0.0, 1.0};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    wgpu::CommandEncoder commandEncoder = device.CreateCommandEncoder();
    wgpu::RenderPassEncoder pass = commandEncoder.BeginRenderPass(&passDesc);

    pass.SetPipeline(pipeline);
    // ---------------------------------------------------------
    // Overlapping transparent geometry
    //
    // For blending, instance 0 (red) precedes instance 1 (blue).
    // This is the logical rendering order, not serial shader execution.
    // At the overlap, blue blends over the red already blended with black.
    // Swapping the two colors changes the alpha-blended overlap.
    // ---------------------------------------------------------
    pass.Draw(6, 2);
    pass.End();

    wgpu::CommandBuffer cmdBuffer = commandEncoder.Finish();
    queue.Submit(1, &cmdBuffer);

    wgpu::Status presentStatus = data->surface.Present();
    if (presentStatus != wgpu::Status::Success) {
        std::cout << "Present status failed\n";
    }
}

int main() {
    glfwInit();

    ctx.instance = createInstance();
    ctx.adapter = createAdapter(ctx.instance);
    ctx.device = createDevice(ctx.instance, ctx.adapter);
    ctx.queue = ctx.device.GetQueue();

    data = addWindow(800, 600, "Shadertoy Sandbox", ctx.instance, ctx.adapter,
                     ctx.device);

    wgpu::ShaderModule shaderModule = CreateShaderModule(ctx.device, shader);

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 0;
    pipelineLayoutDesc.bindGroupLayouts = nullptr;
    wgpu::PipelineLayout pipelineLayout =
        ctx.device.CreatePipelineLayout(&pipelineLayoutDesc);

    wgpu::RenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.depthStencil = nullptr;
    pipelineDesc.layout = pipelineLayout;

    pipelineDesc.vertex.buffers = nullptr;
    pipelineDesc.vertex.bufferCount = 0;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = "main_vs";

    pipelineDesc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    pipelineDesc.primitive.cullMode = wgpu::CullMode::None;
    pipelineDesc.primitive.stripIndexFormat = wgpu::IndexFormat::Undefined;
    pipelineDesc.primitive.frontFace = wgpu::FrontFace::CCW;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = 0xFFFFFFFF;
    pipelineDesc.multisample.alphaToCoverageEnabled = false;

    wgpu::ColorTargetState colorTarget = {};
    colorTarget.format = data.currentConfig.format;

    wgpu::BlendState blend = {};

    // ---------------------------------------------------------
    // Alpha blending
    //
    // Source: the RGBA value returned by the fragment shader.
    // Destination: the RGBA value already in the color attachment.
    // Each BlendComponent selects two factors and an operation.
    //
    // For RGB, Add with these factors gives:
    // result.rgb = source.rgb * source.a
    //            + destination.rgb * (1.0 - source.a)
    //
    // At the overlap: red over black gives (0.5, 0.0, 0.0),
    // then blue over that gives (0.25, 0.0, 0.5), before format conversion.
    // ---------------------------------------------------------
    blend.color.srcFactor = wgpu::BlendFactor::SrcAlpha;
    blend.color.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.color.operation = wgpu::BlendOperation::Add;

    // The alpha component uses a separate equation:
    // result.a = source.a + destination.a * (1.0 - source.a)
    // One avoids multiplying source.a by itself. With an opaque destination,
    // result.a stays 1.0 even though each shape has alpha = 0.5.
    blend.alpha.srcFactor = wgpu::BlendFactor::One;
    blend.alpha.dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha;
    blend.alpha.operation = wgpu::BlendOperation::Add;

    colorTarget.blend = &blend;

    // ---------------------------------------------------------
    // Compare blend modes
    //
    // Change these settings BEFORE CreateRenderPipeline, then run again.
    // An existing pipeline does not track changes to this descriptor.
    //
    // No blending: the source replaces the destination, including RGB
    // even when source.a is 0.5.
    // colorTarget.blend = nullptr;
    //
    // Additive: keep blending enabled and use this destination factor.
    // result.rgb = source.rgb * source.a + destination.rgb
    // The overlap adds light instead of attenuating the previous color.
    // blend.color.dstFactor = wgpu::BlendFactor::One;
    //
    // Keep the alpha component above for this opaque output target.
    // ---------------------------------------------------------

    wgpu::FragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = "main_fs";
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    wgpu::RenderPipeline pipeline =
        ctx.device.CreateRenderPipeline(&pipelineDesc);

    while (!glfwWindowShouldClose(data.window)) {
        glfwPollEvents();
        ctx.instance.ProcessEvents();
        SyncFromWindow(&data);

        if (!IsSameConfig(data.currentConfig, data.targetConfig)) {
            data.surface.Configure(&data.targetConfig);
            data.currentConfig = data.targetConfig;
        }

        DoRender(&data, ctx.device, ctx.queue, pipeline);
    }

    glfwDestroyWindow(data.window);
    glfwTerminate();
    return 0;
}
