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
    @location(0) uv: vec2<f32>,
};

fn paniniForwardX(x: f32, d: f32, strength: f32) -> f32 {
    let paniniScale = (d + 1.0) / (d + sqrt(x * x + 1.0));
    return mix(x, x * paniniScale, strength);
}

fn inverseProjectionWarp(ndcIn: vec2<f32>) -> vec2<f32> {
    let strength = clamp(u.projectionWarp.z, 0.0, 1.0);
    if (u.projectionFlags.x == 0 || strength <= 0.0001) {
        return ndcIn;
    }

    let d = max(u.projectionWarp.x, 0.05);
    let compression = clamp(u.projectionWarp.y, 0.0, 1.0);
    let zoom = max(u.projectionWarp.w, 0.25);
    let warped = ndcIn / zoom;

    var x = warped.x;
    for (var i = 0; i < 5; i = i + 1) {
        let e = max(abs(x) * 0.001, 0.0005);
        let f = paniniForwardX(x, d, strength) - warped.x;
        let df = (paniniForwardX(x + e, d, strength) - paniniForwardX(x - e, d, strength)) / (2.0 * e);
        x = x - f / max(abs(df), 0.0001) * sign(df);
    }

    let paniniScale = (d + 1.0) / (d + sqrt(x * x + 1.0));
    let verticalScale = mix(1.0, mix(1.0, paniniScale, compression), strength);
    return vec2<f32>(x, warped.y / max(verticalScale, 0.0001));
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let ndc = inverseProjectionWarp(vec2<f32>(
        input.uv.x * 2.0 - 1.0,
        input.uv.y * 2.0 - 1.0
    ));
    let projX = max(abs(u.projection[0].x), 0.0001);
    let projY = max(abs(u.projection[1].y), 0.0001);
    let viewRay = normalize(vec3<f32>(ndc.x / projX, ndc.y / projY, -1.0));
    let viewRot = mat3x3<f32>(u.view[0].xyz, u.view[1].xyz, u.view[2].xyz);
    let worldRay = normalize(transpose(viewRot) * viewRay);

    let horizonColor = clamp(mix(u.bottomColor.rgb, u.topColor.rgb, 0.5), vec3<f32>(0.0), vec3<f32>(1.0));
    let upMix = pow(clamp(max(worldRay.y, 0.0), 0.0, 1.0), 0.70);
    let downMix = pow(clamp(max(-worldRay.y, 0.0), 0.0, 1.0), 0.80);

    var color = horizonColor;
    if (worldRay.y >= 0.0) {
        color = mix(horizonColor, u.topColor.rgb, upMix);
    } else {
        color = mix(horizonColor, u.bottomColor.rgb, downMix);
    }
    return vec4<f32>(color, 1.0);
}
