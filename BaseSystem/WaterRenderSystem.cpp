#include <string>

namespace RenderInitSystemLogic { bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback); }

namespace {
    struct VisibleWaterSection {
        const ChunkRenderBuffers* buffers = nullptr;
        glm::vec3 minBounds = glm::vec3(0.0f);
        glm::vec3 maxBounds = glm::vec3(0.0f);
        float dist2 = 0.0f;
    };

    bool chunkHasWaterBuffers(const ChunkRenderBuffers& buffers) {
        for (int faceType = 0; faceType < 6; ++faceType) {
            if (buffers.waterBuffers.surfaceCounts[static_cast<size_t>(faceType)] > 0) return true;
            if (buffers.waterBuffers.bodyCounts[static_cast<size_t>(faceType)] > 0) return true;
        }
        return false;
    }

    bool isNightworldDimensionForWaterRender(const BaseSystem& baseSystem) {
        if (baseSystem.worldSave && baseSystem.worldSave->activeDimensionId == "nightworld") {
            return true;
        }
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("ActiveDimensionId");
            if (it != baseSystem.registry->end()
                && std::holds_alternative<std::string>(it->second)
                && std::get<std::string>(it->second) == "nightworld") {
                return true;
            }
        }
        return false;
    }

    float dimensionDayFractionForWaterRender(const BaseSystem& baseSystem, float dayFraction) {
        if (isNightworldDimensionForWaterRender(baseSystem)) {
            dayFraction += 0.5f;
            dayFraction -= std::floor(dayFraction);
        }
        return dayFraction;
    }

    void collectVisibleWaterSections(const BaseSystem& baseSystem,
                                     const glm::vec3& cameraPos,
                                     std::vector<VisibleWaterSection>& outSections) {
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
            if (!chunkHasWaterBuffers(cluster.buffers)) return;
            if (!FrustumCullingSystemLogic::ShouldRenderWorldAabb(baseSystem, cluster.minBounds, cluster.maxBounds)) {
                return;
            }
            if (OcclusionCullingSystemLogic::IsWorldAabbOccluded(baseSystem, cluster.minBounds, cluster.maxBounds)) {
                return;
            }
            const glm::vec3 center = 0.5f * (cluster.minBounds + cluster.maxBounds);
            const glm::vec3 delta = center - cameraPos;
            outSections.push_back({&cluster.buffers, cluster.minBounds, cluster.maxBounds, glm::dot(delta, delta)});
        };

        if (baseSystem.voxelWorld && baseSystem.voxelRender) {
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
            for (const auto& [sectionKey, clusters] : voxelRender.renderClusters) {
                auto secIt = voxelWorld.sections.find(sectionKey);
                if (secIt == voxelWorld.sections.end()) continue;
                for (const VoxelRenderCluster& cluster : clusters) {
                    appendCluster(cluster);
                }
            }
        }

        if (farTerrainEnabled && baseSystem.farTerrain && baseSystem.farTerrain->enabled) {
            auto appendFarClusters = [&](const std::vector<VoxelRenderCluster>& clusters) {
                for (const VoxelRenderCluster& cluster : clusters) {
                    appendCluster(cluster);
                }
            };
            appendFarClusters(baseSystem.farTerrain->bodyRenderClusters);
            appendFarClusters(baseSystem.farTerrain->handoffRenderClusters);
        }
    }

    void drawCompositePass(RendererContext& renderer,
                           IRenderBackend& renderBackend,
                           Shader& shader,
                           RenderHandle sourceTexture,
                           const PlayerContext* postProjectionPlayer = nullptr) {
        if (!renderer.godrayQuadVAO || sourceTexture == 0) return;
        shader.use();
        if (postProjectionPlayer) {
            PaniniProjectionSystemLogic::ApplyPostProjectionWarpUniforms(*postProjectionPlayer, shader);
        } else {
            PaniniProjectionSystemLogic::DisableProjectionWarpUniforms(shader);
        }
        renderBackend.setDepthTestEnabled(false);
        renderBackend.setDepthWriteEnabled(false);
        renderBackend.setBlendEnabled(false);
        renderBackend.setCullEnabled(false);
        renderBackend.bindTexture2D(sourceTexture, 0);
        renderBackend.bindVertexArray(renderer.godrayQuadVAO);
        renderBackend.drawArraysTriangles(0, 6);
        renderBackend.unbindVertexArray();
    }
}

