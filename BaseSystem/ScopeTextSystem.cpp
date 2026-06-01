#pragma once

#include "Host/PlatformInput.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace ScopeTextSystemLogic {
    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr float kVirtualSampleRate = 48000.0f;

        struct Vec2 {
            float x = 0.0f;
            float y = 0.0f;
        };

        struct GlyphSpec {
            std::vector<Vec2> points;
            float advance = 1.4f;
        };

        struct SignalVertex {
            float x = 0.0f;
            float y = 0.0f;
            float intensity = 1.0f;
        };

        struct ScopeGpuVertex {
            glm::vec2 position;
            float intensity = 1.0f;
        };

        enum class ScopePreset {
            Clean,
            Chorus,
            WaveFolder,
            Warp
        };

        struct ScopeTextEngine {
            std::string phrase = "prismals";
            ScopePreset preset = ScopePreset::Chorus;
            float amount = 0.42f;
            int sampleCount = 1200;
            std::vector<Vec2> baseLoop;
            std::vector<SignalVertex> vertices;

            void rebuild();
            void update(float timeSeconds);
        };

        struct ScopeResources {
            std::unique_ptr<Shader> shader;
            std::string vertexSource;
            std::string fragmentSource;
            RenderHandle vao = 0;
            RenderHandle vbo = 0;
            ScopeTextEngine engine;
        };

        static ScopeResources g_scope;

        static const std::vector<VertexAttribLayout> kScopeVertexLayout = {
            {0, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(ScopeGpuVertex)), offsetof(ScopeGpuVertex, position), 0},
            {1, 1, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(ScopeGpuVertex)), offsetof(ScopeGpuVertex, intensity), 0}
        };

        float clampf(float value, float lo, float hi) {
            return std::max(lo, std::min(value, hi));
        }

        Vec2 lerp(const Vec2& a, const Vec2& b, float t) {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t
            };
        }

        float distance(const Vec2& a, const Vec2& b) {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            return std::sqrt(dx * dx + dy * dy);
        }

        float wrapIndex(float index, float size) {
            float wrapped = std::fmod(index, size);
            if (wrapped < 0.0f) wrapped += size;
            return wrapped;
        }

        Vec2 sampleWrapped(const std::vector<Vec2>& points, float index) {
            const float size = static_cast<float>(points.size());
            const float wrapped = wrapIndex(index, size);
            const size_t i0 = static_cast<size_t>(wrapped);
            const size_t i1 = (i0 + 1u) % points.size();
            return lerp(points[i0], points[i1], wrapped - static_cast<float>(i0));
        }

        float easeInAmount(float amount) {
            return amount * amount;
        }

        void blendTowards(std::vector<Vec2>& points, const std::vector<Vec2>& dry, float wetMix) {
            if (points.size() != dry.size()) return;
            const float mix = clampf(wetMix, 0.0f, 1.0f);
            for (size_t i = 0; i < points.size(); ++i) {
                points[i] = lerp(dry[i], points[i], mix);
            }
        }

        GlyphSpec makeGlyph(std::initializer_list<Vec2> points, float advance) {
            return {std::vector<Vec2>(points), advance};
        }

        GlyphSpec glyphFor(char raw) {
            const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(raw)));
            switch (c) {
                case 'p':
                    return makeGlyph({
                        {0.18f, -0.26f}, {0.18f, 1.48f}, {0.74f, 1.48f}, {1.08f, 1.30f},
                        {1.12f, 0.98f}, {0.88f, 0.78f}, {0.18f, 0.78f}, {0.18f, -0.26f},
                        {0.56f, 0.22f}
                    }, 1.40f);
                case 'r':
                    return makeGlyph({
                        {0.16f, 0.20f}, {0.16f, 1.18f}, {0.16f, 0.92f}, {0.42f, 1.14f},
                        {0.82f, 1.10f}, {1.02f, 0.88f}, {0.72f, 0.78f}, {0.34f, 0.78f},
                        {1.08f, 0.24f}
                    }, 1.18f);
                case 'i':
                    return makeGlyph({
                        {0.42f, 0.04f}, {0.56f, 0.00f}, {0.50f, 0.16f},
                        {0.48f, 0.34f}, {0.48f, 1.08f}, {0.40f, 1.08f},
                        {0.60f, 1.12f}, {0.58f, 0.24f}, {0.88f, 0.22f}
                    }, 1.02f);
                case 's':
                    return makeGlyph({
                        {1.02f, 1.34f}, {0.84f, 1.50f}, {0.34f, 1.50f}, {0.14f, 1.28f},
                        {0.18f, 1.04f}, {0.42f, 0.90f}, {0.82f, 0.78f}, {1.06f, 0.58f},
                        {1.02f, 0.32f}, {0.78f, 0.14f}, {0.30f, 0.16f}, {0.10f, 0.34f},
                        {0.42f, 0.36f}, {1.16f, 0.24f}
                    }, 1.30f);
                case 'm':
                    return makeGlyph({
                        {0.10f, 0.20f}, {0.10f, 1.18f}, {0.36f, 1.18f}, {0.58f, 0.86f},
                        {0.78f, 1.18f}, {1.04f, 1.18f}, {1.24f, 0.86f}, {1.24f, 0.22f},
                        {1.46f, 0.22f}
                    }, 1.50f);
                case 'a':
                    return makeGlyph({
                        {1.00f, 0.20f}, {1.00f, 1.14f}, {0.72f, 1.26f}, {0.34f, 1.16f},
                        {0.14f, 0.86f}, {0.18f, 0.42f}, {0.42f, 0.18f}, {0.78f, 0.24f},
                        {1.00f, 0.54f}, {0.18f, 0.70f}, {1.18f, 0.24f}
                    }, 1.34f);
                case 'l':
                    return makeGlyph({
                        {0.46f, 0.20f}, {0.46f, 1.52f}, {0.48f, 0.22f}, {0.82f, 0.22f}
                    }, 0.96f);
                default:
                    return makeGlyph({
                        {0.08f, 0.18f}, {0.08f, 1.18f}, {0.92f, 1.18f}, {0.92f, 0.18f},
                        {0.08f, 0.18f}, {1.08f, 0.24f}
                    }, 1.18f);
            }
        }

        std::vector<Vec2> buildPhrasePath(const std::string& phrase) {
            std::vector<Vec2> path;
            float cursor = 0.0f;
            for (char raw : phrase) {
                if (raw == ' ') {
                    cursor += 0.85f;
                    path.push_back({cursor, 0.18f});
                    continue;
                }

                const GlyphSpec glyph = glyphFor(raw);
                for (const Vec2& p : glyph.points) {
                    path.push_back({p.x + cursor, p.y});
                }
                cursor += glyph.advance;
            }

            if (path.empty()) path.push_back({0.0f, 0.0f});

            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();
            for (const Vec2& p : path) {
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
            }

            const float width = std::max(0.001f, maxX - minX);
            const float height = std::max(0.001f, maxY - minY);
            const float scale = std::min(1.82f / width, 1.02f / height);
            const float centerX = (minX + maxX) * 0.5f;
            const float centerY = (minY + maxY) * 0.5f;
            for (Vec2& p : path) {
                p.x = (p.x - centerX) * scale;
                p.y = (p.y - centerY) * scale;
            }
            return path;
        }

        std::vector<Vec2> buildClosedLoopSamples(const std::vector<Vec2>& path, int sampleCount) {
            std::vector<float> cumulative(path.size() + 1u, 0.0f);
            float totalLength = 0.0f;
            for (size_t i = 0; i < path.size(); ++i) {
                totalLength += distance(path[i], path[(i + 1u) % path.size()]);
                cumulative[i + 1u] = totalLength;
            }

            std::vector<Vec2> samples;
            samples.reserve(static_cast<size_t>(sampleCount));
            size_t segmentIndex = 0;
            for (int i = 0; i < sampleCount; ++i) {
                const float target = (static_cast<float>(i) / static_cast<float>(sampleCount)) * totalLength;
                while (segmentIndex + 1u < cumulative.size() && cumulative[segmentIndex + 1u] < target) {
                    ++segmentIndex;
                }
                const Vec2& a = path[segmentIndex % path.size()];
                const Vec2& b = path[(segmentIndex + 1u) % path.size()];
                const float segStart = cumulative[segmentIndex];
                const float segEnd = cumulative[segmentIndex + 1u];
                const float t = clampf((target - segStart) / std::max(1e-6f, segEnd - segStart), 0.0f, 1.0f);
                samples.push_back(lerp(a, b, t));
            }
            return samples;
        }

        void applyChorus(std::vector<Vec2>& points, float timeSeconds, float amount) {
            const std::vector<Vec2> source = points;
            const float modA = 8.0f + amount * 40.0f;
            const float modB = 17.0f + amount * 70.0f;
            const float baseDelayA = 18.0f + amount * 55.0f;
            const float baseDelayB = 42.0f + amount * 80.0f;
            for (size_t i = 0; i < points.size(); ++i) {
                const float phase = static_cast<float>(i) / static_cast<float>(points.size());
                const float delayA = baseDelayA + std::sin(2.0f * kPi * (phase * 1.2f + timeSeconds * 0.61f)) * modA;
                const float delayB = baseDelayB + std::sin(2.0f * kPi * (phase * 0.7f - timeSeconds * 0.37f)) * modB;
                const Vec2 copyA = sampleWrapped(source, static_cast<float>(i) - delayA);
                const Vec2 copyB = sampleWrapped(source, static_cast<float>(i) + delayB);
                points[i].x = source[i].x * 0.58f + copyA.x * 0.27f + copyB.x * 0.22f * amount;
                points[i].y = source[i].y * 0.58f + copyB.y * 0.27f + copyA.y * 0.22f * amount;
            }
        }

        float waveFold(float value, float threshold) {
            const float limit = std::max(0.05f, threshold);
            const float span = limit * 2.0f;
            while (value > limit || value < -limit) {
                value = value > limit ? limit - (value - limit) : -limit - (value + limit);
            }
            return clampf(value, -span, span);
        }

        void applyWaveFolder(std::vector<Vec2>& points, float amount) {
            const float drive = 1.0f + amount * 3.2f;
            const float threshold = 0.94f - amount * 0.54f;
            for (Vec2& point : points) {
                point.x = waveFold(point.x * drive, threshold);
                point.y = waveFold(point.y * drive, threshold);
            }
        }

        void applyWarp(std::vector<Vec2>& points, float timeSeconds, float amount) {
            const std::vector<Vec2> source = points;
            const float bend = 0.10f + amount * 0.35f;
            const float twist = 0.15f + amount * 0.55f;
            for (size_t i = 0; i < points.size(); ++i) {
                const float phase = static_cast<float>(i) / static_cast<float>(points.size());
                const float carrierA = std::sin(2.0f * kPi * (phase * (2.0f + amount * 8.0f) + timeSeconds * (0.45f + amount * 1.8f)));
                const float carrierB = std::sin(2.0f * kPi * (phase * (3.0f + amount * 11.0f) - timeSeconds * (0.80f + amount * 2.6f)));
                const float radius = 1.0f + bend * carrierA * carrierB;
                const float angle = twist * carrierA + bend * 0.6f * carrierB;
                const float cs = std::cos(angle);
                const float sn = std::sin(angle);
                const float x = source[i].x * radius;
                const float y = source[i].y * (1.0f - bend * 0.25f * carrierA);
                points[i].x = x * cs - y * sn;
                points[i].y = x * sn + y * cs;
            }
        }

        void updateIntensities(const std::vector<Vec2>& points, std::vector<SignalVertex>& vertices) {
            float maxAbs = 0.0f;
            for (const Vec2& p : points) {
                maxAbs = std::max(maxAbs, std::max(std::abs(p.x), std::abs(p.y)));
            }
            const float normalization = maxAbs > 0.95f ? (0.95f / maxAbs) : 1.0f;
            vertices.resize(points.size());
            for (size_t i = 0; i < points.size(); ++i) {
                const Vec2& current = points[i];
                const Vec2& next = points[(i + 1u) % points.size()];
                const float segLength = distance(current, next);
                const float brightness = clampf(0.85f / (0.025f + segLength * 20.0f), 0.18f, 1.0f);
                vertices[i] = {current.x * normalization, current.y * normalization, brightness};
            }
        }

        void ScopeTextEngine::rebuild() {
            sampleCount = std::clamp(sampleCount, 240, 2400);
            baseLoop = buildClosedLoopSamples(buildPhrasePath(phrase), sampleCount);
            vertices.resize(baseLoop.size());
        }

        void ScopeTextEngine::update(float timeSeconds) {
            if (baseLoop.empty()) rebuild();
            const std::vector<Vec2> dry = baseLoop;
            std::vector<Vec2> processed = dry;
            const float manualAmount = clampf(amount, 0.0f, 1.0f);
            const float animatedAmount = clampf(manualAmount * (0.96f + 0.04f * std::sin(timeSeconds * 0.81f)), 0.0f, 1.0f);
            const float drive = easeInAmount(animatedAmount);
            float wetMix = animatedAmount;

            switch (preset) {
                case ScopePreset::Clean:
                    break;
                case ScopePreset::Chorus:
                    applyChorus(processed, timeSeconds, drive);
                    break;
                case ScopePreset::WaveFolder:
                    applyWaveFolder(processed, drive);
                    break;
                case ScopePreset::Warp:
                    applyWarp(processed, timeSeconds, drive);
                    break;
            }

            blendTowards(processed, dry, wetMix);
            updateIntensities(processed, vertices);
        }

        std::string registryString(const BaseSystem& baseSystem, const char* key, const char* fallback) {
            if (!baseSystem.registry) return std::string(fallback ? fallback : "");
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) {
                return std::string(fallback ? fallback : "");
            }
            return std::get<std::string>(it->second);
        }

        bool registryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            std::string value = std::get<std::string>(it->second);
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
            if (value == "0" || value == "false" || value == "no" || value == "off") return false;
            return fallback;
        }

        float registryFloat(const BaseSystem& baseSystem, const char* key, float fallback) {
            const std::string value = registryString(baseSystem, key, "");
            if (value.empty()) return fallback;
            try {
                return std::stof(value);
            } catch (...) {
                return fallback;
            }
        }

        int registryInt(const BaseSystem& baseSystem, const char* key, int fallback) {
            const std::string value = registryString(baseSystem, key, "");
            if (value.empty()) return fallback;
            try {
                return std::stoi(value);
            } catch (...) {
                return fallback;
            }
        }

        ScopePreset parsePreset(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (value == "clean") return ScopePreset::Clean;
            if (value == "wavefolder" || value == "wavefold" || value == "fold") return ScopePreset::WaveFolder;
            if (value == "warp") return ScopePreset::Warp;
            return ScopePreset::Chorus;
        }

        glm::vec2 pixelToNdc(const glm::vec2& pixel, float width, float height) {
            return glm::vec2(
                (pixel.x / std::max(1.0f, width)) * 2.0f - 1.0f,
                1.0f - (pixel.y / std::max(1.0f, height)) * 2.0f
            );
        }

        bool ensureShader(WorldContext& world) {
            auto vertIt = world.shaders.find("SCOPE_TEXT_VERTEX_SHADER");
            auto fragIt = world.shaders.find("SCOPE_TEXT_FRAGMENT_SHADER");
            if (vertIt == world.shaders.end() || fragIt == world.shaders.end()) return false;

            const std::string& vertexSource = vertIt->second;
            const std::string& fragmentSource = fragIt->second;
            if (!g_scope.shader) {
                g_scope.shader = std::make_unique<Shader>(vertexSource.c_str(), fragmentSource.c_str());
                g_scope.vertexSource = vertexSource;
                g_scope.fragmentSource = fragmentSource;
                return g_scope.shader->isValid();
            }

            if (g_scope.vertexSource != vertexSource || g_scope.fragmentSource != fragmentSource) {
                std::string error;
                if (!g_scope.shader->rebuild(vertexSource.c_str(), fragmentSource.c_str(), &error)) {
                    std::cerr << "ScopeTextSystem: shader rebuild failed: " << error << "\n";
                    return false;
                }
                g_scope.vertexSource = vertexSource;
                g_scope.fragmentSource = fragmentSource;
            }
            return g_scope.shader->isValid();
        }

        bool ensureBuffers(IRenderBackend& renderBackend) {
            if (g_scope.vao == 0) renderBackend.ensureVertexArray(g_scope.vao);
            if (g_scope.vbo == 0) renderBackend.ensureArrayBuffer(g_scope.vbo);
            if (g_scope.vao == 0 || g_scope.vbo == 0) return false;
            renderBackend.configureVertexArray(g_scope.vao, g_scope.vbo, kScopeVertexLayout, 0, {});
            return true;
        }

        bool isScopeTextInstance(const EntityInstance& inst, const std::vector<Entity>& prototypes) {
            if (inst.prototypeID >= 0 && inst.prototypeID < static_cast<int>(prototypes.size())) {
                if (prototypes[static_cast<size_t>(inst.prototypeID)].name == "ScopeText") return true;
            }
            return inst.name == "ScopeText" || inst.controlRole == "scope_text";
        }

        void appendScopeLineVertices(std::vector<ScopeGpuVertex>& out,
                                     const ScopeTextEngine& engine,
                                     const EntityInstance& inst,
                                     float drawWidth,
                                     float drawHeight,
                                     float screenWidth,
                                     float screenHeight) {
            if (engine.vertices.size() < 2u) return;
            out.reserve(out.size() + engine.vertices.size() * 2u);
            for (size_t i = 0; i < engine.vertices.size(); ++i) {
                const SignalVertex& a = engine.vertices[i];
                const SignalVertex& b = engine.vertices[(i + 1u) % engine.vertices.size()];
                const glm::vec2 pa(
                    inst.position.x + a.x * drawWidth * 0.5f,
                    inst.position.y - a.y * drawHeight * 0.5f
                );
                const glm::vec2 pb(
                    inst.position.x + b.x * drawWidth * 0.5f,
                    inst.position.y - b.y * drawHeight * 0.5f
                );
                out.push_back({pixelToNdc(pa, screenWidth, screenHeight), a.intensity});
                out.push_back({pixelToNdc(pb, screenWidth, screenHeight), b.intensity});
            }
        }
    }

    void RenderScopeText(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        if (!baseSystem.renderBackend || !baseSystem.world || !baseSystem.level || !baseSystem.ui || !win) return;
        if (!registryBool(baseSystem, "ScopeTextEnabled", true)) return;
        if (registryString(baseSystem, "level", "") != "menu") return;
        if (!baseSystem.ui->active) return;

        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        WorldContext& world = *baseSystem.world;
        if (!ensureShader(world) || !ensureBuffers(renderBackend)) return;

        int windowWidth = 0;
        int windowHeight = 0;
        PlatformInput::GetWindowSize(win, windowWidth, windowHeight);
        const float screenWidth = windowWidth > 0 ? static_cast<float>(windowWidth) : 1920.0f;
        const float screenHeight = windowHeight > 0 ? static_cast<float>(windowHeight) : 1080.0f;

        const std::string defaultPhrase = registryString(baseSystem, "ScopeTextText", "prismals");
        const ScopePreset preset = parsePreset(registryString(baseSystem, "ScopeTextPreset", "chorus"));
        const float amount = std::clamp(registryFloat(baseSystem, "ScopeTextAmount", 0.42f), 0.0f, 1.0f);
        const int samples = std::clamp(registryInt(baseSystem, "ScopeTextSamples", 1200), 240, 2400);

        std::vector<ScopeGpuVertex> vertices;
        glm::vec3 drawColor(0.18f, 1.0f, 0.32f);
        bool found = false;
        for (const auto& scopeWorld : baseSystem.level->worlds) {
            for (const auto& inst : scopeWorld.instances) {
                if (!isScopeTextInstance(inst, prototypes)) continue;
                found = true;

                const std::string phrase = inst.text.empty() ? defaultPhrase : inst.text;
                if (g_scope.engine.phrase != phrase
                    || g_scope.engine.preset != preset
                    || std::abs(g_scope.engine.amount - amount) > 0.0001f
                    || g_scope.engine.sampleCount != samples) {
                    g_scope.engine.phrase = phrase;
                    g_scope.engine.preset = preset;
                    g_scope.engine.amount = amount;
                    g_scope.engine.sampleCount = samples;
                    g_scope.engine.rebuild();
                }
                g_scope.engine.update(static_cast<float>(PlatformInput::GetTimeSeconds()));

                const float drawWidth = inst.size.x > 0.0f ? inst.size.x : registryFloat(baseSystem, "ScopeTextWidth", 560.0f);
                const float drawHeight = inst.size.y > 0.0f ? inst.size.y : registryFloat(baseSystem, "ScopeTextHeight", 140.0f);
                if (drawWidth <= 0.0f || drawHeight <= 0.0f) continue;
                if (std::max(inst.color.r, std::max(inst.color.g, inst.color.b)) > 0.001f) {
                    drawColor = inst.color;
                }
                EntityInstance centeredInst = inst;
                centeredInst.position.x = screenWidth * 0.5f;
                appendScopeLineVertices(vertices, g_scope.engine, centeredInst, drawWidth, drawHeight, screenWidth, screenHeight);
            }
        }
        if (!found || vertices.empty()) return;

        renderBackend.setDepthTestEnabled(false);
        renderBackend.setBlendEnabled(true);
        renderBackend.setBlendModeAlpha();
        g_scope.shader->use();
        g_scope.shader->setVec3("color", drawColor);

        renderBackend.bindVertexArray(g_scope.vao);
        renderBackend.uploadArrayBufferData(g_scope.vbo, vertices.data(), vertices.size() * sizeof(ScopeGpuVertex), true);

        const float opacity = std::clamp(registryFloat(baseSystem, "ScopeTextOpacity", 0.92f), 0.0f, 1.0f);
        const float lineWidth = std::clamp(registryFloat(baseSystem, "ScopeTextLineWidth", 1.6f), 1.0f, 8.0f);
        const bool glow = registryBool(baseSystem, "ScopeTextGlow", true);
        if (glow) {
            g_scope.shader->setFloat("opacity", opacity * 0.16f);
            renderBackend.setLineWidth(lineWidth * 4.0f);
            renderBackend.drawArraysLines(0, static_cast<int>(vertices.size()));
            g_scope.shader->setFloat("opacity", opacity * 0.34f);
            renderBackend.setLineWidth(lineWidth * 2.0f);
            renderBackend.drawArraysLines(0, static_cast<int>(vertices.size()));
        }
        g_scope.shader->setFloat("opacity", opacity);
        renderBackend.setLineWidth(lineWidth);
        renderBackend.drawArraysLines(0, static_cast<int>(vertices.size()));
        renderBackend.setLineWidth(1.0f);

        renderBackend.unbindVertexArray();
        renderBackend.setBlendEnabled(false);
        renderBackend.setDepthTestEnabled(true);
    }

    void CleanupScopeText(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle) {
        if (baseSystem.renderBackend) {
            if (g_scope.vbo != 0) baseSystem.renderBackend->destroyArrayBuffer(g_scope.vbo);
            if (g_scope.vao != 0) baseSystem.renderBackend->destroyVertexArray(g_scope.vao);
        }
        g_scope.shader.reset();
        g_scope = ScopeResources{};
    }
}
