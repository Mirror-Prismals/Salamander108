#pragma once

#include "Host/PlatformInput.h"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
#include "../stb_image.h"

namespace TitleLogoSystemLogic {
    namespace {
        struct LogoVertex {
            glm::vec2 position;
            glm::vec2 uv;
        };

        static const std::vector<VertexAttribLayout> kLogoVertexLayout = {
            {0, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(LogoVertex)), offsetof(LogoVertex, position), 0},
            {1, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(LogoVertex)), offsetof(LogoVertex, uv), 0}
        };

        struct LogoResources {
            std::unique_ptr<Shader> shader;
            std::string vertexSource;
            std::string fragmentSource;
            RenderHandle vao = 0;
            RenderHandle vbo = 0;
            RenderHandle texture = 0;
            std::string texturePath;
            int textureWidth = 0;
            int textureHeight = 0;
            double lastLoadAttempt = -1000.0;
            bool missingLogged = false;
        };

        static LogoResources g_logo;

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

        glm::vec2 pixelToNdc(const glm::vec2& pixel, float width, float height) {
            return glm::vec2(
                (pixel.x / std::max(1.0f, width)) * 2.0f - 1.0f,
                1.0f - (pixel.y / std::max(1.0f, height)) * 2.0f
            );
        }

        bool ensureShader(WorldContext& world) {
            auto vertIt = world.shaders.find("TITLE_LOGO_VERTEX_SHADER");
            auto fragIt = world.shaders.find("TITLE_LOGO_FRAGMENT_SHADER");
            if (vertIt == world.shaders.end() || fragIt == world.shaders.end()) return false;

            const std::string& vertexSource = vertIt->second;
            const std::string& fragmentSource = fragIt->second;
            if (!g_logo.shader) {
                g_logo.shader = std::make_unique<Shader>(vertexSource.c_str(), fragmentSource.c_str());
                g_logo.vertexSource = vertexSource;
                g_logo.fragmentSource = fragmentSource;
                return g_logo.shader->isValid();
            }

            if (g_logo.vertexSource != vertexSource || g_logo.fragmentSource != fragmentSource) {
                std::string error;
                if (!g_logo.shader->rebuild(vertexSource.c_str(), fragmentSource.c_str(), &error)) {
                    std::cerr << "TitleLogoSystem: shader rebuild failed: " << error << "\n";
                    return false;
                }
                g_logo.vertexSource = vertexSource;
                g_logo.fragmentSource = fragmentSource;
            }
            return g_logo.shader->isValid();
        }

        bool ensureBuffers(IRenderBackend& renderBackend) {
            if (g_logo.vao == 0) {
                renderBackend.ensureVertexArray(g_logo.vao);
            }
            if (g_logo.vbo == 0) {
                renderBackend.ensureArrayBuffer(g_logo.vbo);
            }
            if (g_logo.vao == 0 || g_logo.vbo == 0) return false;
            renderBackend.configureVertexArray(g_logo.vao, g_logo.vbo, kLogoVertexLayout, 0, {});
            return true;
        }

        bool ensureTexture(IRenderBackend& renderBackend, const std::string& path) {
            if (path.empty()) return false;
            if (g_logo.texture != 0 && g_logo.texturePath == path) return true;

            const double now = PlatformInput::GetTimeSeconds();
            if (g_logo.texture == 0
                && g_logo.texturePath == path
                && (now - g_logo.lastLoadAttempt) < 1.0) {
                return false;
            }

            if (g_logo.texture != 0 && g_logo.texturePath != path) {
                renderBackend.destroyTexture(g_logo.texture);
                g_logo.textureWidth = 0;
                g_logo.textureHeight = 0;
            }

            g_logo.texturePath = path;
            g_logo.lastLoadAttempt = now;

            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_set_flip_vertically_on_load(false);
            unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (!pixels || width <= 0 || height <= 0) {
                if (pixels) stbi_image_free(pixels);
                if (!g_logo.missingLogged) {
                    std::cerr << "TitleLogoSystem: waiting for title logo PNG at " << path << "\n";
                    g_logo.missingLogged = true;
                }
                return false;
            }

            std::vector<unsigned char> rgba(
                pixels,
                pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4u
            );
            stbi_image_free(pixels);

            TextureUploadParams params;
            params.minFilter = TextureFilterMode::Linear;
            params.magFilter = TextureFilterMode::Linear;
            params.wrapS = TextureWrapMode::ClampToEdge;
            params.wrapT = TextureWrapMode::ClampToEdge;

            if (!renderBackend.uploadRgbaTexture2D(g_logo.texture, width, height, rgba, params)) {
                std::cerr << "TitleLogoSystem: failed to upload title logo texture " << path << "\n";
                return false;
            }

            g_logo.textureWidth = width;
            g_logo.textureHeight = height;
            g_logo.missingLogged = false;
            return true;
        }

        bool isTitleLogoInstance(const EntityInstance& inst, const std::vector<Entity>& prototypes) {
            if (inst.prototypeID >= 0 && inst.prototypeID < static_cast<int>(prototypes.size())) {
                if (prototypes[static_cast<size_t>(inst.prototypeID)].name == "TitleLogo") return true;
            }
            return inst.name == "TitleLogo" || inst.controlRole == "title_logo";
        }

        void appendLogoGrid(std::vector<LogoVertex>& vertices,
                            const EntityInstance& inst,
                            float drawWidth,
                            float drawHeight,
                            float screenWidth,
                            float screenHeight,
                            int rows) {
            const float left = inst.position.x - drawWidth * 0.5f;
            const float right = inst.position.x + drawWidth * 0.5f;
            const float top = inst.position.y - drawHeight * 0.5f;
            const float bottom = inst.position.y + drawHeight * 0.5f;
            rows = std::clamp(rows, 4, 256);

            for (int row = 0; row < rows; ++row) {
                const float v0 = static_cast<float>(row) / static_cast<float>(rows);
                const float v1 = static_cast<float>(row + 1) / static_cast<float>(rows);
                const float y0 = top + (bottom - top) * v0;
                const float y1 = top + (bottom - top) * v1;

                const glm::vec2 p0 = pixelToNdc(glm::vec2(left, y0), screenWidth, screenHeight);
                const glm::vec2 p1 = pixelToNdc(glm::vec2(right, y0), screenWidth, screenHeight);
                const glm::vec2 p2 = pixelToNdc(glm::vec2(right, y1), screenWidth, screenHeight);
                const glm::vec2 p3 = pixelToNdc(glm::vec2(left, y1), screenWidth, screenHeight);

                vertices.push_back({p0, glm::vec2(0.0f, v0)});
                vertices.push_back({p1, glm::vec2(1.0f, v0)});
                vertices.push_back({p2, glm::vec2(1.0f, v1)});
                vertices.push_back({p0, glm::vec2(0.0f, v0)});
                vertices.push_back({p2, glm::vec2(1.0f, v1)});
                vertices.push_back({p3, glm::vec2(0.0f, v1)});
            }
        }
    }

    void RenderTitleLogo(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        if (!baseSystem.renderBackend || !baseSystem.world || !baseSystem.level || !baseSystem.ui || !win) return;
        if (!registryBool(baseSystem, "TitleLogoEnabled", true)) return;
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
        const int gridRows = registryInt(baseSystem, "TitleLogoWaveRows", 96);

        std::vector<LogoVertex> vertices;
        for (const auto& logoWorld : baseSystem.level->worlds) {
            for (const auto& inst : logoWorld.instances) {
                if (!isTitleLogoInstance(inst, prototypes)) continue;
                const std::string imagePath = inst.text.empty()
                    ? registryString(baseSystem, "TitleLogoImagePath", "Procedures/assets/title_logo.png")
                    : inst.text;
                if (!ensureTexture(renderBackend, imagePath)) continue;

                const float fallbackWidth = registryFloat(baseSystem, "TitleLogoWidth", 760.0f);
                float drawWidth = inst.size.x > 0.0f ? inst.size.x : fallbackWidth;
                float drawHeight = inst.size.y > 0.0f ? inst.size.y : 0.0f;
                if (drawHeight <= 0.0f && g_logo.textureWidth > 0 && g_logo.textureHeight > 0) {
                    drawHeight = drawWidth * (static_cast<float>(g_logo.textureHeight) / static_cast<float>(g_logo.textureWidth));
                }
                if (drawWidth <= 0.0f || drawHeight <= 0.0f) continue;
                EntityInstance centeredInst = inst;
                centeredInst.position.x = screenWidth * 0.5f;
                appendLogoGrid(vertices, centeredInst, drawWidth, drawHeight, screenWidth, screenHeight, gridRows);
            }
        }
        if (vertices.empty()) return;

        const float threshold = std::clamp(registryFloat(baseSystem, "TitleLogoLightnessThreshold", 0.45f), 0.0f, 1.0f);
        const float softness = std::clamp(registryFloat(baseSystem, "TitleLogoMaskSoftness", 0.08f), 0.0001f, 1.0f);
        const float waveAmplitudePx = std::max(0.0f, registryFloat(baseSystem, "TitleLogoWaveAmplitudePx", 8.0f));
        const float waveAmplitudeNdc = (waveAmplitudePx / std::max(1.0f, screenWidth)) * 2.0f;
        const float waveFrequency = registryFloat(baseSystem, "TitleLogoWaveFrequency", 18.0f);
        const float waveSpeed = registryFloat(baseSystem, "TitleLogoWaveSpeed", 1.1f);
        const float opacity = std::clamp(registryFloat(baseSystem, "TitleLogoOpacity", 1.0f), 0.0f, 1.0f);

        renderBackend.setDepthTestEnabled(false);
        renderBackend.setBlendEnabled(true);
        renderBackend.setBlendModeAlpha();
        g_logo.shader->use();
        g_logo.shader->setFloat("time", static_cast<float>(PlatformInput::GetTimeSeconds()));
        g_logo.shader->setFloat("mapZoom", threshold);
        g_logo.shader->setFloat("exposure", softness);
        g_logo.shader->setFloat("decay", waveAmplitudeNdc);
        g_logo.shader->setFloat("density", waveFrequency);
        g_logo.shader->setFloat("weight", waveSpeed);
        g_logo.shader->setFloat("opacity", opacity);
        g_logo.shader->setInt("logoTex", 0);

        renderBackend.bindTexture2D(g_logo.texture, 0);
        renderBackend.bindVertexArray(g_logo.vao);
        renderBackend.uploadArrayBufferData(
            g_logo.vbo,
            vertices.data(),
            vertices.size() * sizeof(LogoVertex),
            true
        );
        renderBackend.drawArraysTriangles(0, static_cast<int>(vertices.size()));
        renderBackend.unbindVertexArray();
        renderBackend.bindTexture2D(0, 0);
        renderBackend.setBlendEnabled(false);
        renderBackend.setDepthTestEnabled(true);
    }

    void CleanupTitleLogo(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle) {
        if (baseSystem.renderBackend) {
            if (g_logo.texture != 0) baseSystem.renderBackend->destroyTexture(g_logo.texture);
            if (g_logo.vbo != 0) baseSystem.renderBackend->destroyArrayBuffer(g_logo.vbo);
            if (g_logo.vao != 0) baseSystem.renderBackend->destroyVertexArray(g_logo.vao);
        }
        g_logo.shader.reset();
        g_logo = LogoResources{};
    }
}