namespace WaterRenderSystemLogic {

    void RenderWater(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)dt;
        (void)win;
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.player || !baseSystem.renderBackend) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        PlayerContext& player = *baseSystem.player;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;

        const bool hasSceneTargets =
            renderer.waterSceneFBO != 0
            && renderer.waterSceneTex != 0
            && renderer.waterSceneCopyFBO != 0
            && renderer.waterSceneCopyTex != 0
            && renderer.waterSceneWidth > 0
            && renderer.waterSceneHeight > 0;
        if (!hasSceneTargets || !renderer.waterCompositeShader) {
            return;
        }

        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 playerPos = player.cameraPosition;
        if (baseSystem.securityCamera && baseSystem.securityCamera->dawViewActive) {
            const SecurityCameraContext& securityCamera = *baseSystem.securityCamera;
            if (glm::length(securityCamera.viewForward) > 1e-4f) {
                playerPos = securityCamera.viewPosition;
                view = glm::lookAt(
                    playerPos,
                    playerPos + glm::normalize(securityCamera.viewForward),
                    glm::vec3(0.0f, 1.0f, 0.0f)
                );
            }
        }

        float time = static_cast<float>(PlatformInput::GetTimeSeconds());
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
        dayFraction = dimensionDayFractionForWaterRender(baseSystem, dayFraction);
        glm::vec3 skyTop(0.52f, 0.66f, 0.95f);
        glm::vec3 skyBottom(0.03f, 0.06f, 0.18f);
        SkyboxSystemLogic::getCurrentSkyColors(baseSystem, dayFraction, world.skyKeys, skyTop, skyBottom);

        const float hour = dayFraction * 24.0f;
        const float sunU = (hour - 6.0f) / 12.0f;
        const float moonHour = (hour < 6.0f) ? (hour + 24.0f) : hour;
        const float moonU = (moonHour - 18.0f) / 12.0f;
        const float sunY = glm::max(0.0f, std::sin(sunU * 3.14159265359f));
        const float moonY = glm::max(0.0f, std::sin(moonU * 3.14159265359f));
        const float moonStrength = glm::clamp(RenderInitSystemLogic::getRegistryFloat(baseSystem, "TimeOfDayMoonlightStrength", 0.20f), 0.0f, 1.0f);
        const float dayLightFactor = glm::clamp(glm::max(sunY, moonY * moonStrength), 0.0f, 1.0f);
        const float ambientNight = glm::clamp(RenderInitSystemLogic::getRegistryFloat(baseSystem, "TimeOfDayAmbientNight", 0.08f), 0.0f, 1.0f);
        const float ambientDay = glm::clamp(RenderInitSystemLogic::getRegistryFloat(baseSystem, "TimeOfDayAmbientDay", 0.40f), ambientNight, 1.0f);
        const float diffuseNight = glm::clamp(RenderInitSystemLogic::getRegistryFloat(baseSystem, "TimeOfDayDiffuseNight", 0.10f), 0.0f, 1.0f);
        const float diffuseDay = glm::clamp(RenderInitSystemLogic::getRegistryFloat(baseSystem, "TimeOfDayDiffuseDay", 0.60f), diffuseNight, 1.0f);
        const float ambientScalar = ambientNight + (ambientDay - ambientNight) * dayLightFactor;
        const float diffuseScalar = diffuseNight + (diffuseDay - diffuseNight) * dayLightFactor;
        const glm::vec3 ambientLightColor(ambientScalar);
        const glm::vec3 diffuseLightColor(diffuseScalar);
        const glm::vec3 lightDir = glm::normalize(glm::vec3(1.0f, 2.0f, 1.0f));

        std::vector<VisibleWaterSection> visibleSections;
        collectVisibleWaterSections(baseSystem, playerPos, visibleSections);

        if (!visibleSections.empty() && renderer.waterShader) {
            renderBackend.beginOffscreenColorPass(
                renderer.waterSceneCopyFBO,
                renderer.waterSceneWidth,
                renderer.waterSceneHeight,
                0.0f,
                0.0f,
                0.0f,
                1.0f
            );
            drawCompositePass(renderer, renderBackend, *renderer.waterCompositeShader, renderer.waterSceneTex);
            renderBackend.endOffscreenColorPass();

            renderBackend.resumeOffscreenColorPass(
                renderer.waterSceneFBO,
                renderer.waterSceneWidth,
                renderer.waterSceneHeight
            );
            renderBackend.setDepthTestEnabled(true);
            renderBackend.setDepthWriteEnabled(true);
            renderBackend.setBlendEnabled(false);
            renderBackend.setCullEnabled(false);

            renderer.waterShader->use();
            renderer.waterShader->setMat4("view", view);
            renderer.waterShader->setMat4("projection", projection);
            PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, *renderer.waterShader);
            renderer.waterShader->setMat4("model", glm::mat4(1.0f));
            renderer.waterShader->setVec3("cameraPos", playerPos);
            renderer.waterShader->setFloat("time", time);
            renderer.waterShader->setVec3("lightDir", lightDir);
            renderer.waterShader->setVec3("ambientLight", ambientLightColor);
            renderer.waterShader->setVec3("diffuseLight", diffuseLightColor);
            renderer.waterShader->setVec3("topColor", skyTop);
            renderer.waterShader->setVec3("bottomColor", skyBottom);
            renderer.waterShader->setVec2(
                "uResolution",
                glm::vec2(
                    static_cast<float>(renderer.waterSceneWidth),
                    static_cast<float>(renderer.waterSceneHeight)
                )
            );
            renderer.waterShader->setInt("waterPlanarReflectionEnabled", renderer.waterReflectionTex != 0 ? 1 : 0);
            renderBackend.bindTexture2D(renderer.waterSceneCopyTex, 0);
            renderBackend.bindTexture2D(renderer.waterReflectionTex, 1);

            auto drawWaterBuffers = [&](bool surfacePass) {
                renderer.waterShader->setInt("ready", surfacePass ? 1 : 0);
                for (const VisibleWaterSection& section : visibleSections) {
                    for (int faceType = 0; faceType < 6; ++faceType) {
                        const int count = surfacePass
                            ? section.buffers->waterBuffers.surfaceCounts[static_cast<size_t>(faceType)]
                            : section.buffers->waterBuffers.bodyCounts[static_cast<size_t>(faceType)];
                        const RenderHandle vao = surfacePass
                            ? section.buffers->waterBuffers.surfaceVaos[static_cast<size_t>(faceType)]
                            : section.buffers->waterBuffers.bodyVaos[static_cast<size_t>(faceType)];
                        if (count <= 0 || vao == 0) continue;
                        renderer.waterShader->setInt("faceType", faceType);
                        renderBackend.bindVertexArray(vao);
                        renderBackend.drawArraysTrianglesInstanced(0, 6, count);
                    }
                }
            };

            drawWaterBuffers(false);
            drawWaterBuffers(true);
            renderBackend.unbindVertexArray();
            renderBackend.endOffscreenColorPass();
        }

        renderBackend.clearDefaultFramebuffer(0.0f, 0.0f, 0.0f, 1.0f, true);
        drawCompositePass(renderer, renderBackend, *renderer.waterCompositeShader, renderer.waterSceneTex, &player);
    }
}
