// 3D grass art-test pass.
// The blade math is kept close to temp/grass_3d.glsl; the camera and patch source
// are adapted to Cardinal world-space cells.

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

const PI: f32 = 3.14159265359;
const CELL: f32 = 0.075;
const GRASS_H: f32 = 0.95;
const BLOCK_HALF: f32 = 0.5;
const SURFACE_EPSILON: f32 = 0.015;
const PATCH_BLOCKS: f32 = 1.0;
const PATCH_HALF: f32 = PATCH_BLOCKS * 0.5;
const VOLUME_TOP_PAD: f32 = 0.08;

const GRASS_STEPS: i32 = 8;
const CANDIDATES: i32 = 1;
const MAX_GRASS_CELLS: i32 = 64;
const VOLUME_ATTEMPTS: i32 = 6;

fn sat(x: f32) -> f32 { return clamp(x, 0.0, 1.0); }

fn grassSurfaceYFromCellY(cellY: f32) -> f32 {
    return cellY + BLOCK_HALF + SURFACE_EPSILON;
}

fn grassRootYFromCellY(cellY: f32) -> f32 {
    return grassSurfaceYFromCellY(cellY) + u.extra.w;
}

fn grassVisibleMinYFromCellY(cellY: f32) -> f32 {
    return grassRootYFromCellY(cellY);
}

fn grassVisibleMaxYFromCellY(cellY: f32) -> f32 {
    return grassRootYFromCellY(cellY) + GRASS_H + VOLUME_TOP_PAD;
}

fn hash12(p: vec2<f32>) -> f32 {
    var p3 = fract(vec3<f32>(p.x, p.y, p.x) * 0.1031);
    p3 = p3 + vec3<f32>(dot(p3, p3.yzx + vec3<f32>(33.33)));
    return fract((p3.x + p3.y) * p3.z);
}

fn hash22(p: vec2<f32>) -> vec2<f32> {
    return vec2<f32>(hash12(p), hash12(p + vec2<f32>(19.19)));
}

fn windAt(p: vec3<f32>) -> vec3<f32> {
    let d0 = normalize(vec2<f32>(0.82, 0.28));
    let d1 = normalize(vec2<f32>(-0.33, 0.94));

    let time = u.params.x;
    let a = sin(dot(p.xz, d0) * 3.4 - time * 2.0);
    let b = sin(dot(p.xz, d1) * 6.5 - time * 3.2 + sin(p.x * 1.5));
    let c = sin((p.x - p.z) * 10.0 - time * 4.5);

    var s = 0.35 + 0.18 * a + 0.12 * b + 0.05 * c;
    s = max(s, 0.04);

    return normalize(vec3<f32>(d0.x, 0.035 * a, d0.y)) * s;
}

fn bezier2(a: vec3<f32>, b: vec3<f32>, c: vec3<f32>, t: f32) -> vec3<f32> {
    let x = mix(a, b, t);
    let y = mix(b, c, t);
    return mix(x, y, t);
}

fn bezierTangent(a: vec3<f32>, b: vec3<f32>, c: vec3<f32>, t: f32) -> vec3<f32> {
    return normalize(mix(b - a, c - b, t));
}

fn candidateOffset(k: i32) -> vec2<f32> {
    var o = vec2<f32>(0.0);
    if (k == 1) { o = vec2<f32>( 1.0,  0.0); }
    if (k == 2) { o = vec2<f32>(-1.0,  0.0); }
    if (k == 3) { o = vec2<f32>( 0.0,  1.0); }
    if (k == 4) { o = vec2<f32>( 0.0, -1.0); }
    return o;
}

