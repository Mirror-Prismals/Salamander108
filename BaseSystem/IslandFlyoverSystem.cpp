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
        constexpr float kRadToDeg = 57.295779513082320876f;
        constexpr float kDegToRad = 0.01745329251994329577f;

        struct FlightState {
            glm::vec3 position = glm::vec3(0.0f);
            float yaw = 0.0f;
            float pitch = -5.0f;
            float roll = 0.0f;
            bool initialized = false;
        };

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

        float wrapDegrees(float value) {
            while (value > 180.0f) value -= 360.0f;
            while (value < -180.0f) value += 360.0f;
            return value;
        }

        float approachExp(float current, float target, float sharpness, float dt) {
            const float alpha = 1.0f - std::exp(-std::max(0.0f, sharpness) * std::max(0.0f, dt));
            return current + (target - current) * std::clamp(alpha, 0.0f, 1.0f);
        }

        glm::vec3 forwardFromYawPitch(float yawDeg, float pitchDeg) {
            const float yaw = yawDeg * kDegToRad;
            const float pitch = pitchDeg * kDegToRad;
            const float cp = std::cos(pitch);
            return glm::normalize(glm::vec3(
                std::cos(yaw) * cp,
                std::sin(pitch),
                std::sin(yaw) * cp
            ));
        }
    }

    void UpdateIslandFlyover(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)win;
        if (!baseSystem.player) return;

        static bool wasActive = false;
        static float elapsed = 0.0f;
        static FlightState flight;
        static int activeScene = -1;

        const std::string level = getRegistryString(baseSystem, "level");
        const bool active = (level == "menu") && getRegistryBool(baseSystem, "IslandFlyoverEnabled", true);
        if (!active) {
            if (wasActive && baseSystem.player) {
                baseSystem.player->cameraRoll = 0.0f;
            }
            wasActive = false;
            elapsed = 0.0f;
            flight.initialized = false;
            activeScene = -1;
            return;
        }

        const bool firstActiveFrame = !wasActive;
        if (firstActiveFrame) {
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
        const float minHeight = std::max(18.0f, getRegistryFloat(baseSystem, "IslandFlyoverMinHeight", 8.0f));
        const float heightWave = std::max(0.0f, getRegistryFloat(baseSystem, "IslandFlyoverHeightWave", 24.0f));
        const float classicMinHeight = std::max(18.0f, getRegistryFloat(baseSystem, "IslandFlyoverClassicMinHeight", 58.0f));
        const float classicHeightWave = std::max(0.0f, getRegistryFloat(baseSystem, "IslandFlyoverClassicHeightWave", heightWave));
        const float flySpeed = std::max(12.0f, getRegistryFloat(baseSystem, "IslandFlyoverFlightSpeed", orbitRadius * kTau / duration));
        const float turnRateMax = std::max(12.0f, getRegistryFloat(baseSystem, "IslandFlyoverTurnRateDeg", 42.0f));
        const float gyroStrength = std::max(0.0f, getRegistryFloat(baseSystem, "IslandFlyoverGyroStrength", 1.0f));
        const float dtClamped = std::clamp(std::max(0.0f, dt), 0.0f, 0.1f);

        const int sceneIndex = static_cast<int>(std::floor(elapsed / duration)) % 2;
        const bool sceneJustChanged = firstActiveFrame || sceneIndex != activeScene;
        if (sceneJustChanged) {
            flight.initialized = false;
            activeScene = sceneIndex;
        }
        const float sceneElapsed = std::fmod(elapsed, duration);
        const float t = std::fmod(sceneElapsed / duration, 1.0f);
        const float a = t * kTau;

        if (sceneIndex == 1) {
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
            const float altitude = classicMinHeight + classicHeightWave * (0.5f + 0.5f * std::sin(a * 1.7f + 0.4f));

            glm::vec3 cameraPos(posXZ.x, std::max(posGround + altitude, waterSurface + classicMinHeight), posXZ.y);
            glm::vec3 target(targetXZ.x, targetGround + 18.0f + 6.0f * std::sin(a * 0.7f), targetXZ.y);
            glm::vec3 forward = target - cameraPos;
            if (glm::length(forward) < 0.001f) forward = glm::vec3(0.0f, -0.2f, -1.0f);
            forward = glm::normalize(forward);

            PlayerContext& player = *baseSystem.player;
            const glm::vec3 previousCameraPos = sceneJustChanged ? cameraPos : player.cameraPosition;
            player.cameraPosition = cameraPos;
            player.prevCameraPosition = previousCameraPos;
            player.cameraYaw = glm::degrees(std::atan2(forward.z, forward.x));
            player.cameraPitch = glm::degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
            player.cameraRoll = 0.0f;
            player.verticalVelocity = 0.0f;
            player.onGround = false;
            player.viewBobWeight = 0.0f;
            return;
        }

        if (!flight.initialized) {
            const glm::vec2 startXZ(
                center.x + std::cos(a) * orbitRadius,
                center.y + std::sin(a) * orbitRadius
            );
            const float startGround = sampledGroundY(baseSystem, startXZ.x, startXZ.y, waterSurface);
            flight.position = glm::vec3(
                startXZ.x,
                std::max(startGround + minHeight + heightWave * 0.5f, waterSurface + minHeight),
                startXZ.y
            );
            const float startAngle = std::atan2(startXZ.y - center.y, startXZ.x - center.x) * kRadToDeg;
            flight.yaw = startAngle + 90.0f;
            flight.pitch = -5.0f;
            flight.roll = 0.0f;
            flight.initialized = true;
        }

        float gyroYaw = 0.0f;
        float gyroPitch = 0.0f;
        if (win) {
            int windowWidth = 0;
            int windowHeight = 0;
            PlatformInput::GetWindowSize(win, windowWidth, windowHeight);
            double cursorX = 0.0;
            double cursorY = 0.0;
            PlatformInput::GetCursorPosition(win, cursorX, cursorY);
            if (windowWidth > 1 && windowHeight > 1) {
                const float halfW = static_cast<float>(windowWidth) * 0.5f;
                const float halfH = static_cast<float>(windowHeight) * 0.5f;
                gyroYaw = std::clamp((static_cast<float>(cursorX) - halfW) / halfW, -1.0f, 1.0f);
                gyroPitch = std::clamp((halfH - static_cast<float>(cursorY)) / halfH, -1.0f, 1.0f);
            }
            if (PlatformInput::IsKeyDown(win, PlatformInput::Key::ArrowLeft)) gyroYaw -= 0.85f;
            if (PlatformInput::IsKeyDown(win, PlatformInput::Key::ArrowRight)) gyroYaw += 0.85f;
            if (PlatformInput::IsKeyDown(win, PlatformInput::Key::ArrowUp)) gyroPitch += 0.85f;
            if (PlatformInput::IsKeyDown(win, PlatformInput::Key::ArrowDown)) gyroPitch -= 0.85f;
            gyroYaw = std::clamp(gyroYaw, -1.0f, 1.0f) * gyroStrength;
            gyroPitch = std::clamp(gyroPitch, -1.0f, 1.0f) * gyroStrength;
        }

        const glm::vec2 currentXZ(flight.position.x, flight.position.z);
        glm::vec2 radial = currentXZ - center;
        float radialLength = glm::length(radial);
        if (radialLength < 0.001f) {
            radial = glm::vec2(1.0f, 0.0f);
            radialLength = 1.0f;
        }
        const float radialAngle = std::atan2(radial.y, radial.x) * kRadToDeg;
        const float radiusError = std::clamp((radialLength - orbitRadius) / orbitRadius, -1.0f, 1.0f);
        const float autopilotYaw = radialAngle + 90.0f + radiusError * 42.0f;
        const float yawError = wrapDegrees(autopilotYaw - flight.yaw);
        float yawRate = std::clamp(yawError * 1.2f + gyroYaw * turnRateMax, -turnRateMax, turnRateMax);
        flight.yaw = wrapDegrees(flight.yaw + yawRate * dtClamped);

        const float groundY = sampledGroundY(baseSystem, flight.position.x, flight.position.z, waterSurface);
        const float wave = heightWave * (0.5f + 0.5f * std::sin(elapsed * 0.19f + 0.4f));
        const float desiredY = std::max(groundY + minHeight + wave + gyroPitch * heightWave * 0.35f,
                                        waterSurface + minHeight);
        flight.position.y = approachExp(flight.position.y, desiredY, 1.65f, dtClamped);

        const float clearancePitch = std::clamp((desiredY - flight.position.y) * 0.03f, -7.0f, 7.0f);
        const float pitchTarget = std::clamp(-4.0f + gyroPitch * 12.0f + clearancePitch, -18.0f, 14.0f);
        flight.pitch = approachExp(flight.pitch, pitchTarget, 3.5f, dtClamped);
        const float rollTarget = std::clamp(-yawRate / turnRateMax * 24.0f + gyroYaw * 8.0f, -32.0f, 32.0f);
        flight.roll = approachExp(flight.roll, rollTarget, 4.5f, dtClamped);

        glm::vec3 forward = forwardFromYawPitch(flight.yaw, flight.pitch);
        glm::vec2 forwardXZ(forward.x, forward.z);
        if (glm::length(forwardXZ) > 0.001f) {
            forwardXZ = glm::normalize(forwardXZ);
            flight.position.x += forwardXZ.x * flySpeed * dtClamped;
            flight.position.z += forwardXZ.y * flySpeed * dtClamped;
        }

        PlayerContext& player = *baseSystem.player;
        const glm::vec3 previousCameraPos = firstActiveFrame ? flight.position : player.cameraPosition;
        player.cameraPosition = flight.position;
        player.prevCameraPosition = previousCameraPos;
        player.cameraYaw = flight.yaw;
        player.cameraPitch = flight.pitch;
        player.cameraRoll = flight.roll;
        player.verticalVelocity = 0.0f;
        player.onGround = false;
        player.viewBobWeight = 0.0f;
    }
}
