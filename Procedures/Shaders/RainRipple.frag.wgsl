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
    @location(0) texCoord: vec2<f32>,
    @location(1) shadingNormal: vec3<f32>,
    @location(2) worldPos: vec3<f32>,
    @location(3) screenUv: vec2<f32>,
};

fn rot(a: f32) -> mat2x2<f32> {
    let s = sin(a);
    let c = cos(a);
    return mat2x2<f32>(
        vec2<f32>(c, -s),
        vec2<f32>(s, c)
    );
}

fn hash21(p: vec2<f32>) -> f32 {
    var p3 = fract(vec3<f32>(p.x, p.y, p.x) * 0.1031);
    p3 = p3 + vec3<f32>(dot(p3, p3.yzx + vec3<f32>(33.33)));
    return fract((p3.x + p3.y) * p3.z);
}

fn hash22(p: vec2<f32>) -> vec2<f32> {
    let n = hash21(p);
    return vec2<f32>(n, hash21(p + vec2<f32>(n + 19.19)));
}

fn noise(p: vec2<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let u2 = f * f * (vec2<f32>(3.0) - 2.0 * f);

    let a = hash21(i);
    let b = hash21(i + vec2<f32>(1.0, 0.0));
    let c = hash21(i + vec2<f32>(0.0, 1.0));
    let d = hash21(i + vec2<f32>(1.0, 1.0));

    return mix(mix(a, b, u2.x), mix(c, d, u2.x), u2.y);
}

fn fbm(pIn: vec2<f32>) -> f32 {
    var p = pIn;
    var v = 0.0;
    var a = 0.5;

    for (var i: i32 = 0; i < 3; i = i + 1) {
        v = v + a * noise(p);
        p = rot(0.6) * p * 2.0;
        a = a * 0.5;
    }

    return v;
}

fn puddleMask(p: vec2<f32>) -> f32 {
    let n = fbm(p * 0.24) + noise(p * 1.0) * 0.18;
    return smoothstep(0.58, 0.78, n);
}

fn rippleCell(p: vec2<f32>, t: f32) -> f32 {
    let id = floor(p);
    let gv = fract(p) - vec2<f32>(0.5);

    let rnd = hash21(id);
    let pos = hash22(id) - vec2<f32>(0.5);

    let age = fract(t + rnd);
    let d = length(gv - pos);
    let radius = age * 0.55;
    var ring = smoothstep(0.028, 0.0, abs(d - radius));

    ring = ring * smoothstep(0.0, 0.12, age);
    ring = ring * smoothstep(1.0, 0.45, age);
    return ring;
}

fn ripples(p: vec2<f32>) -> f32 {
    let t = u.params.x * 1.45;
    var r = 0.0;
    r = r + rippleCell(p * 1.8, t);
    r = r + rippleCell(p * 3.3 + vec2<f32>(11.2), t * 1.23) * 0.65;
    return r;
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let intensity = clamp(u.extra.x * max(u.extra.y, 0.0), 0.0, 2.0);
    if (intensity <= 0.001) {
        discard;
    }

    let p = input.worldPos.xz;
    let ripple = ripples(p);
    let mask = max(0.42, puddleMask(p));
    let viewDist = length(input.worldPos - u.cameraAndScale.xyz);
    let distFade = 1.0 - smoothstep(96.0, 180.0, viewDist);
    let viewDir = normalize(u.cameraAndScale.xyz - input.worldPos);
    let grazing = pow(1.0 - max(dot(viewDir, normalize(input.shadingNormal)), 0.0), 2.0);

    var alpha = clamp(ripple * mask * (0.22 + grazing * 0.18) * distFade * intensity, 0.0, 0.42);
    if (alpha <= 0.002) {
        discard;
    }

    let coldRain = vec3<f32>(0.68, 0.74, 0.80);
    let impactTint = coldRain + vec3<f32>(0.12, 0.15, 0.18) * ripple * 0.25;
    return vec4<f32>(impactTint, alpha);
}
