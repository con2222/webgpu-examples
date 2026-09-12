
struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
};

struct VertexOutput {
    @builtin(position) position : vec4f,
    @location(1) color : vec4f,
};


@vertex
fn main_vs(in: VertexInput) -> VertexOutput {
    return vec4f(position.xyz, 1.0);
}

@fragment
fn main_fs(in: VertexOutput) -> @location(0) vec4<f32> {
    return in.color;
}