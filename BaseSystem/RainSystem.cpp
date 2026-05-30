#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace RenderInitSystemLogic {
    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback);
}

namespace RainSystemLogic {
    namespace {
        constexpr int kTopWaterFaceType = 2;

        struct VisibleRainWaterSection {
            const ChunkRenderBuffers* buffers = nullptr;
            glm::vec3 minBounds = glm::vec3(0.0f);
            glm::vec3 maxBounds = glm::vec3(0.0f);
            float dist2 = 0.0f;
        };

        struct RainState {
            bool rolledWeather = false;
            bool raining = false;
            uint32_t seed = 0;
            float visualIntensity = 0.0f;
            bool warnedMissingShaders = false;
        };

        RainState& rainState() {
            static RainState state;
            return state;
        }

        std::string toLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool parseBool(const std::string& value, bool fallback) {
            const std::string lower = toLowerCopy(value);
            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") return true;
            if (lower == "0" || lower == "false" || lower == "no" || lower == "off") return false;
            return fallback;
        }

        bool registryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (std::holds_alternative<std::string>(it->second)) return parseBool(std::get<std::string>(it->second), fallback);
            return fallback;
        }

        std::string registryString(const BaseSystem& baseSystem, const std::string& key, const std::string& fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<std::string>(it->second)) return std::get<std::string>(it->second);
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? "true" : "false";
            return fallback;
        }

        int registryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<std::string>(it->second)) {
                try {
                    return std::stoi(std::get<std::string>(it->second));
                } catch (...) {
                    return fallback;
                }
            }
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? 1 : 0;
            return fallback;
        }

        float registryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<std::string>(it->second)) {
                try {
                    return std::stof(std::get<std::string>(it->second));
                } catch (...) {
                    return fallback;
                }
            }
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? 1.0f : 0.0f;
            return fallback;
        }

        void setRegistryBool(BaseSystem& baseSystem, const std::string& key, bool value) {
            if (!baseSystem.registry) return;
            (*baseSystem.registry)[key] = value;
        }

        void setRegistryString(BaseSystem& baseSystem, const std::string& key, const std::string& value) {
            if (!baseSystem.registry) return;
            (*baseSystem.registry)[key] = value;
        }

        uint32_t makeWeatherSeed() {
            const auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
            const uint64_t ticks = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
            std::random_device rd;
            return static_cast<uint32_t>(ticks ^ (ticks >> 32) ^ static_cast<uint64_t>(rd()));
        }

        bool rollRainDay(int chanceDivisor, uint32_t seed) {
            if (chanceDivisor <= 1) return true;
            std::mt19937 rng(seed);
            std::uniform_int_distribution<int> dist(0, chanceDivisor - 1);
            return dist(rng) == 0;
        }

        bool ensureShader(std::unique_ptr<Shader>& shader,
                          const WorldContext& world,
                          const char* vertexKey,
                          const char* fragmentKey,
                          const char* label,
                          bool& warnedMissingShaders) {
            if (shader && shader->isValid()) return true;

            const auto vertexIt = world.shaders.find(vertexKey);
            const auto fragmentIt = world.shaders.find(fragmentKey);
            if (vertexIt == world.shaders.end() || fragmentIt == world.shaders.end()) {
                if (!warnedMissingShaders) {
                    std::cerr << "RainSystem: missing shader source for " << label << "." << std::endl;
                    warnedMissingShaders = true;
                }
                return false;
            }

            shader = std::make_unique<Shader>(vertexIt->second.c_str(), fragmentIt->second.c_str());
            if (!shader->isValid()) {
                if (!warnedMissingShaders) {
                    std::cerr << "RainSystem: failed to build " << label << "." << std::endl;
                    warnedMissingShaders = true;
                }
                return false;
            }
            return true;
        }

        bool ensureRainShaders(RendererContext& renderer, const WorldContext& world, RainState& state) {
            const bool fullscreenOk = ensureShader(
                renderer.rainFullscreenShader,
                world,
                "RAIN_FULLSCREEN_VERTEX_SHADER",
                "RAIN_FULLSCREEN_FRAGMENT_SHADER",
                "rainFullscreenShader",
                state.warnedMissingShaders
            );
            const bool rippleOk = ensureShader(
                renderer.rainRippleShader,
                world,
                "RAIN_RIPPLE_VERTEX_SHADER",
                "RAIN_RIPPLE_FRAGMENT_SHADER",
                "rainRippleShader",
                state.warnedMissingShaders
            );
            return fullscreenOk && rippleOk;
        }

        bool hasTopWaterSurfaceBuffers(const ChunkRenderBuffers& buffers) {
            return buffers.waterBuffers.surfaceCounts[static_cast<size_t>(kTopWaterFaceType)] > 0
                && buffers.waterBuffers.surfaceVaos[static_cast<size_t>(kTopWaterFaceType)] != 0;
        }

        void collectVisibleTopWaterSections(const BaseSystem& baseSystem,
                                            const glm::vec3& cameraPos,
                                            std::vector<VisibleRainWaterSection>& outSections) {
            outSections.clear();

            size_t clusterCapacity = 0;
            if (baseSystem.voxelRender) {
                for (const auto& [_, clusters] : baseSystem.voxelRender->renderClusters) clusterCapacity += clusters.size();
            }
            const bool farTerrainEnabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "FarTerrainEnabled", true);
            if (farTerrainEnabled && baseSystem.farTerrain && baseSystem.farTerrain->enabled) {
                clusterCapacity += baseSystem.farTerrain->bodyRenderClusters.size();
                clusterCapacity += baseSystem.farTerrain->handoffRenderClusters.size();
            }
            outSections.reserve(clusterCapacity);

            auto appendCluster = [&](const VoxelRenderCluster& cluster) {
                if (!hasTopWaterSurfaceBuffers(cluster.buffers)) return;
                if (!FrustumCullingSystemLogic::ShouldRenderWorldAabb(baseSystem, cluster.minBounds, cluster.maxBounds)) return;
                if (OcclusionCullingSystemLogic::IsWorldAabbOccluded(baseSystem, cluster.minBounds, cluster.maxBounds)) return;

                const glm::vec3 center = 0.5f * (cluster.minBounds + cluster.maxBounds);
                const glm::vec3 delta = center - cameraPos;
                outSections.push_back({&cluster.buffers, cluster.minBounds, cluster.maxBounds, glm::dot(delta, delta)});
            };

            if (baseSystem.voxelWorld && baseSystem.voxelRender) {
                const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
                const VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
                for (const auto& [sectionKey, clusters] : voxelRender.renderClusters) {
                    if (voxelWorld.sections.find(sectionKey) == voxelWorld.sections.end()) continue;
                    for (const VoxelRenderCluster& cluster : clusters) appendCluster(cluster);
                }
            }

            if (farTerrainEnabled && baseSystem.farTerrain && baseSystem.farTerrain->enabled) {
                for (const VoxelRenderCluster& cluster : baseSystem.farTerrain->bodyRenderClusters) appendCluster(cluster);
                for (const VoxelRenderCluster& cluster : baseSystem.farTerrain->handoffRenderClusters) appendCluster(cluster);
            }

            std::sort(outSections.begin(), outSections.end(), [](const VisibleRainWaterSection& a, const VisibleRainWaterSection& b) {
                return a.dist2 > b.dist2;
            });
        }

        void resolveCamera(const BaseSystem& baseSystem,
                           glm::mat4& outView,
                           glm::mat4& outProjection,
                           glm::vec3& outCameraPos) {
            const PlayerContext& player = *baseSystem.player;
            outView = player.viewMatrix;
            outProjection = player.projectionMatrix;
            outCameraPos = player.cameraPosition;
            if (baseSystem.securityCamera && baseSystem.securityCamera->dawViewActive) {
                const SecurityCameraContext& securityCamera = *baseSystem.securityCamera;
                if (glm::length(securityCamera.viewForward) > 1e-4f) {
                    outCameraPos = securityCamera.viewPosition;
                    outView = glm::lookAt(
                        outCameraPos,
                        outCameraPos + glm::normalize(securityCamera.viewForward),
                        glm::vec3(0.0f, 1.0f, 0.0f)
                    );
                }
            }
        }

        glm::ivec2 framebufferSize(const BaseSystem& baseSystem, PlatformWindowHandle win) {
            int width = 0;
            int height = 0;
            PlatformInput::GetFramebufferSize(win, width, height);
            if ((width <= 0 || height <= 0) && baseSystem.app) {
                width = static_cast<int>(baseSystem.app->windowWidth);
                height = static_cast<int>(baseSystem.app->windowHeight);
            }
            return glm::ivec2(std::max(width, 1), std::max(height, 1));
        }

        void publishRainDebug(BaseSystem& baseSystem,
                              const RainState& state,
                              bool active,
                              int chanceDivisor,
                              float targetIntensity) {
            setRegistryBool(baseSystem, "WeatherRaining", active);
            setRegistryBool(baseSystem, "WeatherRainInitialized", state.rolledWeather);
            setRegistryString(baseSystem, "WeatherRainSeed", std::to_string(state.seed));
            setRegistryString(baseSystem, "RainVisualIntensity", std::to_string(state.visualIntensity));

            std::ostringstream ss;
            ss << "RAIN: active=" << (active ? "true" : "false")
               << " rolled=" << (state.rolledWeather ? "true" : "false")
               << " rainy_day=" << (state.raining ? "true" : "false")
               << " chance=1/" << std::max(1, chanceDivisor)
               << " intensity=" << targetIntensity
               << " visual=" << state.visualIntensity;
            setRegistryString(baseSystem, "RainDebugState", ss.str());
        }

        void drawWaterRipples(BaseSystem& baseSystem,
                              RendererContext& renderer,
                              IRenderBackend& renderBackend,
                              const PlayerContext& player,
                              const glm::mat4& view,
                              const glm::mat4& projection,
                              const glm::vec3& cameraPos,
                              const glm::ivec2& targetSize,
                              float time,
                              float intensity) {
            if (!renderer.rainRippleShader) return;

            std::vector<VisibleRainWaterSection> visibleSections;
            collectVisibleTopWaterSections(baseSystem, cameraPos, visibleSections);
            if (visibleSections.empty()) return;

            Shader& shader = *renderer.rainRippleShader;
            shader.use();
            shader.setMat4("model", glm::mat4(1.0f));
            shader.setMat4("view", view);
            shader.setMat4("projection", projection);
            PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, shader);
            shader.setVec3("cameraPos", cameraPos);
            shader.setFloat("time", time);
            shader.setFloat("density", intensity);
            shader.setFloat("weight", registryFloat(baseSystem, "RainRippleStrength", 1.0f));
            shader.setVec2("uResolution", glm::vec2(static_cast<float>(targetSize.x), static_cast<float>(targetSize.y)));
            shader.setInt("ready", 1);
            shader.setInt("faceType", kTopWaterFaceType);

            renderBackend.setDepthTestEnabled(false);
            renderBackend.setDepthWriteEnabled(false);
            renderBackend.setBlendEnabled(true);
            renderBackend.setBlendModeAlpha();
            renderBackend.setCullEnabled(false);

            for (const VisibleRainWaterSection& section : visibleSections) {
                const int count = section.buffers->waterBuffers.surfaceCounts[static_cast<size_t>(kTopWaterFaceType)];
                const RenderHandle vao = section.buffers->waterBuffers.surfaceVaos[static_cast<size_t>(kTopWaterFaceType)];
                if (count <= 0 || vao == 0) continue;
                renderBackend.bindVertexArray(vao);
                renderBackend.drawArraysTrianglesInstanced(0, 6, count);
            }
            renderBackend.unbindVertexArray();
        }

        void drawRainStreaks(const BaseSystem& baseSystem,
                             RendererContext& renderer,
                             IRenderBackend& renderBackend,
                             const PlayerContext& player,
                             const glm::mat4& view,
                             const glm::mat4& projection,
                             const glm::vec3& cameraPos,
                             const glm::ivec2& targetSize,
                             float time,
                             float intensity) {
            if (!renderer.rainFullscreenShader || renderer.godrayQuadVAO == 0) return;

            Shader& shader = *renderer.rainFullscreenShader;
            shader.use();
            shader.setMat4("model", glm::inverse(view));
            shader.setMat4("view", view);
            shader.setMat4("projection", projection);
            PaniniProjectionSystemLogic::ApplyPostProjectionWarpUniforms(player, shader);
            shader.setVec3("cameraPos", cameraPos);
            shader.setVec2("uResolution", glm::vec2(static_cast<float>(targetSize.x), static_cast<float>(targetSize.y)));
            shader.setFloat("time", time);
            shader.setFloat("density", intensity);
            shader.setFloat("weight", registryBool(baseSystem, "RainScreenStreaksEnabled", false) ? 1.0f : 0.0f);

            renderBackend.setDepthTestEnabled(false);
            renderBackend.setDepthWriteEnabled(false);
            renderBackend.setBlendEnabled(true);
            renderBackend.setBlendModeAlpha();
            renderBackend.setCullEnabled(false);
            renderBackend.bindVertexArray(renderer.godrayQuadVAO);
            renderBackend.drawArraysTriangles(0, 6);
            renderBackend.unbindVertexArray();
        }
    }

    void RenderRain(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.player || !baseSystem.renderBackend) return;

        RainState& state = rainState();
        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        PlayerContext& player = *baseSystem.player;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;

        if (!registryBool(baseSystem, "RainSystemEnabled", true)) {
            state.visualIntensity = 0.0f;
            publishRainDebug(baseSystem, state, false, registryInt(baseSystem, "RainChanceDivisor", 7), 0.0f);
            return;
        }

        ensureRainShaders(renderer, world, state);

        const std::string level = registryString(baseSystem, "level", "");
        const bool menuLevel = level == "menu";
        const bool titleRainEnabled = registryBool(baseSystem, "RainTitleScreenEnabled", false);
        const bool forceRain = registryBool(baseSystem, "RainForceEnabled", false)
            || registryBool(baseSystem, "WeatherForceRaining", false);
        const int chanceDivisor = std::max(1, registryInt(baseSystem, "RainChanceDivisor", 7));
        const float configuredIntensity = glm::clamp(registryFloat(baseSystem, "RainIntensity", 1.0f), 0.0f, 2.0f);

        if (forceRain) {
            if (!state.rolledWeather) state.seed = makeWeatherSeed();
            state.rolledWeather = true;
            state.raining = true;
        } else if (!state.rolledWeather && (!menuLevel || titleRainEnabled)) {
            state.seed = makeWeatherSeed();
            state.raining = rollRainDay(chanceDivisor, state.seed);
            state.rolledWeather = true;
        }

        const bool active = state.raining && (!menuLevel || titleRainEnabled || forceRain);
        const float targetIntensity = active ? configuredIntensity : 0.0f;
        const float dtSeconds = (std::isfinite(dt) && dt > 0.0f) ? dt : 1.0f / 60.0f;
        const float fadeRate = std::max(0.01f, registryFloat(baseSystem, "RainFadeRate", 2.8f));
        const float blend = glm::clamp(dtSeconds * fadeRate, 0.0f, 1.0f);
        state.visualIntensity += (targetIntensity - state.visualIntensity) * blend;
        if (std::abs(state.visualIntensity - targetIntensity) < 0.001f) state.visualIntensity = targetIntensity;

        publishRainDebug(baseSystem, state, active, chanceDivisor, targetIntensity);
        if (state.visualIntensity <= 0.002f) return;
        if (!renderer.rainFullscreenShader || !renderer.rainRippleShader) return;

        glm::mat4 view(1.0f);
        glm::mat4 projection(1.0f);
        glm::vec3 cameraPos(0.0f);
        resolveCamera(baseSystem, view, projection, cameraPos);

        const glm::ivec2 targetSize = framebufferSize(baseSystem, win);
        const float time = static_cast<float>(PlatformInput::GetTimeSeconds());
        const float intensity = glm::clamp(state.visualIntensity, 0.0f, 2.0f);

        drawWaterRipples(baseSystem, renderer, renderBackend, player, view, projection, cameraPos, targetSize, time, intensity);
        drawRainStreaks(baseSystem, renderer, renderBackend, player, view, projection, cameraPos, targetSize, time, intensity);

        renderBackend.setCullEnabled(true);
        renderBackend.setBlendEnabled(false);
        renderBackend.setDepthWriteEnabled(true);
        renderBackend.setDepthTestEnabled(true);
    }
}
