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

struct FSIn {
    @builtin(position) position: vec4<f32>,
    @location(0) localRectUv: vec2<f32>,
};

fn packDepth(depth: f32) -> vec4<f32> {
    let clampedDepth = min(max(depth, 0.0), 0.99999994);
    var enc = fract(clampedDepth * vec4<f32>(1.0, 255.0, 65025.0, 16581375.0));
    enc = enc - enc.yzww * vec4<f32>(1.0 / 255.0, 1.0 / 255.0, 1.0 / 255.0, 0.0);
    return enc;
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    if (input.localRectUv.x < 0.0 || input.localRectUv.x > 1.0
        || input.localRectUv.y < 0.0 || input.localRectUv.y > 1.0) {
        discard;
    }
    return packDepth(input.position.z);
}
