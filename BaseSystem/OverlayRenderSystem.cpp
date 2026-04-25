#pragma once
#include "Host/PlatformInput.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace ExpanseBiomeSystemLogic { bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight); }
namespace LeyLineSystemLogic { float SampleLeyStress(const WorldContext& worldCtx, float x, float z); float SampleLeyUplift(const WorldContext& worldCtx, float x, float z); }
namespace GroundCraftingSystemLogic { void RenderGroundCrafting(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win); }
namespace OreMiningSystemLogic {
    bool IsMiningActive(const BaseSystem& baseSystem);
    void RenderOreMining(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win);
}
namespace GemChiselSystemLogic {
    bool IsChiselActive(const BaseSystem& baseSystem);
    void RenderGemChisel(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win);
}
namespace BlockChargeSystemLogic { void RenderBlockDamage(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win); }

namespace OverlayRenderSystemLogic {
    namespace {
        struct LeyLineDebugVertex {
            glm::vec3 position;
            glm::vec3 color;
        };

        struct WireframeVertexKey {
            int x2 = 0;
            int y2 = 0;
            int z2 = 0;

            bool operator==(const WireframeVertexKey& other) const noexcept {
                return x2 == other.x2 && y2 == other.y2 && z2 == other.z2;
            }
        };

        struct WireframeVertexKeyHash {
            size_t operator()(const WireframeVertexKey& key) const noexcept {
                size_t h = std::hash<int>()(key.x2);
                h ^= std::hash<int>()(key.y2) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
                h ^= std::hash<int>()(key.z2) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
                return h;
            }
        };

        struct WireframeEdgeKey {
            WireframeVertexKey a{};
            WireframeVertexKey b{};
            uint8_t colorClass = 0;

            bool operator==(const WireframeEdgeKey& other) const noexcept {
                return a == other.a && b == other.b && colorClass == other.colorClass;
            }
        };

        struct WireframeEdgeKeyHash {
            size_t operator()(const WireframeEdgeKey& key) const noexcept {
                WireframeVertexKeyHash vertexHash;
                size_t h = vertexHash(key.a);
                h ^= vertexHash(key.b) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
                h ^= std::hash<int>()(static_cast<int>(key.colorClass)) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
                return h;
            }
        };

        struct TerrainWireframeStats {
            size_t nearFaceCount = 0;
            size_t farFaceCount = 0;
            size_t lineSegmentCount = 0;
        };

        struct TerrainWireframeCache {
            uint64_t nearSignature = 0;
            uint64_t farSignature = 0;
            std::vector<LeyLineDebugVertex> vertices;
            TerrainWireframeStats stats{};
            bool valid = false;
        };

        WireframeVertexKey makeWireframeVertexKey(const glm::vec3& p) {
            return WireframeVertexKey{
                static_cast<int>(std::lround(static_cast<double>(p.x * 2.0f))),
                static_cast<int>(std::lround(static_cast<double>(p.y * 2.0f))),
                static_cast<int>(std::lround(static_cast<double>(p.z * 2.0f)))
            };
        }

        glm::vec3 wireframeVertexPosition(const WireframeVertexKey& key) {
            return glm::vec3(
                static_cast<float>(key.x2) * 0.5f,
                static_cast<float>(key.y2) * 0.5f,
                static_cast<float>(key.z2) * 0.5f
            );
        }

        void getTerrainWireframeFaceCorners(glm::vec3 (&corners)[4],
                                            int faceType,
                                            const FaceInstanceRenderData& face) {
            if (faceType == 0 || faceType == 1) {
                const float x = face.position.x;
                const float z0 = face.position.z - face.scale.x * 0.5f;
                const float z1 = face.position.z + face.scale.x * 0.5f;
                const float y0 = face.position.y - face.scale.y * 0.5f;
                const float y1 = face.position.y + face.scale.y * 0.5f;
                corners[0] = glm::vec3(x, y0, z0);
                corners[1] = glm::vec3(x, y1, z0);
                corners[2] = glm::vec3(x, y1, z1);
                corners[3] = glm::vec3(x, y0, z1);
            } else if (faceType == 2 || faceType == 3) {
                const float y = face.position.y;
                const float x0 = face.position.x - face.scale.x * 0.5f;
                const float x1 = face.position.x + face.scale.x * 0.5f;
                const float z0 = face.position.z - face.scale.y * 0.5f;
                const float z1 = face.position.z + face.scale.y * 0.5f;
                corners[0] = glm::vec3(x0, y, z0);
                corners[1] = glm::vec3(x1, y, z0);
                corners[2] = glm::vec3(x1, y, z1);
                corners[3] = glm::vec3(x0, y, z1);
            } else {
                const float z = face.position.z;
                const float x0 = face.position.x - face.scale.x * 0.5f;
                const float x1 = face.position.x + face.scale.x * 0.5f;
                const float y0 = face.position.y - face.scale.y * 0.5f;
                const float y1 = face.position.y + face.scale.y * 0.5f;
                corners[0] = glm::vec3(x0, y0, z);
                corners[1] = glm::vec3(x1, y0, z);
                corners[2] = glm::vec3(x1, y1, z);
                corners[3] = glm::vec3(x0, y1, z);
            }
        }

