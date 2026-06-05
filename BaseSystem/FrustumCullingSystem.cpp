#pragma once

#include <cmath>

namespace FrustumCullingSystemLogic {
    namespace {
        void extractFrustumPlanes(const glm::mat4& viewProj, std::array<glm::vec4, 6>& planes);

        void captureFrozenSnapshot(const PlayerContext& player, FrustumCullingContext& frustum) {
            frustum.frozenCameraPosition = player.cameraPosition;
            frustum.frozenPrevCameraPosition = player.prevCameraPosition;
            frustum.frozenCameraYaw = player.cameraYaw;
            frustum.frozenCameraPitch = player.cameraPitch;
            frustum.frozenViewProj = player.projectionMatrix * player.viewMatrix;
            extractFrustumPlanes(frustum.frozenViewProj, frustum.frozenPlanes);
            frustum.frozenValid = true;
            frustum.lastViewProj = frustum.frozenViewProj;
            frustum.planes = frustum.frozenPlanes;
            frustum.valid = true;
        }

        void normalizePlane(glm::vec4& plane) {
            glm::vec3 n(plane.x, plane.y, plane.z);
            float len = glm::length(n);
            if (len <= 1e-6f) return;
            plane /= len;
        }

        void extractFrustumPlanes(const glm::mat4& viewProj, std::array<glm::vec4, 6>& planes) {
            glm::vec4 row0(viewProj[0][0], viewProj[1][0], viewProj[2][0], viewProj[3][0]);
            glm::vec4 row1(viewProj[0][1], viewProj[1][1], viewProj[2][1], viewProj[3][1]);
            glm::vec4 row2(viewProj[0][2], viewProj[1][2], viewProj[2][2], viewProj[3][2]);
            glm::vec4 row3(viewProj[0][3], viewProj[1][3], viewProj[2][3], viewProj[3][3]);

            planes[0] = row3 + row0;
            planes[1] = row3 - row0;
            planes[2] = row3 + row1;
            planes[3] = row3 - row1;
            planes[4] = row3 + row2;
            planes[5] = row3 - row2;

            for (auto& p : planes) normalizePlane(p);
        }

        bool aabbIntersectsFrustum(const std::array<glm::vec4, 6>& planes,
                                   const glm::vec3& minB,
                                   const glm::vec3& maxB,
                                   float margin) {
            const glm::vec3 expandedMin = minB - glm::vec3(margin);
            const glm::vec3 expandedMax = maxB + glm::vec3(margin);
            for (const glm::vec4& plane : planes) {
                const glm::vec3 n(plane.x, plane.y, plane.z);
                const glm::vec3 positive(
                    n.x >= 0.0f ? expandedMax.x : expandedMin.x,
                    n.y >= 0.0f ? expandedMax.y : expandedMin.y,
                    n.z >= 0.0f ? expandedMax.z : expandedMin.z
                );
                if (glm::dot(n, positive) + plane.w < 0.0f) return false;
            }
            return true;
        }

        bool pointPassesPlanarView(const glm::vec2& cameraXZ,
                                   const glm::vec2& forwardXZ,
                                   float nearRadiusSq,
                                   float cosHalfAngle,
                                   const glm::vec2& point) {
            const glm::vec2 delta = point - cameraXZ;
            const float dist2 = glm::dot(delta, delta);
            if (dist2 <= nearRadiusSq) return true;
            if (dist2 <= 1e-6f) return true;
            const float invDist = 1.0f / std::sqrt(dist2);
            return glm::dot(delta * invDist, forwardXZ) >= cosHalfAngle;
        }

