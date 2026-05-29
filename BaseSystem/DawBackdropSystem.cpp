#pragma once

#include "Host/PlatformInput.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace DawBackdropSystemLogic {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Color {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct FlatVertex {
    float x = 0.0f;
    float y = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct FullscreenVertex {
    float x = 0.0f;
    float y = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

struct StereoSample {
    float l = 0.0f;
    float r = 0.0f;
};

struct SoundtrackWindow {
    bool active = false;
    std::vector<StereoSample> samples;
};

struct RedWaveNode {
    float startX = 0.0f;
    int stepIndex = 0;
    float copyIndicator = 0.0f;
    float currentInterp = 0.0f;
    float nextTransitionTime = 0.0f;
    float transitionDuration = 0.0f;
    bool transitioning = false;
    bool targetIsFlash = true;
    float transitionStartTime = 0.0f;
    float startInterp = 0.0f;
};

struct RedWaveTemplateVertex {
    float ox = 0.0f;
    float oy = 0.0f;
    float oz = 0.0f;
    float shade = 0.0f;
};

struct RedWaveInstance {
    float cx = 0.0f;
    float cy = 0.0f;
    float sx = 0.0f;
    float sy = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float pad = 0.0f;
};

struct RedWaveState {
    bool initialized = false;
    float effectiveAccelTime = 0.0f;
    bool stopAnimActive = false;
    float stopAnimTimer = 0.0f;
    float stopAnimInitialAccel = 0.0f;
    bool restartPhaseActive = false;
    float restartPhaseTimer = 0.0f;
    bool thirdStageActive = false;
    float thirdStageTimer = 0.0f;
    bool spacePressed = false;
    int innerIndex = -1;
    int outerIndex = -1;
    std::vector<RedWaveNode> fanData;
};

struct RuntimeState {
    RedWaveState redWave;
    std::vector<Color> trueColorStrip;
    double trueColorAccumulator = 0.0;
    std::string binaryTrackPath;
    size_t binaryTrackFrameCount = 0;
    int binaryWidth = 0;
    int binaryHeight = 0;
    std::vector<FlatVertex> binaryTrackVertices;
};

RuntimeState g_state;

Color rgba(float r, float g, float b, float a = 1.0f) {
    return {r, g, b, a};
}

float randomFloat(float minValue, float maxValue) {
    return minValue + static_cast<float>(std::rand()) /
        (static_cast<float>(RAND_MAX / (maxValue - minValue)));
}

float pxToClipX(float x, int width) {
    return width > 0 ? (x / static_cast<float>(width)) * 2.0f - 1.0f : 0.0f;
}

float pxToClipY(float y, int height) {
    return height > 0 ? 1.0f - (y / static_cast<float>(height)) * 2.0f : 0.0f;
}

FlatVertex makePxVertex(float x, float y, int width, int height, Color color) {
    return {pxToClipX(x, width), pxToClipY(y, height), color.r, color.g, color.b, color.a};
}

void addTrianglePx(std::vector<FlatVertex>& out,
                   std::array<float, 2> a,
                   std::array<float, 2> b,
                   std::array<float, 2> c,
                   int width,
                   int height,
                   Color color) {
    out.push_back(makePxVertex(a[0], a[1], width, height, color));
    out.push_back(makePxVertex(b[0], b[1], width, height, color));
    out.push_back(makePxVertex(c[0], c[1], width, height, color));
}

void addRectPx(std::vector<FlatVertex>& out,
               float x,
               float y,
               float w,
               float h,
               int width,
               int height,
               Color color) {
    const std::array<float, 2> a{x, y};
    const std::array<float, 2> b{x + w, y};
    const std::array<float, 2> c{x + w, y + h};
    const std::array<float, 2> d{x, y + h};
    addTrianglePx(out, a, b, c, width, height, color);
    addTrianglePx(out, a, c, d, width, height, color);
}

void addConvexPolyPx(std::vector<FlatVertex>& out,
                     const std::vector<std::array<float, 2>>& points,
                     int width,
                     int height,
                     Color color) {
    if (points.size() < 3u) return;
    for (size_t i = 1; i + 1 < points.size(); ++i) {
        addTrianglePx(out, points[0], points[i], points[i + 1], width, height, color);
    }
}

void addCirclePx(std::vector<FlatVertex>& out,
                 float cx,
                 float cy,
                 float radius,
                 int width,
                 int height,
                 Color color,
                 int segments = 32) {
    std::vector<std::array<float, 2>> points;
    points.reserve(static_cast<size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        const float a = static_cast<float>(i) * kPi * 2.0f / static_cast<float>(segments);
        points.push_back({cx + std::cos(a) * radius, cy + std::sin(a) * radius});
    }
    addConvexPolyPx(out, points, width, height, color);
}

void addThickSegmentPx(std::vector<FlatVertex>& out,
                       std::array<float, 2> a,
                       std::array<float, 2> b,
                       float thickness,
                       int width,
                       int height,
                       Color color) {
    const float dx = b[0] - a[0];
    const float dy = b[1] - a[1];
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.001f) return;
    const float nx = -dy / len * thickness * 0.5f;
    const float ny = dx / len * thickness * 0.5f;
    std::vector<std::array<float, 2>> quad{
        {a[0] + nx, a[1] + ny},
        {a[0] - nx, a[1] - ny},
        {b[0] - nx, b[1] - ny},
        {b[0] + nx, b[1] + ny},
    };
    addConvexPolyPx(out, quad, width, height, color);
}

Color hsvToRgb(float h, float s, float v) {
    h = h - std::floor(h);
    const float c = v * s;
    const float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    const float m = v - c;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (h < 1.0f / 6.0f) { r = c; g = x; }
    else if (h < 2.0f / 6.0f) { r = x; g = c; }
    else if (h < 3.0f / 6.0f) { g = c; b = x; }
    else if (h < 4.0f / 6.0f) { g = x; b = c; }
    else if (h < 5.0f / 6.0f) { r = x; b = c; }
    else { r = c; b = x; }
    return {r + m, g + m, b + m, 1.0f};
}

Color sampleToTrueColor(float sample) {
    sample = std::clamp(sample, -1.0f, 1.0f);
    float normalized = (sample + 1.0f) * 0.5f;
    normalized = std::pow(normalized, 3.0f);
    return hsvToRgb(normalized, 1.0f, 1.0f);
}

Color stereoToTrueColor(const StereoSample& sample) {
    const float mid = std::clamp((sample.l + sample.r) * 0.5f, -1.0f, 1.0f);
    const float side = std::clamp((sample.l - sample.r) * 0.5f, -1.0f, 1.0f);
    const float level = std::clamp(std::max(std::fabs(sample.l), std::fabs(sample.r)), 0.0f, 1.0f);
    const float hue = std::fmod(0.58f + side * 0.35f + mid * 0.12f + 1.0f, 1.0f);
    const float value = std::clamp(0.12f + std::pow(level, 0.55f) * 0.98f, 0.0f, 1.0f);
    return hsvToRgb(hue, 0.92f, value);
}