        uint64_t hashMixU64(uint64_t h, uint64_t v) {
            h ^= v + 0x9e3779b97f4a7c15ull + (h << 6u) + (h >> 2u);
            return h;
        }

        uint64_t computeNearWireframeSignature(const BaseSystem& baseSystem) {
            if (!baseSystem.voxelRender) return 0;
            const auto& sourceMeshes = !baseSystem.voxelRender->wireframeMeshes.empty()
                ? baseSystem.voxelRender->wireframeMeshes
                : baseSystem.voxelRender->preparedMeshes;
            uint64_t h = 1469598103934665603ull;
            for (const auto& [key, prepared] : sourceMeshes) {
                const auto renderIt = baseSystem.voxelRender->renderBuffers.find(key);
                if (renderIt == baseSystem.voxelRender->renderBuffers.end()) continue;
                const VoxelChunkLifecycleState* state = baseSystem.voxelWorld
                    ? baseSystem.voxelWorld->findChunkState(key)
                    : nullptr;
                if (!state || !state->isRenderable()) continue;
                h = hashMixU64(h, static_cast<uint64_t>(static_cast<uint32_t>(key.coord.x)));
                h = hashMixU64(h, static_cast<uint64_t>(static_cast<uint32_t>(key.coord.y)));
                h = hashMixU64(h, static_cast<uint64_t>(static_cast<uint32_t>(key.coord.z)));
                h = hashMixU64(h, prepared.dirtyTicket);
                for (int faceType = 0; faceType < 6; ++faceType) {
                    h = hashMixU64(h, prepared.opaqueFaces[static_cast<size_t>(faceType)].size());
                    h = hashMixU64(h, prepared.alphaFaces[static_cast<size_t>(faceType)].size());
                }
            }
            return h;
        }

        uint64_t computeFarWireframeSignature(const BaseSystem& baseSystem) {
            if (!baseSystem.farTerrain || !baseSystem.farTerrain->enabled) return 0;
            const FarTerrainClipmapContext& far = *baseSystem.farTerrain;
            uint64_t h = 1469598103934665603ull;
            h = hashMixU64(h, far.rebuildCount);
            h = hashMixU64(h, far.lastBuildFrame);
            h = hashMixU64(h, far.visibleFaceCount);
            h = hashMixU64(h, far.handoffVisibleFaceCount);
            h = hashMixU64(h, far.bodyVisibleFaceCount);
            return h;
        }

        void appendTerrainWireframeFace(std::vector<LeyLineDebugVertex>& vertices,
                                        std::unordered_set<WireframeEdgeKey, WireframeEdgeKeyHash>& uniqueEdges,
                                        int faceType,
                                        const FaceInstanceRenderData& face,
                                        const glm::vec3& color,
                                        uint8_t colorClass) {
            glm::vec3 corners[4];
            getTerrainWireframeFaceCorners(corners, faceType, face);

            auto pushUniqueLine = [&](const glm::vec3& a, const glm::vec3& b) {
                WireframeVertexKey ka = makeWireframeVertexKey(a);
                WireframeVertexKey kb = makeWireframeVertexKey(b);
                if (kb.x2 < ka.x2
                    || (kb.x2 == ka.x2 && kb.y2 < ka.y2)
                    || (kb.x2 == ka.x2 && kb.y2 == ka.y2 && kb.z2 < ka.z2)) {
                    std::swap(ka, kb);
                }
                WireframeEdgeKey edge{ka, kb, colorClass};
                if (!uniqueEdges.insert(edge).second) return;
                vertices.push_back({wireframeVertexPosition(ka), color});
                vertices.push_back({wireframeVertexPosition(kb), color});
            };
            auto pushDiagonal = [&](const glm::vec3& a, const glm::vec3& b) {
                vertices.push_back({a, color});
                vertices.push_back({b, color});
            };
            pushUniqueLine(corners[0], corners[1]);
            pushUniqueLine(corners[1], corners[2]);
            pushUniqueLine(corners[2], corners[3]);
            pushUniqueLine(corners[3], corners[0]);
            pushDiagonal(corners[0], corners[2]);
        }

