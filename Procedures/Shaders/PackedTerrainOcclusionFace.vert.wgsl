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
    @location(0) localRectUv: vec2<f32>,
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

    var pos = input.position;
    pos.x = pos.x * scale.x;
    pos.y = pos.y * scale.y;

    if (faceType == 0) {
        pos = rotateY(pos, 1.57079632679);
    } else if (faceType == 1) {
        pos = rotateY(pos, -1.57079632679);
    } else if (faceType == 2) {
        pos = rotateX(pos, -1.57079632679);
    } else if (faceType == 3) {
        pos = rotateX(pos, 1.57079632679);
    } else if (faceType == 5) {
        pos = rotateY(pos, 3.14159265359);
    }

    if (faceType == 0 || faceType == 1) {
        pos.z = -pos.z;
    }
    if (faceType == 2 || faceType == 3) {
        pos.x = -pos.x;
    }

    let worldPos = offset + pos;
    out.position = u.mvp * vec4<f32>(worldPos, 1.0);
    out.localRectUv = input.texCoord;
    return out;
}