StereoSample sampleStereoFrame(const std::vector<float>& buffer, int channels, size_t frame) {
    if (buffer.empty() || channels <= 0) return {};
    const size_t frameCount = buffer.size() / static_cast<size_t>(channels);
    if (frameCount == 0) return {};
    frame = std::min(frame, frameCount - 1);
    const size_t base = frame * static_cast<size_t>(channels);
    StereoSample out;
    out.l = buffer[base];
    out.r = (channels > 1 && base + 1 < buffer.size()) ? buffer[base + 1] : out.l;
    return out;
}

SoundtrackWindow collectSoundtrackWindow(BaseSystem& baseSystem, int count, double spacingFrames = 1.0) {
    SoundtrackWindow out;
    if (!baseSystem.audio || count <= 0) return out;
    AudioContext& audio = *baseSystem.audio;
    std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
    const int channels = std::max(1, audio.headTrackChannels);
    const size_t frameCount = audio.headTrackBuffer.size() / static_cast<size_t>(channels);
    if (!audio.headTrackActive || frameCount == 0) return out;

    out.active = true;
    out.samples.resize(static_cast<size_t>(count));
    double pos = audio.headTrackPos;
    for (int i = 0; i < count; ++i) {
        size_t frame = static_cast<size_t>(std::clamp(pos, 0.0, static_cast<double>(frameCount - 1)));
        out.samples[static_cast<size_t>(i)] = sampleStereoFrame(audio.headTrackBuffer, channels, frame);
        pos += spacingFrames;
        if (pos >= static_cast<double>(frameCount)) pos = static_cast<double>(frameCount - 1);
    }
    return out;
}

std::string normalizeMode(std::string mode) {
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    for (char& c : mode) {
        if (c == '-' || c == ' ') c = '_';
    }
    if (mode == "world" || mode == "camera" || mode == "security_camera") return "world_camera";
    if (mode == "binary") return "binary_waterfall";
    if (mode == "truecolor") return "true_color";
    if (mode == "redwave") return "red_wave";
    return mode;
}

bool isSpecialMode(const std::string& mode) {
    const std::string m = normalizeMode(mode);
    return m == "red_wave"
        || m == "binary_waterfall"
        || m == "oscilloscope"
        || m == "true_color"
        || m == "pinwheel";
}

const char* flatVertexShader() {
    return R"WGSL(
struct VSIn {
    @location(0) position: vec2<f32>,
    @location(1) color: vec4<f32>,
};
struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
};
@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;
    out.position = vec4<f32>(input.position, 0.25, 1.0);
    out.color = input.color;
    return out;
}
)WGSL";
}

const char* redWaveVertexShader() {
    return R"WGSL(
struct VSIn {
    @location(0) unitOffset: vec3<f32>,
    @location(1) shade: f32,
    @location(2) center: vec2<f32>,
    @location(3) halfSize: vec2<f32>,
    @location(4) color: vec3<f32>,
};
struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) color: vec4<f32>,
};
@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;
    let xy = input.center + input.unitOffset.xy * input.halfSize;
    let color = clamp(input.color + vec3<f32>(input.shade), vec3<f32>(0.0), vec3<f32>(1.0));
    out.position = vec4<f32>(xy, 0.45 + (-input.unitOffset.z) * 0.01, 1.0);
    out.color = vec4<f32>(color, 1.0);
    return out;
}
)WGSL";
}

const char* fullscreenVertexShader() {
    return R"WGSL(
struct VSIn {
    @location(0) position: vec2<f32>,
    @location(1) uv: vec2<f32>,
};
struct VSOut {
    @builtin(position) position: vec4<f32>,
    @location(0) uv: vec2<f32>,
};
@vertex
fn vs_main(input: VSIn) -> VSOut {
    var out: VSOut;
    out.position = vec4<f32>(input.position, 0.95, 1.0);
    out.uv = input.uv;
    return out;
}
)WGSL";
}

const char* colorFragmentShader() {
    return R"WGSL(
struct FSIn { @location(0) color: vec4<f32>, };
@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    return input.color;
}
)WGSL";
}

std::string uniformBlockWgsl() {
    return R"WGSL(
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
struct FSIn { @location(0) uv: vec2<f32>, };
)WGSL";
}

std::string binaryFragmentShader() {
    std::string shader = uniformBlockWgsl();
    shader += R"WGSL(
fn palette(index: u32) -> vec3<f32> {
    var colors = array<vec3<f32>, 18>(
        vec3<f32>(1.0, 0.0, 0.0), vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(0.0, 0.0, 1.0),
        vec3<f32>(1.0, 0.0, 1.0), vec3<f32>(0.0, 1.0, 1.0), vec3<f32>(1.0, 1.0, 0.0),
        vec3<f32>(1.0, 0.75, 0.8), vec3<f32>(0.5, 1.0, 0.0), vec3<f32>(0.0, 0.75, 1.0),
        vec3<f32>(0.76, 0.7, 0.0), vec3<f32>(0.9, 0.3, 0.0), vec3<f32>(0.58, 0.0, 0.83),
        vec3<f32>(0.29, 0.0, 0.51), vec3<f32>(0.0, 0.42, 0.5), vec3<f32>(0.0, 1.0, 0.5),
        vec3<f32>(0.42, 0.56, 0.14), vec3<f32>(1.0, 0.65, 0.0), vec3<f32>(0.4, 0.0, 1.0)
    );
    return colors[index % 18u];
}
fn hashByte(v: u32) -> u32 {
    var x = v;
    x = (x ^ 61u) ^ (x >> 16u);
    x = x + (x << 3u);
    x = x ^ (x >> 4u);
    x = x * 0x27d4eb2du;
    x = x ^ (x >> 15u);
    return x & 255u;
}
@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let resolution = max(u.vec2Data.zw, vec2<f32>(1.0, 1.0));
    let frag = vec2<f32>(input.uv.x * resolution.x, (1.0 - input.uv.y) * resolution.y);
    let scale = 4.0;
    let frameSize = vec2<f32>(64.0 * scale, 128.0 * scale);
    let tile = floor(frag / frameSize);
    let local = floor((frag - tile * frameSize) / scale);
    let frame = u32(floor(u.params.x * 24.0)) + u32(tile.x) + u32(tile.y) * 17u;
    let value = hashByte(u32(local.x) * 1973u + u32(local.y) * 9277u + frame * 26699u);
    let colorIndex = (value / 14u) % 18u;
    let intensity = f32((value % 14u) + 1u) / 14.0;
    let grid = step(0.92, fract(frag.x / scale)) + step(0.92, fract(frag.y / scale));
    let col = palette(colorIndex) * intensity * (1.0 - min(grid, 1.0) * 0.18);
    return vec4<f32>(col, 1.0);
}
)WGSL";
    return shader;
}