        const std::vector<LeyLineDebugVertex>& buildTerrainTriangleWireframeVertices(BaseSystem& baseSystem) {
            static TerrainWireframeCache cache;

            const glm::vec3 nearColor(0.95f, 0.95f, 0.95f);
            const glm::vec3 nearAlphaColor(0.72f, 0.88f, 0.72f);
            const glm::vec3 farColor(1.00f, 0.72f, 0.20f);
            const uint64_t nearSignature = computeNearWireframeSignature(baseSystem);
            const uint64_t farSignature = computeFarWireframeSignature(baseSystem);

            if (!cache.valid || cache.nearSignature != nearSignature || cache.farSignature != farSignature) {
                cache.vertices.clear();
                cache.stats = {};
                std::unordered_set<WireframeEdgeKey, WireframeEdgeKeyHash> uniqueEdges;
                uniqueEdges.reserve(65536);

                if (baseSystem.voxelRender) {
                    const auto& sourceMeshes = !baseSystem.voxelRender->wireframeMeshes.empty()
                        ? baseSystem.voxelRender->wireframeMeshes
                        : baseSystem.voxelRender->preparedMeshes;
                    for (const auto& [key, prepared] : sourceMeshes) {
                        const auto renderIt = baseSystem.voxelRender->renderBuffers.find(key);
                        if (renderIt == baseSystem.voxelRender->renderBuffers.end()) continue;
                        const VoxelChunkLifecycleState* state = baseSystem.voxelWorld
                            ? baseSystem.voxelWorld->findChunkState(key)
                            : nullptr;
                        if (!state || !state->isRenderable()) continue;
                        for (int faceType = 0; faceType < 6; ++faceType) {
                            for (const FaceInstanceRenderData& face : prepared.opaqueFaces[static_cast<size_t>(faceType)]) {
                                appendTerrainWireframeFace(cache.vertices, uniqueEdges, faceType, face, nearColor, 0);
                                cache.stats.nearFaceCount += 1;
                            }
                            for (const FaceInstanceRenderData& face : prepared.alphaFaces[static_cast<size_t>(faceType)]) {
                                appendTerrainWireframeFace(cache.vertices, uniqueEdges, faceType, face, nearAlphaColor, 1);
                                cache.stats.nearFaceCount += 1;
                            }
                        }
                    }
                }

                if (baseSystem.farTerrain && baseSystem.farTerrain->enabled) {
                    const auto appendFarSet = [&](const std::array<std::vector<FaceInstanceRenderData>, 6>& faces) {
                        for (int faceType = 0; faceType < 6; ++faceType) {
                            for (const FaceInstanceRenderData& face : faces[static_cast<size_t>(faceType)]) {
                                appendTerrainWireframeFace(cache.vertices, uniqueEdges, faceType, face, farColor, 2);
                                cache.stats.farFaceCount += 1;
                            }
                        }
                    };
                    appendFarSet(baseSystem.farTerrain->bodyFaces);
                    appendFarSet(baseSystem.farTerrain->handoffFaces);
                }

                cache.stats.lineSegmentCount = cache.vertices.size() / 2u;
                cache.nearSignature = nearSignature;
                cache.farSignature = farSignature;
                cache.valid = true;
            }

            if (baseSystem.voxelRender) {
                baseSystem.voxelRender->wireframeOverlayNearFaces = cache.stats.nearFaceCount;
                baseSystem.voxelRender->wireframeOverlayFarFaces = cache.stats.farFaceCount;
                baseSystem.voxelRender->wireframeOverlayLineSegments = cache.stats.lineSegmentCount;
            }

            return cache.vertices;
        }

        bool readRegistryBool(const BaseSystem& baseSystem, const char* key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
        }

