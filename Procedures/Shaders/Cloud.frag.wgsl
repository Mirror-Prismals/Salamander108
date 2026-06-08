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

fn saturate(x: f32) -> f32 {
    return clamp(x, 0.0, 1.0);
}

fn hash13(p: vec3<f32>) -> f32 {
    return fract(sin(dot(p, vec3<f32>(127.1, 311.7, 74.7))) * 43758.5453123);
}

fn hash21(p: vec2<f32>) -> f32 {
    var p3 = fract(vec3<f32>(p.x, p.y, p.x) * 0.1031);
    p3 = p3 + vec3<f32>(dot(p3, p3.yzx + vec3<f32>(33.33)));
    return fract((p3.x + p3.y) * p3.z);
}

fn valueNoise(p: vec3<f32>) -> f32 {
    let i = floor(p);
    let f = fract(p);
    let w = f * f * (vec3<f32>(3.0) - 2.0 * f);

    let n000 = hash13(i + vec3<f32>(0.0, 0.0, 0.0));
    let n100 = hash13(i + vec3<f32>(1.0, 0.0, 0.0));
    let n010 = hash13(i + vec3<f32>(0.0, 1.0, 0.0));
    let n110 = hash13(i + vec3<f32>(1.0, 1.0, 0.0));
    let n001 = hash13(i + vec3<f32>(0.0, 0.0, 1.0));
    let n101 = hash13(i + vec3<f32>(1.0, 0.0, 1.0));
    let n011 = hash13(i + vec3<f32>(0.0, 1.0, 1.0));
    let n111 = hash13(i + vec3<f32>(1.0, 1.0, 1.0));

    let x00 = mix(n000, n100, w.x);
    let x10 = mix(n010, n110, w.x);
    let x01 = mix(n001, n101, w.x);
    let x11 = mix(n011, n111, w.x);
    let y0 = mix(x00, x10, w.y);
    let y1 = mix(x01, x11, w.y);
    return mix(y0, y1, w.z);
}

fn fbm(pIn: vec3<f32>) -> f32 {
    var p = pIn;
    var amp = 0.56;
    var sum = 0.0;
    var norm = 0.0;
    for (var i: i32 = 0; i < 4; i = i + 1) {
        sum = sum + valueNoise(p) * amp;
        norm = norm + amp;
        p = p * 2.03 + vec3<f32>(17.1, 9.2, 11.7);
        amp = amp * 0.52;
    }
    return sum / max(norm, 0.0001);
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

fn cloudDensity(p: vec3<f32>, cloudBase: f32, thickness: f32, cloudScale: f32, densityMultiplier: f32) -> f32 {
    let q = p * 0.0015 * cloudScale + vec3<f32>(0.0, u.params.x * 0.008, 0.0);
    let r = p * 0.0045 * cloudScale + vec3<f32>(u.params.x * 0.015);
    let n1 = fbm(q);
    let n2 = fbm(r);
    let cover = smoothstep(0.43, 0.76, n1 * 0.72 + n2 * 0.28);
    let bottom = smoothstep(cloudBase, cloudBase + thickness * 0.24, p.y);
    let top = 1.0 - smoothstep(cloudBase + thickness * 0.62, cloudBase + thickness, p.y);
    let billow = smoothstep(0.18, 0.92, n2);
    return cover * bottom * top * (0.58 + billow * 0.42) * densityMultiplier;
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let warpedUv = inverseProjectionWarpUv(input.uv);
    let uv = clamp(warpedUv, vec2<f32>(0.001), vec2<f32>(0.999));
    let ro = u.cameraAndScale.xyz;
    let rd = cameraRayFromUv(uv);

    let cloudBase = u.params.y;
    let thickness = max(u.params.z, 1.0);
    let cloudScale = max(u.params.w, 0.01);
    let densityMultiplier = max(u.extra.x, 0.0);
    let lightMultiplier = max(u.extra.y, 0.0);
    let stepCount = i32(clamp(u.extra.z, 1.0, 128.0));
    let cloudRadius = max(u.vec2Data.x, 100.0);
    let fadeBand = clamp(u.vec2Data.y, 1.0, cloudRadius);
    let maxSkip = max(u.extra.w, 0.0);

    var tMin = 0.0;
    var tMax = cloudRadius;
    if (abs(rd.y) > 0.00001) {
        let t1 = (cloudBase - ro.y) / rd.y;
        let t2 = (cloudBase + thickness - ro.y) / rd.y;
        tMin = max(tMin, min(t1, t2));
        tMax = min(tMax, max(t1, t2));
    }
    if (tMax <= tMin) {
        discard;
    }

    let baseDt = (tMax - tMin) / f32(max(stepCount, 1));
    var t = max(tMin, 0.0) + baseDt * hash21(input.fragCoord.xy + vec2<f32>(u.params.x * 3.17));
    var transmittance = 1.0;
    var accum = vec3<f32>(0.0);
    let sunPhase = max(dot(normalize(u.lightAndGrid.xyz), -rd), 0.0);
    let cloudLight = vec3<f32>(1.0) * (0.55 + 0.45 * sunPhase) * lightMultiplier;

    for (var i: i32 = 0; i < 128; i = i + 1) {
        if (i >= stepCount || t > tMax || transmittance < 0.02) {
            break;
        }

        let probe = ro + rd * (t + baseDt * 0.5);
        let horizDist = length(probe.xz - ro.xz);
        if (horizDist > cloudRadius) {
            t = t + baseDt;
            continue;
        }

        let horizFade = 1.0 - smoothstep(cloudRadius - fadeBand, cloudRadius, horizDist);
        let fadeStart = cloudRadius * 0.45;
        let adaptFactor = 1.0 + smoothstep(fadeStart, cloudRadius, horizDist) * maxSkip;
        let dtAdaptive = baseDt * adaptFactor;
        let p = ro + rd * (t + dtAdaptive * 0.5);
        let density = cloudDensity(p, cloudBase, thickness, cloudScale, densityMultiplier) * horizFade;

        if (density > 0.0002) {
            let alpha = clamp(1.0 - exp(-density * dtAdaptive * 0.7), 0.0, 1.0);
            accum = accum + cloudLight * alpha * transmittance;
            transmittance = transmittance * (1.0 - alpha);
        }

        t = t + dtAdaptive;
    }

    let alphaOut = 1.0 - transmittance;
    if (alphaOut <= 0.001) {
        discard;
    }
    return vec4<f32>(accum, alphaOut);
}