std::string pinwheelBackgroundFragmentShader() {
    std::string shader = uniformBlockWgsl();
    shader += R"WGSL(
@fragment
fn fs_main(input: FSIn) -> @location(0) vec4<f32> {
    let t = u.params.x;
    let p = vec2<f32>(input.uv.x, 1.0 - input.uv.y);
    let skyTop = vec3<f32>(120.0, 190.0, 250.0) / 255.0;
    let skyBottom = vec3<f32>(10.0, 80.0, 200.0) / 255.0;
    var col = mix(skyTop, skyBottom, clamp(p.y / 0.5, 0.0, 1.0));
    let wave0 = 0.48 + sin(p.x * 4.5 + t * 0.24) * 0.095 + cos(p.x * 11.8 + t * 0.48) * 0.020;
    let wave1 = 0.53 + sin(p.x * 5.4 + t * 0.31 + 0.7) * 0.105 + cos(p.x * 14.1 + t * 0.62) * 0.024;
    let wave2 = 0.58 + sin(p.x * 6.3 + t * 0.38 + 1.4) * 0.115 + cos(p.x * 16.3 + t * 0.76) * 0.028;
    if (p.y > wave0) { col = vec3<f32>(0.08, 0.90, 0.37); }
    if (p.y > wave1) { col = vec3<f32>(0.16, 0.72, 0.36); }
    if (p.y > wave2) { col = vec3<f32>(0.24, 0.52, 0.28); }
    return vec4<f32>(col, 1.0);
}
)WGSL";
    return shader;
}

void appendRedWaveQuad(std::vector<RedWaveTemplateVertex>& vertices,
                       RedWaveTemplateVertex a,
                       RedWaveTemplateVertex b,
                       RedWaveTemplateVertex c,
                       RedWaveTemplateVertex d,
                       float shade) {
    a.shade = shade; b.shade = shade; c.shade = shade; d.shade = shade;
    vertices.push_back(a); vertices.push_back(b); vertices.push_back(c);
    vertices.push_back(a); vertices.push_back(c); vertices.push_back(d);
}

std::vector<RedWaveTemplateVertex> buildRedWaveTemplate() {
    std::vector<RedWaveTemplateVertex> vertices;
    vertices.reserve(30);
    const RedWaveTemplateVertex fm{-1.0f, -1.0f, 0.0f, 0.0f};
    const RedWaveTemplateVertex fp{1.0f, -1.0f, 0.0f, 0.0f};
    const RedWaveTemplateVertex fpp{1.0f, 1.0f, 0.0f, 0.0f};
    const RedWaveTemplateVertex fmp{-1.0f, 1.0f, 0.0f, 0.0f};
    const RedWaveTemplateVertex bm{-1.5f, -1.5f, -0.5f, 0.0f};
    const RedWaveTemplateVertex bp{0.5f, -1.5f, -0.5f, 0.0f};
    const RedWaveTemplateVertex bpp{0.5f, 0.5f, -0.5f, 0.0f};
    const RedWaveTemplateVertex bmp{-1.5f, 0.5f, -0.5f, 0.0f};
    appendRedWaveQuad(vertices, fm, fp, bp, bm, 0.15f);
    appendRedWaveQuad(vertices, fp, fpp, bpp, bp, -0.10f);
    appendRedWaveQuad(vertices, fmp, fpp, bpp, bmp, 0.05f);
    appendRedWaveQuad(vertices, fm, fmp, bmp, bm, -0.05f);
    appendRedWaveQuad(vertices, fm, fp, fpp, fmp, 0.0f);
    return vertices;
}

void ensureResources(RendererContext& renderer, IRenderBackend& backend) {
    if (!renderer.dawBackdropFlatShader) {
        renderer.dawBackdropFlatShader = std::make_unique<Shader>(flatVertexShader(), colorFragmentShader());
    }
    if (!renderer.dawBackdropRedWaveShader) {
        renderer.dawBackdropRedWaveShader = std::make_unique<Shader>(redWaveVertexShader(), colorFragmentShader());
    }
    if (!renderer.dawBackdropBinaryShader) {
        const std::string fragment = binaryFragmentShader();
        renderer.dawBackdropBinaryShader = std::make_unique<Shader>(fullscreenVertexShader(), fragment.c_str());
    }
    if (!renderer.dawBackdropPinwheelBgShader) {
        const std::string fragment = pinwheelBackgroundFragmentShader();
        renderer.dawBackdropPinwheelBgShader = std::make_unique<Shader>(fullscreenVertexShader(), fragment.c_str());
    }
    if (!renderer.dawBackdropFlatVAO) {
        backend.ensureVertexArray(renderer.dawBackdropFlatVAO);
        backend.ensureArrayBuffer(renderer.dawBackdropFlatVBO);
        backend.configureVertexArray(renderer.dawBackdropFlatVAO,
                                     renderer.dawBackdropFlatVBO,
                                     {
                                         VertexAttribLayout{0, 2, VertexAttribType::Float, false, sizeof(FlatVertex), offsetof(FlatVertex, x), 0},
                                         VertexAttribLayout{1, 4, VertexAttribType::Float, false, sizeof(FlatVertex), offsetof(FlatVertex, r), 0},
                                     },
                                     0,
                                     {});
    }
    if (!renderer.dawBackdropFullscreenVAO) {
        const std::vector<FullscreenVertex> fullscreen{
            {-1.0f, -1.0f, 0.0f, 0.0f},
            { 1.0f, -1.0f, 1.0f, 0.0f},
            { 1.0f,  1.0f, 1.0f, 1.0f},
            {-1.0f, -1.0f, 0.0f, 0.0f},
            { 1.0f,  1.0f, 1.0f, 1.0f},
            {-1.0f,  1.0f, 0.0f, 1.0f},
        };
        backend.ensureVertexArray(renderer.dawBackdropFullscreenVAO);
        backend.ensureArrayBuffer(renderer.dawBackdropFullscreenVBO);
        backend.uploadArrayBufferData(renderer.dawBackdropFullscreenVBO, fullscreen.data(), fullscreen.size() * sizeof(FullscreenVertex), false);
        backend.configureVertexArray(renderer.dawBackdropFullscreenVAO,
                                     renderer.dawBackdropFullscreenVBO,
                                     {
                                         VertexAttribLayout{0, 2, VertexAttribType::Float, false, sizeof(FullscreenVertex), offsetof(FullscreenVertex, x), 0},
                                         VertexAttribLayout{1, 2, VertexAttribType::Float, false, sizeof(FullscreenVertex), offsetof(FullscreenVertex, u), 0},
                                     },
                                     0,
                                     {});
    }
    if (!renderer.dawBackdropRedWaveVAO) {
        const std::vector<RedWaveTemplateVertex> cubeTemplate = buildRedWaveTemplate();
        renderer.dawBackdropRedWaveVertexCount = static_cast<int>(cubeTemplate.size());
        backend.ensureVertexArray(renderer.dawBackdropRedWaveVAO);
        backend.ensureArrayBuffer(renderer.dawBackdropRedWaveTemplateVBO);
        backend.ensureArrayBuffer(renderer.dawBackdropRedWaveInstanceVBO);
        backend.uploadArrayBufferData(renderer.dawBackdropRedWaveTemplateVBO, cubeTemplate.data(), cubeTemplate.size() * sizeof(RedWaveTemplateVertex), false);
        backend.configureVertexArray(renderer.dawBackdropRedWaveVAO,
                                     renderer.dawBackdropRedWaveTemplateVBO,
                                     {
                                         VertexAttribLayout{0, 3, VertexAttribType::Float, false, sizeof(RedWaveTemplateVertex), offsetof(RedWaveTemplateVertex, ox), 0},
                                         VertexAttribLayout{1, 1, VertexAttribType::Float, false, sizeof(RedWaveTemplateVertex), offsetof(RedWaveTemplateVertex, shade), 0},
                                     },
                                     renderer.dawBackdropRedWaveInstanceVBO,
                                     {
                                         VertexAttribLayout{2, 2, VertexAttribType::Float, false, sizeof(RedWaveInstance), offsetof(RedWaveInstance, cx), 1},
                                         VertexAttribLayout{3, 2, VertexAttribType::Float, false, sizeof(RedWaveInstance), offsetof(RedWaveInstance, sx), 1},
                                         VertexAttribLayout{4, 3, VertexAttribType::Float, false, sizeof(RedWaveInstance), offsetof(RedWaveInstance, r), 1},
                                     });
    }
}

