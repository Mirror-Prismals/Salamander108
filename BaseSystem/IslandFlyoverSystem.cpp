#pragma once

#include "../Host.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace ExpanseBiomeSystemLogic {
    bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight);
}

namespace IslandFlyoverSystemLogic {
    namespace {
        constexpr float kTau = 6.28318530717958647692f;

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
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

        float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        std::string getRegistryString(const BaseSystem& baseSystem, const std::string& key, const std::string& fallback = "") {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            return std::get<std::string>(it->second);
        }

        float sampledGroundY(const BaseSystem& baseSystem, float x, float z, float fallback) {
            if (!baseSystem.world || !baseSystem.world->expanse.loaded) return fallback;
            float h = fallback;
            if (ExpanseBiomeSystemLogic::SampleTerrain(*baseSystem.world, x, z, h)) return h;
            return baseSystem.world->expanse.waterSurface;
        }
    }

    void UpdateIslandFlyover(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)win;
        if (!baseSystem.player) return;

        static bool wasActive = false;
        static float elapsed = 0.0f;

        const std::string level = getRegistryString(baseSystem, "level");
        const bool active = (level == "menu") && getRegistryBool(baseSystem, "IslandFlyoverEnabled", true);
        if (!active) {
            wasActive = false;
            elapsed = 0.0f;
            return;
        }

        if (!wasActive) {
            elapsed = 0.0f;
            wasActive = true;
        }
        elapsed += std::max(0.0f, dt);

        glm::vec2 center(0.0f);
        float radius = 220.0f;
        float waterSurface = 0.0f;
        if (baseSystem.world && baseSystem.world->expanse.loaded) {
            const ExpanseConfig& cfg = baseSystem.world->expanse;
            center = glm::vec2(cfg.islandCenterX, cfg.islandCenterZ);
            if (cfg.islandRadius > 0.0f) radius = cfg.islandRadius;
            waterSurface = cfg.waterSurface;
        }

        const float duration = std::max(12.0f, getRegistryFloat(baseSystem, "IslandFlyoverDurationSeconds", 90.0f));
        const float orbitRadius = std::max(48.0f, getRegistryFloat(baseSystem, "IslandFlyoverRadius", radius * 0.74f));
        const float minHeight = std::max(18.0f, getRegistryFloat(baseSystem, "IslandFlyoverMinHeight", 58.0f));
        const float heightWave = std::max(0.0f, getRegistryFloat(baseSystem, "IslandFlyoverHeightWave", 24.0f));

        const float t = std::fmod(elapsed / duration, 1.0f);
        const float a = t * kTau;
        const float b = a * 0.53f + 1.2f;

        glm::vec2 posXZ(
            center.x + std::cos(a) * orbitRadius,
            center.y + std::sin(a * 0.86f) * orbitRadius * 0.62f
        );
        glm::vec2 targetXZ(
            center.x + std::cos(b) * orbitRadius * 0.22f,
            center.y + std::sin(b) * orbitRadius * 0.18f
        );

        const float posGround = sampledGroundY(baseSystem, posXZ.x, posXZ.y, waterSurface);
        const float targetGround = sampledGroundY(baseSystem, targetXZ.x, targetXZ.y, waterSurface);
        const float altitude = minHeight + heightWave * (0.5f + 0.5f * std::sin(a * 1.7f + 0.4f));

        glm::vec3 cameraPos(posXZ.x, std::max(posGround + altitude, waterSurface + minHeight), posXZ.y);
        glm::vec3 target(targetXZ.x, targetGround + 18.0f + 6.0f * std::sin(a * 0.7f), targetXZ.y);
        glm::vec3 forward = target - cameraPos;
        if (glm::length(forward) < 0.001f) forward = glm::vec3(0.0f, -0.2f, -1.0f);
        forward = glm::normalize(forward);

        PlayerContext& player = *baseSystem.player;
        player.cameraPosition = cameraPos;
        player.prevCameraPosition = cameraPos;
        player.cameraYaw = glm::degrees(std::atan2(forward.z, forward.x));
        player.cameraPitch = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
        player.verticalVelocity = 0.0f;
        player.onGround = false;
        player.viewBobWeight = 0.0f;
    }
}
