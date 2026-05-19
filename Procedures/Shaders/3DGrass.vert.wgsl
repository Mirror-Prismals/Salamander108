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

const GRASS_H: f32 = 0.95;
const BLOCK_HALF: f32 = 0.5;
const SURFACE_EPSILON: f32 = 0.015;
const PATCH_HALF: f32 = 0.5;
const PROXY_PAD: f32 = 0.08;
const VOLUME_TOP_PAD: f32 = 0.08;

struct VSIn {
    @location(0) position: vec3<f32>,
    @location(1) cell: vec3<f32>,
};

struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) @interpolate(linear) screenUv: vec2<f32>,
    @location(1) @interpolate(flat) blockCell: vec3<f32>,
};

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

struct ProjectedBounds {
    ndcMin: vec2<f32>,
    ndcMax: vec2<f32>,
    depthMin: f32,
    valid: bool,
};

fn includeProjectedCorner(p: vec3<f32>, bounds: ProjectedBounds) -> ProjectedBounds {
    var b = bounds;
    let clip = applyProjectionWarp(u.projection * u.view * vec4<f32>(p, 1.0));
    if (clip.w <= 0.05) {
        return b;
    }

    let ndc = clip.xyz / clip.w;
    b.ndcMin = min(b.ndcMin, ndc.xy);
    b.ndcMax = max(b.ndcMax, ndc.xy);
    b.depthMin = min(b.depthMin, ndc.z);
    b.valid = true;
    return b;
}

@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;
    let rootY = input.cell.y + BLOCK_HALF + SURFACE_EPSILON + u.extra.w;
    let bmin = vec3<f32>(
        input.cell.x - PATCH_HALF - PROXY_PAD,
        rootY,
        input.cell.z - PATCH_HALF - PROXY_PAD
    );
    let bmax = vec3<f32>(
        input.cell.x + PATCH_HALF + PROXY_PAD,
        rootY + GRASS_H + VOLUME_TOP_PAD,
        input.cell.z + PATCH_HALF + PROXY_PAD
    );

    var bounds: ProjectedBounds;
    bounds.ndcMin = vec2<f32>(100000.0);
    bounds.ndcMax = vec2<f32>(-100000.0);
    bounds.depthMin = 1.0;
    bounds.valid = false;
    bounds = includeProjectedCorner(vec3<f32>(bmin.x, bmin.y, bmin.z), bounds);
    bounds = includeProjectedCorner(vec3<f32>(bmax.x, bmin.y, bmin.z), bounds);
    bounds = includeProjectedCorner(vec3<f32>(bmin.x, bmax.y, bmin.z), bounds);
    bounds = includeProjectedCorner(vec3<f32>(bmax.x, bmax.y, bmin.z), bounds);
    bounds = includeProjectedCorner(vec3<f32>(bmin.x, bmin.y, bmax.z), bounds);
    bounds = includeProjectedCorner(vec3<f32>(bmax.x, bmin.y, bmax.z), bounds);
    bounds = includeProjectedCorner(vec3<f32>(bmin.x, bmax.y, bmax.z), bounds);
    bounds = includeProjectedCorner(vec3<f32>(bmax.x, bmax.y, bmax.z), bounds);

    let pad = vec2<f32>(0.015);
    if (!bounds.valid || bounds.ndcMax.x < -1.15 || bounds.ndcMin.x > 1.15 || bounds.ndcMax.y < -1.15 || bounds.ndcMin.y > 1.15) {
        out.position = vec4<f32>(2.0, 2.0, 1.0, 1.0);
        out.screenUv = vec2<f32>(0.0);
        out.blockCell = input.cell;
        return out;
    }

    let ndcMin = clamp(bounds.ndcMin - pad, vec2<f32>(-1.15), vec2<f32>(1.15));
    let ndcMax = clamp(bounds.ndcMax + pad, vec2<f32>(-1.15), vec2<f32>(1.15));
    let corner = input.position.xy * 0.5 + vec2<f32>(0.5);
    let ndc = mix(ndcMin, ndcMax, corner);

    out.position = vec4<f32>(ndc, clamp(bounds.depthMin, 0.0, 1.0), 1.0);
    out.screenUv = ndc * 0.5 + vec2<f32>(0.5);
    out.blockCell = input.cell;
    return out;
}