        bool aabbIntersectsPlanarTerrainView(const BaseSystem& baseSystem,
                                             const glm::vec3& minB,
                                             const glm::vec3& maxB) {
            if (!baseSystem.player) return true;

            const glm::vec3 cameraPos = GetCullingCameraPosition(baseSystem);
            const glm::vec2 cameraXZ(cameraPos.x, cameraPos.z);
            const float yawRadians = glm::radians(GetCullingCameraYaw(baseSystem));
            glm::vec2 forwardXZ(std::cos(yawRadians), std::sin(yawRadians));
            const float forwardLen2 = glm::dot(forwardXZ, forwardXZ);
            if (forwardLen2 > 1e-6f) {
                forwardXZ /= std::sqrt(forwardLen2);
            } else {
                forwardXZ = glm::vec2(0.0f, -1.0f);
            }

            const float defaultNearRadius = std::max(
                0.0f,
                RenderInitSystemLogic::getRegistryFloat(baseSystem, "voxelCpuViewNearRadiusBlocks", 96.0f)
            );
            const float nearRadius = std::max(
                0.0f,
                RenderInitSystemLogic::getRegistryFloat(baseSystem, "voxelTerrainPlanarNearRadiusBlocks", defaultNearRadius)
            );
            const float halfAngleDegrees = glm::clamp(
                RenderInitSystemLogic::getRegistryFloat(baseSystem, "voxelTerrainPlanarHalfAngleDegrees", 120.0f),
                1.0f,
                180.0f
            );
            const float margin = std::max(
                0.0f,
                RenderInitSystemLogic::getRegistryFloat(
                    baseSystem,
                    "voxelTerrainPlanarMarginBlocks",
                    baseSystem.frustumCulling ? baseSystem.frustumCulling->margin : 24.0f
                )
            );
            const float minX = minB.x - margin;
            const float maxX = maxB.x + margin;
            const float minZ = minB.z - margin;
            const float maxZ = maxB.z + margin;
            const glm::vec2 center((minX + maxX) * 0.5f, (minZ + maxZ) * 0.5f);
            const float nearRadiusSq = nearRadius * nearRadius;
            const float cosHalfAngle = std::cos(glm::radians(halfAngleDegrees));

            if (pointPassesPlanarView(cameraXZ, forwardXZ, nearRadiusSq, cosHalfAngle, center)) return true;
            if (pointPassesPlanarView(cameraXZ, forwardXZ, nearRadiusSq, cosHalfAngle, glm::vec2(minX, minZ))) return true;
            if (pointPassesPlanarView(cameraXZ, forwardXZ, nearRadiusSq, cosHalfAngle, glm::vec2(maxX, minZ))) return true;
            if (pointPassesPlanarView(cameraXZ, forwardXZ, nearRadiusSq, cosHalfAngle, glm::vec2(minX, maxZ))) return true;
            return pointPassesPlanarView(cameraXZ, forwardXZ, nearRadiusSq, cosHalfAngle, glm::vec2(maxX, maxZ));
        }
    }

    void UpdateFrustumCulling(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle) {
        if (!baseSystem.player || !baseSystem.frustumCulling) return;
        FrustumCullingContext& frustum = *baseSystem.frustumCulling;
        frustum.enabled = RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelFrustumCulling", true);
        frustum.margin = glm::clamp(RenderInitSystemLogic::getRegistryFloat(baseSystem, "voxelFrustumMargin", 12.0f), 0.0f, 128.0f);
        if (!frustum.enabled) {
            frustum.valid = false;
            return;
        }
        const PlayerContext& player = *baseSystem.player;
        if (frustum.debugFrozen) {
            if (!frustum.frozenValid) {
                captureFrozenSnapshot(player, frustum);
            } else {
                frustum.lastViewProj = frustum.frozenViewProj;
                frustum.planes = frustum.frozenPlanes;
                frustum.valid = true;
            }
            return;
        }
        const glm::mat4 viewProj = player.projectionMatrix * player.viewMatrix;
        if (!frustum.valid || frustum.lastViewProj != viewProj) {
            frustum.lastViewProj = viewProj;
            extractFrustumPlanes(viewProj, frustum.planes);
            frustum.valid = true;
        }
    }