void drawFlatTriangles(RendererContext& renderer, IRenderBackend& backend, const std::vector<FlatVertex>& vertices, bool blend) {
    if (vertices.empty() || !renderer.dawBackdropFlatShader) return;
    backend.setBlendEnabled(blend);
    if (blend) backend.setBlendModeAlpha();
    backend.setDepthTestEnabled(false);
    backend.setDepthWriteEnabled(false);
    renderer.dawBackdropFlatShader->use();
    backend.uploadArrayBufferData(renderer.dawBackdropFlatVBO, vertices.data(), vertices.size() * sizeof(FlatVertex), true);
    backend.bindVertexArray(renderer.dawBackdropFlatVAO);
    backend.drawArraysTriangles(0, static_cast<int>(vertices.size()));
    backend.unbindVertexArray();
}

void drawFlatLines(RendererContext& renderer, IRenderBackend& backend, const std::vector<FlatVertex>& vertices, bool blend) {
    if (vertices.empty() || !renderer.dawBackdropFlatShader) return;
    backend.setBlendEnabled(blend);
    if (blend) backend.setBlendModeAlpha();
    backend.setDepthTestEnabled(false);
    backend.setDepthWriteEnabled(false);
    renderer.dawBackdropFlatShader->use();
    backend.uploadArrayBufferData(renderer.dawBackdropFlatVBO, vertices.data(), vertices.size() * sizeof(FlatVertex), true);
    backend.bindVertexArray(renderer.dawBackdropFlatVAO);
    backend.drawArraysLines(0, static_cast<int>(vertices.size()));
    backend.unbindVertexArray();
}

void drawFullscreen(RendererContext& renderer, IRenderBackend& backend, Shader* shader, float time, int width, int height) {
    if (!shader) return;
    backend.setBlendEnabled(false);
    backend.setDepthTestEnabled(false);
    backend.setDepthWriteEnabled(false);
    shader->use();
    shader->setFloat("time", time);
    shader->setVec2("uResolution", glm::vec2(static_cast<float>(width), static_cast<float>(height)));
    backend.bindVertexArray(renderer.dawBackdropFullscreenVAO);
    backend.drawArraysTriangles(0, 6);
    backend.unbindVertexArray();
}

float masterLevel(const DawContext& daw) {
    float level = 0.0f;
    for (const auto& bus : daw.masterBusLevels) {
        level = std::max(level, bus.load(std::memory_order_relaxed));
    }
    return std::clamp(level, 0.0f, 1.0f);
}

std::vector<FlatVertex> buildTrueColorGeometry(RuntimeState& state, BaseSystem& baseSystem, const DawContext& daw, int width, int height, float time, float dt) {
    constexpr int stripWidth = 2;
    const int maxBlocks = std::max(1, width / stripWidth);
    const float opacity = std::clamp(daw.activeThemeBackground.a, 0.0f, 1.0f);
    state.trueColorAccumulator += static_cast<double>(dt) * 480.0;
    int samplesToEmit = static_cast<int>(state.trueColorAccumulator);
    state.trueColorAccumulator -= static_cast<double>(samplesToEmit);
    samplesToEmit = std::clamp(samplesToEmit, 0, 240);
    const SoundtrackWindow soundtrack = collectSoundtrackWindow(baseSystem, samplesToEmit);
    if (soundtrack.active) {
        for (const StereoSample& sample : soundtrack.samples) {
            Color color = stereoToTrueColor(sample);
            color.a = opacity;
            state.trueColorStrip.push_back(color);
        }
    } else {
        const float audioLevel = masterLevel(daw);
        for (int i = 0; i < samplesToEmit; ++i) {
            const float t = time + static_cast<float>(i) * 0.002f;
            float sample = 0.55f * std::sin(t * kPi * 2.0f * 0.47f)
                + 0.30f * std::sin(t * kPi * 2.0f * 1.31f)
                + 0.15f * std::sin(t * kPi * 2.0f * 4.90f);
            sample = std::clamp(sample * (0.35f + audioLevel * 1.25f), -1.0f, 1.0f);
            Color color = sampleToTrueColor(sample);
            color.a = opacity;
            state.trueColorStrip.push_back(color);
        }
    }
    if (state.trueColorStrip.size() > static_cast<size_t>(maxBlocks)) {
        state.trueColorStrip.erase(state.trueColorStrip.begin(), state.trueColorStrip.end() - maxBlocks);
    }
    std::vector<FlatVertex> vertices;
    vertices.reserve(state.trueColorStrip.size() * 6u);
    const int startX = std::max(0, width - static_cast<int>(state.trueColorStrip.size()) * stripWidth);
    for (size_t i = 0; i < state.trueColorStrip.size(); ++i) {
        addRectPx(vertices, static_cast<float>(startX + static_cast<int>(i) * stripWidth), 0.0f, stripWidth, static_cast<float>(height), width, height, state.trueColorStrip[i]);
    }
    return vertices;
}

