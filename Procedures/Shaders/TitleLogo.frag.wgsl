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
var logoTex: texture_2d<f32>;

struct FSIn {
    @location(0) uv: vec2<f32>,
};

fn waveSlope(uv: vec2<f32>, direction: vec2<f32>, frequency: f32, phase: f32, strength: f32) -> vec2<f32> {
    return direction * cos(dot(uv, direction) * frequency + phase) * strength;
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let timeSeconds = u.params.x;
    let waveAmplitude = max(u.params.w, 0.0001);
    let waveFrequency = max(u.extra.x, 0.0001);
    let waveSpeed = u.extra.y;

    let slope =
        waveSlope(input.uv, vec2<f32>(0.86, 0.50), waveFrequency * 0.88, timeSeconds * waveSpeed * 0.74, 0.44) +
        waveSlope(input.uv, vec2<f32>(-0.34, 0.94), waveFrequency * 0.61, -timeSeconds * waveSpeed * 0.48, 0.36) +
        waveSlope(input.uv, vec2<f32>(0.18, 0.98), waveFrequency * 1.25, timeSeconds * waveSpeed * 0.31, 0.20) +
        waveSlope(input.uv, vec2<f32>(-0.78, 0.62), waveFrequency * 0.43, timeSeconds * waveSpeed * 0.22, 0.18);
    let microRipple = vec2<f32>(
        sin(dot(input.uv, vec2<f32>(9.0, 17.0)) + timeSeconds * waveSpeed * 1.35),
        cos(dot(input.uv, vec2<f32>(13.0, -7.0)) - timeSeconds * waveSpeed * 1.10)
    ) * 0.075;
    let lensing = 0.82 + 0.18 * sin(timeSeconds * 0.37 + input.uv.y * 1.8);
    let refract = (slope + microRipple) * waveAmplitude * lensing;
    let warpedUv = clamp(input.uv + refract, vec2<f32>(0.001, 0.001), vec2<f32>(0.999, 0.999));

    let texel = textureSample(logoTex, sceneSampler, warpedUv);
    let lightness = dot(texel.rgb, vec3<f32>(0.2126, 0.7152, 0.0722));
    let threshold = u.params.y;
    let softness = max(u.params.z, 0.0001);
    let mask = smoothstep(threshold, threshold + softness, lightness);
    let causticField =
        sin(dot(warpedUv, vec2<f32>(8.0, 15.0)) + timeSeconds * waveSpeed * 0.70) * 0.55 +
        sin(dot(warpedUv, vec2<f32>(-14.0, 9.0)) - timeSeconds * waveSpeed * 0.52) * 0.45;
    let caustic = 0.92 + 0.08 * smoothstep(0.15, 0.95, causticField * 0.5 + 0.5);
    let alpha = texel.a * mask * u.color.a * caustic;
    if (alpha <= 0.001) {
        discard;
    }
    let waterTint = vec3<f32>(0.95, 0.985, 1.0);
    return vec4<f32>(texel.rgb * waterTint * (0.97 + caustic * 0.03), alpha);
}
