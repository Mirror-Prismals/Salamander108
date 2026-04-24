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
@group(0) @binding(1)
var sceneSampler: sampler;
@group(0) @binding(2)
var sceneColorTexture: texture_2d<f32>;
@group(0) @binding(3)
var reflectionTexture: texture_2d<f32>;

struct FSIn {
    @location(0) texCoord: vec2<f32>,
    @location(1) shadingNormal: vec3<f32>,
    @location(2) axisNormal: vec3<f32>,
    @location(3) worldPos: vec3<f32>,
    @location(4) screenUv: vec2<f32>,
    @location(5) waveClass: f32,
    @location(6) metrics: vec4<f32>,
    @builtin(front_facing) frontFacing: bool,
};

fn channelAt(v: vec3<f32>, idx: i32) -> f32 {
    if (idx == 0) {
        return v.x;
    }
    if (idx == 1) {
        return v.y;
    }
    return v.z;
}

fn orientNormal(n: vec3<f32>, viewDir: vec3<f32>) -> vec3<f32> {
    return select(n, -n, dot(n, -viewDir) < 0.0);
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let surfacePass = u.intParams0.x != 0;
    let planarReflectionEnabled = (u.intParams6.y & 4) != 0;
    let resolution = max(u.vec2Data.zw, vec2<f32>(1.0, 1.0));
    let cameraPos = u.cameraAndScale.xyz;
    let lightDir = normalize(u.lightAndGrid.xyz);

    let rayDir = normalize(input.worldPos - cameraPos);
    let axisNormal = orientNormal(normalize(input.axisNormal), rayDir);
    let shadingNormal = orientNormal(normalize(input.shadingNormal), rayDir);

    var refrDirMid = refract(rayDir, shadingNormal, 1.0 / 1.33);
    if (length(refrDirMid) < 0.1) {
        refrDirMid = reflect(rayDir, shadingNormal);
    }

    let bodyDenom = max(abs(dot(refrDirMid, axisNormal)), 0.12);
    let surfaceDenom = max(abs(refrDirMid.y), 0.12);
    var dIn = select(
        input.metrics.x / bodyDenom,
        input.metrics.y / surfaceDenom,
        surfacePass
    );
    dIn = clamp(dIn, 0.05, 24.0);

    let absorb = exp(-dIn * vec3<f32>(0.5, 0.25, 0.1));
    let iors = array<f32, 3>(1.31, 1.33, 1.35);
    var refractedRgb = array<f32, 3>(0.0, 0.0, 0.0);
    let baseUv = clamp(input.screenUv, vec2<f32>(0.001, 0.001), vec2<f32>(0.999, 0.999));

    for (var i: i32 = 0; i < 3; i = i + 1) {
        var refrDir = refract(rayDir, shadingNormal, 1.0 / iors[i]);
        if (length(refrDir) < 0.1) {
            refrDir = reflect(rayDir, shadingNormal);
        }
        let refrDirView = normalize((u.view * vec4<f32>(refrDir, 0.0)).xyz);
        let refractPixels = clamp(dIn * 22.0, 1.0, 96.0);
        let uv = clamp(baseUv + refrDirView.xy * (refractPixels / resolution), vec2<f32>(0.001, 0.001), vec2<f32>(0.999, 0.999));
        let localCol = textureSample(sceneColorTexture, sceneSampler, uv).rgb;
        refractedRgb[i] = channelAt(localCol, i) * channelAt(absorb, i) + (1.0 - channelAt(absorb, i)) * 0.15;
    }

    let refractedCol = vec3<f32>(refractedRgb[0], refractedRgb[1], refractedRgb[2]);
    let reflected = reflect(rayDir, shadingNormal);
    let reflectedView = normalize((u.view * vec4<f32>(reflected, 0.0)).xyz);
    var skyCol = mix(
        mix(u.bottomColor.rgb, vec3<f32>(0.05, 0.1, 0.2), 0.55),
        mix(u.topColor.rgb, vec3<f32>(0.5, 0.7, 1.0), 0.35),
        max(0.0, reflected.y)
    );
    if (planarReflectionEnabled) {
        let reflectionUv = clamp(baseUv + reflectedView.xy * 0.03, vec2<f32>(0.001, 0.001), vec2<f32>(0.999, 0.999));
        let reflectionCol = textureSample(reflectionTexture, sceneSampler, reflectionUv).rgb;
        skyCol = mix(skyCol, reflectionCol, 0.6);
    }

    let fresnel = pow(1.0 - max(dot(-rayDir, shadingNormal), 0.0), 5.0);
    let spec = pow(max(dot(reflect(rayDir, shadingNormal), lightDir), 0.0), 128.0);
    let col = mix(refractedCol, skyCol, fresnel) + vec3<f32>(spec * 0.5);
    return vec4<f32>(col, 1.0);
}