fn rayBox(ro: vec3<f32>, rd: vec3<f32>, bmin: vec3<f32>, bmax: vec3<f32>) -> vec2<f32> {
    var t0 = 0.02;
    var t1 = 100000.0;

    if (abs(rd.x) < 0.0001) {
        if (ro.x < bmin.x || ro.x > bmax.x) { return vec2<f32>(-1.0); }
    } else {
        let tx0 = (bmin.x - ro.x) / rd.x;
        let tx1 = (bmax.x - ro.x) / rd.x;
        t0 = max(t0, min(tx0, tx1));
        t1 = min(t1, max(tx0, tx1));
    }

    if (abs(rd.y) < 0.0001) {
        if (ro.y < bmin.y || ro.y > bmax.y) { return vec2<f32>(-1.0); }
    } else {
        let ty0 = (bmin.y - ro.y) / rd.y;
        let ty1 = (bmax.y - ro.y) / rd.y;
        t0 = max(t0, min(ty0, ty1));
        t1 = min(t1, max(ty0, ty1));
    }

    if (abs(rd.z) < 0.0001) {
        if (ro.z < bmin.z || ro.z > bmax.z) { return vec2<f32>(-1.0); }
    } else {
        let tz0 = (bmin.z - ro.z) / rd.z;
        let tz1 = (bmax.z - ro.z) / rd.z;
        t0 = max(t0, min(tz0, tz1));
        t1 = min(t1, max(tz0, tz1));
    }

    if (t1 < t0) { return vec2<f32>(-1.0); }
    return vec2<f32>(t0, t1);
}

struct VolumeHit {
    valid: bool,
    index: i32,
    interval: vec2<f32>,
};

fn findNearestGrassVolumeAfter(ro: vec3<f32>, rd: vec3<f32>, minT: f32) -> VolumeHit {
    var hit: VolumeHit;
    hit.valid = false;
    hit.index = -1;
    hit.interval = vec2<f32>(-1.0);

    let count = clamp(u.intParams0.w, 0, MAX_GRASS_CELLS);
    var bestT = 100000.0;
    for (var i = 0; i < MAX_GRASS_CELLS; i = i + 1) {
        if (i >= count) { break; }
        let cell = vec3<f32>(u.blockDamageCells[i].xyz);
        let bmin = vec3<f32>(cell.x - PATCH_HALF - 0.08, grassVisibleMinYFromCellY(cell.y), cell.z - PATCH_HALF - 0.08);
        let bmax = vec3<f32>(cell.x + PATCH_HALF + 0.08, grassVisibleMaxYFromCellY(cell.y), cell.z + PATCH_HALF + 0.08);
        var interval = rayBox(ro, rd, bmin, bmax);
        interval.x = max(interval.x, minT);
        if (interval.x >= 0.0 && interval.x < bestT) {
            bestT = interval.x;
            hit.valid = true;
            hit.index = i;
            hit.interval = interval;
        }
    }

    return hit;
}

