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
    projectionWarp: vec4<f32>,
    projectionFlags: vec4<i32>,
};

@group(0) @binding(0)
var<uniform> u: Uniforms;

struct VSIn {
    @location(0) position: vec2<f32>,
    @location(1) uv: vec2<f32>,
};

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;
    let timeSeconds = u.params.x;
    let waveAmplitudeNdc = u.params.w;
    let waveFrequency = u.extra.x;
    let waveSpeed = u.extra.y;
    let surfaceDrift =
        sin(dot(input.uv, vec2<f32>(0.75, 1.00)) * waveFrequency * 0.12 + timeSeconds * waveSpeed * 0.22) * 0.45 +
        sin(dot(input.uv, vec2<f32>(-0.35, 1.00)) * waveFrequency * 0.16 - timeSeconds * waveSpeed * 0.17) * 0.35 +
        sin((input.uv.x - input.uv.y) * waveFrequency * 0.08 + timeSeconds * 0.19) * 0.20;
    let edgeCurl = (input.uv.y - 0.5) * surfaceDrift * waveAmplitudeNdc * 0.12;
    out.position = vec4<f32>(
        input.position.x + edgeCurl,
        input.position.y + surfaceDrift * waveAmplitudeNdc * 0.025,
        0.0,
        1.0
    );
    out.uv = input.uv;
    return out;
}