        int readRegistryInt(const BaseSystem& baseSystem, const char* key, int fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stoi(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        float readRegistryFloat(const BaseSystem& baseSystem, const char* key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
            try {
                return std::stof(std::get<std::string>(it->second));
            } catch (...) {
                return fallback;
            }
        }

        int alignDownToStep(int value, int step) {
            if (step <= 0) return value;
            int r = value % step;
            if (r < 0) r += step;
            return value - r;
        }

        glm::vec3 stressColor(float stressNorm, float upliftNorm) {
            float s = glm::clamp(std::fabs(stressNorm), 0.0f, 1.0f);
            glm::vec3 color = (stressNorm >= 0.0f)
                ? glm::mix(glm::vec3(0.95f, 0.78f, 0.25f), glm::vec3(1.00f, 0.25f, 0.08f), s)
                : glm::mix(glm::vec3(0.30f, 0.85f, 1.00f), glm::vec3(0.15f, 0.40f, 1.00f), s);
            float brightness = 0.55f + 0.45f * glm::clamp(upliftNorm, 0.0f, 1.0f);
            return glm::clamp(color * brightness, glm::vec3(0.0f), glm::vec3(1.0f));
        }

        void pushLine(std::vector<LeyLineDebugVertex>& vertices,
                      const glm::vec3& a,
                      const glm::vec3& b,
                      const glm::vec3& color) {
            vertices.push_back({a, color});
            vertices.push_back({b, color});
        }

        int sectionScaleFactor() {
            return 1;
        }

        glm::vec3 chunkBorderColor() {
            return glm::vec3(1.00f, 0.30f, 0.30f);
        }

        void pushSectionBox(std::vector<LeyLineDebugVertex>& vertices,
                            const glm::vec3& minCorner,
                            const glm::vec3& maxCorner,
                            const glm::vec3& color) {
            const glm::vec3 p000(minCorner.x, minCorner.y, minCorner.z);
            const glm::vec3 p001(minCorner.x, minCorner.y, maxCorner.z);
            const glm::vec3 p010(minCorner.x, maxCorner.y, minCorner.z);
            const glm::vec3 p011(minCorner.x, maxCorner.y, maxCorner.z);
            const glm::vec3 p100(maxCorner.x, minCorner.y, minCorner.z);
            const glm::vec3 p101(maxCorner.x, minCorner.y, maxCorner.z);
            const glm::vec3 p110(maxCorner.x, maxCorner.y, minCorner.z);
            const glm::vec3 p111(maxCorner.x, maxCorner.y, maxCorner.z);

            // Bottom loop
            pushLine(vertices, p000, p100, color);
            pushLine(vertices, p100, p101, color);
            pushLine(vertices, p101, p001, color);
            pushLine(vertices, p001, p000, color);
            // Top loop
            pushLine(vertices, p010, p110, color);
            pushLine(vertices, p110, p111, color);
            pushLine(vertices, p111, p011, color);
            pushLine(vertices, p011, p010, color);
            // Vertical edges
            pushLine(vertices, p000, p010, color);
            pushLine(vertices, p100, p110, color);
            pushLine(vertices, p101, p111, color);
            pushLine(vertices, p001, p011, color);
        }

        std::vector<LeyLineDebugVertex> buildChunkBorderDebugVertices(const BaseSystem& baseSystem) {
            std::vector<LeyLineDebugVertex> vertices;
            if (!baseSystem.voxelWorld || !baseSystem.player) return vertices;

            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const glm::vec3 cameraPos = baseSystem.player->cameraPosition;
            const float radius = glm::clamp(readRegistryFloat(baseSystem, "ChunkBorderDebugRadius", 256.0f), 16.0f, 8192.0f);
            const float radiusSq = radius * radius;
            const int maxSections = std::clamp(readRegistryInt(baseSystem, "ChunkBorderDebugMaxSections", 256), 1, 4096);

            struct Candidate {
                VoxelSectionKey key;
                float dist2 = 0.0f;
            };
            std::vector<Candidate> candidates;
            candidates.reserve(voxelWorld.sections.size());

            for (const auto& [key, section] : voxelWorld.sections) {
                const int sectionScale = sectionScaleFactor();
                const float worldSpan = static_cast<float>(section.size * sectionScale);
                const float centerX = (static_cast<float>(key.coord.x) + 0.5f) * worldSpan;
                const float centerZ = (static_cast<float>(key.coord.z) + 0.5f) * worldSpan;
                const float dx = centerX - cameraPos.x;
                const float dz = centerZ - cameraPos.z;
                const float dist2 = dx * dx + dz * dz;
                if (dist2 > radiusSq) continue;
                candidates.push_back({key, dist2});
            }

            if (candidates.empty()) return vertices;
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
                if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
                if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
                if (a.key.coord.y != b.key.coord.y) return a.key.coord.y < b.key.coord.y;
                return a.key.coord.z < b.key.coord.z;
            });

            const size_t drawCount = std::min(candidates.size(), static_cast<size_t>(maxSections));
            vertices.reserve(drawCount * 24u);
            for (size_t i = 0; i < drawCount; ++i) {
                const VoxelSectionKey& key = candidates[i].key;
                auto secIt = voxelWorld.sections.find(key);
                if (secIt == voxelWorld.sections.end()) continue;
                const VoxelSection& section = secIt->second;
                const int sectionScale = sectionScaleFactor();
                const float worldSpan = static_cast<float>(section.size * sectionScale);
                const glm::vec3 minCorner(
                    static_cast<float>(key.coord.x) * worldSpan,
                    static_cast<float>(key.coord.y) * worldSpan,
                    static_cast<float>(key.coord.z) * worldSpan
                );
                const glm::vec3 maxCorner = minCorner + glm::vec3(worldSpan);
                pushSectionBox(vertices, minCorner, maxCorner, chunkBorderColor());
            }

            return vertices;
        }