fn sampleBlade(blockCell: vec3<i32>, id: vec2<f32>, p: vec3<f32>, ro: vec3<f32>, rd: vec3<f32>, lightDir: vec3<f32>) -> vec4<f32> {
    let rnd = hash22(id + vec2<f32>(f32(blockCell.x) * 13.17 + f32(blockCell.z) * 3.71, f32(blockCell.y) * 5.43));
    let blockBaseXZ = vec2<f32>(f32(blockCell.x) - PATCH_HALF, f32(blockCell.z) - PATCH_HALF);
    let rootLocalXZ = (id + rnd) * CELL;
    let rootXZ = blockBaseXZ + rootLocalXZ;

    if (rootLocalXZ.x < -0.05 || rootLocalXZ.x > PATCH_BLOCKS + 0.05 || rootLocalXZ.y < -0.05 || rootLocalXZ.y > PATCH_BLOCKS + 0.05) {
        return vec4<f32>(0.0);
    }

    let edge = min(min(rootLocalXZ.x, PATCH_BLOCKS - rootLocalXZ.x), min(rootLocalXZ.y, PATCH_BLOCKS - rootLocalXZ.y));
    let patchFade = smoothstep(-0.03, 0.22, edge);
    if (patchFade <= 0.001) { return vec4<f32>(0.0); }

    let live = hash12(id + vec2<f32>(37.0));
    if (live < 0.025) { return vec4<f32>(0.0); }

    let baseY = grassRootYFromCellY(f32(blockCell.y));
    let v0 = vec3<f32>(rootXZ.x, baseY, rootXZ.y);

    let distCam = length(v0 - ro);
    let keep = 1.0 - 0.45 * smoothstep(2.0, 7.0, distCam);
    if (hash12(id + vec2<f32>(91.7)) > keep) { return vec4<f32>(0.0); }

    let up = vec3<f32>(0.0, 1.0, 0.0);

    let h = mix(0.32, 0.82, hash12(id + vec2<f32>(5.3)));
    let width = mix(0.013, 0.031, hash12(id + vec2<f32>(9.8)));
    let stiffness = mix(0.35, 0.90, hash12(id + vec2<f32>(13.4)));

    let ang = 6.2831853 * hash12(id + vec2<f32>(17.6));
    let front = vec3<f32>(cos(ang), 0.0, sin(ang));
    let widthDir = vec3<f32>(-front.z, 0.0, front.x);

    let w = windAt(v0);
    let wh = normalize(vec3<f32>(w.x, 0.0, w.z) + vec3<f32>(0.001, 0.0, 0.0));
    let directionalAlignment = 0.25 + 0.75 * abs(dot(wh, front));

    let windBend = w * h * 0.44 * directionalAlignment * (1.0 - 0.48 * stiffness);
    var gravityBend = front * h * 0.080 * (1.0 - stiffness);
    gravityBend.y = gravityBend.y - h * 0.020;

    var v2 = v0 + up * h + windBend + gravityBend;
    v2.y = max(v2.y, v0.y + h * 0.035);

    let lproj = length(vec2<f32>(v2.x - v0.x, v2.z - v0.z));
    let f1 = max(1.0 - lproj / h, 0.05) * max(lproj / h, 1.0);

    var v1 = v0 + up * h * f1;

    let L0 = length(v2 - v0);
    let L1 = length(v1 - v0) + length(v2 - v1);
    let L = (2.0 * L0 + L1) / 3.0;

    let lenRatio = h / max(L, 0.001);

    v1 = v0 + lenRatio * (v1 - v0);
    v2 = v1 + lenRatio * (v2 - v1);

    var bladeV = (p.y - baseY) / h;
    if (bladeV < -0.04 || bladeV > 1.08) { return vec4<f32>(0.0); }
    bladeV = sat(bladeV);

    let c = bezier2(v0, v1, v2, bladeV);
    let tangent = bezierTangent(v0, v1, v2, bladeV);
    let bladeNormal = normalize(cross(tangent, widthDir));

    let q = p - c;

    let tip = smoothstep(0.55, 1.0, bladeV);
    let taperedWidth = width * mix(1.0, 0.06, tip);

    let lateral = abs(dot(q, widthDir)) / taperedWidth;
    let folded = abs(dot(q, bladeNormal)) / (width * 0.28);

    let body = max(lateral, folded);
    var inside = 1.0 - smoothstep(0.78, 1.16, body);

    if (inside <= 0.001) { return vec4<f32>(0.0); }

    let face = abs(dot(bladeNormal, -rd));
    inside = inside * smoothstep(0.015, 0.24, face);
    inside = inside * (1.0 - 0.18 * smoothstep(0.84, 1.0, bladeV));

    let hueRand = hash12(id + vec2<f32>(29.4));

    let baseA = vec3<f32>(0.020, 0.120, 0.020);
    let baseB = vec3<f32>(0.080, 0.420, 0.045);
    let baseC = vec3<f32>(0.520, 0.680, 0.130);

    var albedo = mix(baseA, baseB, hueRand);
    albedo = mix(albedo, baseC, 0.32 * smoothstep(0.45, 1.0, bladeV));

    let ndl = sat(dot(bladeNormal, lightDir));
    let back = 0.35 * pow(sat(dot(-rd, lightDir)), 2.0);
    let rim = 0.20 * pow(1.0 - sat(abs(dot(bladeNormal, -rd))), 2.0);

    let col = albedo * (0.28 + 0.72 * ndl + back + rim);
    let alpha = inside * 0.48 * patchFade;

    return vec4<f32>(col, alpha);
}

struct GrassHit {
    rgb: vec3<f32>,
    alpha: f32,
    firstT: f32,
};

