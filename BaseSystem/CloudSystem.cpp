#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <ctime>
#include <iostream>
#include <sstream>
#include <string>

namespace CloudSystemLogic {

    namespace {
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

        void setRegistryString(BaseSystem& baseSystem, const std::string& key, const std::string& value) {
            if (!baseSystem.registry) return;
            (*baseSystem.registry)[key] = value;
        }

        std::string activeDimensionId(const BaseSystem& baseSystem) {
            if (baseSystem.worldSave && !baseSystem.worldSave->activeDimensionId.empty()) {
                return baseSystem.worldSave->activeDimensionId;
            }
            if (baseSystem.registry) {
                auto it = baseSystem.registry->find("ActiveDimensionId");
                if (it != baseSystem.registry->end() && std::holds_alternative<std::string>(it->second)) {
                    return std::get<std::string>(it->second);
                }
            }
            return "overworld";
        }

        float currentDayFraction(const BaseSystem& baseSystem) {
            auto now = std::chrono::system_clock::now();
            double epochSeconds = std::chrono::duration<double>(now.time_since_epoch()).count();
            time_t ct = static_cast<time_t>(std::floor(epochSeconds));
            double subSecond = epochSeconds - static_cast<double>(ct);
            tm lt;
            #ifdef _WIN32
            localtime_s(&lt, &ct);
            #else
            localtime_r(&ct, &lt);
            #endif
            double daySeconds = static_cast<double>(lt.tm_hour) * 3600.0
                              + static_cast<double>(lt.tm_min) * 60.0
                              + static_cast<double>(lt.tm_sec)
                              + subSecond;
            float dayFraction = static_cast<float>(daySeconds / 86400.0);
            if (activeDimensionId(baseSystem) == "nightworld") {
                dayFraction += 0.5f;
                dayFraction -= std::floor(dayFraction);
            }
            return dayFraction;
        }

        glm::vec3 currentLightDir(float dayFraction) {
            const float hour = dayFraction * 24.0f;
            glm::vec3 sunDir;
            glm::vec3 moonDir;
            {
                const float u = (hour - 6.0f) / 12.0f;
                sunDir = glm::normalize(glm::vec3(0.0f, std::sin(u * 3.14159265359f), -std::cos(u * 3.14159265359f)));
            }
            {
                const float adjustedHour = (hour < 6.0f) ? hour + 24.0f : hour;
                const float u = (adjustedHour - 18.0f) / 12.0f;
                moonDir = glm::normalize(glm::vec3(0.0f, std::sin(u * 3.14159265359f), -std::cos(u * 3.14159265359f)));
            }
            return (hour >= 6.0f && hour < 18.0f) ? sunDir : moonDir;
        }

        bool isRainActiveForClouds(const BaseSystem& baseSystem) {
            if (!registryBool(baseSystem, "RainSystemEnabled", true)) return false;
            const bool forceRain = registryBool(baseSystem, "RainForceEnabled", false)
                || registryBool(baseSystem, "WeatherForceRaining", false);
            const bool weatherRaining = registryBool(baseSystem, "WeatherRaining", false);
            return forceRain || weatherRaining;
        }

        bool shouldRenderClouds(const BaseSystem& baseSystem) {
            if (!registryBool(baseSystem, "CloudSystem", true)) return false;
            if (!registryBool(baseSystem, "CloudRenderEnabled", true)) return false;
            const bool rainOnly = registryBool(
                baseSystem,
                "CloudsWhenRainingOnly",
                registryBool(baseSystem, "CloudRenderWhenRainingOnly", true)
            );
            if (!rainOnly) return true;

            return isRainActiveForClouds(baseSystem);
        }

        bool renderToWaterSceneTarget(const RendererContext& renderer) {
            return renderer.waterCompositeShader
                && renderer.waterSceneFBO != 0
                && renderer.waterSceneTex != 0
                && renderer.waterSceneWidth > 0
                && renderer.waterSceneHeight > 0;
        }

        bool ensureCloudShader(BaseSystem& baseSystem) {
            if (!baseSystem.renderer || !baseSystem.world) return false;
            RendererContext& renderer = *baseSystem.renderer;
            if (renderer.cloudShader && renderer.cloudShader->isValid()) return true;

            const auto vertexIt = baseSystem.world->shaders.find("CLOUD_VERTEX_SHADER");
            const auto fragmentIt = baseSystem.world->shaders.find("CLOUD_FRAGMENT_SHADER");
            if (vertexIt == baseSystem.world->shaders.end() || fragmentIt == baseSystem.world->shaders.end()) {
                static bool warnedMissingShaders = false;
                if (!warnedMissingShaders) {
                    std::cerr << "CloudSystem: missing shader sources." << std::endl;
                    warnedMissingShaders = true;
                }
                return false;
            }

            renderer.cloudShader = std::make_unique<Shader>(vertexIt->second.c_str(), fragmentIt->second.c_str());
            if (!renderer.cloudShader->isValid()) {
                std::cerr << "CloudSystem: failed to create shader." << std::endl;
                renderer.cloudShader.reset();
                return false;
            }
            return true;
        }