        std::vector<LeyLineDebugVertex> buildLeyLineDebugVertices(const BaseSystem& baseSystem) {
            std::vector<LeyLineDebugVertex> vertices;
            if (!baseSystem.world || !baseSystem.player) return vertices;

            const WorldContext& world = *baseSystem.world;
            const LeyLineContext& ley = world.leyLines;
            if (!ley.enabled || !ley.loaded) return vertices;

            const int radius = std::clamp(readRegistryInt(baseSystem, "LeyLineDebugRadius", 56), 8, 256);
            const int defaultStep = std::max(4, static_cast<int>(std::round(std::max(2.0f, ley.sampleStep))));
            const int step = std::clamp(readRegistryInt(baseSystem, "LeyLineDebugStep", defaultStep), 2, 64);
            const float yOffset = glm::clamp(readRegistryFloat(baseSystem, "LeyLineDebugYOffset", 1.25f), -8.0f, 32.0f);
            const float heightScale = glm::clamp(readRegistryFloat(baseSystem, "LeyLineDebugHeightScale", 7.5f), 0.25f, 64.0f);
            const float maxHeight = glm::clamp(readRegistryFloat(baseSystem, "LeyLineDebugMaxHeight", 16.0f), 0.25f, 128.0f);
            const bool showGrid = readRegistryBool(baseSystem, "LeyLineDebugGrid", true);
            const float stressClamp = std::max(1e-4f, ley.stressClamp);
            const float upliftMax = std::max(1e-4f, ley.upliftMax);

            const glm::vec3 cameraPos = baseSystem.player->cameraPosition;
            const int centerX = alignDownToStep(static_cast<int>(std::floor(cameraPos.x)), step);
            const int centerZ = alignDownToStep(static_cast<int>(std::floor(cameraPos.z)), step);
            const int minX = centerX - radius;
            const int maxX = centerX + radius;
            const int minZ = centerZ - radius;
            const int maxZ = centerZ + radius;
            const int countX = ((maxX - minX) / step) + 1;
            const int countZ = ((maxZ - minZ) / step) + 1;
            if (countX <= 0 || countZ <= 0) return vertices;

            struct SampleNode {
                bool valid = false;
                glm::vec3 tip = glm::vec3(0.0f);
                glm::vec3 color = glm::vec3(1.0f);
            };
            std::vector<SampleNode> nodes(static_cast<size_t>(countX * countZ));
            auto nodeAt = [&](int gx, int gz) -> SampleNode& {
                return nodes[static_cast<size_t>(gz * countX + gx)];
            };

            vertices.reserve(static_cast<size_t>(countX * countZ) * 8u);
            for (int gz = 0; gz < countZ; ++gz) {
                int z = minZ + gz * step;
                for (int gx = 0; gx < countX; ++gx) {
                    int x = minX + gx * step;
                    float terrainHeight = 0.0f;
                    bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(world, static_cast<float>(x), static_cast<float>(z), terrainHeight);
                    if (!isLand) terrainHeight = world.expanse.waterSurface;

                    float stress = LeyLineSystemLogic::SampleLeyStress(world, static_cast<float>(x), static_cast<float>(z));
                    float uplift = LeyLineSystemLogic::SampleLeyUplift(world, static_cast<float>(x), static_cast<float>(z));
                    float stressNorm = glm::clamp(stress / stressClamp, -1.0f, 1.0f);
                    float upliftNorm = glm::clamp(uplift / upliftMax, 0.0f, 1.0f);
                    glm::vec3 color = stressColor(stressNorm, upliftNorm);

                    float barHeight = 0.35f + (std::fabs(stressNorm) * heightScale) + (upliftNorm * heightScale * 0.45f);
                    barHeight = glm::clamp(barHeight, 0.35f, maxHeight);
                    glm::vec3 base(static_cast<float>(x), terrainHeight + yOffset, static_cast<float>(z));
                    glm::vec3 tip = base + glm::vec3(0.0f, barHeight, 0.0f);

                    pushLine(vertices, base, tip, color);
                    float crossHalf = glm::clamp(static_cast<float>(step) * 0.22f, 0.30f, 1.8f);
                    pushLine(vertices, tip + glm::vec3(-crossHalf, 0.0f, 0.0f), tip + glm::vec3(crossHalf, 0.0f, 0.0f), color * 0.85f);
                    pushLine(vertices, tip + glm::vec3(0.0f, 0.0f, -crossHalf), tip + glm::vec3(0.0f, 0.0f, crossHalf), color * 0.85f);

                    SampleNode& node = nodeAt(gx, gz);
                    node.valid = true;
                    node.tip = tip;
                    node.color = color;
                }
            }

            if (showGrid) {
                for (int gz = 0; gz < countZ; ++gz) {
                    for (int gx = 0; gx < countX; ++gx) {
                        const SampleNode& a = nodeAt(gx, gz);
                        if (!a.valid) continue;
                        if (gx + 1 < countX) {
                            const SampleNode& b = nodeAt(gx + 1, gz);
                            if (b.valid) {
                                pushLine(vertices, a.tip, b.tip, glm::mix(a.color, b.color, 0.5f) * 0.72f);
                            }
                        }
                        if (gz + 1 < countZ) {
                            const SampleNode& b = nodeAt(gx, gz + 1);
                            if (b.valid) {
                                pushLine(vertices, a.tip, b.tip, glm::mix(a.color, b.color, 0.5f) * 0.72f);
                            }
                        }
                    }
                }
            }

            return vertices;
        }
    } // namespace