    void SetDebugFrozen(BaseSystem& baseSystem, bool frozen) {
        if (!baseSystem.frustumCulling) return;
        FrustumCullingContext& frustum = *baseSystem.frustumCulling;
        if (frustum.debugFrozen == frozen) return;
        frustum.debugFrozen = frozen;
        if (frozen) {
            if (!baseSystem.player) return;
            captureFrozenSnapshot(*baseSystem.player, frustum);
            std::cout << "[FrustumCulling] debug freeze on" << std::endl;
            return;
        }
        frustum.frozenValid = false;
        if (baseSystem.player) {
            const glm::mat4 viewProj = baseSystem.player->projectionMatrix * baseSystem.player->viewMatrix;
            frustum.lastViewProj = viewProj;
            extractFrustumPlanes(viewProj, frustum.planes);
            frustum.valid = true;
        } else {
            frustum.valid = false;
        }
        std::cout << "[FrustumCulling] debug freeze off" << std::endl;
    }

    bool IsDebugFrozen(const BaseSystem& baseSystem) {
        return baseSystem.frustumCulling && baseSystem.frustumCulling->debugFrozen;
    }

    glm::vec3 GetCullingCameraPosition(const BaseSystem& baseSystem) {
        if (!baseSystem.player) return glm::vec3(0.0f);
        if (baseSystem.frustumCulling && baseSystem.frustumCulling->debugFrozen && baseSystem.frustumCulling->frozenValid) {
            return baseSystem.frustumCulling->frozenCameraPosition;
        }
        return baseSystem.player->cameraPosition;
    }

    glm::vec3 GetCullingPreviousCameraPosition(const BaseSystem& baseSystem) {
        if (!baseSystem.player) return glm::vec3(0.0f);
        if (baseSystem.frustumCulling && baseSystem.frustumCulling->debugFrozen && baseSystem.frustumCulling->frozenValid) {
            return baseSystem.frustumCulling->frozenPrevCameraPosition;
        }
        return baseSystem.player->prevCameraPosition;
    }

    float GetCullingCameraYaw(const BaseSystem& baseSystem) {
        if (!baseSystem.player) return -90.0f;
        if (baseSystem.frustumCulling && baseSystem.frustumCulling->debugFrozen && baseSystem.frustumCulling->frozenValid) {
            return baseSystem.frustumCulling->frozenCameraYaw;
        }
        return baseSystem.player->cameraYaw;
    }

    float GetCullingCameraPitch(const BaseSystem& baseSystem) {
        if (!baseSystem.player) return 0.0f;
        if (baseSystem.frustumCulling && baseSystem.frustumCulling->debugFrozen && baseSystem.frustumCulling->frozenValid) {
            return baseSystem.frustumCulling->frozenCameraPitch;
        }
        return baseSystem.player->cameraPitch;
    }

    bool ShouldRenderWorldAabb(const BaseSystem& baseSystem, const glm::vec3& minB, const glm::vec3& maxB) {
        if (!baseSystem.frustumCulling) return true;
        const FrustumCullingContext& frustum = *baseSystem.frustumCulling;
        if (!frustum.enabled || !frustum.valid) return true;
        return aabbIntersectsFrustum(frustum.planes, minB, maxB, frustum.margin);
    }

    bool IsVoxelTerrainPlanarCullingEnabled(const BaseSystem& baseSystem) {
        return RenderInitSystemLogic::getRegistryBool(baseSystem, "voxelTerrainPlanarCullingEnabled", false);
    }

    bool ShouldRenderVoxelTerrainAabb(const BaseSystem& baseSystem, const glm::vec3& minB, const glm::vec3& maxB) {
        if (baseSystem.frustumCulling && !baseSystem.frustumCulling->enabled) return true;
        if (IsVoxelTerrainPlanarCullingEnabled(baseSystem)) {
            return aabbIntersectsPlanarTerrainView(baseSystem, minB, maxB);
        }
        return ShouldRenderWorldAabb(baseSystem, minB, maxB);
    }

    bool ShouldRenderVoxelSection(const BaseSystem& baseSystem, const VoxelSection& section) {
        const glm::vec3 minB(
            static_cast<float>(section.coord.x * section.size),
            static_cast<float>(section.coord.y * section.size),
            static_cast<float>(section.coord.z * section.size)
        );
        const glm::vec3 maxB = minB + glm::vec3(static_cast<float>(section.size));
        return ShouldRenderVoxelTerrainAabb(baseSystem, minB, maxB);
    }
}
