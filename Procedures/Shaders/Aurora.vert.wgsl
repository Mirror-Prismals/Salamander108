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
    @location(1) ribbonU: f32,
    @location(2) ribbonV: f32,
};

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) ribbonUv: vec2<f32>,
    @location(1) worldPos: vec3<f32>,
};

const TAU: f32 = 6.28318530718;

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

fn sRawAt(ribbonU: f32, bend: f32, seed: f32) -> f32 {
    let a1 = min(bend * 0.0015, 0.8);
    let a2 = a1 * 0.55;
    let freq1 = 4.0 + seed * 6.0;
    let freq2 = freq1 * 1.6;
    let phi = seed * TAU;
    return ribbonU
        + a1 * sin(ribbonU * freq1 * TAU + phi)
        + a2 * sin(ribbonU * freq2 * TAU + phi * 1.37);
}

fn centerAt(ribbonU: f32, bend: f32, seed: f32, timeSeconds: f32, layer: f32) -> vec2<f32> {
    let s0 = sRawAt(0.0, bend, seed);
    let s1 = sRawAt(1.0, bend, seed);
    var denom = s1 - s0;
    if (abs(denom) < 0.0001) {
        denom = 0.0001;
    }
    let sNorm = (sRawAt(ribbonU, bend, seed) - s0) / denom;
    let lowA = 0.6 * bend * 0.0025;
    let lowFreq = 0.8 + seed * 0.7;
    let phase = seed * 3.0 + layer * 0.91;
    let longX = (sNorm - 0.5)
        + lowA * (
            sin(sNorm * lowFreq * TAU + timeSeconds * 0.014 + phase)
            + 0.4 * sin(sNorm * (lowFreq * 1.7) * TAU + timeSeconds * 0.012 + phase * 0.7)
        );
    let medA = 0.18 * bend * 0.002;
    let med = medA * sin(sNorm * TAU * (2.3 + seed * 1.8) + timeSeconds * 0.049 + layer);
    let hi = 0.06 * sin(sNorm * TAU * (12.0 + seed * 8.0) + timeSeconds * 0.084 + layer * 1.7);
    let centerX = longX + med + hi;
    let centerZ = (
        0.4 * sin(sNorm * TAU * (1.1 + seed * 0.6) + timeSeconds * 0.021 + layer)
        + 0.25 * cos(sNorm * TAU * (0.7 + seed * 0.3) + timeSeconds * 0.013)
    ) * (1.0 + seed * 0.6);
    return vec2<f32>(centerX, centerZ);
}

@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;
    let ribbonU = input.ribbonU;
    let ribbonV = input.ribbonV;
    let bend = max(u.extra.x, 1.0);
    let seed = fract(abs(u.extra.y) + 0.001);
    let layer = u.params.w;
    let center = centerAt(ribbonU, bend, seed, u.params.x, layer);
    let verticalShear = (ribbonV - 0.5) * 0.035
        * sin(ribbonU * TAU * (3.0 + seed * 2.0) + u.params.x * 0.18 + layer);
    let local = vec3<f32>(center.x + verticalShear, ribbonV, center.y);
    let world = u.model * vec4<f32>(local, 1.0);
    out.position = applyProjectionWarp(u.projection * u.view * world);
    out.ribbonUv = vec2<f32>(input.ribbonU, input.ribbonV);
    out.worldPos = world.xyz;
    return out;
}