    void RenderOverlays(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes; (void)dt; (void)win;
        if (!baseSystem.renderer || !baseSystem.player || !baseSystem.renderBackend) return;
        RendererContext& renderer = *baseSystem.renderer;
        PlayerContext& player = *baseSystem.player;
        auto& renderBackend = *baseSystem.renderBackend;

        float time = static_cast<float>(PlatformInput::GetTimeSeconds());
        glm::mat4 view = player.viewMatrix;
        glm::mat4 projection = player.projectionMatrix;
        glm::vec3 playerPos = player.cameraPosition;
        const bool oreMiningActive = OreMiningSystemLogic::IsMiningActive(baseSystem);
        const bool gemChiselActive = GemChiselSystemLogic::IsChiselActive(baseSystem);
        auto setDepthTestEnabled = [&](bool enabled) {
            renderBackend.setDepthTestEnabled(enabled);
        };
        auto setDepthWriteEnabled = [&](bool enabled) {
            renderBackend.setDepthWriteEnabled(enabled);
        };
        auto setBlendEnabled = [&](bool enabled) {
            renderBackend.setBlendEnabled(enabled);
        };
        auto bindTexture2D = [&](RenderHandle texture, int unit) {
            renderBackend.bindTexture2D(texture, unit);
        };
        auto setLineWidth = [&](float width) {
            renderBackend.setLineWidth(width);
        };

        bool blockSelectionVisualEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("BlockSelectionVisualEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                blockSelectionVisualEnabled = std::get<bool>(it->second);
            }
        }
        if (!oreMiningActive
            && !gemChiselActive
            && blockSelectionVisualEnabled
            && player.hasBlockTarget
            && renderer.selectionShader
            && renderer.selectionVAO
            && renderer.selectionVertexCount > 0) {
            renderer.selectionShader->use();
            glm::mat4 selectionModel = glm::translate(glm::mat4(1.0f), player.targetedBlockPosition);
            selectionModel = glm::scale(selectionModel, glm::vec3(1.02f));
            renderer.selectionShader->setMat4("model", selectionModel);
            renderer.selectionShader->setMat4("view", view);
            renderer.selectionShader->setMat4("projection", projection);
            PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, *renderer.selectionShader);
            renderer.selectionShader->setVec3("cameraPos", playerPos);
            renderer.selectionShader->setFloat("time", time);
            renderBackend.bindVertexArray(renderer.selectionVAO);
            renderBackend.drawArraysLines(0, renderer.selectionVertexCount);
        }
        BlockChargeSystemLogic::RenderBlockDamage(baseSystem, prototypes, dt, win);

        if (renderer.audioRayShader && renderer.audioRayVAO && renderer.audioRayVertexCount > 0) {
            setBlendEnabled(true);
            renderer.audioRayShader->use();
            renderer.audioRayShader->setMat4("view", view);
            renderer.audioRayShader->setMat4("projection", projection);
            PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, *renderer.audioRayShader);
            renderBackend.bindVertexArray(renderer.audioRayVAO);
            setLineWidth(1.6f);
            renderBackend.drawArraysLines(0, renderer.audioRayVertexCount);
            setLineWidth(1.0f);
        }

        const bool leyLineDebugVisualizerEnabled = readRegistryBool(baseSystem, "LeyLineDebugVisualizerEnabled", false);
        if (leyLineDebugVisualizerEnabled
            && renderer.audioRayShader
            && renderer.leyLineDebugVAO
            && renderer.leyLineDebugVBO) {
            std::vector<LeyLineDebugVertex> vertices = buildLeyLineDebugVertices(baseSystem);
            renderer.leyLineDebugVertexCount = static_cast<int>(vertices.size());
            if (!vertices.empty()) {
                renderBackend.uploadArrayBufferData(
                    renderer.leyLineDebugVBO,
                    vertices.data(),
                    vertices.size() * sizeof(LeyLineDebugVertex),
                    true
                );
                setDepthWriteEnabled(false);
                setBlendEnabled(true);
                renderer.audioRayShader->use();
                renderer.audioRayShader->setMat4("view", view);
                renderer.audioRayShader->setMat4("projection", projection);
                PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, *renderer.audioRayShader);
                renderBackend.bindVertexArray(renderer.leyLineDebugVAO);
                setLineWidth(1.4f);
                renderBackend.drawArraysLines(0, renderer.leyLineDebugVertexCount);
                setLineWidth(1.0f);
                setDepthWriteEnabled(true);
            }
        } else {
            renderer.leyLineDebugVertexCount = 0;
        }

        const bool chunkBorderDebugEnabled = readRegistryBool(baseSystem, "ChunkBorderDebugEnabled", false);
        if (chunkBorderDebugEnabled
            && renderer.audioRayShader
            && renderer.leyLineDebugVAO
            && renderer.leyLineDebugVBO) {
            std::vector<LeyLineDebugVertex> vertices = buildChunkBorderDebugVertices(baseSystem);
            if (!vertices.empty()) {
                renderBackend.uploadArrayBufferData(
                    renderer.leyLineDebugVBO,
                    vertices.data(),
                    vertices.size() * sizeof(LeyLineDebugVertex),
                    true
                );
                setDepthWriteEnabled(false);
                setBlendEnabled(true);
                renderer.audioRayShader->use();
                renderer.audioRayShader->setMat4("view", view);
                renderer.audioRayShader->setMat4("projection", projection);
                PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, *renderer.audioRayShader);
                renderBackend.bindVertexArray(renderer.leyLineDebugVAO);
                setLineWidth(1.8f);
                renderBackend.drawArraysLines(0, static_cast<int>(vertices.size()));
                setLineWidth(1.0f);
                setDepthWriteEnabled(true);
            }
        }

        const bool sceneTriangleWireframeDebugEnabled = readRegistryBool(baseSystem, "SceneTriangleWireframeDebugEnabled", false);
        if (sceneTriangleWireframeDebugEnabled
            && renderer.audioRayShader
            && renderer.leyLineDebugVAO
            && renderer.leyLineDebugVBO) {
            const std::vector<LeyLineDebugVertex>& vertices = buildTerrainTriangleWireframeVertices(baseSystem);
            if (!vertices.empty()) {
                renderBackend.uploadArrayBufferData(
                    renderer.leyLineDebugVBO,
                    vertices.data(),
                    vertices.size() * sizeof(LeyLineDebugVertex),
                    true
                );
                setDepthWriteEnabled(false);
                setBlendEnabled(true);
                renderer.audioRayShader->use();
                renderer.audioRayShader->setMat4("view", view);
                renderer.audioRayShader->setMat4("projection", projection);
                PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, *renderer.audioRayShader);
                renderBackend.bindVertexArray(renderer.leyLineDebugVAO);
                setLineWidth(1.1f);
                renderBackend.drawArraysLines(0, static_cast<int>(vertices.size()));
                setLineWidth(1.0f);
                setDepthWriteEnabled(true);
            }
        } else if (baseSystem.voxelRender) {
            baseSystem.voxelRender->wireframeOverlayNearFaces = 0;
            baseSystem.voxelRender->wireframeOverlayFarFaces = 0;
            baseSystem.voxelRender->wireframeOverlayLineSegments = 0;
        }

        GemSystemLogic::RenderGems(baseSystem, prototypes, dt, win);
        FishingSystemLogic::RenderFishing(baseSystem, prototypes, dt, win);
        ColorEmotionSystemLogic::RenderColorEmotions(baseSystem, prototypes, dt, win);

        bool crosshairEnabled = true;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("CrosshairEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                crosshairEnabled = std::get<bool>(it->second);
            }
        }
        if (!oreMiningActive
            && !gemChiselActive
            && crosshairEnabled
            && renderer.crosshairShader
            && renderer.crosshairVAO
            && renderer.crosshairVertexCount > 0) {
            setDepthTestEnabled(false);
            renderer.crosshairShader->use();
            renderBackend.bindVertexArray(renderer.crosshairVAO);
            setLineWidth(1.0f);
            renderBackend.drawArraysLines(0, renderer.crosshairVertexCount);
            setLineWidth(1.0f);
            setDepthTestEnabled(true);
        }

        bool legacyMeterEnabled = false;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("LegacyChargeMeterEnabled");
            if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                legacyMeterEnabled = std::get<bool>(it->second);
            }
        }
        if (legacyMeterEnabled && baseSystem.hud && renderer.hudShader && renderer.hudVAO) {
            HUDContext& hud = *baseSystem.hud;
            if (hud.showCharge) {
                setDepthTestEnabled(false);
                renderer.hudShader->use();
                renderer.hudShader->setFloat("fillAmount", glm::clamp(hud.chargeValue, 0.0f, 1.0f));
                renderer.hudShader->setInt("ready", hud.chargeReady ? 1 : 0);
                renderer.hudShader->setInt("buildModeType", hud.buildModeType);
                renderer.hudShader->setVec3("previewColor", hud.buildPreviewColor);
                renderer.hudShader->setInt("channelIndex", hud.buildChannel);
                renderer.hudShader->setInt("previewTileIndex", hud.buildPreviewTileIndex);
                renderer.hudShader->setInt("atlasEnabled", (renderer.atlasTexture != 0 && renderer.atlasTilesPerRow > 0 && renderer.atlasTilesPerCol > 0) ? 1 : 0);
                renderer.hudShader->setVec2("atlasTileSize", glm::vec2(renderer.atlasTileSize));
                renderer.hudShader->setVec2("atlasTextureSize", glm::vec2(renderer.atlasTextureSize));
                renderer.hudShader->setInt("tilesPerRow", renderer.atlasTilesPerRow);
                renderer.hudShader->setInt("tilesPerCol", renderer.atlasTilesPerCol);
                renderer.hudShader->setInt("atlasTexture", 0);
                if (renderer.atlasTexture != 0) {
                    bindTexture2D(renderer.atlasTexture, 0);
                }
                renderBackend.bindVertexArray(renderer.hudVAO);
                renderBackend.drawArraysTriangles(0, 6);
                setDepthTestEnabled(true);
            }
        }

        OreMiningSystemLogic::RenderOreMining(baseSystem, prototypes, dt, win);
        GroundCraftingSystemLogic::RenderGroundCrafting(baseSystem, prototypes, dt, win);
        GemChiselSystemLogic::RenderGemChisel(baseSystem, prototypes, dt, win);
    }
}
