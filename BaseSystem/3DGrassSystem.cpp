#pragma once

#include "Host/PlatformInput.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ThreeDGrassSystemLogic {
    namespace {
        constexpr int kMaxGrassCells = 64;
        constexpr float kGrassBlockHalf = 0.5f;
        constexpr float kGrassSurfaceEpsilon = 0.015f;
        constexpr float kGrassPatchHalf = 0.5f;
        constexpr float kGrassProxyPad = 0.08f;
        constexpr float kGrassHeight = 0.95f;
        constexpr float kGrassVolumeTopPad = 0.08f;
        constexpr float kClipWMin = 0.05f;

        struct GrassCellCandidate {
            glm::ivec3 cell{0};
            float distanceSq = 0.0f;
        };

        struct GrassPrototypeLookup {
            std::vector<uint32_t> ids;
            std::vector<uint8_t> mask;
            std::string atlasTileConfig;
            const WorldContext* world = nullptr;
            size_t prototypeCount = 0;
            size_t atlasMappingCount = 0;
            uint64_t version = 1;
        };

        struct GrassSectionCacheEntry {
            uint32_t editVersion = 0;
            uint64_t dirtyTicket = 0;
            uint64_t prototypeLookupVersion = 0;
            int size = 0;
            int nonAirCount = 0;
            size_t idCount = 0;
            const uint32_t* idsData = nullptr;
            std::vector<glm::ivec3> sourceCells;
        };

        struct GrassCollectStats {
            int maxCells = 0;
            int sectionsConsidered = 0;
            int sectionsCulled = 0;
            int sectionsScanned = 0;
            int cacheHits = 0;
            int cacheMisses = 0;
            int rawSourceCells = 0;
            int candidatesConsidered = 0;
            int candidatesVisible = 0;
            int candidatesBlockedAbove = 0;
            int selectedCells = 0;
            int debugCells = 0;
            int drawCells = 0;
            size_t voxelsScanned = 0;
            double collectMs = 0.0;
            double cacheBuildMs = 0.0;
            double uploadMs = 0.0;
            double drawSetupMs = 0.0;
            double totalMs = 0.0;
        };

        std::unordered_map<VoxelSectionKey, GrassSectionCacheEntry, VoxelSectionKeyHash> g_grassSectionCache;
        std::chrono::steady_clock::time_point g_lastGrassPerfLog{};

        double elapsedMs(std::chrono::steady_clock::time_point start,
                         std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now()) {
            return std::chrono::duration<double, std::milli>(end - start).count();
        }

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

        const GrassPrototypeLookup& coniferGrassPrototypeLookup(const BaseSystem& baseSystem,
                                                                const std::vector<Entity>& prototypes) {
            static GrassPrototypeLookup lookup;
            const std::string atlasTileConfig = registryString(baseSystem, "ThreeDGrassAtlasTiles", "");
            const WorldContext* world = baseSystem.world ? baseSystem.world.get() : nullptr;
            const size_t atlasMappingCount = world ? world->atlasMappings.size() : 0u;
            const bool rebuild =
                lookup.prototypeCount != prototypes.size()
                || lookup.world != world
                || lookup.atlasMappingCount != atlasMappingCount
                || lookup.atlasTileConfig != atlasTileConfig;
            if (!rebuild) return lookup;

            lookup.ids = coniferGrassPrototypeIDs(baseSystem, prototypes);
            size_t maskSize = prototypes.size() + 1u;
            for (uint32_t id : lookup.ids) {
                maskSize = std::max(maskSize, static_cast<size_t>(id) + 1u);
            }
            lookup.mask.assign(maskSize, 0u);
            for (uint32_t id : lookup.ids) {
                if (static_cast<size_t>(id) < lookup.mask.size()) {
                    lookup.mask[static_cast<size_t>(id)] = 1u;
                }
            }
            lookup.prototypeCount = prototypes.size();
            lookup.world = world;
            lookup.atlasMappingCount = atlasMappingCount;
            lookup.atlasTileConfig = atlasTileConfig;
            ++lookup.version;
            if (lookup.version == 0) lookup.version = 1;
            return lookup;
        }

        bool containsID(const GrassPrototypeLookup& lookup, uint32_t id) {
            return static_cast<size_t>(id) < lookup.mask.size()
                && lookup.mask[static_cast<size_t>(id)] != 0u;
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

        bool pointInsideAabb(const glm::vec3& point, const glm::vec3& minB, const glm::vec3& maxB) {
            return point.x >= minB.x && point.x <= maxB.x
                && point.y >= minB.y && point.y <= maxB.y
                && point.z >= minB.z && point.z <= maxB.z;
        }

        bool projectedAabbInView(const glm::vec3& minB,
                                 const glm::vec3& maxB,
                                 const glm::vec3& cameraPos,
                                 const glm::mat4& viewProjection,
                                 float ndcMargin) {
            if (pointInsideAabb(cameraPos, minB, maxB)) return true;

            glm::vec2 ndcMin(std::numeric_limits<float>::max());
            glm::vec2 ndcMax(std::numeric_limits<float>::lowest());
            bool hasProjectedCorner = false;

            for (int xi = 0; xi < 2; ++xi) {
                for (int yi = 0; yi < 2; ++yi) {
                    for (int zi = 0; zi < 2; ++zi) {
                        const glm::vec3 corner(
                            xi == 0 ? minB.x : maxB.x,
                            yi == 0 ? minB.y : maxB.y,
                            zi == 0 ? minB.z : maxB.z
                        );
                        const glm::vec4 clip = viewProjection * glm::vec4(corner, 1.0f);
                        if (clip.w <= kClipWMin) continue;
                        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
                        ndcMin = glm::min(ndcMin, ndc);
                        ndcMax = glm::max(ndcMax, ndc);
                        hasProjectedCorner = true;
                    }
                }
            }

            if (!hasProjectedCorner) return false;
            const float limit = 1.0f + std::max(0.0f, ndcMargin);
            return ndcMax.x >= -limit
                && ndcMin.x <= limit
                && ndcMax.y >= -limit
                && ndcMin.y <= limit;
        }

        void grassCellBounds(const glm::ivec3& cell,
                             float surfaceYOffset,
                             glm::vec3& outMin,
                             glm::vec3& outMax) {
            const float rootY = static_cast<float>(cell.y) + kGrassBlockHalf + kGrassSurfaceEpsilon + surfaceYOffset;
            outMin = glm::vec3(
                static_cast<float>(cell.x) - kGrassPatchHalf - kGrassProxyPad,
                rootY,
                static_cast<float>(cell.z) - kGrassPatchHalf - kGrassProxyPad
            );
            outMax = glm::vec3(
                static_cast<float>(cell.x) + kGrassPatchHalf + kGrassProxyPad,
                rootY + kGrassHeight + kGrassVolumeTopPad,
                static_cast<float>(cell.z) + kGrassPatchHalf + kGrassProxyPad
            );
        }

        bool grassCellInView(const glm::ivec3& cell,
                             const glm::vec3& cameraPos,
                             const glm::mat4& viewProjection,
                             float surfaceYOffset,
                             float ndcMargin) {
            glm::vec3 minB;
            glm::vec3 maxB;
            grassCellBounds(cell, surfaceYOffset, minB, maxB);
            return projectedAabbInView(minB, maxB, cameraPos, viewProjection, ndcMargin);
        }

        bool sectionInView(const VoxelSection& section,
                           const glm::vec3& cameraPos,
                           const glm::mat4& viewProjection,
                           float ndcMargin) {
            if (section.size <= 0) return false;
            const glm::vec3 minB = glm::vec3(section.coord * section.size);
            const glm::vec3 maxB = minB + glm::vec3(static_cast<float>(section.size));
            return projectedAabbInView(minB, maxB, cameraPos, viewProjection, ndcMargin);
        }

        float distanceSqToAabb(const glm::vec3& point, const glm::vec3& minB, const glm::vec3& maxB) {
            const glm::vec3 clamped = glm::clamp(point, minB, maxB);
            const glm::vec3 delta = point - clamped;
            return glm::dot(delta, delta);
        }

        float sectionDistanceSqToCamera(const VoxelSection& section, const glm::vec3& cameraPos) {
            if (section.size <= 0) return std::numeric_limits<float>::max();
            const glm::vec3 minB = glm::vec3(section.coord * section.size);
            const glm::vec3 maxB = minB + glm::vec3(static_cast<float>(section.size));
            return distanceSqToAabb(cameraPos, minB, maxB);
        }

        void appendGrassCellCandidate(const glm::ivec3& cell,
                                      const glm::vec3& cameraPos,
                                      const glm::mat4& viewProjection,
                                      float surfaceYOffset,
                                      float ndcMargin,
                                      int maxCells,
                                      float& worstDistanceSq,
                                      std::vector<GrassCellCandidate>& candidates,
                                      GrassCollectStats& stats) {
            ++stats.candidatesConsidered;
            if (!grassCellInView(cell, cameraPos, viewProjection, surfaceYOffset, ndcMargin)) return;
            ++stats.candidatesVisible;
            const glm::vec3 center = glm::vec3(cell);
            const glm::vec3 delta = center - cameraPos;
            const float distanceSq = glm::dot(delta, delta);
            if (static_cast<int>(candidates.size()) < maxCells) {
                candidates.push_back({cell, distanceSq});
                worstDistanceSq = std::max(worstDistanceSq, distanceSq);
                return;
            }
            if (distanceSq >= worstDistanceSq) return;

            auto worstIt = std::max_element(
                candidates.begin(),
                candidates.end(),
                [](const GrassCellCandidate& a, const GrassCellCandidate& b) {
                    return a.distanceSq < b.distanceSq;
                }
            );
            if (worstIt != candidates.end()) {
                *worstIt = {cell, distanceSq};
            }
            worstDistanceSq = 0.0f;
            for (const GrassCellCandidate& candidate : candidates) {
                worstDistanceSq = std::max(worstDistanceSq, candidate.distanceSq);
            }
        }

        void scanVoxelSectionForConiferGrassSourceCells(const BaseSystem& baseSystem,
                                                        const VoxelSection& section,
                                                        const GrassPrototypeLookup& coniferGrassLookup,
                                                        std::vector<glm::ivec3>& outCells,
                                                        GrassCollectStats& stats) {
            outCells.clear();
            if (!baseSystem.voxelWorld || !baseSystem.world || section.size <= 0) return;
            const int size = section.size;
            const glm::ivec3 base = section.coord * size;
            stats.voxelsScanned += section.ids.size();

            for (int z = 0; z < size; ++z) {
                for (int y = 0; y < size; ++y) {
                    for (int x = 0; x < size; ++x) {
                        const int idx = x + y * size + z * size * size;
                        if (idx < 0 || idx >= static_cast<int>(section.ids.size())) continue;
                        const uint32_t id = section.ids[static_cast<size_t>(idx)];
                        if (!containsID(coniferGrassLookup, id)) continue;

                        const glm::ivec3 cell = base + glm::ivec3(x, y, z);
                        if (ExpanseBiomeSystemLogic::ResolveBiome(*baseSystem.world, static_cast<float>(cell.x), static_cast<float>(cell.z)) != 0) {
                            continue;
                        }

                        outCells.push_back(cell);
                    }
                }
            }
        }

        const std::vector<glm::ivec3>& cachedGrassSourceCellsForSection(const BaseSystem& baseSystem,
                                                                        const VoxelWorldContext& voxelWorld,
                                                                        const VoxelSectionKey& key,
                                                                        const VoxelSection& section,
                                                                        const GrassPrototypeLookup& coniferGrassLookup,
                                                                        GrassCollectStats& stats) {
            const uint64_t dirtyTicket = voxelWorld.getSectionDirtyTicket(key);
            auto cacheIt = g_grassSectionCache.find(key);
            if (cacheIt != g_grassSectionCache.end()) {
                const GrassSectionCacheEntry& entry = cacheIt->second;
                if (entry.editVersion == section.editVersion
                    && entry.dirtyTicket == dirtyTicket
                    && entry.prototypeLookupVersion == coniferGrassLookup.version
                    && entry.size == section.size
                    && entry.nonAirCount == section.nonAirCount
                    && entry.idCount == section.ids.size()
                    && entry.idsData == section.ids.data()) {
                    ++stats.cacheHits;
                    stats.rawSourceCells += static_cast<int>(entry.sourceCells.size());
                    return entry.sourceCells;
                }
            }

            ++stats.cacheMisses;
            auto buildStart = std::chrono::steady_clock::now();
            GrassSectionCacheEntry& entry = g_grassSectionCache[key];
            entry.editVersion = section.editVersion;
            entry.dirtyTicket = dirtyTicket;
            entry.prototypeLookupVersion = coniferGrassLookup.version;
            entry.size = section.size;
            entry.nonAirCount = section.nonAirCount;
            entry.idCount = section.ids.size();
            entry.idsData = section.ids.data();
            scanVoxelSectionForConiferGrassSourceCells(baseSystem, section, coniferGrassLookup, entry.sourceCells, stats);
            stats.rawSourceCells += static_cast<int>(entry.sourceCells.size());
            stats.cacheBuildMs += elapsedMs(buildStart);
            return entry.sourceCells;
        }

        void pruneGrassSectionCache(const VoxelWorldContext& voxelWorld) {
            for (auto it = g_grassSectionCache.begin(); it != g_grassSectionCache.end();) {
                if (voxelWorld.sections.find(it->first) == voxelWorld.sections.end()) {
                    it = g_grassSectionCache.erase(it);
                } else {
                    ++it;
                }
            }
        }

        std::vector<glm::ivec3> collectExpanseGrassBlockCells(const BaseSystem& baseSystem,
                                                              const std::vector<Entity>& prototypes,
                                                              const glm::vec3& cameraPos,
                                                              const glm::mat4& viewProjection,
                                                              float surfaceYOffset,
                                                              float ndcMargin,
                                                              int maxCells,
                                                              GrassCollectStats& stats) {
            std::vector<glm::ivec3> cells;
            if (!isExpanseLevelActive(baseSystem)
                || !baseSystem.voxelWorld
                || !baseSystem.voxelWorld->enabled
                || !baseSystem.voxelRender) {
                return cells;
            }

            const GrassPrototypeLookup& coniferGrassLookup = coniferGrassPrototypeLookup(baseSystem, prototypes);
            if (coniferGrassLookup.ids.empty()) return cells;

            std::vector<GrassCellCandidate> candidates;
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const VoxelRenderContext& voxelRender = *baseSystem.voxelRender;
            pruneGrassSectionCache(voxelWorld);

            struct SectionCandidate {
                VoxelSectionKey key{};
                const VoxelSection* section = nullptr;
                float distanceSq = 0.0f;
            };
            std::vector<SectionCandidate> sections;
            sections.reserve(!voxelRender.renderClusters.empty()
                ? voxelRender.renderClusters.size()
                : voxelWorld.sections.size());

            auto addSectionCandidate = [&](const VoxelSectionKey& key, const VoxelSection& section) {
                ++stats.sectionsConsidered;
                if (section.size <= 0 || section.nonAirCount <= 0) return;
                if (!sectionInView(section, cameraPos, viewProjection, ndcMargin)) {
                    ++stats.sectionsCulled;
                    return;
                }
                sections.push_back({key, &section, sectionDistanceSqToCamera(section, cameraPos)});
            };

            if (!voxelRender.renderClusters.empty()) {
                for (const auto& [key, _] : voxelRender.renderClusters) {
                    auto sectionIt = voxelWorld.sections.find(key);
                    if (sectionIt == voxelWorld.sections.end()) continue;
                    addSectionCandidate(key, sectionIt->second);
                }
            } else {
                for (const auto& [key, section] : voxelWorld.sections) {
                    addSectionCandidate(key, section);
                }
            }

            std::sort(
                sections.begin(),
                sections.end(),
                [](const SectionCandidate& a, const SectionCandidate& b) {
                    return a.distanceSq < b.distanceSq;
                }
            );

            float worstDistanceSq = 0.0f;
            candidates.reserve(static_cast<size_t>(maxCells));
            for (const SectionCandidate& sectionCandidate : sections) {
                if (static_cast<int>(candidates.size()) >= maxCells
                    && sectionCandidate.distanceSq > worstDistanceSq) {
                    break;
                }
                if (!sectionCandidate.section) continue;
                ++stats.sectionsScanned;
                const std::vector<glm::ivec3>& sourceCells = cachedGrassSourceCellsForSection(
                    baseSystem,
                    voxelWorld,
                    sectionCandidate.key,
                    *sectionCandidate.section,
                    coniferGrassLookup,
                    stats
                );
                for (const glm::ivec3& cell : sourceCells) {
                    const uint32_t aboveID = voxelWorld.getBlockWorld(cell + glm::ivec3(0, 1, 0));
                    if (solidPrototypeAt(prototypes, aboveID)) {
                        ++stats.candidatesBlockedAbove;
                        continue;
                    }
                    appendGrassCellCandidate(
                        cell,
                        cameraPos,
                        viewProjection,
                        surfaceYOffset,
                        ndcMargin,
                        maxCells,
                        worstDistanceSq,
                        candidates,
                        stats
                    );
                }
            }

            if (candidates.empty()) return cells;

            std::sort(
                candidates.begin(),
                candidates.end(),
                [](const GrassCellCandidate& a, const GrassCellCandidate& b) {
                    return a.distanceSq < b.distanceSq;
                }
            );

            cells.reserve(candidates.size());
            for (const GrassCellCandidate& candidate : candidates) {
                cells.push_back(candidate.cell);
            }
            return cells;
        }

        std::vector<glm::ivec3> collectDebugGrassBlockCells(const BaseSystem& baseSystem,
                                                            const glm::vec3& cameraPos,
                                                            const glm::mat4& viewProjection,
                                                            float surfaceYOffset,
                                                            float ndcMargin) {
            std::vector<glm::ivec3> cells;
            if (!baseSystem.level) return cells;

            const std::string grassWorldName = registryString(baseSystem, "ThreeDGrassWorld", "3DGrassBlockWorld");
            const std::string blockPrototypeName = registryString(baseSystem, "ThreeDGrassBlockPrototype", "DebugBlockTex");
            const int maxCells = std::clamp(registryInt(baseSystem, "ThreeDGrassMaxCells", 1), 1, kMaxGrassCells);

            for (const Entity& world : baseSystem.level->worlds) {
                if (world.name != grassWorldName) continue;
                for (const EntityInstance& inst : world.instances) {
                    if (!blockPrototypeName.empty() && inst.name != blockPrototypeName) continue;
                    const glm::ivec3 cell = glm::ivec3(glm::round(inst.position));
                    if (!grassCellInView(cell, cameraPos, viewProjection, surfaceYOffset, ndcMargin)) continue;
                    cells.push_back(cell);
                    if (static_cast<int>(cells.size()) >= maxCells) return cells;
                }
            }

            return cells;
        }

        std::vector<glm::ivec3> collectGrassBlockCells(const BaseSystem& baseSystem,
                                                       const std::vector<Entity>& prototypes,
                                                       const glm::vec3& cameraPos,
                                                       const glm::mat4& viewProjection,
                                                       float surfaceYOffset,
                                                       float ndcMargin,
                                                       GrassCollectStats& stats) {
            const int maxCells = std::clamp(registryInt(baseSystem, "ThreeDGrassMaxCells", kMaxGrassCells), 1, kMaxGrassCells);
            stats.maxCells = maxCells;
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
                std::vector<glm::ivec3> debugCells = collectDebugGrassBlockCells(
                    baseSystem,
                    cameraPos,
                    viewProjection,
                    surfaceYOffset,
                    ndcMargin
                );
                stats.debugCells += static_cast<int>(debugCells.size());
                appendUniqueCells(debugCells);
            }

            if (static_cast<int>(cells.size()) < maxCells
                && registryBool(baseSystem, "ThreeDGrassExpanseConiferEnabled", true)) {
                std::vector<glm::ivec3> expanseCells = collectExpanseGrassBlockCells(
                    baseSystem,
                    prototypes,
                    cameraPos,
                    viewProjection,
                    surfaceYOffset,
                    ndcMargin,
                    maxCells - static_cast<int>(cells.size()),
                    stats
                );
                appendUniqueCells(expanseCells);
            }

            if (cells.empty()) {
                std::vector<glm::ivec3> debugCells = collectDebugGrassBlockCells(
                    baseSystem,
                    cameraPos,
                    viewProjection,
                    surfaceYOffset,
                    ndcMargin
                );
                stats.debugCells += static_cast<int>(debugCells.size());
                appendUniqueCells(debugCells);
            }
            stats.selectedCells = static_cast<int>(cells.size());
            return cells;
        }

        void maybeLogGrassPerf(const BaseSystem& baseSystem, const GrassCollectStats& stats) {
            const bool defaultEnabled = registryBool(baseSystem, "DebugVoxelMeshingPerf", false);
            if (!registryBool(baseSystem, "ThreeDGrassPerfLogEnabled", defaultEnabled)) return;
            const auto now = std::chrono::steady_clock::now();
            if (g_lastGrassPerfLog.time_since_epoch().count() != 0
                && now - g_lastGrassPerfLog < std::chrono::seconds(1)) {
                return;
            }
            g_lastGrassPerfLog = now;
            std::cout << "[3DGrassPerf]"
                      << " totalMs=" << stats.totalMs
                      << " collectMs=" << stats.collectMs
                      << " cacheBuildMs=" << stats.cacheBuildMs
                      << " uploadMs=" << stats.uploadMs
                      << " drawSetupMs=" << stats.drawSetupMs
                      << " cells=" << stats.drawCells << "/" << stats.maxCells
                      << " selected=" << stats.selectedCells
                      << " debug=" << stats.debugCells
                      << " sections=" << stats.sectionsScanned
                      << "/" << stats.sectionsConsidered
                      << " culled=" << stats.sectionsCulled
                      << " cache=" << stats.cacheHits << "/" << stats.cacheMisses
                      << " voxelsScanned=" << stats.voxelsScanned
                      << " rawCells=" << stats.rawSourceCells
                      << " candidates=" << stats.candidatesVisible
                      << "/" << stats.candidatesConsidered
                      << " blockedAbove=" << stats.candidatesBlockedAbove
                      << std::endl;
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
        const auto frameStart = std::chrono::steady_clock::now();
        GrassCollectStats stats;

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
        const float surfaceYOffset = registryFloat(baseSystem, "ThreeDGrassSurfaceYOffset", -0.48f);
        const float ndcMargin = glm::clamp(registryFloat(baseSystem, "ThreeDGrassFrustumMargin", 0.15f), 0.0f, 1.0f);
        const auto collectStart = std::chrono::steady_clock::now();
        std::vector<glm::ivec3> cells = collectGrassBlockCells(
            baseSystem,
            prototypes,
            cameraPos,
            viewProjection,
            surfaceYOffset,
            ndcMargin,
            stats
        );
        stats.collectMs = elapsedMs(collectStart);
        if (cells.empty()) {
            stats.totalMs = elapsedMs(frameStart);
            maybeLogGrassPerf(baseSystem, stats);
            return;
        }

        const int cellCount = static_cast<int>(std::min(cells.size(), static_cast<size_t>(kMaxGrassCells)));
        stats.drawCells = cellCount;
        std::vector<glm::vec3> instanceCells;
        instanceCells.reserve(static_cast<size_t>(cellCount));
        for (int i = 0; i < cellCount; ++i) {
            instanceCells.push_back(glm::vec3(cells[static_cast<size_t>(i)]));
        }
        if (instanceCells.empty()) {
            stats.totalMs = elapsedMs(frameStart);
            maybeLogGrassPerf(baseSystem, stats);
            return;
        }
        const auto uploadStart = std::chrono::steady_clock::now();
        renderBackend.uploadArrayBufferData(
            renderer.grass3DVolumeInstanceVBO,
            instanceCells.data(),
            instanceCells.size() * sizeof(glm::vec3),
            true
        );
        stats.uploadMs = elapsedMs(uploadStart);

        const auto drawSetupStart = std::chrono::steady_clock::now();
        Shader& shader = *renderer.grass3DShader;
        shader.use();
        shader.setMat4("view", view);
        shader.setMat4("projection", projection);
        PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, shader);
        shader.setVec3("cameraPos", cameraPos);
        shader.setFloat("time", static_cast<float>(PlatformInput::GetTimeSeconds()));
        shader.setFloat("grassSurfaceYOffset", surfaceYOffset);

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
        stats.drawSetupMs = elapsedMs(drawSetupStart);
        stats.totalMs = elapsedMs(frameStart);
        maybeLogGrassPerf(baseSystem, stats);
    }
}
