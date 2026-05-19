#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace OcclusionCullingSystemLogic {
    namespace {
        struct ClusterCandidate {
            const VoxelRenderCluster* cluster = nullptr;
            bool isFar = false;
        };

        glm::vec3 resolveOcclusionCameraPosition(const BaseSystem& baseSystem) {
            glm::vec3 cameraPos = FrustumCullingSystemLogic::GetCullingCameraPosition(baseSystem);
            if (!FrustumCullingSystemLogic::IsDebugFrozen(baseSystem)
                && baseSystem.securityCamera
                && baseSystem.securityCamera->dawViewActive) {
                const SecurityCameraContext& securityCamera = *baseSystem.securityCamera;
                if (glm::length(securityCamera.viewForward) > 1e-4f) {
                    cameraPos = securityCamera.viewPosition;
                }
            }
            return cameraPos;
        }

        glm::mat4 resolveOcclusionViewProj(const BaseSystem& baseSystem) {
            if (baseSystem.frustumCulling && baseSystem.frustumCulling->valid) {
                return baseSystem.frustumCulling->lastViewProj;
            }
            if (baseSystem.player) {
                return baseSystem.player->projectionMatrix * baseSystem.player->viewMatrix;
            }
            return glm::mat4(1.0f);
        }

        OcclusionAabbKey makeOcclusionAabbKey(const glm::vec3& minB, const glm::vec3& maxB) {
            auto quantize = [](float value) -> int {
                return static_cast<int>(std::lround(value * 2.0f));
            };
            OcclusionAabbKey key{};
            key.minX2 = quantize(minB.x);
            key.minY2 = quantize(minB.y);
            key.minZ2 = quantize(minB.z);
            key.maxX2 = quantize(maxB.x);
            key.maxY2 = quantize(maxB.y);
            key.maxZ2 = quantize(maxB.z);
            return key;
        }

        bool clusterHasOccluderGeometry(const VoxelRenderCluster& cluster) {
            if (cluster.buffers.faceBuffers.mergedClippedOpaqueCount > 0
                || cluster.buffers.faceBuffers.mergedOpaqueCount > 0) {
                return true;
            }
            for (int faceType = 0; faceType < 6; ++faceType) {
                if (cluster.buffers.faceBuffers.clippedOpaqueCounts[faceType] > 0
                    || cluster.buffers.faceBuffers.opaqueCounts[faceType] > 0) {
                    return true;
                }
            }
            return false;
        }

        int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stoi(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
        }

        bool ensureOcclusionRenderTarget(BaseSystem& baseSystem,
                                         PlatformWindowHandle win,
                                         int& outWidth,
                                         int& outHeight) {
            if (!baseSystem.renderer || !baseSystem.renderBackend) return false;
            RendererContext& renderer = *baseSystem.renderer;
            IRenderBackend& renderBackend = *baseSystem.renderBackend;

            int framebufferWidth = baseSystem.app ? static_cast<int>(baseSystem.app->windowWidth) : 1920;
            int framebufferHeight = baseSystem.app ? static_cast<int>(baseSystem.app->windowHeight) : 1080;
            if (win) {
                renderBackend.getFramebufferSize(win, framebufferWidth, framebufferHeight);
            }
            framebufferWidth = std::max(framebufferWidth, 1);
            framebufferHeight = std::max(framebufferHeight, 1);

            const int targetWidth = std::max(96, framebufferWidth / 12);
            const int targetHeight = std::max(54, framebufferHeight / 12);
            if (renderer.occlusionHzbFBO != 0
                && renderer.occlusionHzbTex != 0
                && renderer.occlusionHzbWidth == targetWidth
                && renderer.occlusionHzbHeight == targetHeight) {
                outWidth = targetWidth;
                outHeight = targetHeight;
                return true;
            }

            renderBackend.destroyColorRenderTarget(renderer.occlusionHzbFBO, renderer.occlusionHzbTex);
            renderer.occlusionHzbWidth = 0;
            renderer.occlusionHzbHeight = 0;

            if (!renderBackend.createColorRenderTarget(
                    targetWidth,
                    targetHeight,
                    renderer.occlusionHzbFBO,
                    renderer.occlusionHzbTex
                )) {
                renderer.occlusionHzbFBO = 0;
                renderer.occlusionHzbTex = 0;
                return false;
            }

            renderer.occlusionHzbWidth = targetWidth;
            renderer.occlusionHzbHeight = targetHeight;
            outWidth = targetWidth;
            outHeight = targetHeight;
            return true;
        }

        float unpackDepthRgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            const float rf = static_cast<float>(r) / 255.0f;
            const float gf = static_cast<float>(g) / 255.0f;
            const float bf = static_cast<float>(b) / 255.0f;
            const float af = static_cast<float>(a) / 255.0f;
            const float depth =
                rf
                + gf / 255.0f
                + bf / 65025.0f
                + af / 16581375.0f;
            return glm::clamp(depth, 0.0f, 1.0f);
        }

        void buildDepthPyramidFromPixels(OcclusionCullingContext& occlusion,
                                         int width,
                                         int height,
                                         const std::vector<unsigned char>& rgbaPixels) {
            const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
            occlusion.hzbBaseDepth.assign(pixelCount, 1.0f);
            for (size_t i = 0; i < pixelCount; ++i) {
                const size_t base = i * 4u;
                occlusion.hzbBaseDepth[i] = unpackDepthRgba8(
                    rgbaPixels[base + 0u],
                    rgbaPixels[base + 1u],
                    rgbaPixels[base + 2u],
                    rgbaPixels[base + 3u]
                );
            }

            occlusion.hzbMipLevels.clear();
            occlusion.hzbMipSizes.clear();
            occlusion.hzbMipLevels.push_back(occlusion.hzbBaseDepth);
            occlusion.hzbMipSizes.push_back(glm::ivec2(width, height));

            int levelWidth = width;
            int levelHeight = height;
            while (levelWidth > 1 || levelHeight > 1) {
                const std::vector<float>& src = occlusion.hzbMipLevels.back();
                const int nextWidth = std::max(1, (levelWidth + 1) / 2);
                const int nextHeight = std::max(1, (levelHeight + 1) / 2);
                std::vector<float> next(static_cast<size_t>(nextWidth) * static_cast<size_t>(nextHeight), 1.0f);

                for (int y = 0; y < nextHeight; ++y) {
                    for (int x = 0; x < nextWidth; ++x) {
                        float maxDepth = 0.0f;
                        for (int dy = 0; dy < 2; ++dy) {
                            for (int dx = 0; dx < 2; ++dx) {
                                const int srcX = x * 2 + dx;
                                const int srcY = y * 2 + dy;
                                if (srcX >= levelWidth || srcY >= levelHeight) continue;
                                const size_t srcIndex = static_cast<size_t>(srcY) * static_cast<size_t>(levelWidth)
                                    + static_cast<size_t>(srcX);
                                maxDepth = std::max(maxDepth, src[srcIndex]);
                            }
                        }
                        const size_t dstIndex = static_cast<size_t>(y) * static_cast<size_t>(nextWidth)
                            + static_cast<size_t>(x);
                        next[dstIndex] = maxDepth;
                    }
                }

                occlusion.hzbMipLevels.push_back(std::move(next));
                occlusion.hzbMipSizes.push_back(glm::ivec2(nextWidth, nextHeight));
                levelWidth = nextWidth;
                levelHeight = nextHeight;
            }

            occlusion.hzbValid = !occlusion.hzbMipLevels.empty();
        }

        bool projectAabbToCapture(const glm::mat4& viewProj,
                                  const glm::vec3& minB,
                                  const glm::vec3& maxB,
                                  int width,
                                  int height,
                                  int& outMinX,
                                  int& outMaxX,
                                  int& outMinY,
                                  int& outMaxY,
                                  float& outNearestDepth) {
            const std::array<glm::vec3, 8> corners = {{
                {minB.x, minB.y, minB.z},
                {maxB.x, minB.y, minB.z},
                {minB.x, maxB.y, minB.z},
                {maxB.x, maxB.y, minB.z},
                {minB.x, minB.y, maxB.z},
                {maxB.x, minB.y, maxB.z},
                {minB.x, maxB.y, maxB.z},
                {maxB.x, maxB.y, maxB.z},
            }};

            float minScreenX = static_cast<float>(width);
            float maxScreenX = 0.0f;
            float minScreenY = static_cast<float>(height);
            float maxScreenY = 0.0f;
            float nearestDepth = 1.0f;
            bool anyValid = false;

            for (const glm::vec3& corner : corners) {
                const glm::vec4 clip = viewProj * glm::vec4(corner, 1.0f);
                if (clip.w <= 1e-5f) {
                    return false;
                }
                const glm::vec3 ndc = glm::vec3(clip) / clip.w;
                const float depth = glm::clamp(ndc.z * 0.5f + 0.5f, 0.0f, 1.0f);
                const float screenX = (ndc.x * 0.5f + 0.5f) * static_cast<float>(width);
                const float screenY = (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(height);
                minScreenX = std::min(minScreenX, screenX);
                maxScreenX = std::max(maxScreenX, screenX);
                minScreenY = std::min(minScreenY, screenY);
                maxScreenY = std::max(maxScreenY, screenY);
                nearestDepth = std::min(nearestDepth, depth);
                anyValid = true;
            }

            if (!anyValid) return false;
            if (maxScreenX < 0.0f || maxScreenY < 0.0f
                || minScreenX >= static_cast<float>(width)
                || minScreenY >= static_cast<float>(height)) {
                return false;
            }

            outMinX = std::clamp(static_cast<int>(std::floor(minScreenX)), 0, width - 1);
            outMaxX = std::clamp(static_cast<int>(std::ceil(maxScreenX)), 0, width - 1);
            outMinY = std::clamp(static_cast<int>(std::floor(minScreenY)), 0, height - 1);
            outMaxY = std::clamp(static_cast<int>(std::ceil(maxScreenY)), 0, height - 1);
            outNearestDepth = nearestDepth;
            return outMinX <= outMaxX && outMinY <= outMaxY;
        }

        bool queryDepthPyramidOcclusion(const OcclusionCullingContext& occlusion,
                                        int minX,
                                        int maxX,
                                        int minY,
                                        int maxY,
                                        float nearestDepth,
                                        float depthBias) {
            if (!occlusion.hzbValid || occlusion.hzbMipLevels.empty() || occlusion.hzbMipSizes.empty()) {
                return false;
            }

            int rectWidth = std::max(1, maxX - minX + 1);
            int rectHeight = std::max(1, maxY - minY + 1);
            int level = 0;
            while (level + 1 < static_cast<int>(occlusion.hzbMipLevels.size())
                && (rectWidth > 2 || rectHeight > 2)) {
                rectWidth = std::max(1, (rectWidth + 1) / 2);
                rectHeight = std::max(1, (rectHeight + 1) / 2);
                ++level;
            }

            const glm::ivec2 levelSize = occlusion.hzbMipSizes[static_cast<size_t>(level)];
            const std::vector<float>& levelDepths = occlusion.hzbMipLevels[static_cast<size_t>(level)];
            const int levelMinX = std::clamp(minX >> level, 0, levelSize.x - 1);
            const int levelMaxX = std::clamp(maxX >> level, 0, levelSize.x - 1);
            const int levelMinY = std::clamp(minY >> level, 0, levelSize.y - 1);
            const int levelMaxY = std::clamp(maxY >> level, 0, levelSize.y - 1);

            float maxDepth = 0.0f;
            for (int y = levelMinY; y <= levelMaxY; ++y) {
                for (int x = levelMinX; x <= levelMaxX; ++x) {
                    const size_t index = static_cast<size_t>(y) * static_cast<size_t>(levelSize.x)
                        + static_cast<size_t>(x);
                    maxDepth = std::max(maxDepth, levelDepths[index]);
                }
            }

            return maxDepth < (nearestDepth - depthBias);
        }

        bool readPendingOcclusionDepth(BaseSystem& baseSystem, OcclusionCullingContext& occlusion) {
            if (!occlusion.hzbReadbackPending) return occlusion.hzbValid;
            occlusion.hzbReadbackPending = false;

            if (!baseSystem.renderer || !baseSystem.renderBackend) {
                occlusion.hzbValid = false;
                return false;
            }
            RendererContext& renderer = *baseSystem.renderer;
            if (renderer.occlusionHzbTex == 0
                || occlusion.hzbPendingWidth <= 0
                || occlusion.hzbPendingHeight <= 0
                || renderer.occlusionHzbWidth != occlusion.hzbPendingWidth
                || renderer.occlusionHzbHeight != occlusion.hzbPendingHeight) {
                occlusion.hzbValid = false;
                return false;
            }

            std::vector<unsigned char> rgbaPixels;
            const auto readbackStart = std::chrono::steady_clock::now();
            if (!baseSystem.renderBackend->readTexture2DRgba(
                    renderer.occlusionHzbTex,
                    occlusion.hzbPendingWidth,
                    occlusion.hzbPendingHeight,
                    rgbaPixels
                )) {
                occlusion.hzbValid = false;
                return false;
            }
            occlusion.hzbReadbackMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - readbackStart
            ).count();

            const auto buildStart = std::chrono::steady_clock::now();
            buildDepthPyramidFromPixels(
                occlusion,
                occlusion.hzbPendingWidth,
                occlusion.hzbPendingHeight,
                rgbaPixels
            );
            occlusion.hzbBuildMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - buildStart
            ).count();
            occlusion.hzbViewProj = occlusion.hzbPendingViewProj;
            occlusion.hzbCameraPosition = occlusion.hzbPendingCameraPosition;
            occlusion.lastReadbackFrame = baseSystem.frameIndex;
            return occlusion.hzbValid;
        }

        bool captureOcclusionDepth(BaseSystem& baseSystem,
                                   const glm::mat4& viewProj,
                                   const std::vector<const VoxelRenderCluster*>& occluders,
                                   int width,
                                   int height) {
            if (!baseSystem.renderer || !baseSystem.renderBackend) return false;
            RendererContext& renderer = *baseSystem.renderer;
            IRenderBackend& renderBackend = *baseSystem.renderBackend;
            if (!renderer.occlusionFaceShader || renderer.occlusionHzbFBO == 0 || renderer.occlusionHzbTex == 0) {
                return false;
            }

            renderBackend.beginOffscreenColorPass(
                renderer.occlusionHzbFBO,
                width,
                height,
                1.0f,
                1.0f,
                1.0f,
                1.0f
            );
            renderBackend.setDepthTestEnabled(true);
            renderBackend.setDepthWriteEnabled(true);
            renderBackend.setBlendEnabled(false);
            renderBackend.setCullEnabled(true);
            renderBackend.setCullBackFaceCCW();

            renderer.occlusionFaceShader->use();
            renderer.occlusionFaceShader->setMat4("model", glm::mat4(1.0f));
            renderer.occlusionFaceShader->setMat4("mvp", viewProj);
            renderer.occlusionFaceShader->setInt("faceType", 0);

            if (renderer.packedTerrainOcclusionFaceShader) {
                renderer.packedTerrainOcclusionFaceShader->use();
                renderer.packedTerrainOcclusionFaceShader->setMat4("model", glm::mat4(1.0f));
                renderer.packedTerrainOcclusionFaceShader->setMat4("mvp", viewProj);
                renderer.packedTerrainOcclusionFaceShader->setInt("faceType", 0);
                for (const VoxelRenderCluster* cluster : occluders) {
                    if (!cluster) continue;
                    const VoxelFaceRenderBuffers& faceBuffers = cluster->buffers.faceBuffers;
                    if (faceBuffers.packedClippedOpaqueCount > 0 && faceBuffers.packedClippedOpaqueVao != 0) {
                        renderBackend.bindVertexArray(faceBuffers.packedClippedOpaqueVao);
                        renderBackend.drawArraysTrianglesInstanced(0, 3, faceBuffers.packedClippedOpaqueCount);
                    }
                }
                renderer.occlusionFaceShader->use();
            }

            for (const VoxelRenderCluster* cluster : occluders) {
                if (!cluster) continue;
                const VoxelFaceRenderBuffers& faceBuffers = cluster->buffers.faceBuffers;
                if (faceBuffers.mergedClippedOpaqueCount > 0 && faceBuffers.mergedClippedOpaqueVao != 0) {
                    renderBackend.bindVertexArray(faceBuffers.mergedClippedOpaqueVao);
                    renderBackend.drawArraysTrianglesInstanced(0, 3, faceBuffers.mergedClippedOpaqueCount);
                }
                if (faceBuffers.mergedOpaqueCount > 0 && faceBuffers.mergedOpaqueVao != 0) {
                    renderBackend.bindVertexArray(faceBuffers.mergedOpaqueVao);
                    renderBackend.drawArraysTrianglesInstanced(0, 6, faceBuffers.mergedOpaqueCount);
                }
                if (faceBuffers.mergedClippedOpaqueCount == 0 && faceBuffers.mergedOpaqueCount == 0) {
                    for (int faceType = 0; faceType < 6; ++faceType) {
                        const int clippedCount = faceBuffers.clippedOpaqueCounts[faceType];
                        const RenderHandle clippedVao = faceBuffers.clippedOpaqueVaos[faceType];
                        if (clippedCount > 0 && clippedVao != 0) {
                            renderer.occlusionFaceShader->setInt("faceType", faceType);
                            renderBackend.bindVertexArray(clippedVao);
                            renderBackend.drawArraysTrianglesInstanced(0, 3, clippedCount);
                        }
                        const int opaqueCount = faceBuffers.opaqueCounts[faceType];
                        const RenderHandle opaqueVao = faceBuffers.opaqueVaos[faceType];
                        if (opaqueCount > 0 && opaqueVao != 0) {
                            renderer.occlusionFaceShader->setInt("faceType", faceType);
                            renderBackend.bindVertexArray(opaqueVao);
                            renderBackend.drawArraysTrianglesInstanced(0, 6, opaqueCount);
                        }
                    }
                }
            }

            renderBackend.unbindVertexArray();
            renderBackend.endOffscreenColorPass();
            return true;
        }
    }

    bool IsVoxelSectionOccluded(const BaseSystem& baseSystem, const VoxelSectionKey& key) {
        (void)baseSystem;
        (void)key;
        return false;
    }

    bool IsWorldAabbOccluded(const BaseSystem& baseSystem, const glm::vec3& minB, const glm::vec3& maxB) {
        if (!getRegistryBool(baseSystem, "OcclusionCullingSystem", true)) return false;
        if (!baseSystem.occlusionCulling) return false;
        const OcclusionCullingContext& occlusion = *baseSystem.occlusionCulling;
        if (!occlusion.enabled || !occlusion.hzbValid) return false;
        return occlusion.occludedClusterBounds.count(makeOcclusionAabbKey(minB, maxB)) > 0;
    }

    void SetDebugFrozen(BaseSystem& baseSystem, bool frozen) {
        if (!baseSystem.occlusionCulling) return;
        OcclusionCullingContext& occlusion = *baseSystem.occlusionCulling;
        if (occlusion.debugFrozen == frozen) return;
        occlusion.debugFrozen = frozen;
        if (frozen) {
            occlusion.frozenValid = false;
            std::cout << "[OcclusionCulling] debug freeze on" << std::endl;
            return;
        }
        occlusion.frozenValid = false;
        std::cout << "[OcclusionCulling] debug freeze off" << std::endl;
    }

    bool IsDebugFrozen(const BaseSystem& baseSystem) {
        return baseSystem.occlusionCulling && baseSystem.occlusionCulling->debugFrozen;
    }

    void UpdateOcclusionCulling(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle win) {
        if (!baseSystem.voxelWorld || !baseSystem.voxelRender || !baseSystem.occlusionCulling || !baseSystem.player) return;

        VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
        OcclusionCullingContext& occlusion = *baseSystem.occlusionCulling;
        if (!getRegistryBool(baseSystem, "OcclusionCullingSystem", true)) {
            occlusion.occludedVoxelSections.clear();
            occlusion.occludedClusterBounds.clear();
            occlusion.hzbValid = false;
            occlusion.hzbReadbackPending = false;
            return;
        }

        if (occlusion.debugFrozen && occlusion.frozenValid) {
            return;
        }

        occlusion.occludedVoxelSections.clear();
        occlusion.occludedClusterBounds.clear();
        occlusion.testedSectionCount = 0;
        occlusion.visibleSectionCount = 0;
        occlusion.occludedSectionCount = 0;
        occlusion.nearKeptSectionCount = 0;
        occlusion.frustumRejectedSectionCount = 0;
        occlusion.farTestedCount = 0;
        occlusion.farOccludedCount = 0;
        occlusion.hzbOccluderClusterCount = 0;
        occlusion.hzbCaptureMs = 0.0f;
        occlusion.hzbReadbackMs = 0.0f;
        occlusion.hzbBuildMs = 0.0f;
        occlusion.hzbQueryMs = 0.0f;
        occlusion.lastBuildFrame = baseSystem.frameIndex;
        occlusion.lastCameraPosition = resolveOcclusionCameraPosition(baseSystem);
        occlusion.azimuthHorizon.fill(-std::numeric_limits<float>::infinity());

        const bool nearTerrainEnabled = baseSystem.voxelWorld->enabled && !voxelRender.renderClusters.empty();
        const bool farTerrainFeatureEnabled = getRegistryBool(baseSystem, "FarTerrainEnabled", true);
        const bool farTerrainEnabled = baseSystem.farTerrain
            && farTerrainFeatureEnabled
            && baseSystem.farTerrain->enabled
            && (!baseSystem.farTerrain->bodyRenderClusters.empty()
                || !baseSystem.farTerrain->handoffRenderClusters.empty());
        const bool cullFarTerrain = getRegistryBool(baseSystem, "OcclusionCullingCullFarTerrain", false);
        if (!nearTerrainEnabled && !farTerrainEnabled) {
            occlusion.hzbValid = false;
            return;
        }

        const int sectionSize = std::max(1, baseSystem.voxelWorld->sectionSize);
        const float keepNearRadius = static_cast<float>(sectionSize) * 6.0f;
        occlusion.keepNearRadius = keepNearRadius;
        const float keepNearRadiusSq = keepNearRadius * keepNearRadius;
        const glm::mat4 viewProj = resolveOcclusionViewProj(baseSystem);

        std::vector<ClusterCandidate> candidates;
        std::vector<const VoxelRenderCluster*> occluders;
        size_t clusterCapacity = 0;
        if (nearTerrainEnabled) {
            for (const auto& [_, clusters] : voxelRender.renderClusters) clusterCapacity += clusters.size();
        }
        if (farTerrainEnabled) {
            clusterCapacity += baseSystem.farTerrain->bodyRenderClusters.size();
            clusterCapacity += baseSystem.farTerrain->handoffRenderClusters.size();
        }
        candidates.reserve(clusterCapacity);
        occluders.reserve(clusterCapacity);

        if (nearTerrainEnabled) {
            for (const auto& [_, clusters] : voxelRender.renderClusters) {
                for (const VoxelRenderCluster& cluster : clusters) {
                    if (!FrustumCullingSystemLogic::ShouldRenderWorldAabb(baseSystem, cluster.minBounds, cluster.maxBounds)) {
                        occlusion.frustumRejectedSectionCount += 1;
                        continue;
                    }
                    candidates.push_back({&cluster, false});
                    if (clusterHasOccluderGeometry(cluster)) {
                        occluders.push_back(&cluster);
                    }
                }
            }
        }
        if (farTerrainEnabled) {
            auto appendFarClusters = [&](const std::vector<VoxelRenderCluster>& clusters) {
                for (const VoxelRenderCluster& cluster : clusters) {
                    if (!FrustumCullingSystemLogic::ShouldRenderWorldAabb(baseSystem, cluster.minBounds, cluster.maxBounds)) {
                        occlusion.frustumRejectedSectionCount += 1;
                        continue;
                    }
                    if (cullFarTerrain) {
                        candidates.push_back({&cluster, true});
                    }
                    if (clusterHasOccluderGeometry(cluster)) {
                        occluders.push_back(&cluster);
                    }
                }
            };
            appendFarClusters(baseSystem.farTerrain->bodyRenderClusters);
            appendFarClusters(baseSystem.farTerrain->handoffRenderClusters);
        }

        readPendingOcclusionDepth(baseSystem, occlusion);

        occlusion.hzbOccluderClusterCount = occluders.size();
        if (candidates.empty() || occluders.empty()) {
            occlusion.visibleSectionCount = candidates.size();
            occlusion.hzbValid = false;
            if (occlusion.debugFrozen) occlusion.frozenValid = true;
            return;
        }

        const float maxReuseCameraMove = std::max(
            0.0f,
            getRegistryFloat(
                baseSystem,
                "OcclusionCullingMaxReuseCameraMove",
                std::max(2.0f, static_cast<float>(sectionSize) * 0.5f)
            )
        );
        const glm::vec3 reuseDelta = occlusion.lastCameraPosition - occlusion.hzbCameraPosition;
        const bool reuseCameraCloseEnough = occlusion.debugFrozen
            || maxReuseCameraMove <= 0.0f
            || glm::dot(reuseDelta, reuseDelta) <= maxReuseCameraMove * maxReuseCameraMove;
        const bool hzbUsable = occlusion.hzbValid
            && !occlusion.hzbMipLevels.empty()
            && !occlusion.hzbMipSizes.empty()
            && reuseCameraCloseEnough;
        const float depthBias = glm::clamp(
            getRegistryFloat(baseSystem, "OcclusionCullingDepthBias", 0.01f),
            0.0f,
            0.10f
        );
        const int screenPaddingPixels = std::max(
            0,
            std::min(8, getRegistryInt(baseSystem, "OcclusionCullingScreenPaddingPixels", 1))
        );

        if (hzbUsable) {
            const glm::ivec2 querySize = occlusion.hzbMipSizes.front();
            const auto queryStart = std::chrono::steady_clock::now();
            for (const ClusterCandidate& candidate : candidates) {
                const VoxelRenderCluster* cluster = candidate.cluster;
                if (!cluster) continue;
                occlusion.testedSectionCount += 1;
                if (candidate.isFar) {
                    occlusion.farTestedCount += 1;
                }

                const glm::vec3 center = 0.5f * (cluster->minBounds + cluster->maxBounds);
                const glm::vec3 delta = center - occlusion.lastCameraPosition;
                const float distanceSq = glm::dot(delta, delta);
                if (distanceSq <= keepNearRadiusSq) {
                    occlusion.visibleSectionCount += 1;
                    occlusion.nearKeptSectionCount += 1;
                    continue;
                }

                int minX = 0;
                int maxX = 0;
                int minY = 0;
                int maxY = 0;
                float nearestDepth = 1.0f;
                if (!projectAabbToCapture(
                        occlusion.hzbViewProj,
                        cluster->minBounds,
                        cluster->maxBounds,
                        querySize.x,
                        querySize.y,
                        minX,
                        maxX,
                        minY,
                        maxY,
                        nearestDepth
                    )) {
                    occlusion.visibleSectionCount += 1;
                    continue;
                }

                minX = std::max(0, minX - screenPaddingPixels);
                maxX = std::min(querySize.x - 1, maxX + screenPaddingPixels);
                minY = std::max(0, minY - screenPaddingPixels);
                maxY = std::min(querySize.y - 1, maxY + screenPaddingPixels);

                if (queryDepthPyramidOcclusion(occlusion, minX, maxX, minY, maxY, nearestDepth, depthBias)) {
                    occlusion.occludedSectionCount += 1;
                    if (candidate.isFar) {
                        occlusion.farOccludedCount += 1;
                    }
                    occlusion.occludedClusterBounds.insert(makeOcclusionAabbKey(cluster->minBounds, cluster->maxBounds));
                } else {
                    occlusion.visibleSectionCount += 1;
                }
            }
            occlusion.hzbQueryMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - queryStart
            ).count();
        } else {
            occlusion.visibleSectionCount = candidates.size();
        }

        const int captureFrameInterval = std::max(
            1,
            getRegistryInt(baseSystem, "OcclusionCullingFrameInterval", 1)
        );
        const uint64_t framesSinceCapture = (baseSystem.frameIndex >= occlusion.lastCaptureFrame)
            ? (baseSystem.frameIndex - occlusion.lastCaptureFrame)
            : static_cast<uint64_t>(captureFrameInterval);
        const bool neverCaptured = !occlusion.hzbValid
            && !occlusion.hzbReadbackPending
            && occlusion.lastCaptureFrame == 0;
        const bool shouldCapture = !occlusion.hzbReadbackPending
            && (neverCaptured || !hzbUsable || framesSinceCapture >= static_cast<uint64_t>(captureFrameInterval));

        if (!shouldCapture) {
            if (occlusion.debugFrozen) occlusion.frozenValid = true;
            return;
        }

        int captureWidth = 0;
        int captureHeight = 0;
        if (!ensureOcclusionRenderTarget(baseSystem, win, captureWidth, captureHeight)) {
            if (occlusion.debugFrozen) occlusion.frozenValid = true;
            return;
        }

        const auto captureStart = std::chrono::steady_clock::now();
        if (!captureOcclusionDepth(baseSystem, viewProj, occluders, captureWidth, captureHeight)) {
            if (occlusion.debugFrozen) occlusion.frozenValid = true;
            return;
        }
        occlusion.hzbCaptureMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - captureStart
        ).count();

        occlusion.hzbReadbackPending = true;
        occlusion.hzbPendingWidth = captureWidth;
        occlusion.hzbPendingHeight = captureHeight;
        occlusion.hzbPendingViewProj = viewProj;
        occlusion.hzbPendingCameraPosition = occlusion.lastCameraPosition;
        occlusion.lastCaptureFrame = baseSystem.frameIndex;

        if (occlusion.debugFrozen) {
            occlusion.frozenValid = true;
        }
    }
}
