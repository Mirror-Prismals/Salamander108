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
    @builtin(position) fragCoord: vec4<f32>,
    @location(0) uv: vec2<f32>,
};

const FAR: f32 = 28.0;
const RAIN_STEPS: i32 = 24;

fn saturate(x: f32) -> f32 {
    return clamp(x, 0.0, 1.0);
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

fn cameraRayFromUv(uv: vec2<f32>) -> vec3<f32> {
    let ndc = vec2<f32>(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    let tanX = abs(1.0 / max(abs(u.projection[0][0]), 0.0001));
    let tanY = abs(1.0 / max(abs(u.projection[1][1]), 0.0001));
    let cameraRight = normalize(u.model[0].xyz);
    let cameraUp = normalize(u.model[1].xyz);
    let cameraForward = normalize(-u.model[2].xyz);
    return normalize(cameraRight * ndc.x * tanX + cameraUp * ndc.y * tanY + cameraForward);
}

fn rainCell(pIn: vec3<f32>, cellSize: vec3<f32>, speed: f32, wind: vec2<f32>, density: f32) -> f32 {
    var q = pIn;
    q = vec3<f32>(q.x - wind.x * q.y, q.y, q.z - wind.y * q.y);
    q.y = q.y + u.params.x * speed;

    let g = q / cellSize;
    let id = floor(g);
    let f = fract(g) - vec3<f32>(0.5);

    let rnd = hash21(id.xz + vec2<f32>(id.y * 17.13));
    if (rnd < 1.0 - density) {
        return 0.0;
    }

    let offs = (hash22(id.xz + vec2<f32>(id.y * 31.7)) - vec2<f32>(0.5)) * 0.7;
    let radius = mix(0.012, 0.026, hash21(id.xz + vec2<f32>(id.y * 5.3)));
    let len = mix(0.22, 0.48, hash21(id.xz + vec2<f32>(id.y * 9.1)));

    let radial = smoothstep(radius, 0.0, length(f.xz - offs));
    let vertical = smoothstep(0.50, 0.10, f.y) *
        smoothstep(-len - 0.02, -len + 0.08, f.y);
    return radial * vertical;
}

fn rainField(p: vec3<f32>) -> f32 {
    var r = 0.0;
    r = r + rainCell(p, vec3<f32>(0.44, 1.75, 0.44), 6.8, vec2<f32>(0.14, 0.03), 0.72) * 1.00;
    r = r + rainCell(p + vec3<f32>(8.2, 0.0, -5.1), vec3<f32>(0.26, 1.10, 0.26), 10.5, vec2<f32>(0.18, 0.05), 0.48) * 0.55;
    return clamp(r, 0.0, 1.0);
}

fn marchRain(ro: vec3<f32>, rd: vec3<f32>, tMax: f32, jitter: f32) -> vec4<f32> {
    var accum = vec3<f32>(0.0);
    var trans = 1.0;

    for (var i: i32 = 0; i < RAIN_STEPS; i = i + 1) {
        let fi = (f32(i) + jitter) / f32(RAIN_STEPS);
        let t = mix(0.1, tMax, fi);
        let p = ro + rd * t;
        let r = rainField(p);

        if (r > 0.001) {
            let a = r * 0.30;
            let c = vec3<f32>(0.68, 0.74, 0.80) * r;
            accum = accum + trans * a * c;
            trans = trans * (1.0 - a);

            if (trans < 0.08) {
                break;
            }
        }
    }

    return vec4<f32>(accum, 1.0 - trans);
}

fn nearCameraStreaks(ro: vec3<f32>, rd: vec3<f32>, fragCoord: vec2<f32>, intensity: f32) -> f32 {
    var energy = 0.0;
    for (var i: i32 = 0; i < 8; i = i + 1) {
        let fi = (f32(i) + hash21(fragCoord + vec2<f32>(f32(i) * 13.17))) / 8.0;
        let t = mix(0.55, 7.5, fi);
        let p = ro + rd * t;
        energy = energy + rainField(p) * (1.0 - fi * 0.55);
    }

    let slantUv = fragCoord * vec2<f32>(0.018, 0.052) + vec2<f32>(u.params.x * 0.18, u.params.x * -3.8);
    let cell = floor(slantUv);
    let gv = fract(slantUv) - vec2<f32>(0.5);
    let rnd = hash21(cell);
    let dropOffset = hash21(cell + vec2<f32>(17.7, 3.1)) - 0.5;
    let thin = smoothstep(0.035, 0.0, abs(gv.x - dropOffset * 0.34));
    let lengthMask = smoothstep(0.50, -0.20, gv.y) * smoothstep(-0.55, -0.10, gv.y);
    let keep = smoothstep(0.34, 0.98, rnd);
    return clamp(energy * 0.22 + thin * lengthMask * keep * 0.72, 0.0, 1.0) * intensity;
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let intensity = clamp(u.extra.x, 0.0, 2.0);
    if (intensity <= 0.001) {
        discard;
    }

    let warpedUv = inverseProjectionWarpUv(input.uv);
    let uv = clamp(warpedUv, vec2<f32>(0.001), vec2<f32>(0.999));
    let rd = cameraRayFromUv(uv);
    let ro = u.cameraAndScale.xyz;
    let jitter = hash21(input.fragCoord.xy + vec2<f32>(u.params.x * 7.13));
    let rain = marchRain(ro, rd, FAR, jitter);
    let screenStreakWeight = step(0.5, u.extra.y);
    let nearStreak = nearCameraStreaks(ro, rd, input.fragCoord.xy, intensity) * screenStreakWeight;

    var alpha = clamp((rain.a * 1.35 + nearStreak * 0.58) * intensity, 0.0, 0.82);
    let resolution = max(u.vec2Data.zw, vec2<f32>(1.0));
    let centered = (uv - vec2<f32>(0.5)) * vec2<f32>(resolution.x / resolution.y, 1.0);
    let vig = smoothstep(1.35, 0.25, length(centered));
    alpha = alpha * vig;

    if (alpha <= 0.002) {
        discard;
    }

    let baseColor = max(rain.rgb / max(rain.a, 0.001), vec3<f32>(0.68, 0.74, 0.80) * nearStreak);
    let gammaColor = pow(clamp(baseColor, vec3<f32>(0.0), vec3<f32>(1.0)), vec3<f32>(0.4545));
    return vec4<f32>(gammaColor, alpha);
}
