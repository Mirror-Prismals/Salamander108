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
    @location(3) offset: vec3<f32>,
    @location(4) waveClass: f32,
    @location(5) metrics: vec4<f32>,
    @location(6) scale: vec2<f32>,
    @location(7) uvScale: vec2<f32>,
};

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) texCoord: vec2<f32>,
    @location(1) shadingNormal: vec3<f32>,
    @location(2) axisNormal: vec3<f32>,
    @location(3) worldPos: vec3<f32>,
    @location(4) screenUv: vec2<f32>,
    @location(5) waveClass: f32,
    @location(6) metrics: vec4<f32>,
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

fn waterRot(a: f32) -> mat2x2<f32> {
    let c = cos(a);
    let s = sin(a);
    return mat2x2<f32>(
        vec2<f32>(c, s),
        vec2<f32>(-s, c)
    );
}

fn getWaves(p: vec2<f32>, time: f32) -> f32 {
    let t = time * 1.2;
    var w = 0.0;
    let amp = 0.05;
    let freq = 2.5;
    var dir = vec2<f32>(1.0, 0.7);

    let phase1 = dot(p, dir) * freq + t;
    w = w + amp * pow(sin(phase1) * 0.5 + 0.5, 2.0);

    dir = waterRot(1.5) * dir;
    let phase2 = dot(p, dir) * freq * 1.8 + t * 1.3;
    w = w + amp * 0.5 * pow(sin(phase2) * 0.5 + 0.5, 1.5);

    dir = waterRot(2.2) * dir;
    let phase3 = dot(p, dir) * freq * 3.5 + t * 2.0;
    w = w + amp * 0.2 * sin(phase3);

    return w;
}

fn waveAmplitude(waveClass: i32) -> f32 {
    if (waveClass == 1) {
        return 0.78;
    }
    if (waveClass == 3) {
        return 1.12;
    }
    if (waveClass == 4) {
        return 1.32;
    }
    return 1.0;
}

fn waveNormal(p: vec2<f32>, time: f32, scale: f32) -> vec3<f32> {
    let e = 0.035;
    let center = getWaves(p, time);
    let dx = getWaves(p + vec2<f32>(e, 0.0), time) - center;
    let dz = getWaves(p + vec2<f32>(0.0, e), time) - center;
    return normalize(vec3<f32>(-dx * scale / e, 1.0, -dz * scale / e));
}

@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;

    let faceType = u.intParams1.x;
    let surfacePass = u.intParams0.x != 0;
    let time = u.params.x;

    var localPos = input.position;
    localPos.x = localPos.x * input.scale.x;
    localPos.y = localPos.y * input.scale.y;

    var axisNormal = input.normal;
    if (faceType == 0) {
        localPos = rotateY(localPos, 1.57079632679);
        axisNormal = normalize(rotateY(axisNormal, 1.57079632679));
    } else if (faceType == 1) {
        localPos = rotateY(localPos, -1.57079632679);
        axisNormal = normalize(rotateY(axisNormal, -1.57079632679));
    } else if (faceType == 2) {
        localPos = rotateX(localPos, -1.57079632679);
        axisNormal = normalize(rotateX(axisNormal, -1.57079632679));
    } else if (faceType == 3) {
        localPos = rotateX(localPos, 1.57079632679);
        axisNormal = normalize(rotateX(axisNormal, 1.57079632679));
    } else if (faceType == 5) {
        localPos = rotateY(localPos, 3.14159265359);
        axisNormal = normalize(rotateY(axisNormal, 3.14159265359));
    }

    var worldPos = input.offset + localPos;
    var shadingNormal = axisNormal;
    if (surfacePass && faceType == 2) {
        let waveScale = waveAmplitude(i32(round(input.waveClass)));
        worldPos.y = worldPos.y + getWaves(worldPos.xz, time) * waveScale;
        shadingNormal = waveNormal(worldPos.xz, time, waveScale);
    }

    let clipPos = u.projection * u.view * u.model * vec4<f32>(worldPos, 1.0);
    out.position = clipPos;
    out.texCoord = input.texCoord * input.uvScale;
    out.shadingNormal = shadingNormal;
    out.axisNormal = axisNormal;
    out.worldPos = worldPos;
    out.screenUv = clipPos.xy / clipPos.w * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5, 0.5);
    out.waveClass = input.waveClass;
    out.metrics = input.metrics;
    return out;
}
