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
    @location(0) position: vec3<f32>,
    @location(1) color: vec3<f32>,
};

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec3<f32>,
};

fn applyProjectionWarp(clip: vec4<f32>) -> vec4<f32> {
    let strength = clamp(u.projectionWarp.z, 0.0, 1.0);
    if (u.projectionFlags.x == 0 || strength <= 0.0001 || clip.w <= 0.0001) {
        return clip;
    }
    let ndc = clip.xy / clip.w;
    let d = max(u.projectionWarp.x, 0.05);
    let compression = clamp(u.projectionWarp.y, 0.0, 1.0);
    let zoom = max(u.projectionWarp.w, 0.25);
    let paniniScale = (d + 1.0) / (d + sqrt(ndc.x * ndc.x + 1.0));
    let panini = vec2<f32>(
        ndc.x * paniniScale,
        ndc.y * mix(1.0, paniniScale, compression)
    );
    let warped = mix(ndc, panini, strength) * zoom;
    return vec4<f32>(warped * clip.w, clip.z, clip.w);
}

@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;
    out.position = applyProjectionWarp(u.mvp * vec4<f32>(input.position, 1.0));
    out.color = input.color;
    return out;
}