std::vector<FlatVertex> buildOscilloscopeLines(BaseSystem& baseSystem, const DawContext& daw, int width, int height, float time) {
    std::vector<FlatVertex> lines;
    lines.reserve(8400);
    const Color grid = rgba(0.0f, 0.28f, 0.08f, 0.42f);
    for (int i = 1; i < 8; ++i) {
        const float x = static_cast<float>(width) * static_cast<float>(i) / 8.0f;
        lines.push_back(makePxVertex(x, 0.0f, width, height, grid));
        lines.push_back(makePxVertex(x, static_cast<float>(height), width, height, grid));
    }
    for (int i = 1; i < 6; ++i) {
        const float y = static_cast<float>(height) * static_cast<float>(i) / 6.0f;
        lines.push_back(makePxVertex(0.0f, y, width, height, grid));
        lines.push_back(makePxVertex(static_cast<float>(width), y, width, height, grid));
    }
    constexpr int samples = 4096;
    const SoundtrackWindow soundtrack = collectSoundtrackWindow(baseSystem, samples, 4.0);
    if (soundtrack.active) {
        for (int i = 0; i + 1 < samples; ++i) {
            const float fade = static_cast<float>(i) / static_cast<float>(samples - 1);
            const Color color = rgba(0.05f + 0.20f * fade, 1.0f, 0.08f + 0.28f * (1.0f - fade), 0.72f);
            auto pointFor = [&](const StereoSample& sample) {
                return std::array<float, 2>{
                    static_cast<float>(width) * (0.5f + std::clamp(sample.l, -1.0f, 1.0f) * 0.43f),
                    static_cast<float>(height) * (0.5f - std::clamp(sample.r, -1.0f, 1.0f) * 0.43f),
                };
            };
            const auto a = pointFor(soundtrack.samples[static_cast<size_t>(i)]);
            const auto b = pointFor(soundtrack.samples[static_cast<size_t>(i + 1)]);
            lines.push_back(makePxVertex(a[0], a[1], width, height, color));
            lines.push_back(makePxVertex(b[0], b[1], width, height, color));
        }
        return lines;
    }

    const float audioLevel = masterLevel(daw);
    auto samplePoint = [&](int index) {
        const float p = static_cast<float>(index) / static_cast<float>(samples - 1);
        const float phase = p * kPi * 2.0f * 7.0f;
        const float amp = 0.40f + audioLevel * 0.35f;
        const float l = amp * std::sin(phase + time * 1.70f)
            + 0.18f * std::sin(phase * 2.13f + time * 0.61f);
        const float r = amp * std::sin(phase * 1.017f + time * 1.93f + std::sin(phase * 0.19f + time) * 0.75f)
            + 0.18f * std::cos(phase * 1.71f - time * 0.74f);
        return std::array<float, 2>{
            static_cast<float>(width) * (0.5f + l * 0.39f),
            static_cast<float>(height) * (0.5f - r * 0.39f),
        };
    };
    for (int i = 0; i + 1 < samples; ++i) {
        const float fade = static_cast<float>(i) / static_cast<float>(samples - 1);
        const Color color = rgba(0.05f + 0.20f * fade, 1.0f, 0.08f + 0.28f * (1.0f - fade), 0.72f);
        const auto a = samplePoint(i);
        const auto b = samplePoint(i + 1);
        lines.push_back(makePxVertex(a[0], a[1], width, height, color));
        lines.push_back(makePxVertex(b[0], b[1], width, height, color));
    }
    return lines;
}

Color binaryAudioColor(const StereoSample& sample, int row, int col) {
    const float mid = std::clamp((sample.l + sample.r) * 0.5f, -1.0f, 1.0f);
    const float side = std::clamp((sample.l - sample.r) * 0.5f, -1.0f, 1.0f);
    const float amp = std::clamp(std::max(std::fabs(sample.l), std::fabs(sample.r)), 0.0f, 1.0f);
    const float gate = std::fmod(std::fabs(mid) * 37.0f + std::fabs(side) * 19.0f + static_cast<float>((row * 13 + col * 7) % 11), 1.0f);
    if (gate > std::clamp(amp * 1.8f + 0.08f, 0.12f, 0.92f)) {
        return rgba(0.0f, 0.0f, 0.0f, 1.0f);
    }
    const float hue = std::fmod(0.34f + side * 0.42f + static_cast<float>((row + col) % 17) / 51.0f + 1.0f, 1.0f);
    const float value = std::clamp(0.22f + std::pow(amp, 0.45f) * 1.15f, 0.0f, 1.0f);
    return hsvToRgb(hue, 0.95f, value);
}

std::vector<FlatVertex> buildBinaryWaterfallAudioGeometry(RuntimeState& state, BaseSystem& baseSystem, int width, int height) {
    if (!baseSystem.audio) return {};

    std::string trackPath;
    std::vector<float> trackBuffer;
    int trackChannels = 1;
    double trackPos = 0.0;
    size_t trackFrameCount = 0;
    bool cacheValid = false;
    {
        AudioContext& audio = *baseSystem.audio;
        std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
        trackChannels = std::max(1, audio.headTrackChannels);
        trackFrameCount = audio.headTrackBuffer.size() / static_cast<size_t>(trackChannels);
        if (!audio.headTrackActive || trackFrameCount == 0) {
            state.binaryTrackPath.clear();
            state.binaryTrackFrameCount = 0;
            state.binaryTrackVertices.clear();
            return {};
        }
        trackPath = audio.headTrackPath;
        trackPos = audio.headTrackPos;
        cacheValid = state.binaryTrackPath == trackPath
            && state.binaryTrackFrameCount == trackFrameCount
            && state.binaryWidth == width
            && state.binaryHeight == height
            && !state.binaryTrackVertices.empty();
        if (!cacheValid) {
            trackBuffer = audio.headTrackBuffer;
        }
    }

    if (trackPath.empty()) {
        state.binaryTrackPath.clear();
        state.binaryTrackFrameCount = 0;
        state.binaryTrackVertices.clear();
        return {};
    }

    constexpr int cell = 6;
    const int cols = std::max(1, width / cell);
    const int rows = std::max(1, height / cell);

    if (!cacheValid) {
        state.binaryTrackPath = trackPath;
        state.binaryTrackFrameCount = trackFrameCount;
        state.binaryWidth = width;
        state.binaryHeight = height;
        state.binaryTrackVertices.clear();
        state.binaryTrackVertices.reserve(static_cast<size_t>(cols) * static_cast<size_t>(rows) * 6u);
        addRectPx(state.binaryTrackVertices, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), width, height, rgba(0.0f, 0.0f, 0.0f, 1.0f));
        const size_t totalCells = static_cast<size_t>(cols) * static_cast<size_t>(rows);
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                const size_t cellIndex = static_cast<size_t>(row) * static_cast<size_t>(cols) + static_cast<size_t>(col);
                const size_t frame = (totalCells > 1)
                    ? std::min(trackFrameCount - 1, (cellIndex * trackFrameCount) / totalCells)
                    : 0;
                const StereoSample sample = sampleStereoFrame(trackBuffer, trackChannels, frame);
                Color color = binaryAudioColor(sample, row, col);
                if (color.r <= 0.0f && color.g <= 0.0f && color.b <= 0.0f) continue;
                addRectPx(state.binaryTrackVertices,
                          static_cast<float>(col * cell),
                          static_cast<float>(row * cell),
                          static_cast<float>(cell),
                          static_cast<float>(cell),
                          width,
                          height,
                          color);
            }
        }
    }

    std::vector<FlatVertex> vertices = state.binaryTrackVertices;
    if (trackFrameCount > 0) {
        const float progress = std::clamp(static_cast<float>(trackPos / static_cast<double>(trackFrameCount)), 0.0f, 1.0f);
        const float x = progress * static_cast<float>(width);
        addRectPx(vertices, x - 2.0f, 0.0f, 4.0f, static_cast<float>(height), width, height, rgba(1.0f, 1.0f, 1.0f, 0.95f));
    }
    return vertices;
}

