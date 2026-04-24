struct Uniforms {
    model: mat4x4<f32>,
    view: mat4x4<f32>,
    projection: mat4x4<f32>,
    mvp: mat4x4<f32>,
    color: vec4<f32>,
    topColor: vec4<f32>,
    bottomColor: vec4<f32>,
    params: vec4<f32>,
    vec2Data: vec4<f32>,
    extra: vec4<f32>,
    cameraAndScale: vec4<f32>,
    lightAndGrid: vec4<f32>,
    ambientAndLeaf: vec4<f32>,
    diffuseAndWater: vec4<f32>,
    atlasInfo: vec4<f32>,
    wallStoneAndWater2: vec4<f32>,
    intParams0: vec4<i32>,
    intParams1: vec4<i32>,
    intParams2: vec4<i32>,
    intParams3: vec4<i32>,
    intParams4: vec4<i32>,
    intParams5: vec4<i32>,
    intParams6: vec4<i32>,
    blockDamageCells: array<vec4<i32>, 64>,
    blockDamageProgress: array<vec4<f32>, 16>,
};

@group(0) @binding(0)
var<uniform> u: Uniforms;
@group(0) @binding(1)
var sceneSampler: sampler;
@group(0) @binding(2)
var sceneTexture: texture_2d<f32>;

struct FSIn {
    @location(0) uv: vec2<f32>,
};

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let uv = clamp(input.uv, vec2<f32>(0.001, 0.001), vec2<f32>(0.999, 0.999));
    return vec4<f32>(textureSample(sceneTexture, sceneSampler, uv).rgb, 1.0);
}