        void resolveCamera(const BaseSystem& baseSystem,
                           glm::mat4& outView,
                           glm::mat4& outProjection,
                           glm::vec3& outCameraPos,
                           const PlayerContext*& outPlayer) {
            const PlayerContext& player = *baseSystem.player;
            outPlayer = &player;
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

        void publishCloudDebug(BaseSystem& baseSystem, bool active, const char* reason = nullptr) {
            std::ostringstream ss;
            ss << "CLOUDS: active=" << (active ? "true" : "false")
               << " mode=volumetric"
               << " rain_only=" << (registryBool(baseSystem, "CloudRenderWhenRainingOnly", true) ? "true" : "false")
               << " force=" << ((registryBool(baseSystem, "RainForceEnabled", false)
                    || registryBool(baseSystem, "WeatherForceRaining", false)) ? "true" : "false")
               << " raining=" << (registryBool(baseSystem, "WeatherRaining", false) ? "true" : "false")
               << " rain_active=" << (isRainActiveForClouds(baseSystem) ? "true" : "false");
            if (reason && *reason) ss << " reason=" << reason;
            setRegistryString(baseSystem, "CloudDebugState", ss.str());
        }
    }

    void RenderClouds(BaseSystem& baseSystem, const glm::vec3& lightDir, float time, float dayFraction) {
        (void)dayFraction;
        if (!baseSystem.renderer || !baseSystem.player || !baseSystem.renderBackend) return;
        const bool active = shouldRenderClouds(baseSystem);
        publishCloudDebug(baseSystem, active);
        if (!active) return;

        RendererContext& renderer = *baseSystem.renderer;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        if (!ensureCloudShader(baseSystem)) return;
        if (!renderer.cloudShader || renderer.godrayQuadVAO == 0) return;

        glm::mat4 view(1.0f);
        glm::mat4 projection(1.0f);
        glm::vec3 cameraPos(0.0f);
        const PlayerContext* projectionPlayer = nullptr;
        resolveCamera(baseSystem, view, projection, cameraPos, projectionPlayer);

        const float cloudBase = registryFloat(baseSystem, "CloudBase", 1600.0f);
        const float cloudThickness = registryFloat(baseSystem, "CloudThickness", 900.0f);
        const float cloudScale = registryFloat(baseSystem, "CloudScale", 1.0f);
        const int steps = std::clamp(registryInt(baseSystem, "CloudRaymarchSteps", 28), 4, 96);
        const float density = registryFloat(baseSystem, "CloudDensityMultiplier", 0.006f);
        const float light = registryFloat(baseSystem, "CloudLightMultiplier", 1.0f);
        const float radius = registryFloat(baseSystem, "CloudRadius", 12000.0f);
        const float fadeBand = registryFloat(baseSystem, "CloudFadeBand", 1000.0f);
        const float maxSkip = registryFloat(baseSystem, "CloudMaxSkip", 7.0f);

        Shader& shader = *renderer.cloudShader;
        shader.use();
        shader.setMat4("model", glm::inverse(view));
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        if (projectionPlayer) {
            PaniniProjectionSystemLogic::ApplyPostProjectionWarpUniforms(*projectionPlayer, shader);
        } else {
            PaniniProjectionSystemLogic::DisableProjectionWarpUniforms(shader);
        }
        shader.setVec3("cameraPos", cameraPos);
        shader.setVec3("lightDir", lightDir);
        shader.setFloat("time", time);
        shader.setFloat("mapZoom", cloudBase);
        shader.setFloat("exposure", std::max(1.0f, cloudThickness));
        shader.setFloat("decay", std::max(0.01f, cloudScale));
        shader.setFloat("density", std::max(0.0f, density));
        shader.setFloat("weight", std::max(0.0f, light));
        shader.setInt("samples", steps);
        shader.setVec2("lightPos", glm::vec2(std::max(100.0f, radius), std::max(1.0f, fadeBand)));
        shader.setFloat("waterReflectionPlaneY", std::max(0.0f, maxSkip));

        renderBackend.setDepthTestEnabled(false);
        renderBackend.setDepthWriteEnabled(false);
        renderBackend.setBlendEnabled(true);
        renderBackend.setBlendModeAlpha();
        renderBackend.setCullEnabled(false);
        renderBackend.bindVertexArray(renderer.godrayQuadVAO);
        renderBackend.drawArraysTriangles(0, 6);
        renderBackend.unbindVertexArray();

        renderBackend.setCullEnabled(true);
        renderBackend.setBlendEnabled(false);
        renderBackend.setDepthWriteEnabled(true);
        renderBackend.setDepthTestEnabled(true);
    }

    void RenderClouds(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)dt;
        (void)win;
        if (!baseSystem.renderer || !baseSystem.renderBackend) return;

        const bool active = shouldRenderClouds(baseSystem);
        publishCloudDebug(baseSystem, active);
        if (!active) return;

        RendererContext& renderer = *baseSystem.renderer;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        const bool renderToSceneTarget = renderToWaterSceneTarget(renderer);
        if (renderToSceneTarget) {
            renderBackend.resumeOffscreenColorPass(
                renderer.waterSceneFBO,
                renderer.waterSceneWidth,
                renderer.waterSceneHeight
            );
        }

        const float dayFraction = currentDayFraction(baseSystem);
        const float time = static_cast<float>(PlatformInput::GetTimeSeconds());
        RenderClouds(baseSystem, currentLightDir(dayFraction), time, dayFraction);

        if (renderToSceneTarget) {
            renderBackend.endOffscreenColorPass();
        }
    }

}