std::vector<FlatVertex> buildPinwheelSymbol(int width, int height, float time) {
    std::vector<FlatVertex> vertices;
    vertices.reserve(220);
    const float cx = static_cast<float>(width) * 0.5f;
    const float cy = static_cast<float>(height) * 0.5f;
    const float size = static_cast<float>(std::min(width, height)) * 0.28f;
    const float rot = std::fmod(time * 0.7f, kPi * 2.0f);
    auto addArm = [&](float theta0, float arrowW, Color color) {
        const float armLen = size * 0.52f;
        const float arrowL = size * 0.23f;
        const float theta1 = theta0 + 2.6f;
        const float x0 = cx + std::cos(theta0) * armLen;
        const float y0 = cy + std::sin(theta0) * armLen;
        const float x1 = cx + std::cos(theta1) * armLen;
        const float y1 = cy + std::sin(theta1) * armLen;
        float vx = x1 - x0;
        float vy = y1 - y0;
        const float len = std::sqrt(vx * vx + vy * vy);
        if (len <= 0.001f) return;
        vx /= len; vy /= len;
        const float nx = -vy;
        const float ny = vx;
        std::vector<std::array<float, 2>> shaft{
            {x0 + nx * arrowW * 0.5f, y0 + ny * arrowW * 0.5f},
            {x0 - nx * arrowW * 0.5f, y0 - ny * arrowW * 0.5f},
            {x1 - nx * arrowW * 0.5f, y1 - ny * arrowW * 0.5f},
            {x1 + nx * arrowW * 0.5f, y1 + ny * arrowW * 0.5f},
        };
        std::vector<std::array<float, 2>> head{
            {x1 + vx * arrowL, y1 + vy * arrowL},
            {x1 + nx * arrowW, y1 + ny * arrowW},
            {x1 - nx * arrowW, y1 - ny * arrowW},
        };
        addConvexPolyPx(vertices, shaft, width, height, color);
        addConvexPolyPx(vertices, head, width, height, color);
    };
    for (int i = 0; i < 3; ++i) addArm(rot + static_cast<float>(i) * 2.0f * kPi / 3.0f, size * 0.15f, rgba(0.94f, 0.94f, 1.0f, 1.0f));
    for (int i = 0; i < 3; ++i) addArm(rot + static_cast<float>(i) * 2.0f * kPi / 3.0f, size * 0.11f, rgba(60.0f / 255.0f, 110.0f / 255.0f, 1.0f, 1.0f));
    addCirclePx(vertices, cx, cy, size * 0.08f, width, height, rgba(230.0f / 255.0f, 245.0f / 255.0f, 1.0f, 0.78f), 40);
    return vertices;
}

constexpr float kRedCenterCubeSize = 0.02f;
constexpr float kRedOuterCubeSize = 0.02f;
constexpr float kRedFlowSpeed = 0.025f;
constexpr float kRedCurveRate = 0.5f;
constexpr float kRedCurveParamA = 0.3f;
constexpr float kRedCurveParamB = 0.7f;
constexpr float kRedCopySpacing = 0.07f;
constexpr float kRedTIncrement = 0.01f;
constexpr float kRedAccelerationFactor = 0.1f;
constexpr float kRedNormalAccel = 0.6f;
constexpr float kRedRestartDuration = 6.0f;
constexpr float kRedSlowRestartFactor = 0.2f;
constexpr float kRedThirdStageDuration = 3.0f;
constexpr float kRedBoostFactor = 1.5f;
constexpr int kRedCentralLines = 8;
constexpr int kRedExtraLeft = 26;
constexpr int kRedExtraRight = 26;
constexpr float kRedBottomY = -1.0f;
constexpr float kRedTopY = 1.0f;
constexpr float kRedTopSpread = 0.2f;
constexpr float kRedCubeScaleBottom = 2.0f;
constexpr float kRedCubeScaleTop = 0.5f;
constexpr float kRedBaseBlack[3] = {0.5f, 0.0f, 0.0f};
constexpr float kRedBaseWhite[3] = {0.25f, 0.0f, 0.0f};

std::vector<RedWaveNode> generateRedWaveFanData() {
    std::vector<RedWaveNode> data;
    const float centralSpacing = 2.0f / static_cast<float>(kRedCentralLines - 1);
    std::vector<float> mainLineXs;
    for (int i = kRedExtraLeft; i >= 1; --i) mainLineXs.push_back(-1.0f - static_cast<float>(i) * centralSpacing);
    for (int i = 0; i < kRedCentralLines; ++i) {
        const float fraction = static_cast<float>(i) / static_cast<float>(kRedCentralLines - 1);
        mainLineXs.push_back(-1.0f + fraction * 2.0f);
    }
    for (int i = 1; i <= kRedExtraRight; ++i) mainLineXs.push_back(1.0f + static_cast<float>(i) * centralSpacing);
    const int numSteps = static_cast<int>(std::ceil(1.0f / kRedTIncrement));
    data.reserve(mainLineXs.size() * static_cast<size_t>(numSteps) * 3u);
    for (float startX : mainLineXs) {
        for (int i = 0; i < numSteps; ++i) {
            RedWaveNode left; left.startX = startX; left.stepIndex = i; left.copyIndicator = -1.0f; left.nextTransitionTime = randomFloat(6.0f, 10.0f); left.transitionDuration = randomFloat(6.0f, 10.0f); data.push_back(left);
            RedWaveNode center; center.startX = startX; center.stepIndex = i; center.copyIndicator = 0.0f; center.nextTransitionTime = randomFloat(1.0f, 5.0f); center.transitionDuration = randomFloat(1.0f, 5.0f); data.push_back(center);
            RedWaveNode right; right.startX = startX; right.stepIndex = i; right.copyIndicator = 1.0f; right.nextTransitionTime = randomFloat(6.0f, 10.0f); right.transitionDuration = randomFloat(6.0f, 10.0f); data.push_back(right);
        }
    }
    return data;
}

void initRedWave(RedWaveState& state) {
    if (state.initialized) return;
    state = RedWaveState{};
    state.fanData = generateRedWaveFanData();
    std::vector<int> inner;
    std::vector<int> outer;
    for (size_t i = 0; i < state.fanData.size(); ++i) {
        if (state.fanData[i].copyIndicator == 0.0f) inner.push_back(static_cast<int>(i));
        else outer.push_back(static_cast<int>(i));
    }
    state.innerIndex = inner.empty() ? -1 : inner[std::rand() % inner.size()];
    state.outerIndex = outer.empty() ? -1 : outer[std::rand() % outer.size()];
    state.stopAnimInitialAccel = kRedNormalAccel * kRedRestartDuration;
    state.thirdStageActive = true;
    state.initialized = true;
}

