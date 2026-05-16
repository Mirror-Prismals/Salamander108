#pragma once

#include "Host/PlatformInput.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace ThreeDGrassSystemLogic {
    namespace {
        constexpr int kMaxGrassCells = 64;

        struct GrassCellCandidate {
            glm::ivec3 cell{0};
            float distanceSq = 0.0f;
        };

        std::string registryString(const BaseSystem& baseSystem,
                                   const std::string& key,
                                   const std::string& fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            return std::get<std::string>(it->second);
        }

        int registryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stoi(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        float registryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        bool registryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            std::string raw = std::get<std::string>(it->second);
            std::transform(raw.begin(), raw.end(), raw.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return raw == "1" || raw == "true" || raw == "yes" || raw == "on";
        }

        int prototypeIDByName(const std::vector<Entity>& prototypes, const std::string& name) {
            for (const Entity& proto : prototypes) {
                if (proto.name == name) return proto.prototypeID;
            }
            return -1;
        }

        std::vector<int> parseIntList(const std::string& raw, const std::vector<int>& fallback) {
            if (raw.empty()) return fallback;
            std::vector<int> values;
            std::stringstream stream(raw);
            std::string item;
            while (std::getline(stream, item, ',')) {
                try {
                    values.push_back(std::stoi(item));
                } catch (...) {
                }
            }
            return values.empty() ? fallback : values;
        }

        int inlineAtlasTileIndex(const std::string& textureKey) {
            constexpr const char* kPrefix = "@idx:";
            if (textureKey.rfind(kPrefix, 0) != 0) return -1;
            try {
                return std::stoi(textureKey.substr(std::strlen(kPrefix)));
            } catch (...) {
                return -1;
            }
        }

        bool prototypeUsesAtlasTile(const WorldContext& world, const Entity& proto, const std::vector<int>& allowedTiles) {
            if (allowedTiles.empty() || proto.textureKey.empty()) return false;

            auto tileAllowed = [&](int tile) {
                return tile >= 0 && std::find(allowedTiles.begin(), allowedTiles.end(), tile) != allowedTiles.end();
            };

            const int inlineTile = inlineAtlasTileIndex(proto.textureKey);
            if (tileAllowed(inlineTile)) return true;

            auto mappingIt = world.atlasMappings.find(proto.textureKey);
            if (mappingIt == world.atlasMappings.end()) return false;

            const FaceTextureSet& tiles = mappingIt->second;
            return tileAllowed(tiles.all)
                || tileAllowed(tiles.top)
                || tileAllowed(tiles.bottom)
                || tileAllowed(tiles.side);
        }

        void pushUniqueID(std::vector<uint32_t>& ids, int id) {
            if (id <= 0) return;
            const uint32_t u = static_cast<uint32_t>(id);
            if (std::find(ids.begin(), ids.end(), u) == ids.end()) ids.push_back(u);
        }

        std::vector<uint32_t> coniferGrassPrototypeIDs(const BaseSystem& baseSystem,
                                                       const std::vector<Entity>& prototypes) {
            std::vector<uint32_t> ids;
            const std::array<const char*, 5> names = {{
                "GrassBlockTex",
                "GrassBlockTexV002",
                "GrassBlockTexV003",
                "GrassBlockTexV004",
                "GrassBlockTemperateTex"
            }};
            for (const char* name : names) {
                const int id = prototypeIDByName(prototypes, name);
                pushUniqueID(ids, id);
            }

            if (baseSystem.world) {
                const std::vector<int> defaultTiles = {{
                    61,
                    704, 705, 706, 707,
                    708, 709, 710, 711,
                    712, 713, 714, 715
                }};
                const std::vector<int> allowedTiles = parseIntList(
                    registryString(baseSystem, "ThreeDGrassAtlasTiles", ""),
                    defaultTiles
                );
                for (const Entity& proto : prototypes) {
                    if (!proto.isBlock || !proto.isSolid) continue;
                    if (prototypeUsesAtlasTile(*baseSystem.world, proto, allowedTiles)) {
                        pushUniqueID(ids, proto.prototypeID);
                    }
                }
            }
            return ids;
        }

        bool containsID(const std::vector<uint32_t>& ids, uint32_t id) {
            return std::find(ids.begin(), ids.end(), id) != ids.end();
        }

        bool solidPrototypeAt(const std::vector<Entity>& prototypes, uint32_t id) {
            if (id == 0 || id >= prototypes.size()) return false;
            return prototypes[static_cast<size_t>(id)].isSolid;
        }

        bool isExpanseLevelActive(const BaseSystem& baseSystem) {
            if (!baseSystem.level || !baseSystem.world || !baseSystem.world->expanse.loaded) return false;
            const ExpanseConfig& expanse = baseSystem.world->expanse;
            for (const Entity& world : baseSystem.level->worlds) {
                if (world.name == expanse.terrainWorld
                    || world.name == expanse.waterWorld
                    || world.name == expanse.treesWorld) {
                    return true;
                }
            }
            return false;
        }

        bool grassCellInView(const glm::ivec3& cell, const glm::mat4& viewProjection) {
            const glm::vec3 center = glm::vec3(cell) + glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec4 clip = viewProjection * glm::vec4(center, 1.0f);
            if (clip.w <= 0.05f) return false;

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            constexpr float kFrustumMargin = 1.75f;
            return ndc.x >= -kFrustumMargin
                && ndc.x <= kFrustumMargin
                && ndc.y >= -kFrustumMargin
                && ndc.y <= kFrustumMargin;
        }

        void appendGrassCellCandidate(const glm::ivec3& cell,
                                      const glm::vec3& cameraPos,
                                      const glm::mat4& viewProjection,
                                      std::vector<GrassCellCandidate>& candidates) {
            if (!grassCellInView(cell, viewProjection)) return;
            const glm::vec3 center = glm::vec3(cell);
            const glm::vec3 delta = center - cameraPos;
            candidates.push_back({cell, glm::dot(delta, delta)});
        }

        void scanVoxelSectionForConiferGrass(const BaseSystem& baseSystem,
                                             const std::vector<Entity>& prototypes,
                                             const VoxelSection& section,
                                             const std::vector<uint32_t>& coniferGrassIDs,
                                             const glm::vec3& cameraPos,
                                             const glm::mat4& viewProjection,
                                             std::vector<GrassCellCandidate>& candidates) {
            if (!baseSystem.voxelWorld || !baseSystem.world || section.size <= 0) return;
            const int size = section.size;
            const glm::ivec3 base = section.coord * size;

            for (int z = 0; z < size; ++z) {
                for (int y = 0; y < size; ++y) {
                    for (int x = 0; x < size; ++x) {
                        const int idx = x + y * size + z * size * size;
                        if (idx < 0 || idx >= static_cast<int>(section.ids.size())) continue;
                        const uint32_t id = section.ids[static_cast<size_t>(idx)];
                        if (!containsID(coniferGrassIDs, id)) continue;

                        const glm::ivec3 cell = base + glm::ivec3(x, y, z);
                        if (ExpanseBiomeSystemLogic::ResolveBiome(*baseSystem.world, static_cast<float>(cell.x), static_cast<float>(cell.z)) != 0) {
                            continue;
                        }

                        const uint32_t aboveID = baseSystem.voxelWorld->getBlockWorld(cell + glm::ivec3(0, 1, 0));
                        if (solidPrototypeAt(prototypes, aboveID)) continue;

                        appendGrassCellCandidate(cell, cameraPos, viewProjection, candidates);
                    }
                }
            }
        }

        std::vector<glm::ivec3> collectExpanseGrassBlockCells(const BaseSystem& baseSystem,
                                                              const std::vector<Entity>& prototypes,
                                                              const glm::vec3& cameraPos,
                                                              const glm::mat4& viewProjection) {
            std::vector<glm::ivec3> cells;
            if (!isExpanseLevelActive(baseSystem)
                || !baseSystem.voxelWorld
                || !baseSystem.voxelWorld->enabled
                || !baseSystem.voxelRender) {
                return cells;
            }

            const std::vector<uint32_t> coniferGrassIDs = coniferGrassPrototypeIDs(baseSystem, prototypes);
            if (coniferGrassIDs.empty()) return cells;

            std::vector<GrassCellCandidate> candidates;
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const VoxelRenderContext& voxelRender = *baseSystem.voxelRender;

            if (!voxelRender.renderClusters.empty()) {
                for (const auto& [key, _] : voxelRender.renderClusters) {
                    auto sectionIt = voxelWorld.sections.find(key);
                    if (sectionIt == voxelWorld.sections.end()) continue;
                    scanVoxelSectionForConiferGrass(baseSystem, prototypes, sectionIt->second, coniferGrassIDs, cameraPos, viewProjection, candidates);
                }
            } else {
                for (const auto& [_, section] : voxelWorld.sections) {
                    scanVoxelSectionForConiferGrass(baseSystem, prototypes, section, coniferGrassIDs, cameraPos, viewProjection, candidates);
                }
            }

            if (candidates.empty()) return cells;

            const int maxCells = std::clamp(registryInt(baseSystem, "ThreeDGrassMaxCells", kMaxGrassCells), 1, kMaxGrassCells);
            if (static_cast<int>(candidates.size()) > maxCells) {
                std::partial_sort(
                    candidates.begin(),
                    candidates.begin() + maxCells,
                    candidates.end(),
                    [](const GrassCellCandidate& a, const GrassCellCandidate& b) {
                        return a.distanceSq < b.distanceSq;
                    }
                );
                candidates.resize(static_cast<size_t>(maxCells));
            } else {
                std::sort(
                    candidates.begin(),
                    candidates.end(),
                    [](const GrassCellCandidate& a, const GrassCellCandidate& b) {
                        return a.distanceSq < b.distanceSq;
                    }
                );
            }

            cells.reserve(candidates.size());
            for (const GrassCellCandidate& candidate : candidates) {
                cells.push_back(candidate.cell);
            }
            return cells;
        }

        std::vector<glm::ivec3> collectDebugGrassBlockCells(const BaseSystem& baseSystem) {
            std::vector<glm::ivec3> cells;
            if (!baseSystem.level) return cells;

            const std::string grassWorldName = registryString(baseSystem, "ThreeDGrassWorld", "3DGrassBlockWorld");
            const std::string blockPrototypeName = registryString(baseSystem, "ThreeDGrassBlockPrototype", "DebugBlockTex");
            const int maxCells = std::clamp(registryInt(baseSystem, "ThreeDGrassMaxCells", 1), 1, kMaxGrassCells);

            for (const Entity& world : baseSystem.level->worlds) {
                if (world.name != grassWorldName) continue;
                for (const EntityInstance& inst : world.instances) {
                    if (!blockPrototypeName.empty() && inst.name != blockPrototypeName) continue;
                    cells.push_back(glm::ivec3(glm::round(inst.position)));
                    if (static_cast<int>(cells.size()) >= maxCells) return cells;
                }
            }

            return cells;
        }

        std::vector<glm::ivec3> collectGrassBlockCells(const BaseSystem& baseSystem,
                                                       const std::vector<Entity>& prototypes,
                                                       const glm::vec3& cameraPos,
                                                       const glm::mat4& viewProjection) {
            const int maxCells = std::clamp(registryInt(baseSystem, "ThreeDGrassMaxCells", kMaxGrassCells), 1, kMaxGrassCells);
            std::vector<glm::ivec3> cells;
            cells.reserve(static_cast<size_t>(maxCells));

            auto appendUniqueCells = [&](const std::vector<glm::ivec3>& source) {
                for (const glm::ivec3& cell : source) {
                    if (std::find(cells.begin(), cells.end(), cell) != cells.end()) continue;
                    cells.push_back(cell);
                    if (static_cast<int>(cells.size()) >= maxCells) return;
                }
            };

            if (registryBool(baseSystem, "ThreeDGrassIncludeDebugWorld", false)) {
                appendUniqueCells(collectDebugGrassBlockCells(baseSystem));
            }

            if (registryBool(baseSystem, "ThreeDGrassExpanseConiferEnabled", true)) {
                std::vector<glm::ivec3> expanseCells = collectExpanseGrassBlockCells(baseSystem, prototypes, cameraPos, viewProjection);
                appendUniqueCells(expanseCells);
            }

            if (cells.empty()) {
                appendUniqueCells(collectDebugGrassBlockCells(baseSystem));
            }
            return cells;
        }

        void ensureGrassShader(RendererContext& renderer, WorldContext& world) {
            if (renderer.grass3DShader) return;
            auto vertexIt = world.shaders.find("GRASS3D_VERTEX_SHADER");
            auto fragmentIt = world.shaders.find("GRASS3D_FRAGMENT_SHADER");
            if (vertexIt == world.shaders.end() || fragmentIt == world.shaders.end()) {
                std::cerr << "3DGrassSystem: missing shader sources.\n";
                return;
            }
            renderer.grass3DShader = std::make_unique<Shader>(vertexIt->second.c_str(), fragmentIt->second.c_str());
            if (!renderer.grass3DShader->isValid()) {
                std::cerr << "3DGrassSystem: failed to create shader.\n";
                renderer.grass3DShader.reset();
            }
        }

        void ensureGrassVolumeGeometry(RendererContext& renderer, IRenderBackend& renderBackend) {
            if (renderer.grass3DVolumeVAO != 0
                && renderer.grass3DVolumeVBO != 0
                && renderer.grass3DVolumeInstanceVBO != 0
                && renderer.grass3DVolumeVertexCount > 0) {
                return;
            }

            const std::array<glm::vec3, 6> vertices = {{
                {-1.0f, -1.0f, 0.0f},
                { 1.0f, -1.0f, 0.0f},
                {-1.0f,  1.0f, 0.0f},
                {-1.0f,  1.0f, 0.0f},
                { 1.0f, -1.0f, 0.0f},
                { 1.0f,  1.0f, 0.0f},
            }};

            const std::vector<VertexAttribLayout> vertexLayout = {
                {0u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(glm::vec3)), 0u, 0u},
            };
            const std::vector<VertexAttribLayout> instanceLayout = {
                {1u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(glm::vec3)), 0u, 1u},
            };

            renderBackend.ensureVertexArray(renderer.grass3DVolumeVAO);
            renderBackend.ensureArrayBuffer(renderer.grass3DVolumeVBO);
            renderBackend.ensureArrayBuffer(renderer.grass3DVolumeInstanceVBO);
            renderBackend.uploadArrayBufferData(renderer.grass3DVolumeVBO, vertices.data(), vertices.size() * sizeof(glm::vec3), false);
            renderBackend.configureVertexArray(
                renderer.grass3DVolumeVAO,
                renderer.grass3DVolumeVBO,
                vertexLayout,
                renderer.grass3DVolumeInstanceVBO,
                instanceLayout
            );
            renderer.grass3DVolumeVertexCount = static_cast<int>(vertices.size());
        }
    }

    void Render3DGrass(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        if (!baseSystem.renderer || !baseSystem.world || !baseSystem.player || !baseSystem.level || !baseSystem.renderBackend) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        PlayerContext& player = *baseSystem.player;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;

        ensureGrassShader(renderer, world);
        ensureGrassVolumeGeometry(renderer, renderBackend);
        if (!renderer.grass3DShader || renderer.grass3DVolumeVAO == 0 || renderer.grass3DVolumeVertexCount <= 0) return;

        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 cameraPos = player.cameraPosition;
        if (baseSystem.securityCamera && baseSystem.securityCamera->dawViewActive) {
            const SecurityCameraContext& securityCamera = *baseSystem.securityCamera;
            if (glm::length(securityCamera.viewForward) > 1e-4f) {
                cameraPos = securityCamera.viewPosition;
                view = glm::lookAt(
                    cameraPos,
                    cameraPos + glm::normalize(securityCamera.viewForward),
                    glm::vec3(0.0f, 1.0f, 0.0f)
                );
            }
        }

        const glm::mat4 viewProjection = projection * view;
        std::vector<glm::ivec3> cells = collectGrassBlockCells(baseSystem, prototypes, cameraPos, viewProjection);
        if (cells.empty()) return;

        const int cellCount = static_cast<int>(std::min(cells.size(), static_cast<size_t>(kMaxGrassCells)));
        std::vector<glm::vec3> instanceCells;
        instanceCells.reserve(static_cast<size_t>(cellCount));
        for (int i = 0; i < cellCount; ++i) {
            instanceCells.push_back(glm::vec3(cells[static_cast<size_t>(i)]));
        }
        if (instanceCells.empty()) return;
        renderBackend.uploadArrayBufferData(
            renderer.grass3DVolumeInstanceVBO,
            instanceCells.data(),
            instanceCells.size() * sizeof(glm::vec3),
            true
        );

        Shader& shader = *renderer.grass3DShader;
        shader.use();
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, shader);
        shader.setVec3("cameraPos", cameraPos);
        shader.setFloat("time", static_cast<float>(PlatformInput::GetTimeSeconds()));
        shader.setFloat("grassSurfaceYOffset", registryFloat(baseSystem, "ThreeDGrassSurfaceYOffset", -0.48f));

        const bool renderToWaterSceneTarget = renderer.waterCompositeShader
            && renderer.waterSceneFBO != 0
            && renderer.waterSceneTex != 0
            && renderer.waterSceneWidth > 0
            && renderer.waterSceneHeight > 0;

        int targetWidth = renderer.waterSceneWidth;
        int targetHeight = renderer.waterSceneHeight;
        if (!renderToWaterSceneTarget) {
            PlatformInput::GetFramebufferSize(win, targetWidth, targetHeight);
        }
        targetWidth = std::max(targetWidth, 1);
        targetHeight = std::max(targetHeight, 1);
        shader.setVec2("uResolution", glm::vec2(static_cast<float>(targetWidth), static_cast<float>(targetHeight)));

        const bool depthTestEnabled = registryBool(baseSystem, "ThreeDGrassDepthTestEnabled", true);
        if (renderToWaterSceneTarget) {
            renderBackend.resumeOffscreenColorPass(renderer.waterSceneFBO, renderer.waterSceneWidth, renderer.waterSceneHeight);
        }
        renderBackend.setDepthTestEnabled(depthTestEnabled);
        renderBackend.setDepthWriteEnabled(false);
        renderBackend.setBlendEnabled(true);
        renderBackend.setBlendModeAlpha();
        renderBackend.setCullEnabled(false);
        renderBackend.bindVertexArray(renderer.grass3DVolumeVAO);
        renderBackend.drawArraysTrianglesInstanced(0, renderer.grass3DVolumeVertexCount, cellCount);
        renderBackend.unbindVertexArray();
        renderBackend.setCullEnabled(true);
        renderBackend.setBlendEnabled(false);
        renderBackend.setDepthWriteEnabled(true);
        renderBackend.setDepthTestEnabled(true);
        if (renderToWaterSceneTarget) {
            renderBackend.endOffscreenColorPass();
        }
    }
}
