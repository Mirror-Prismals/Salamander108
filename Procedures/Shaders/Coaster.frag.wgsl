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
@group(0) @binding(1)
var sceneSampler: sampler;
@group(0) @binding(2)
var atlasTexture: texture_2d<f32>;

struct FSIn {
    @location(0) texCoord: vec2<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) worldPos: vec3<f32>,
    @location(3) @interpolate(flat) tileIndex: i32,
};

fn sampleAtlasTile(tileIndex: i32, uv: vec2<f32>) -> vec4<f32> {
    let tilesPerRow = u.intParams1.w;
    let tilesPerCol = u.intParams2.x;
    let atlasTileSize = u.atlasInfo.xy;
    let atlasTextureSize = u.atlasInfo.zw;
    if (tileIndex < 0 || tilesPerRow <= 0 || tilesPerCol <= 0 || atlasTextureSize.x <= 0.0 || atlasTextureSize.y <= 0.0) {
        return vec4<f32>(1.0, 0.0, 1.0, 1.0);
    }
    let tileSizeUV = atlasTileSize / atlasTextureSize;
    let tileX = tileIndex % tilesPerRow;
    let tileY = tilesPerCol - 1 - (tileIndex / tilesPerRow);
    let base = vec2<f32>(f32(tileX), f32(tileY)) * tileSizeUV;
    let localUv = vec2<f32>(
        clamp(uv.x, 0.02, 0.98),
        clamp(fract(uv.y), 0.02, 0.98)
    );
    return textureSampleLevel(atlasTexture, sceneSampler, base + localUv * tileSizeUV, 0.0);
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let atlasEnabled = u.intParams1.z;
    var texel: vec4<f32>;
    if (input.tileIndex == -2) {
        texel = vec4<f32>(1.0, 0.86, 0.12, 1.0);
    } else if (input.tileIndex == -3) {
        texel = vec4<f32>(0.08, 0.95, 1.0, 1.0);
    } else if (atlasEnabled == 1) {
        texel = sampleAtlasTile(input.tileIndex, input.texCoord);
    } else if (input.tileIndex == 147) {
        texel = vec4<f32>(0.85, 0.18, 0.18, 1.0);
    } else {
        texel = vec4<f32>(0.18, 0.32, 0.95, 1.0);
    }
    if (texel.a <= 0.04) {
        discard;
    }

    let n = normalize(input.normal);
    let lightDir = normalize(u.lightAndGrid.xyz);
    let ambient = max(max(u.ambientAndLeaf.x, u.ambientAndLeaf.y), u.ambientAndLeaf.z);
    let diffuseStrength = max(max(u.diffuseAndWater.x, u.diffuseAndWater.y), u.diffuseAndWater.z);
    let diffuse = max(dot(n, -lightDir), 0.0);
    let lighting = clamp(ambient + diffuse * diffuseStrength, 0.25, 1.25);
    return vec4<f32>(texel.rgb * lighting, texel.a);
}
