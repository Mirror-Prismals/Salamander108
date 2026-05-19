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
    @location(1) normal: vec3<f32>,
    @location(2) texCoord: vec2<f32>,
    @location(3) posScale: vec4<i32>,
    @location(4) scaleTileFaceAo: vec4<i32>,
    @location(5) colorRgbPacked: i32,
};

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) texCoord: vec2<f32>,
    @location(1) fragColor: vec3<f32>,
    @location(2) instanceDistance: f32,
    @location(3) normal: vec3<f32>,
    @location(4) worldPos: vec3<f32>,
    @location(5) instanceCell: vec3<f32>,
    @location(6) @interpolate(flat) tileIndex: i32,
    @location(7) alpha: f32,
    @location(8) aoCorners: vec4<f32>,
    @location(9) screenUv: vec2<f32>,
    @location(10) localRectUv: vec2<f32>,
    @location(11) instanceScale: vec2<f32>,
    @location(12) @interpolate(flat) faceType: i32,
};

fn rotateY(v: vec3<f32>, r: f32) -> vec3<f32> {
    let c = cos(r);
    let s = sin(r);
    return vec3<f32>(
        c * v.x - s * v.z,
        v.y,
        s * v.x + c * v.z
    );
}

fn rotateX(v: vec3<f32>, r: f32) -> vec3<f32> {
    let c = cos(r);
    let s = sin(r);
    return vec3<f32>(
        v.x,
        c * v.y + s * v.z,
        -s * v.y + c * v.z
    );
}

fn unpackAo(encoded: i32) -> vec4<f32> {
    let bits = bitcast<u32>(encoded);
    return vec4<f32>(
        f32(bits & 255u),
        f32((bits >> 8u) & 255u),
        f32((bits >> 16u) & 255u),
        f32((bits >> 24u) & 255u)
    ) * (1.0 / 255.0);
}

fn unpackColor(encoded: i32) -> vec3<f32> {
    let bits = bitcast<u32>(encoded);
    return vec3<f32>(
        f32(bits & 255u),
        f32((bits >> 8u) & 255u),
        f32((bits >> 16u) & 255u)
    ) * (1.0 / 255.0);
}

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

@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;

    let faceType = input.scaleTileFaceAo.z;
    let offset = vec3<f32>(
        f32(input.posScale.x) * 0.5,
        f32(input.posScale.y) * 0.5,
        f32(input.posScale.z) * 0.5
    );
    let scale = vec2<f32>(
        f32(max(input.posScale.w, 1)),
        f32(max(input.scaleTileFaceAo.x, 1))
    );
    let baseTex = input.texCoord;

    var pos = input.position;
    var normal = input.normal;
    pos.x = pos.x * scale.x;
    pos.y = pos.y * scale.y;

    if (faceType == 0) {
        pos = rotateY(pos, 1.57079632679);
        normal = normalize(rotateY(normal, 1.57079632679));
    } else if (faceType == 1) {
        pos = rotateY(pos, -1.57079632679);
        normal = normalize(rotateY(normal, -1.57079632679));
    } else if (faceType == 2) {
        pos = rotateX(pos, -1.57079632679);
        normal = normalize(rotateX(normal, -1.57079632679));
    } else if (faceType == 3) {
        pos = rotateX(pos, 1.57079632679);
        normal = normalize(rotateX(normal, 1.57079632679));
    } else if (faceType == 5) {
        pos = rotateY(pos, 3.14159265359);
        normal = normalize(rotateY(normal, 3.14159265359));
    }

    if (faceType == 0 || faceType == 1) {
        pos.z = -pos.z;
        normal.z = -normal.z;
    }
    if (faceType == 2 || faceType == 3) {
        pos.x = -pos.x;
        normal.x = -normal.x;
    }

    let finalPos = pos + offset;
    let worldPos4 = u.model * vec4<f32>(finalPos, 1.0);
    let clipPos = u.projection * u.view * worldPos4;
    let warpedClipPos = applyProjectionWarp(clipPos);

    out.position = warpedClipPos;
    out.screenUv = vec2<f32>(
        warpedClipPos.x / warpedClipPos.w * 0.5 + 0.5,
        -warpedClipPos.y / warpedClipPos.w * 0.5 + 0.5
    );
    out.texCoord = baseTex * scale;
    out.fragColor = unpackColor(input.colorRgbPacked);
    out.instanceDistance = length(offset - u.cameraAndScale.xyz);
    out.normal = normalize((u.model * vec4<f32>(normal, 0.0)).xyz);
    out.worldPos = worldPos4.xyz;
    out.instanceCell = offset;
    out.tileIndex = input.scaleTileFaceAo.y;
    out.alpha = 1.0;
    out.aoCorners = unpackAo(input.scaleTileFaceAo.w);
    out.localRectUv = baseTex;
    out.instanceScale = scale;
    out.faceType = faceType;
    return out;
}