fn renderGrass(blockCell: vec3<i32>, slab: vec2<f32>, ro: vec3<f32>, rd: vec3<f32>, lightDir: vec3<f32>, fragCoord: vec2<f32>) -> GrassHit {
    var hit: GrassHit;
    hit.rgb = vec3<f32>(0.0);
    hit.alpha = 0.0;
    hit.firstT = -1.0;

    let jitter = hash12(fragCoord + vec2<f32>(u.params.x * 17.13)) - 0.5;

    for (var i = 0; i < GRASS_STEPS; i = i + 1) {
        let fi = (f32(i) + 0.5 + jitter * 0.35) / f32(GRASS_STEPS);
        let t = mix(slab.x, slab.y, fi);
        let p = ro + rd * t;

        let localXZ = p.xz - vec2<f32>(f32(blockCell.x) - PATCH_HALF, f32(blockCell.z) - PATCH_HALF);
        if (localXZ.x >= -0.08 && localXZ.x <= PATCH_BLOCKS + 0.08 && localXZ.y >= -0.08 && localXZ.y <= PATCH_BLOCKS + 0.08) {
            let localCell = floor(localXZ / vec2<f32>(CELL));
            for (var k = 0; k < CANDIDATES; k = k + 1) {
                let id = localCell + candidateOffset(k);
                var g = sampleBlade(blockCell, id, p, ro, rd, lightDir);
                if (g.a > 0.001 && hit.firstT < 0.0) {
                    hit.firstT = t;
                }
                g.a = g.a * (1.0 - hit.alpha);
                hit.rgb = hit.rgb + g.rgb * g.a;
                hit.alpha = hit.alpha + g.a;
            }
        }

        if (hit.alpha > 0.985) { break; }
    }

    return hit;
}

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

fn buildWorldRay(screenUv: vec2<f32>) -> vec3<f32> {
    let ndc = inverseProjectionWarp(vec2<f32>(screenUv.x * 2.0 - 1.0, screenUv.y * 2.0 - 1.0));
    let projX = max(abs(u.projection[0].x), 0.0001);
    let projY = max(abs(u.projection[1].y), 0.0001);
    let viewRay = normalize(vec3<f32>(ndc.x / projX, ndc.y / projY, -1.0));
    let viewRot = mat3x3<f32>(u.view[0].xyz, u.view[1].xyz, u.view[2].xyz);
    return normalize(transpose(viewRot) * viewRay);
}

fn depthForWorldPos(p: vec3<f32>) -> f32 {
    let clip = u.projection * u.view * vec4<f32>(p, 1.0);
    if (clip.w <= 0.0001) {
        return -1.0;
    }
    return clip.z / clip.w;
}

struct GrassOut {
    @location(0) color: vec4<f32>,
    @builtin(frag_depth) depth: f32,
};

struct FSIn {
    @location(0) @interpolate(linear) screenUv: vec2<f32>,
    @location(1) @interpolate(flat) blockCell: vec3<f32>,
};

@fragment
fn fs_main(@builtin(position) fragPosition: vec4<f32>, input: FSIn) -> GrassOut {
    let ro = u.cameraAndScale.xyz;
    let resolution = max(u.vec2Data.zw, vec2<f32>(1.0));
    let pixelUv = fragPosition.xy / resolution;
    let rd = buildWorldRay(vec2<f32>(pixelUv.x, 1.0 - pixelUv.y));

    let lightDir = normalize(vec3<f32>(0.45, 0.82, 0.30));

    let blockCellF = vec3<f32>(
        floor(input.blockCell.x + 0.5),
        floor(input.blockCell.y + 0.5),
        floor(input.blockCell.z + 0.5)
    );
    let bmin = vec3<f32>(
        blockCellF.x - PATCH_HALF - 0.08,
        grassVisibleMinYFromCellY(blockCellF.y),
        blockCellF.z - PATCH_HALF - 0.08
    );
    let bmax = vec3<f32>(
        blockCellF.x + PATCH_HALF + 0.08,
        grassVisibleMaxYFromCellY(blockCellF.y),
        blockCellF.z + PATCH_HALF + 0.08
    );
    let slab = rayBox(ro, rd, bmin, bmax);
    if (slab.x < 0.0) {
        discard;
    }

    let blockCell = vec3<i32>(
        i32(blockCellF.x),
        i32(blockCellF.y),
        i32(blockCellF.z)
    );
    let chosenGrass = renderGrass(blockCell, slab, ro, rd, lightDir, fragPosition.xy);

    if (chosenGrass.alpha <= 0.001 || chosenGrass.firstT < 0.0) {
        discard;
    }

    let worldHit = ro + rd * chosenGrass.firstT;
    let depthHit = worldHit - vec3<f32>(0.0, u.extra.w, 0.0);
    let depth = depthForWorldPos(depthHit);
    if (depth < 0.0 || depth > 1.0) {
        discard;
    }

    var out: GrassOut;
    out.color = vec4<f32>(chosenGrass.rgb / max(chosenGrass.alpha, 0.001), chosenGrass.alpha);
    out.depth = depth;
    return out;
}
