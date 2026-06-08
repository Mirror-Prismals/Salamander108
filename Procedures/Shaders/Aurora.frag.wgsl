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

struct FSIn {
    @location(0) ribbonUv: vec2<f32>,
    @location(1) worldPos: vec3<f32>,
};

fn sat(x: f32) -> f32 {
    return clamp(x, 0.0, 1.0);
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let uv = input.ribbonUv;
    let time = u.params.x;
    let seed = fract(abs(u.extra.y) + 0.001);
    let layer = u.params.w;
    let topFade = 1.0 - smoothstep(0.62, 1.0, uv.y);
    let bottomFade = smoothstep(0.0, 0.16, uv.y);
    let edgeFade = smoothstep(0.0, 0.035, uv.x) * (1.0 - smoothstep(0.965, 1.0, uv.x));
    let curtain = 0.58
        + 0.24 * sin(uv.x * 43.0 + time * 0.74 + seed * 8.0 + layer)
        + 0.12 * sin(uv.x * 91.0 - time * 1.18 + uv.y * 4.0 + seed * 13.0);
    let verticalPulse = pow(sat(1.0 - uv.y), 0.55);
    let layerLift = mix(1.0, 0.68, clamp(layer, 0.0, 1.0));
    let alpha = u.color.a * edgeFade * bottomFade * topFade * sat(curtain) * (0.38 + verticalPulse * 0.72) * layerLift;
    if (alpha <= 0.002) {
        discard;
    }

    let coolLift = vec3<f32>(0.55, 0.88, 1.0);
    let hotCore = vec3<f32>(1.0, 0.98, 0.78);
    let vein = smoothstep(0.60, 1.0, curtain) * verticalPulse;
    var rgb = mix(u.color.rgb, coolLift, 0.16 + 0.18 * uv.y);
    rgb = mix(rgb, hotCore, vein * 0.18);
    return vec4<f32>(rgb, alpha);
}