float smoothstep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void updateRedWaveAcceleration(RedWaveState& state, float dt) {
    if (state.stopAnimActive) {
        state.stopAnimTimer += dt;
        const float eased = smoothstep01(state.stopAnimTimer / 1.0f);
        state.effectiveAccelTime = state.stopAnimInitialAccel * (1.0f - eased);
        if (state.stopAnimTimer >= 1.0f) {
            state.stopAnimActive = false;
            state.effectiveAccelTime = 0.0f;
            state.restartPhaseActive = true;
            state.restartPhaseTimer = 0.0f;
        }
    } else if (state.restartPhaseActive) {
        state.restartPhaseTimer += dt;
        const float slowTarget = kRedNormalAccel * kRedRestartDuration * kRedSlowRestartFactor;
        state.effectiveAccelTime = smoothstep01(state.restartPhaseTimer / kRedRestartDuration) * slowTarget;
        if (state.restartPhaseTimer >= kRedRestartDuration) {
            state.restartPhaseActive = false;
            state.thirdStageActive = true;
            state.thirdStageTimer = 0.0f;
        }
    } else if (state.thirdStageActive) {
        state.thirdStageTimer += dt;
        const float slowTarget = kRedNormalAccel * kRedRestartDuration * kRedSlowRestartFactor;
        const float boostTarget = state.stopAnimInitialAccel * kRedBoostFactor;
        state.effectiveAccelTime = slowTarget + smoothstep01(state.thirdStageTimer / kRedThirdStageDuration) * (boostTarget - slowTarget);
        if (state.thirdStageTimer >= kRedThirdStageDuration) {
            state.thirdStageActive = false;
            state.effectiveAccelTime = 0.0f;
        }
    } else {
        state.effectiveAccelTime += kRedNormalAccel * dt;
    }
}

void updateRedWaveNode(RedWaveNode& node, float currentTime) {
    if (currentTime >= node.nextTransitionTime && !node.transitioning) {
        node.transitioning = true;
        node.transitionStartTime = currentTime;
        node.startInterp = node.currentInterp;
        node.targetIsFlash = node.currentInterp < 0.5f;
        if (node.copyIndicator == 0.0f) {
            node.transitionDuration = randomFloat(1.0f, 5.0f);
            node.nextTransitionTime = currentTime + node.transitionDuration + randomFloat(1.0f, 5.0f);
        } else {
            node.transitionDuration = randomFloat(6.0f, 10.0f);
            node.nextTransitionTime = currentTime + node.transitionDuration + randomFloat(6.0f, 10.0f);
        }
    }
    if (node.transitioning) {
        float progress = (currentTime - node.transitionStartTime) / node.transitionDuration;
        if (progress >= 1.0f) { progress = 1.0f; node.transitioning = false; }
        const float target = node.targetIsFlash ? 1.0f : 0.0f;
        node.currentInterp = node.startInterp + (target - node.startInterp) * progress;
    }
}

void redWaveColorForNode(const RedWaveNode& node, float outColor[3]) {
    if (node.copyIndicator == 0.0f) {
        outColor[0] = kRedBaseBlack[0] + node.currentInterp * (kRedBaseWhite[0] - kRedBaseBlack[0]);
        outColor[1] = kRedBaseBlack[1] + node.currentInterp * (kRedBaseWhite[1] - kRedBaseBlack[1]);
        outColor[2] = kRedBaseBlack[2] + node.currentInterp * (kRedBaseWhite[2] - kRedBaseBlack[2]);
    } else {
        outColor[0] = kRedBaseWhite[0] - node.currentInterp * (kRedBaseWhite[0] - kRedBaseBlack[0]);
        outColor[1] = kRedBaseWhite[1] - node.currentInterp * (kRedBaseWhite[1] - kRedBaseBlack[1]);
        outColor[2] = kRedBaseWhite[2] - node.currentInterp * (kRedBaseWhite[2] - kRedBaseBlack[2]);
    }
}

void computeRedWaveBezier(const RedWaveNode& node, const RedWaveState& state, float timeOffset, float& outX, float& outY) {
    const float centralSpacing = 2.0f / static_cast<float>(kRedCentralLines - 1);
    const int totalMainLines = kRedExtraLeft + kRedCentralLines + kRedExtraRight;
    const float xMin = -1.0f - static_cast<float>(kRedExtraLeft) * centralSpacing;
    const float centerIndex = static_cast<float>(totalMainLines - 1) / 2.0f;
    const int index = static_cast<int>(std::round((node.startX - xMin) / centralSpacing));
    const float group = std::fabs(static_cast<float>(index) - centerIndex);
    const int numGroups = (totalMainLines + 1) / 2;
    const float discreteSpeedFactor = 1.0f
        + 1.0f * ((static_cast<float>(numGroups) - group) / static_cast<float>(numGroups))
        - 0.5f * (group / static_cast<float>(numGroups));
    const float multiplier = 1.0f + kRedAccelerationFactor * state.effectiveAccelTime * discreteSpeedFactor;
    float t = std::fmod(static_cast<float>(node.stepIndex) * kRedTIncrement + timeOffset * multiplier, 1.0f);
    if (t < 0.0f) t += 1.0f;
    const float u = 1.0f - t;
    const float p0x = node.startX;
    const float p1x = kRedCurveRate * node.startX;
    const float p2x = kRedCurveRate * node.startX;
    const float p3x = node.startX * kRedTopSpread;
    const float p1y = kRedBottomY + (kRedTopY - kRedBottomY) * kRedCurveParamA;
    const float p2y = kRedBottomY + (kRedTopY - kRedBottomY) * kRedCurveParamB;
    outX = u * u * u * p0x + 3.0f * u * u * t * p1x + 3.0f * u * t * t * p2x + t * t * t * p3x;
    outY = u * u * u * kRedBottomY + 3.0f * u * u * t * p1y + 3.0f * u * t * t * p2y + t * t * t * kRedTopY;
    outX += node.copyIndicator * kRedCopySpacing;
}

void worldToClip(float aspect, float x, float y, float& outX, float& outY) {
    if (aspect >= 1.0f) { outX = x / aspect; outY = y; }
    else { outX = x; outY = y * aspect; }
}

