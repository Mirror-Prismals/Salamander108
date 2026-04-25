#pragma once

namespace PaniniProjectionSystemLogic {
    namespace {
        std::string getRegistryString(const BaseSystem& baseSystem, const char* key, const std::string& fallback) {
            if (!baseSystem.registry || !key) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            return std::get<std::string>(it->second);
        }

        bool getRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry || !key) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (!std::holds_alternative<std::string>(it->second)) return fallback;

            std::string value = std::get<std::string>(it->second);
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (value == "1" || value == "true" || value == "yes" || value == "on" || value == "panini") return true;
            if (value == "0" || value == "false" || value == "no" || value == "off" || value == "rectilinear") return false;
            return fallback;
        }

        float getRegistryFloat(const BaseSystem& baseSystem, const char* key, float fallback) {
            if (!baseSystem.registry || !key) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        bool projectionModeRequestsPanini(const BaseSystem& baseSystem) {
            std::string mode = getRegistryString(baseSystem, "ProjectionMode", "");
            std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return mode == "panini" || mode == "panini_projection" || mode == "paniniprojection";
        }
    }

    void UpdatePaniniProjection(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)dt;
        (void)win;
        if (!baseSystem.player) return;

        PlayerContext& player = *baseSystem.player;
        const bool enabled = projectionModeRequestsPanini(baseSystem);

        player.paniniProjectionEnabled = enabled;
        player.paniniProjectionVertexWarpEnabled = enabled
            && getRegistryBool(baseSystem, "PaniniProjectionVertexWarpEnabled", false);
        player.paniniProjectionDistance = glm::clamp(
            getRegistryFloat(baseSystem, "PaniniProjectionDistance", 1.0f),
            0.05f,
            8.0f
        );
        player.paniniProjectionCompression = glm::clamp(
            getRegistryFloat(baseSystem, "PaniniProjectionCompression", 1.0f),
            0.0f,
            1.0f
        );
        player.paniniProjectionStrength = enabled
            ? glm::clamp(getRegistryFloat(baseSystem, "PaniniProjectionStrength", 1.0f), 0.0f, 1.0f)
            : 0.0f;
        player.paniniProjectionZoom = glm::clamp(
            getRegistryFloat(baseSystem, "PaniniProjectionZoom", 1.0f),
            0.25f,
            4.0f
        );

        if (enabled && baseSystem.app) {
            const AppContext& app = *baseSystem.app;
            const float safeHeight = static_cast<float>(std::max(1u, app.windowHeight));
            const float aspect = static_cast<float>(std::max(1u, app.windowWidth)) / safeHeight;
            const float fovDegrees = glm::clamp(
                getRegistryFloat(baseSystem, "PaniniProjectionFovDegrees", 103.0f),
                45.0f,
                150.0f
            );
            player.projectionMatrix = glm::perspective(glm::radians(fovDegrees), aspect, 0.1f, 2000.0f);
        }
    }

    namespace {
        void applyProjectionWarpUniforms(const PlayerContext& player, const Shader& shader, bool enabled) {
            shader.setInt("paniniProjectionEnabled", enabled ? 1 : 0);
            shader.setVec3(
                "paniniProjectionParams",
                glm::vec3(
                    player.paniniProjectionDistance,
                    player.paniniProjectionCompression,
                    enabled ? player.paniniProjectionStrength : 0.0f
                )
            );
            shader.setFloat("paniniProjectionZoom", player.paniniProjectionZoom);
        }
    }

    void ApplyProjectionWarpUniforms(const PlayerContext& player, const Shader& shader) {
        applyProjectionWarpUniforms(
            player,
            shader,
            player.paniniProjectionEnabled && player.paniniProjectionVertexWarpEnabled
        );
    }

    void ApplyPostProjectionWarpUniforms(const PlayerContext& player, const Shader& shader) {
        applyProjectionWarpUniforms(player, shader, player.paniniProjectionEnabled);
    }

    void DisableProjectionWarpUniforms(const Shader& shader) {
        shader.setInt("paniniProjectionEnabled", 0);
        shader.setVec3(
            "paniniProjectionParams",
            glm::vec3(1.0f, 1.0f, 0.0f)
        );
        shader.setFloat("paniniProjectionZoom", 1.0f);
    }
}
