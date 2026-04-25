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
@group(0) @binding(1)
var sceneSampler: sampler;
@group(0) @binding(2)
var sceneTexture: texture_2d<f32>;

struct FSIn {
    @location(0) uv: vec2<f32>,
};

fn inverseProjectionWarpUv(uv: vec2<f32>) -> vec2<f32> {
    let strength = clamp(u.projectionWarp.z, 0.0, 1.0);
    if (u.projectionFlags.x == 0 || strength <= 0.0001) {
        return uv;
    }

    let d = max(u.projectionWarp.x, 0.05);
    let compression = clamp(u.projectionWarp.y, 0.0, 1.0);
    let edgeScale = (d + 1.0) / (d + sqrt(2.0));
    let fitScale = mix(1.0, edgeScale, strength);
    let zoom = max(max(u.projectionWarp.w, 0.25), 1.002 / max(fitScale, 0.001));
    let targetNdc = ((uv * 2.0) - vec2<f32>(1.0, 1.0)) / zoom;

    var x = targetNdc.x;
    for (var i: i32 = 0; i < 6; i = i + 1) {
        let paniniScale = (d + 1.0) / (d + sqrt(x * x + 1.0));
        let xScale = mix(1.0, paniniScale, strength);
        x = targetNdc.x / max(xScale, 0.001);
    }

    let paniniScale = (d + 1.0) / (d + sqrt(x * x + 1.0));
    let verticalScale = mix(1.0, paniniScale, compression);
    let yScale = mix(1.0, verticalScale, strength);
    let sourceNdc = vec2<f32>(x, targetNdc.y / max(yScale, 0.001));
    return sourceNdc * 0.5 + vec2<f32>(0.5, 0.5);
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let warpedUv = inverseProjectionWarpUv(input.uv);
    let uv = clamp(warpedUv, vec2<f32>(0.001, 0.001), vec2<f32>(0.999, 0.999));
    return vec4<f32>(textureSample(sceneTexture, sceneSampler, uv).rgb, 1.0);
}