std::vector<FlatVertex> buildRedWaveBackground(RedWaveState& state) {
    float inner[3]{};
    float outer[3]{};
    if (state.innerIndex >= 0) redWaveColorForNode(state.fanData[static_cast<size_t>(state.innerIndex)], inner);
    if (state.outerIndex >= 0) redWaveColorForNode(state.fanData[static_cast<size_t>(state.outerIndex)], outer);
    return {
        {-1.0f, -1.0f, inner[0], inner[1], inner[2], 1.0f},
        { 1.0f, -1.0f, inner[0], inner[1], inner[2], 1.0f},
        { 1.0f,  1.0f, outer[0], outer[1], outer[2], 1.0f},
        {-1.0f, -1.0f, inner[0], inner[1], inner[2], 1.0f},
        { 1.0f,  1.0f, outer[0], outer[1], outer[2], 1.0f},
        {-1.0f,  1.0f, outer[0], outer[1], outer[2], 1.0f},
    };
}

std::vector<RedWaveInstance> buildRedWaveInstances(RedWaveState& state, float currentTime, float aspect, float dt) {
    std::vector<RedWaveInstance> instances;
    instances.reserve(state.fanData.size());
    updateRedWaveAcceleration(state, std::clamp(dt, 0.0f, 0.1f));
    const float baseMultiplier = 1.0f + kRedAccelerationFactor * state.effectiveAccelTime;
    const float offset = currentTime * kRedFlowSpeed * baseMultiplier;
    for (RedWaveNode& node : state.fanData) {
        updateRedWaveNode(node, currentTime);
        float x = 0.0f;
        float y = 0.0f;
        computeRedWaveBezier(node, state, offset, x, y);
        const float normalizedY = (y - kRedBottomY) / (kRedTopY - kRedBottomY);
        float cubeSize = (node.copyIndicator == 0.0f) ? kRedCenterCubeSize : kRedOuterCubeSize;
        cubeSize *= kRedCubeScaleBottom + (kRedCubeScaleTop - kRedCubeScaleBottom) * normalizedY;
        RedWaveInstance instance;
        worldToClip(aspect, x, y, instance.cx, instance.cy);
        if (aspect >= 1.0f) { instance.sx = cubeSize / aspect; instance.sy = cubeSize; }
        else { instance.sx = cubeSize; instance.sy = cubeSize * aspect; }
        float color[3]{};
        redWaveColorForNode(node, color);
        instance.r = color[0]; instance.g = color[1]; instance.b = color[2];
        instances.push_back(instance);
    }
    return instances;
}

} // namespace

void UpdateDawBackdrop(BaseSystem& baseSystem, std::vector<Entity>&, float dt, PlatformWindowHandle win) {
    if (!baseSystem.ui || !baseSystem.daw || !baseSystem.renderer || !baseSystem.renderBackend || !win) return;
    UIContext& ui = *baseSystem.ui;
    DawContext& daw = *baseSystem.daw;
    if (!ui.active || !ui.fullscreenActive || ui.loadingActive) return;

    const std::string mode = normalizeMode(daw.activeThemeBackdropMode);
    if (!isSpecialMode(mode)) return;

    RendererContext& renderer = *baseSystem.renderer;
    IRenderBackend& backend = *baseSystem.renderBackend;
    ensureResources(renderer, backend);

    int width = 0;
    int height = 0;
    backend.getFramebufferSize(win, width, height);
    if (width <= 0 || height <= 0) return;
    const float now = static_cast<float>(PlatformInput::GetTimeSeconds());

    if (mode == "binary_waterfall") {
        const std::vector<FlatVertex> audioWaterfall = buildBinaryWaterfallAudioGeometry(g_state, baseSystem, width, height);
        if (!audioWaterfall.empty()) {
            drawFlatTriangles(renderer, backend, audioWaterfall, true);
        } else {
            drawFullscreen(renderer, backend, renderer.dawBackdropBinaryShader.get(), now, width, height);
        }
    } else if (mode == "pinwheel") {
        drawFullscreen(renderer, backend, renderer.dawBackdropPinwheelBgShader.get(), now, width, height);
        drawFlatTriangles(renderer, backend, buildPinwheelSymbol(width, height, now), true);
    } else if (mode == "true_color") {
        drawFlatTriangles(renderer, backend, buildTrueColorGeometry(g_state, baseSystem, daw, width, height, now, dt), daw.activeThemeBackground.a < 0.999f);
    } else if (mode == "oscilloscope") {
        drawFlatLines(renderer, backend, buildOscilloscopeLines(baseSystem, daw, width, height, now), true);
    } else if (mode == "red_wave") {
        // Finish red-wave rendering with instanced cube geometry.
        initRedWave(g_state.redWave);
        drawFlatTriangles(renderer, backend, buildRedWaveBackground(g_state.redWave), false);
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const std::vector<RedWaveInstance> instances = buildRedWaveInstances(g_state.redWave, now, aspect, dt);
        if (!instances.empty()) {
            backend.setBlendEnabled(false);
            backend.setDepthTestEnabled(false);
            backend.setDepthWriteEnabled(false);
            renderer.dawBackdropRedWaveShader->use();
            backend.uploadArrayBufferData(renderer.dawBackdropRedWaveInstanceVBO, instances.data(), instances.size() * sizeof(RedWaveInstance), true);
            backend.bindVertexArray(renderer.dawBackdropRedWaveVAO);
            backend.drawArraysTrianglesInstanced(0, renderer.dawBackdropRedWaveVertexCount, static_cast<int>(instances.size()));
            backend.unbindVertexArray();
        }
    }

    backend.setBlendEnabled(false);
    backend.setDepthTestEnabled(true);
    backend.setDepthWriteEnabled(true);
}

void CleanupDawBackdrop(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle) {
    if (!baseSystem.renderer || !baseSystem.renderBackend) return;
    RendererContext& renderer = *baseSystem.renderer;
    IRenderBackend& backend = *baseSystem.renderBackend;
    backend.destroyArrayBuffer(renderer.dawBackdropRedWaveInstanceVBO);
    renderer.dawBackdropRedWaveInstanceVBO = 0;
    backend.destroyArrayBuffer(renderer.dawBackdropRedWaveTemplateVBO);
    renderer.dawBackdropRedWaveTemplateVBO = 0;
    backend.destroyVertexArray(renderer.dawBackdropRedWaveVAO);
    renderer.dawBackdropRedWaveVAO = 0;
    backend.destroyArrayBuffer(renderer.dawBackdropFullscreenVBO);
    renderer.dawBackdropFullscreenVBO = 0;
    backend.destroyVertexArray(renderer.dawBackdropFullscreenVAO);
    renderer.dawBackdropFullscreenVAO = 0;
    backend.destroyArrayBuffer(renderer.dawBackdropFlatVBO);
    renderer.dawBackdropFlatVBO = 0;
    backend.destroyVertexArray(renderer.dawBackdropFlatVAO);
    renderer.dawBackdropFlatVAO = 0;
    renderer.dawBackdropRedWaveVertexCount = 0;
    renderer.dawBackdropPinwheelBgShader.reset();
    renderer.dawBackdropBinaryShader.reset();
    renderer.dawBackdropRedWaveShader.reset();
    renderer.dawBackdropFlatShader.reset();
    g_state = RuntimeState{};
}

} // namespace DawBackdropSystemLogic
