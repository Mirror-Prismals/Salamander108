#pragma once
#include "Host/PlatformInput.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <deque>
#include <array>
#include <chrono>

namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); }
namespace AudioSystemLogic { bool TriggerGameplaySfx(BaseSystem& baseSystem, const std::string& cueName, float gain); }
namespace ExpanseBiomeSystemLogic {
    bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight);
    int ResolveBiome(const WorldContext& worldCtx, float x, float z);
}
namespace RenderInitSystemLogic {
    int FaceTileIndexFor(const WorldContext* worldCtx, const Entity& proto, int faceType);
}
namespace TerrainSystemLogic {
    bool IsSectionTerrainReady(const VoxelSectionKey& key);
    void GetVoxelStreamingPerfStats(size_t& pending,
                                    size_t& desired,
                                    size_t& generated,
                                    size_t& jobs,
                                    int& stepped,
                                    int& built,
                                    int& consumed,
                                    int& skippedExisting,
                                    int& filteredOut,
                                    int& rescueSurfaceQueued,
                                    int& rescueMissingQueued,
                                    int& droppedByCap,
                                    int& reprioritized,
                                    float& prepMs,
                                    float& generationMs,
                                    float& desiredMs,
                                    float& baseGenMs,
                                    float& featureMs,
                                    float& surfaceMs,
                                    float& caveFieldMs,
                                    int& schedulerPressure,
                                    int& desiredBudget,
                                    int& baseBudget,
                                    int& featureBudget,
                                    int& surfaceBudget,
                                    float& baseBudgetMs,
                                    float& featureBudgetMs,
                                    float& surfaceBudgetMs,
                                    size_t& downstreamDirty,
                                    size_t& downstreamPrepared,
                                    size_t& downstreamUpload,
                                    uint64_t& caveFieldCellsBuilt,
                                    uint64_t& caveSamples);
}
namespace VoxelMeshingSystemLogic { void RequestPriorityVoxelRemesh(BaseSystem& baseSystem, std::vector<Entity>& prototypes, const glm::ivec3& worldCell); }

namespace TreeGenerationSystemLogic {
    namespace {
        struct PineSpec {
            int trunkHeight = 30;
            int canopyOffset = 26;
            int canopyLayers = 30;
            float canopyBottomRadius = 3.0f;
            float canopyTopRadius = 1.0f;
            int canopyLowerExtension = 2;      // extra canopy layers pushed lower (toward ground)
            float canopyLowerRadiusBoost = 1.0f; // wider ring on the lower extension
            int spawnModulo = 100;
            int trunkExclusionRadius = 3;
        };

        struct FoliageSpec {
            bool enabled = true;
            bool grassEnabled = true;
            bool grassCoverEnabled = true;
            bool pebblePatchEnabled = true;
            bool autumnLeafPatchEnabled = true;
            bool lilypadPatchEnabled = true;
            bool flowerEnabled = true;
            bool stickEnabled = true;
            bool waterFoliageEnabled = true;
            bool temperateOnly = true;
            int grassSpawnModulo = 1;
            int grassCoverPercent = 35;
            int pebblePatchPercent = 18;
            int pebblePatchNearWaterRadius = 10;
            int pebblePatchNearWaterVerticalRange = 4;
            int autumnLeafPatchPercent = 18;
            int lilypadPatchPercent = 2;
            int flowerSpawnModulo = 41;
            int miniPineSpawnModulo = 180;
            int shortGrassPercent = 50;
            int grassTuftPercent = 45;
            int stickSpawnPercent = 18;
            int stickCanopySearchRadius = 1;
            int stickCanopySearchHeight = 22;
            int kelpSpawnPercent = 24;
            int seaUrchinSpawnPercent = 10;
            int sandDollarSpawnPercent = 22;
        };

        struct PineNubPlacement {
            int trunkOffsetY = 0;
            glm::ivec2 dir = glm::ivec2(1, 0); // x,z step on trunk side
        };

        static std::unordered_map<VoxelSectionKey, uint32_t, VoxelSectionKeyHash> g_treeAppliedVersion;
        static std::unordered_map<VoxelSectionKey, uint32_t, VoxelSectionKeyHash> g_treeSurfaceAppliedVersion;
        static std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> g_treeBackfillVisited;
        static std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> g_treePendingDependencies;
        static std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> g_treePendingSections;
        static std::deque<VoxelSectionKey> g_treeImmediateQueue;
        static std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> g_treeImmediateQueued;
        static std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> g_treeForceCompleteSections;
        struct TreeSectionProgress {
            int phase = 0;
            int surfaceScanTierX = std::numeric_limits<int>::min();
            int surfaceScanTierZ = std::numeric_limits<int>::min();
            int scanTierX = std::numeric_limits<int>::min();
            int scanTierZ = std::numeric_limits<int>::min();
            bool unresolvedDependencies = false;
            bool startedAsBackfill = false;
            bool surfaceFoliageSeeded = false;
        };
        static std::unordered_map<VoxelSectionKey, TreeSectionProgress, VoxelSectionKeyHash> g_treeSectionProgress;
        void clearImmediateFoliageQueue() {
            g_treeImmediateQueue.clear();
            g_treeImmediateQueued.clear();
            g_treeForceCompleteSections.clear();
        }
        static std::string g_treeLevelKey;
        static std::vector<std::pair<glm::ivec3, int>> g_pendingPineLogRemovals;
        static uint64_t g_treeFrameCounter = 0;
        static uint64_t g_treeFoliageSignature = 0;

        struct TreeFoliagePerfStats {
            size_t pendingSections = 0;
            size_t pendingDependencies = 0;
            size_t backfillVisited = 0;
            int selectedSections = 0;
            int processedSections = 0;
            int deferredByTimeBudget = 0;
            int backfillAppended = 0;
            int scannedColumns = 0;
            int candidateColumns = 0;
            int placedTrees = 0;
            int skippedOutOfSection = 0;
            int skippedNonLand = 0;
            int skippedMissingGround = 0;
            int blockedByDependencies = 0;
            int blockedByColumn = 0;
            int blockedBySpacing = 0;
            bool backfillRan = false;
            float updateMs = 0.0f;
        };
        static TreeFoliagePerfStats g_treePerfStats;

        bool cellBelongsToSection(const glm::ivec3& worldCell, const glm::ivec3& sectionCoord, int sectionSize);

        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        int sectionSizeForTier(const VoxelWorldContext& voxelWorld, int /*ignoredTier*/) {
            return voxelWorld.sectionSize > 0 ? voxelWorld.sectionSize : 1;
        }

        int sectionScaleForTier(int /*ignoredTier*/) {
            return 1;
        }

        uint32_t getBlockAt(const VoxelWorldContext& voxelWorld,
                            const glm::ivec3& worldCoord) {
            return voxelWorld.getBlockWorld(worldCoord);
        }

        uint32_t getColorAt(const VoxelWorldContext& voxelWorld,
                            const glm::ivec3& worldCoord) {
            return voxelWorld.getColorWorld(worldCoord);
        }

        bool isGroundDependencyPending(const VoxelWorldContext& voxelWorld,
                                       const glm::ivec3& worldCell,
                                       int sectionSize) {
            VoxelSectionKey groundKey{
                glm::ivec3(
                    floorDivInt(worldCell.x, sectionSize),
                    floorDivInt(worldCell.y, sectionSize),
                    floorDivInt(worldCell.z, sectionSize)
                )
            };
            auto groundIt = voxelWorld.sections.find(groundKey);
            if (groundIt == voxelWorld.sections.end()) return true;
            // Non-empty, versioned sections are treated as stable even if this exact cell is air.
            // This avoids perpetual retries for valid holes/caves/player edits.
            if (groundIt->second.nonAirCount <= 0) return true;
            if (groundIt->second.editVersion == 0) return true;
            return false;
        }

        uint32_t packColor(const glm::vec3& color) {
            auto clampByte = [](float v) {
                int iv = static_cast<int>(std::round(v * 255.0f));
                if (iv < 0) iv = 0;
                if (iv > 255) iv = 255;
                return static_cast<uint32_t>(iv);
            };
            uint32_t r = clampByte(color.r);
            uint32_t g = clampByte(color.g);
            uint32_t b = clampByte(color.b);
            return (r << 16) | (g << 8) | b;
        }

        int encodeGrassCoverSnapshotTile(int tileIndex) {
            // 0 means "no snapshot"; store tile as +1 in the high byte.
            if (tileIndex < 0 || tileIndex > 254) return 0;
            return tileIndex + 1;
        }

        uint32_t withGrassCoverSnapshotTile(uint32_t packedColorRgb, int tileIndex) {
            const uint32_t rgb = packedColorRgb & 0x00ffffffu;
            const uint32_t encoded = static_cast<uint32_t>(encodeGrassCoverSnapshotTile(tileIndex) & 0xff) << 24;
            return rgb | encoded;
        }

        int decodeGrassCoverSnapshotTile(uint32_t packedColor) {
            const int encoded = static_cast<int>((packedColor >> 24) & 0xffu);
            if (encoded <= 0) return -1;
            // Water metadata now occupies high-byte values whose low nibble is marker [0..5]
            // and high nibble is wave class [0..4]. These are not grass snapshot tiles.
            const int marker = encoded & 0x0f;
            const int waveClass = (encoded >> 4) & 0x0f;
            if (marker <= 5 && waveClass <= 4) return -1;
            return encoded - 1;
        }

        constexpr uint8_t kWaterFoliageMarkerNone = 0u;
        constexpr uint8_t kWaterFoliageMarkerKelp = 1u;
        constexpr uint8_t kWaterFoliageMarkerSeaUrchinX = 2u;
        constexpr uint8_t kWaterFoliageMarkerSeaUrchinZ = 3u;
        constexpr uint8_t kWaterFoliageMarkerSandDollarX = 4u;
        constexpr uint8_t kWaterFoliageMarkerSandDollarZ = 5u;
        constexpr uint8_t kWaterWaveClassUnknown = 0u;
        constexpr uint8_t kWaterWaveClassRiver = 3u;
        constexpr uint8_t kWaterWaveClassOcean = 4u;

        uint8_t waterWaveClassFromPackedColor(uint32_t packedColor) {
            const uint8_t encoded = static_cast<uint8_t>((packedColor >> 24) & 0xffu);
            if (encoded <= kWaterFoliageMarkerSandDollarZ) {
                // Legacy marker-only format has no wave class.
                return kWaterWaveClassUnknown;
            }
            const uint8_t marker = static_cast<uint8_t>(encoded & 0x0fu);
            const uint8_t waveClass = static_cast<uint8_t>((encoded >> 4u) & 0x0fu);
            if (marker <= kWaterFoliageMarkerSandDollarZ && waveClass <= kWaterWaveClassOcean) {
                return waveClass;
            }
            return kWaterWaveClassUnknown;
        }

        uint32_t withWaterFoliageMarker(uint32_t packedColor, uint8_t marker) {
            const uint32_t rgb = packedColor & 0x00ffffffu;
            const uint8_t waveClass = waterWaveClassFromPackedColor(packedColor);
            const uint8_t encoded = static_cast<uint8_t>(((waveClass & 0x0fu) << 4u) | (marker & 0x0fu));
            return rgb | (static_cast<uint32_t>(encoded) << 24u);
        }

        uint8_t waterFoliageMarkerFromPackedColor(uint32_t packedColor) {
            const uint8_t encoded = static_cast<uint8_t>((packedColor >> 24) & 0xffu);
            if (encoded <= kWaterFoliageMarkerSandDollarZ) {
                // Legacy marker-only format.
                return encoded;
            }
            const uint8_t marker = static_cast<uint8_t>(encoded & 0x0fu);
            const uint8_t waveClass = static_cast<uint8_t>((encoded >> 4u) & 0x0fu);
            if (marker <= kWaterFoliageMarkerSandDollarZ && waveClass <= kWaterWaveClassOcean) {
                return marker;
            }
            return kWaterFoliageMarkerNone;
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

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
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

        std::string getRegistryString(const BaseSystem& baseSystem,
                                      const std::string& key,
                                      const std::string& fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            return std::get<std::string>(it->second);
        }

        bool triggerGameplaySfx(BaseSystem& baseSystem, const char* fileName, float cooldownSeconds = 0.0f) {
            if (!fileName) return false;
            static std::unordered_map<std::string, double> s_lastTrigger;
            const double now = PlatformInput::GetTimeSeconds();
            const std::string keyName(fileName);
            auto it = s_lastTrigger.find(keyName);
            if (it != s_lastTrigger.end() && (now - it->second) < static_cast<double>(cooldownSeconds)) {
                return false;
            }

            if (AudioSystemLogic::TriggerGameplaySfx(baseSystem, keyName, 1.0f)) {
                s_lastTrigger[keyName] = now;
                return true;
            }

            if (!getRegistryBool(baseSystem, "GameplaySfxFallbackToChuck", false)) return false;
            if (!baseSystem.audio || !baseSystem.audio->chuck) return false;
            const std::string scriptPath = std::string("Procedures/chuck/gameplay/") + fileName;
            std::vector<t_CKUINT> ids;
            std::lock_guard<std::mutex> chuckLock(baseSystem.audio->chuck_vm_mutex);
            bool ok = baseSystem.audio->chuck->compileFile(scriptPath, "", 1, FALSE, &ids);
            if (ok && !ids.empty()) {
                s_lastTrigger[keyName] = now;
                return true;
            }
            return false;
        }

        uint32_t hash2D(int x, int z) {
            uint32_t ux = static_cast<uint32_t>(x) * 73856093u;
            uint32_t uz = static_cast<uint32_t>(z) * 19349663u;
            uint32_t h = ux ^ uz;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
        }

        uint32_t hash3D(int x, int y, int z) {
            uint32_t ux = static_cast<uint32_t>(x) * 73856093u;
            uint32_t uy = static_cast<uint32_t>(y) * 19349663u;
            uint32_t uz = static_cast<uint32_t>(z) * 83492791u;
            uint32_t h = ux ^ uy ^ uz;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
        }

        struct IVec3Hash {
            std::size_t operator()(const glm::ivec3& v) const noexcept {
                std::size_t hx = std::hash<int>()(v.x);
                std::size_t hy = std::hash<int>()(v.y);
                std::size_t hz = std::hash<int>()(v.z);
                return hx ^ (hy << 1) ^ (hz << 2);
            }
        };

        enum class FallDirection : int { PosX = 0, NegX = 1, PosZ = 2, NegZ = 3 };

        bool isPineVerticalLogName(const std::string& name) {
            return name == "FirLog1Tex" || name == "FirLog2Tex"
                || name == "FirLog1TopTex" || name == "FirLog2TopTex";
        }

        bool isPineAnyLogName(const std::string& name) {
            return isPineVerticalLogName(name)
                || name == "FirLog1TexX" || name == "FirLog2TexX"
                || name == "FirLog1TexZ" || name == "FirLog2TexZ"
                || name == "FirLog1NubTexX" || name == "FirLog2NubTexX"
                || name == "FirLog1NubTexZ" || name == "FirLog2NubTexZ";
        }

        bool isPineVerticalLogPrototypeID(const std::vector<Entity>& prototypes, int prototypeID) {
            return prototypeID >= 0
                && prototypeID < static_cast<int>(prototypes.size())
                && isPineVerticalLogName(prototypes[prototypeID].name);
        }

        bool isPineAnyLogPrototypeID(const std::vector<Entity>& prototypes, uint32_t prototypeID) {
            int id = static_cast<int>(prototypeID);
            return id >= 0
                && id < static_cast<int>(prototypes.size())
                && isPineAnyLogName(prototypes[id].name);
        }

        bool isLeafPrototypeName(const std::string& name) {
            return name == "Leaf"
                || name.rfind("LeafJungle", 0) == 0
                || name == "GrassTuftLeafFanOak"
                || name == "GrassTuftLeafFanPine";
        }

        bool isJungleLeafPrototypeName(const std::string& name) {
            return name.rfind("LeafJungle", 0) == 0;
        }

        bool isLeafPrototypeID(const std::vector<Entity>& prototypes, uint32_t prototypeID) {
            int id = static_cast<int>(prototypeID);
            return id >= 0
                && id < static_cast<int>(prototypes.size())
                && isLeafPrototypeName(prototypes[id].name);
        }

        bool isWithinJungleVolcano(const ExpanseConfig& cfg,
                                   int biomeID,
                                   int worldX,
                                   int worldZ) {
            if (!cfg.loaded
                || !cfg.jungleVolcanoEnabled
                || !cfg.secondaryBiomeEnabled
                || cfg.islandRadius <= 0.0f) {
                return false;
            }
            if (biomeID != 3) return false; // jungle quadrant only
            const float centerFactorX = std::clamp(cfg.jungleVolcanoCenterFactorX, 0.0f, 1.0f);
            const float centerFactorZ = std::clamp(cfg.jungleVolcanoCenterFactorZ, 0.0f, 1.0f);
            const float volcanoCenterX = cfg.islandCenterX + cfg.islandRadius * centerFactorX;
            const float volcanoCenterZ = cfg.islandCenterZ + cfg.islandRadius * centerFactorZ;
            const float volcanoRadius = std::max(8.0f, cfg.jungleVolcanoOuterRadius);
            const float dx = static_cast<float>(worldX) - volcanoCenterX;
            const float dz = static_cast<float>(worldZ) - volcanoCenterZ;
            return (dx * dx + dz * dz) <= (volcanoRadius * volcanoRadius);
        }

        bool startsWith(const std::string& value, const char* prefix) {
            if (!prefix) return false;
            const size_t prefixLen = std::strlen(prefix);
            return value.size() >= prefixLen
                && value.compare(0, prefixLen, prefix) == 0;
        }

        bool isGrassFoliagePrototypeName(const std::string& name) {
            return startsWith(name, "GrassTuft");
        }

        bool isFlowerFoliagePrototypeName(const std::string& name) {
            return startsWith(name, "Flower");
        }

        bool isFoliageGroundPrototypeID(const std::vector<Entity>& prototypes,
                                        uint32_t prototypeID,
                                        int waterPrototypeID) {
            int id = static_cast<int>(prototypeID);
            if (id <= 0 || id >= static_cast<int>(prototypes.size())) return false;
            if (id == waterPrototypeID) return false;
            const Entity& proto = prototypes[static_cast<size_t>(id)];
            if (!proto.isBlock || !proto.isSolid) return false;
            if (isLeafPrototypeName(proto.name)
                || isGrassFoliagePrototypeName(proto.name)
                || isFlowerFoliagePrototypeName(proto.name)
                || proto.name == "StickTexX"
                || proto.name == "StickTexZ"
                || proto.name == "StonePebbleTexX"
                || proto.name == "StonePebbleTexZ"
                || proto.name == "WallStoneTexPosX"
                || proto.name == "WallStoneTexNegX"
                || proto.name == "WallStoneTexPosZ"
                || proto.name == "WallStoneTexNegZ"
                || proto.name.rfind("CeilingStoneTex", 0) == 0) return false;
            return true;
        }

        uint32_t grassColorForCell(int worldX, int worldZ) {
            const uint32_t h = hash2D(worldX - 911, worldZ + 613);
            const float tint = static_cast<float>(h & 0xffu) / 255.0f;
            glm::vec3 c(0.18f, 0.67f, 0.25f);
            c += glm::vec3(0.07f, 0.09f, 0.05f) * (tint - 0.5f);
            c = glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
            return packColor(c);
        }

        uint32_t flowerColorForCell(int worldX, int worldZ) {
            static const std::array<glm::vec3, 6> kFlowerPalette = {
                glm::vec3(0.97f, 0.48f, 0.72f), // pink
                glm::vec3(0.94f, 0.72f, 0.27f), // marigold
                glm::vec3(0.99f, 0.95f, 0.85f), // white
                glm::vec3(0.86f, 0.61f, 0.96f), // lilac
                glm::vec3(0.96f, 0.43f, 0.35f), // coral
                glm::vec3(0.95f, 0.83f, 0.28f)  // yellow
            };
            const uint32_t h = hash2D(worldX + 257, worldZ - 419);
            return packColor(kFlowerPalette[h % kFlowerPalette.size()]);
        }

        constexpr int kBlueFlowerRareChance = 20;
        constexpr int kYoungClematisExtraRarityMultiplier = 6;
        constexpr int kMiniPineRarityMultiplier = 20;
        constexpr int kJungleLeafVariantCount = 16;
        constexpr int kRareFlowerPoolMax = 24;

        int chooseRareFlowerPrototypeIDForSeed(const std::array<int, kRareFlowerPoolMax>& prototypeIDs,
                                               int prototypeCount,
                                               uint32_t seed) {
            if (prototypeCount <= 0) return -1;
            const int clampedCount = std::max(1, std::min(static_cast<int>(prototypeIDs.size()), prototypeCount));
            return prototypeIDs[static_cast<size_t>(seed % static_cast<uint32_t>(clampedCount))];
        }

        int chooseJungleLeafPrototypeIDForCell(const std::array<int, kJungleLeafVariantCount>& prototypeIDs,
                                               int prototypeCount,
                                               const glm::ivec3& worldCell) {
            if (prototypeCount <= 0) return -1;
            const int clampedCount = std::max(1, std::min(static_cast<int>(prototypeIDs.size()), prototypeCount));
            const uint32_t seed = hash3D(worldCell.x + 1901, worldCell.y - 733, worldCell.z + 421);
            return prototypeIDs[static_cast<size_t>(seed % static_cast<uint32_t>(clampedCount))];
        }

        uint32_t stickColorForCell(int worldX, int worldZ) {
            const uint32_t h = hash2D(worldX + 777, worldZ - 1337);
            const float tint = static_cast<float>(h & 0xffu) / 255.0f;
            glm::vec3 c(0.29f, 0.21f, 0.13f);
            c += glm::vec3(0.045f, 0.030f, 0.020f) * (tint - 0.5f);
            c = glm::clamp(c, glm::vec3(0.0f), glm::vec3(1.0f));
            return packColor(c);
        }

        bool isGrassSurfacePrototypeName(const std::string& name) {
            return name == "ScaffoldBlock"
                || startsWith(name, "GrassBlock");
        }

        bool isMeadowGrassSurfacePrototypeName(const std::string& name) {
            return name == "GrassBlockMeadowTex";
        }

        bool isForestFloorSurfacePrototypeName(const std::string& name) {
            return name == "GrassBlockTemperateTex";
        }

        bool isJungleGrassSurfacePrototypeName(const std::string& name) {
            return name == "GrassBlockJungleTex";
        }

        bool isBareWinterSurfacePrototypeName(const std::string& name) {
            return name == "GrassBlockBareWinterTex";
        }

        bool isGrassGrowthBlockedSurfacePrototypeName(const std::string& name) {
            return name == "ChalkBlockTex"
                || name == "ClayBlockTex"
                || name == "GraniteBlockTex";
        }

        bool isDesertGroundPrototypeName(const std::string& name) {
            return name == "SandBlockTex"
                || name == "SandBlockDesertTex"
                || name == "SandBlockBeachTex"
                || name == "SandBlockSeabedTex"
                || name == "ScaffoldBlock";
        }

        bool isStoneSurfacePrototypeName(const std::string& name) {
            return name == "StoneBlockTex" || name == "ScaffoldBlock";
        }

        bool isWallStonePrototypeName(const std::string& name) {
            return name == "WallStoneTexPosX"
                || name == "WallStoneTexNegX"
                || name == "WallStoneTexPosZ"
                || name == "WallStoneTexNegZ";
        }

        bool hasLeafCanopyNear(const VoxelWorldContext& voxelWorld,
                               int sectionTier,
                               int leafPrototypeID,
                               int tierX,
                               int groundTierY,
                               int tierZ,
                               int radius,
                               int searchHeight) {
            if (!voxelWorld.enabled || leafPrototypeID < 0) return false;
            const int clampedRadius = std::max(0, std::min(4, radius));
            const int clampedHeight = std::max(2, std::min(96, searchHeight));
            const int minY = groundTierY + 2;
            const int maxY = groundTierY + clampedHeight;
            for (int dz = -clampedRadius; dz <= clampedRadius; ++dz) {
                for (int dx = -clampedRadius; dx <= clampedRadius; ++dx) {
                    for (int y = minY; y <= maxY; ++y) {
                        const glm::ivec3 probe(tierX + dx, y, tierZ + dz);
                        if (getBlockAt(voxelWorld, probe) == static_cast<uint32_t>(leafPrototypeID)) {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        bool findLeafHangingCellNear(const std::vector<Entity>& prototypes,
                                     const VoxelWorldContext& voxelWorld,
                                     int sectionTier,
                                     const glm::ivec3& sectionCoord,
                                     int sectionSize,
                                     int tierX,
                                     int groundTierY,
                                     int tierZ,
                                     int radius,
                                     int searchHeight,
                                     glm::ivec3& outCell) {
            if (!voxelWorld.enabled) return false;
            const int clampedRadius = std::max(0, std::min(4, radius));
            const int clampedHeight = std::max(2, std::min(96, searchHeight));
            const int minY = groundTierY + 2;
            const int maxY = groundTierY + clampedHeight;

            bool found = false;
            int bestY = std::numeric_limits<int>::max();
            int bestDist2 = std::numeric_limits<int>::max();
            glm::ivec3 bestCell(0);

            for (int dz = -clampedRadius; dz <= clampedRadius; ++dz) {
                for (int dx = -clampedRadius; dx <= clampedRadius; ++dx) {
                    const int dist2 = dx * dx + dz * dz;
                    for (int y = minY; y <= maxY; ++y) {
                        const glm::ivec3 leafCell(tierX + dx, y, tierZ + dz);
                        const uint32_t leafID = getBlockAt(voxelWorld, leafCell);
                        if (leafID == 0u || leafID >= prototypes.size()) continue;
                        if (!isLeafPrototypeName(prototypes[static_cast<size_t>(leafID)].name)) continue;

                        const glm::ivec3 hangingCell = leafCell + glm::ivec3(0, -1, 0);
                        if (!cellBelongsToSection(hangingCell, sectionCoord, sectionSize)) continue;
                        if (getBlockAt(voxelWorld, hangingCell) != 0u) continue;

                        if (!found || hangingCell.y < bestY || (hangingCell.y == bestY && dist2 < bestDist2)) {
                            found = true;
                            bestY = hangingCell.y;
                            bestDist2 = dist2;
                            bestCell = hangingCell;
                        }
                    }
                }
            }

            if (!found) return false;
            outCell = bestCell;
            return true;
        }

        bool shouldSpawnPine(int worldX, int worldZ, const PineSpec& spec) {
            return (hash2D(worldX, worldZ) % static_cast<uint32_t>(spec.spawnModulo)) == 0u;
        }

        PineSpec pineSpecForCell(const PineSpec& baseSpec,
                                 int worldX,
                                 int worldZ,
                                 int minTrunkHeight,
                                 int maxTrunkHeight) {
            PineSpec out = baseSpec;

            int minH = std::max(6, minTrunkHeight);
            int maxH = std::max(minH, maxTrunkHeight);
            if (maxH < minH) std::swap(minH, maxH);

            const uint32_t h = hash2D(worldX + 9091, worldZ - 7919);
            const int span = maxH - minH + 1;
            out.trunkHeight = minH + static_cast<int>(h % static_cast<uint32_t>(span));

            // Preserve the original canopy base/top relationship relative to trunk height.
            const int baseCanopyBase = baseSpec.trunkHeight - baseSpec.canopyOffset;
            const int baseTopOverhang = baseSpec.canopyLayers - baseSpec.canopyOffset - 1;
            const int canopyBase = std::max(2, baseCanopyBase);
            const int canopyTop = std::max(canopyBase, out.trunkHeight + baseTopOverhang);
            out.canopyOffset = std::max(1, out.trunkHeight - canopyBase);
            out.canopyLayers = std::max(1, canopyTop - canopyBase + 1);

            // Short trees: reduce peak canopy thickness by 1 block.
            if (out.trunkHeight < 20) {
                out.canopyBottomRadius = std::max(out.canopyTopRadius, out.canopyBottomRadius - 1.0f);
            }
            return out;
        }

        PineSpec scaledPineSpecForTier(const PineSpec& source, int sectionScale) {
            if (sectionScale <= 1) return source;
            PineSpec out = source;
            auto scaleIntCeil = [&](int v, int minValue) {
                if (v <= 0) return minValue;
                return std::max(minValue, (v + sectionScale - 1) / sectionScale);
            };
            out.trunkHeight = scaleIntCeil(source.trunkHeight, 2);
            out.canopyOffset = scaleIntCeil(source.canopyOffset, 1);
            out.canopyLayers = scaleIntCeil(source.canopyLayers, 1);
            out.canopyLowerExtension = scaleIntCeil(source.canopyLowerExtension, 0);
            out.trunkExclusionRadius = scaleIntCeil(source.trunkExclusionRadius, 1);
            out.canopyBottomRadius = std::max(1.0f / static_cast<float>(sectionScale), source.canopyBottomRadius / static_cast<float>(sectionScale));
            out.canopyTopRadius = std::max(1.0f / static_cast<float>(sectionScale), source.canopyTopRadius / static_cast<float>(sectionScale));
            out.canopyLowerRadiusBoost = source.canopyLowerRadiusBoost / static_cast<float>(sectionScale);
            if (out.canopyOffset >= out.trunkHeight) {
                out.canopyOffset = std::max(1, out.trunkHeight - 1);
            }
            return out;
        }

        float pineCanopyLayerRadius(const PineSpec& spec, int layer) {
            if (layer < 0) {
                const float depthT = (spec.canopyLowerExtension > 0)
                    ? static_cast<float>(-layer) / static_cast<float>(spec.canopyLowerExtension)
                    : 0.0f;
                return spec.canopyBottomRadius + spec.canopyLowerRadiusBoost * depthT;
            }
            const float t = (spec.canopyLayers > 1)
                ? static_cast<float>(layer) / static_cast<float>(spec.canopyLayers - 1)
                : 0.0f;
            return spec.canopyBottomRadius + t * (spec.canopyTopRadius - spec.canopyBottomRadius);
        }

        int pineNubMaxTrunkOffsetY(const PineSpec& spec) {
            const int trunkSafeTop = std::max(3, spec.trunkHeight - 2);
            const int canopyBase = spec.trunkHeight - spec.canopyOffset;
            constexpr float kMinCanopyCoverRadius = 2.0f;
            int canopyCoveredTop = std::max(3, canopyBase);
            bool foundCoveredLayer = false;
            for (int layer = -spec.canopyLowerExtension; layer < spec.canopyLayers; ++layer) {
                const float radius = pineCanopyLayerRadius(spec, layer);
                if (radius + 1e-4f < kMinCanopyCoverRadius) continue;
                const int y = canopyBase + layer;
                if (y < 3) continue;
                canopyCoveredTop = std::max(canopyCoveredTop, y);
                foundCoveredLayer = true;
            }
            if (!foundCoveredLayer) {
                canopyCoveredTop = std::max(3, canopyBase + std::max(0, spec.canopyLayers / 2));
            }
            return std::max(3, std::min(trunkSafeTop, canopyCoveredTop));
        }

        #include "SurfaceFoliageSystem.cpp"
        #include "WaterFoliageSystem.cpp"


        #include "TreeCanopyGenerationSystem.cpp"

        #include "TreeFallSystem.cpp"

        VoxelSectionKey sectionKeyForWorldCell(const glm::ivec3& worldCell, int sectionSize) {
            return VoxelSectionKey{
                glm::ivec3(
                    floorDivInt(worldCell.x, sectionSize),
                    floorDivInt(worldCell.y, sectionSize),
                    floorDivInt(worldCell.z, sectionSize)
                )
            };
        }

        void markStructureSection(std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash>& outSections,
                                  const glm::ivec3& worldCell,
                                  int sectionSize) {
            outSections.insert(sectionKeyForWorldCell(worldCell, sectionSize));
        }

        bool areStructureSectionsTerrainReady(const std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash>& sections) {
            for (const auto& key : sections) {
                if (!TerrainSystemLogic::IsSectionTerrainReady(key)) {
                    return false;
                }
            }
            return true;
        }

        bool arePineTreeSectionsReady(int sectionSize,
                                      int worldX,
                                      int groundY,
                                      int worldZ,
                                      const PineSpec& spec) {
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> requiredSections;
            for (int i = 1; i <= spec.trunkHeight; ++i) {
                markStructureSection(requiredSections, glm::ivec3(worldX, groundY + i, worldZ), sectionSize);
            }

            std::vector<PineNubPlacement> nubs;
            collectPineNubPlacements(worldX, worldZ, spec, nubs);
            for (const auto& nub : nubs) {
                const int trunkY = groundY + nub.trunkOffsetY;
                if (trunkY <= groundY + 1 || trunkY >= groundY + spec.trunkHeight) continue;
                markStructureSection(requiredSections, glm::ivec3(worldX + nub.dir.x, trunkY, worldZ + nub.dir.y), sectionSize);
            }

            const auto& canopy = pineCanopyOffsets(spec);
            for (const auto& off : canopy) {
                const glm::ivec3 cell(worldX + off.x, groundY + off.y, worldZ + off.z);
                if (cell.y <= groundY + 1) continue;
                markStructureSection(requiredSections, cell, sectionSize);
                markStructureSection(requiredSections, cell + glm::ivec3(0, -1, 0), sectionSize);
            }

            return areStructureSectionsTerrainReady(requiredSections);
        }

        bool areBareTreeSectionsReady(int sectionSize,
                                      int worldX,
                                      int groundY,
                                      int worldZ,
                                      int trunkHeight,
                                      uint32_t seed) {
            if (trunkHeight < 2) return true;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> requiredSections;
            for (int i = 1; i <= trunkHeight; ++i) {
                markStructureSection(requiredSections, glm::ivec3(worldX, groundY + i, worldZ), sectionSize);
            }

            const std::array<glm::ivec2, 4> dirs = {
                glm::ivec2(1, 0),
                glm::ivec2(-1, 0),
                glm::ivec2(0, 1),
                glm::ivec2(0, -1)
            };
            const int branchHeightMin = std::max(2, trunkHeight / 3);
            const int branchHeightMax = std::max(branchHeightMin, trunkHeight - 2);
            const int branchCount = 1 + static_cast<int>((seed >> 3u) & 1u);
            std::array<bool, 4> usedDirs = {false, false, false, false};
            for (int i = 0; i < branchCount; ++i) {
                const uint32_t branchSeed = hash3D(worldX + 911 * (i + 1), groundY + 67 * (i + 1), worldZ - 503 * (i + 1));
                const int branchHeight = branchHeightMin + static_cast<int>(
                    branchSeed % static_cast<uint32_t>(branchHeightMax - branchHeightMin + 1)
                );
                int dirIdx = static_cast<int>((branchSeed >> 9u) % dirs.size());
                for (int k = 0; k < 4; ++k) {
                    const int candidate = (dirIdx + k) % 4;
                    if (!usedDirs[static_cast<size_t>(candidate)]) {
                        dirIdx = candidate;
                        break;
                    }
                }
                usedDirs[static_cast<size_t>(dirIdx)] = true;
                const glm::ivec2 dir = dirs[static_cast<size_t>(dirIdx)];
                markStructureSection(requiredSections, glm::ivec3(worldX + dir.x, groundY + branchHeight, worldZ + dir.y), sectionSize);
                if (((branchSeed >> 17u) % 100u) < 60u) {
                    markStructureSection(requiredSections, glm::ivec3(worldX + dir.x, groundY + branchHeight + 1, worldZ + dir.y), sectionSize);
                    markStructureSection(requiredSections, glm::ivec3(worldX + dir.x * 2, groundY + branchHeight + 2, worldZ + dir.y * 2), sectionSize);
                }
            }

            return areStructureSectionsTerrainReady(requiredSections);
        }

        bool areJungleTreeSectionsReady(int sectionSize,
                                        int worldX,
                                        int groundY,
                                        int worldZ,
                                        int trunkHeight,
                                        int canopyRadius) {
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> requiredSections;
            for (int i = 1; i <= trunkHeight; ++i) {
                markStructureSection(requiredSections, glm::ivec3(worldX, groundY + i, worldZ), sectionSize);
            }

            const int centerY = groundY + trunkHeight + 2;
            const int r = std::max(2, canopyRadius);
            for (int dy = -r; dy <= r; ++dy) {
                for (int dx = -r; dx <= r; ++dx) {
                    for (int dz = -r; dz <= r; ++dz) {
                        const float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy + dz * dz));
                        if (dist > static_cast<float>(r)) continue;
                        const glm::ivec3 leafCell(worldX + dx, centerY + dy, worldZ + dz);
                        if (leafCell.y <= groundY + 1) continue;
                        markStructureSection(requiredSections, leafCell, sectionSize);
                    }
                }
            }

            return areStructureSectionsTerrainReady(requiredSections);
        }

        int pineTreeHorizontalReach(const PineSpec& spec) {
            float maxRadius = std::max(spec.canopyBottomRadius, spec.canopyTopRadius);
            if (spec.canopyLowerExtension > 0) {
                maxRadius = std::max(maxRadius, spec.canopyBottomRadius + spec.canopyLowerRadiusBoost);
            }
            return std::max(1, static_cast<int>(std::ceil(std::max(1.0f, maxRadius))));
        }

        bool sectionRangeIntersectsY(int sectionMinY,
                                     int sectionMaxY,
                                     int structureMinY,
                                     int structureMaxY) {
            return structureMaxY >= sectionMinY && structureMinY <= sectionMaxY;
        }
    }

    void GetTreeFoliagePerfStats(size_t& pendingSections,
                                 size_t& pendingDependencies,
                                 size_t& backfillVisited,
                                 int& selectedSections,
                                 int& processedSections,
                                 int& deferredByTimeBudget,
                                 int& backfillAppended,
                                 int& scannedColumns,
                                 int& candidateColumns,
                                 int& placedTrees,
                                 int& skippedOutOfSection,
                                 int& skippedNonLand,
                                 int& skippedMissingGround,
                                 int& blockedByDependencies,
                                 int& blockedByColumn,
                                 int& blockedBySpacing,
                                 bool& backfillRan,
                                 float& updateMs) {
        pendingSections = g_treePerfStats.pendingSections;
        pendingDependencies = g_treePerfStats.pendingDependencies;
        backfillVisited = g_treePerfStats.backfillVisited;
        selectedSections = g_treePerfStats.selectedSections;
        processedSections = g_treePerfStats.processedSections;
        deferredByTimeBudget = g_treePerfStats.deferredByTimeBudget;
        backfillAppended = g_treePerfStats.backfillAppended;
        scannedColumns = g_treePerfStats.scannedColumns;
        candidateColumns = g_treePerfStats.candidateColumns;
        placedTrees = g_treePerfStats.placedTrees;
        skippedOutOfSection = g_treePerfStats.skippedOutOfSection;
        skippedNonLand = g_treePerfStats.skippedNonLand;
        skippedMissingGround = g_treePerfStats.skippedMissingGround;
        blockedByDependencies = g_treePerfStats.blockedByDependencies;
        blockedByColumn = g_treePerfStats.blockedByColumn;
        blockedBySpacing = g_treePerfStats.blockedBySpacing;
        backfillRan = g_treePerfStats.backfillRan;
        updateMs = g_treePerfStats.updateMs;
    }

    void NotifyPineLogRemoved(const glm::ivec3& worldCell, int removedPrototypeID) {
        g_pendingPineLogRemovals.emplace_back(worldCell, removedPrototypeID);
    }

    void UpdateExpanseTrees(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win);

    void RequestImmediateSectionFoliage(const VoxelSectionKey& key) {
        if (g_treeImmediateQueued.insert(key).second) {
            g_treeImmediateQueue.push_back(key);
        }
    }

    void SetForceCompleteSectionFoliageTargets(const std::vector<VoxelSectionKey>& keys) {
        g_treeForceCompleteSections.clear();
        for (const VoxelSectionKey& key : keys) {
            g_treeForceCompleteSections.insert(key);
        }
    }

    bool IsSectionSurfaceFoliageReady(const BaseSystem& baseSystem, const VoxelSectionKey& key) {
        if (!baseSystem.voxelWorld) return false;
        const auto sectionIt = baseSystem.voxelWorld->sections.find(key);
        if (sectionIt == baseSystem.voxelWorld->sections.end()) return false;
        const VoxelChunkLifecycleState* state = baseSystem.voxelWorld->findChunkState(key);
        if (state && state->surfaceFoliageComplete) {
            return true;
        }
        if (g_treePendingSections.count(key) > 0) return false;
        if (g_treePendingDependencies.count(key) > 0) return false;
        if (g_treeSectionProgress.count(key) > 0) return false;

        auto surfaceIt = g_treeSurfaceAppliedVersion.find(key);
        if (surfaceIt != g_treeSurfaceAppliedVersion.end()
            && surfaceIt->second == sectionIt->second.editVersion) {
            return true;
        }

        auto appliedIt = g_treeAppliedVersion.find(key);
        if (appliedIt != g_treeAppliedVersion.end()
            && appliedIt->second == sectionIt->second.editVersion) {
            return true;
        }

        return false;
    }

    bool IsSectionFoliageReady(const BaseSystem& baseSystem, const VoxelSectionKey& key) {
        if (!baseSystem.voxelWorld) return false;
        const auto sectionIt = baseSystem.voxelWorld->sections.find(key);
        if (sectionIt == baseSystem.voxelWorld->sections.end()) return false;
        if (g_treePendingSections.count(key) > 0) return false;
        if (g_treePendingDependencies.count(key) > 0) return false;
        if (g_treeSectionProgress.count(key) > 0) return false;
        auto appliedIt = g_treeAppliedVersion.find(key);
        if (appliedIt == g_treeAppliedVersion.end()) return false;
        return appliedIt->second == sectionIt->second.editVersion;
    }

    bool ProcessSectionSurfaceFoliageNow(BaseSystem& baseSystem,
                                         std::vector<Entity>& prototypes,
                                         const VoxelSectionKey& key,
                                         int maxPasses) {
        if (maxPasses <= 0) maxPasses = 1;
        auto clearSurfaceOnlyPending = [&]() {
            g_treePendingSections.erase(key);
            g_treePendingDependencies.erase(key);
            g_treeSectionProgress.erase(key);
        };
        struct ForceCompleteGuard {
            VoxelSectionKey key;
            ForceCompleteGuard(const VoxelSectionKey& inKey) : key(inKey) {
                g_treeForceCompleteSections.insert(key);
            }
            ~ForceCompleteGuard() {
                g_treeForceCompleteSections.erase(key);
            }
        } forceCompleteGuard(key);

        RequestImmediateSectionFoliage(key);
        if (IsSectionSurfaceFoliageReady(baseSystem, key)) {
            clearSurfaceOnlyPending();
            return true;
        }

        for (int pass = 0; pass < maxPasses; ++pass) {
            UpdateExpanseTrees(baseSystem, prototypes, 0.0f, {});
            if (IsSectionSurfaceFoliageReady(baseSystem, key)) {
                clearSurfaceOnlyPending();
                return true;
            }
        }
        const bool ready = IsSectionSurfaceFoliageReady(baseSystem, key);
        if (ready) {
            clearSurfaceOnlyPending();
        }
        return ready;
    }

    void ProcessImmediateSectionFoliage(BaseSystem& baseSystem,
                                        std::vector<Entity>& prototypes,
                                        int maxPasses) {
        if (maxPasses <= 0) return;
        for (int pass = 0; pass < maxPasses; ++pass) {
            if (g_treeImmediateQueue.empty()) break;
            UpdateExpanseTrees(baseSystem, prototypes, 0.0f, {});
        }
    }

    bool GenerateColumnTerrainFoliage(BaseSystem& baseSystem,
                                      std::vector<Entity>& prototypes,
                                      WorldContext& worldCtx,
                                      VoxelWorldContext& voxelWorld,
                                      const VoxelColumnKey& columnKey) {
        if (!baseSystem.registry || !worldCtx.expanse.loaded || !voxelWorld.enabled) return false;
        const auto columnFoliageStart = std::chrono::steady_clock::now();

        const int sectionSize = std::max(1, voxelWorld.sectionSize);
        const int sectionTier = 0;
        const int sectionScale = 1;
        const glm::ivec3 rootSectionCoord(
            columnKey.coord.x,
            floorDivInt(voxelWorld.columnMinY, sectionSize),
            columnKey.coord.y
        );
        const int minX = columnKey.coord.x * sectionSize;
        const int minZ = columnKey.coord.y * sectionSize;
        const int maxX = minX + sectionSize - 1;
        const int maxZ = minZ + sectionSize - 1;

        struct ColumnPrototypeNameCache {
            const Entity* data = nullptr;
            size_t size = 0;
            std::unordered_map<std::string, int> idsByName;
        };
        static ColumnPrototypeNameCache prototypeNameCache;
        if (prototypeNameCache.data != prototypes.data()
            || prototypeNameCache.size != prototypes.size()) {
            prototypeNameCache.data = prototypes.data();
            prototypeNameCache.size = prototypes.size();
            prototypeNameCache.idsByName.clear();
            prototypeNameCache.idsByName.reserve(prototypes.size());
            for (const Entity& proto : prototypes) {
                prototypeNameCache.idsByName[proto.name] = proto.prototypeID;
            }
        }
        auto findColumnPrototype = [&](const char* name) -> const Entity* {
            if (!name) return nullptr;
            auto it = prototypeNameCache.idsByName.find(name);
            if (it == prototypeNameCache.idsByName.end()) return nullptr;
            const int id = it->second;
            if (id < 0 || id >= static_cast<int>(prototypes.size())) return nullptr;
            return &prototypes[static_cast<size_t>(id)];
        };

        const Entity* trunkProtoA = findColumnPrototype("FirLog1Tex");
        const Entity* trunkProtoB = findColumnPrototype("FirLog2Tex");
        if (!trunkProtoA) trunkProtoA = findColumnPrototype("Branch");
        if (!trunkProtoA) trunkProtoA = findColumnPrototype("Block");
        if (!trunkProtoB) trunkProtoB = trunkProtoA;
        const Entity* leafProto = findColumnPrototype("Leaf");
        const Entity* leafFanOakProto = findColumnPrototype("GrassTuftLeafFanOak");
        const Entity* leafFanPineProto = findColumnPrototype("GrassTuftLeafFanPine");
        const Entity* jungleTrunkProto = findColumnPrototype("OakLogTex");
        const Entity* bareTrunkProto = findColumnPrototype("BareLogTex");
        const Entity* wallBranchLongProtoPosX = findColumnPrototype("WallBranchLongTexPosX");
        const Entity* wallBranchLongProtoNegX = findColumnPrototype("WallBranchLongTexNegX");
        const Entity* wallBranchLongProtoPosZ = findColumnPrototype("WallBranchLongTexPosZ");
        const Entity* wallBranchLongProtoNegZ = findColumnPrototype("WallBranchLongTexNegZ");
        const Entity* wallBranchLongTipProtoPosX = findColumnPrototype("WallBranchLongTipTexPosX");
        const Entity* wallBranchLongTipProtoNegX = findColumnPrototype("WallBranchLongTipTexNegX");
        const Entity* wallBranchLongTipProtoPosZ = findColumnPrototype("WallBranchLongTipTexPosZ");
        const Entity* wallBranchLongTipProtoNegZ = findColumnPrototype("WallBranchLongTipTexNegZ");
        const Entity* pineBeeHiveProto = findColumnPrototype("FlowerBeeHivePineV001");

        const Entity* grassProto = findColumnPrototype("GrassTuft");
        const Entity* shortGrassProto = findColumnPrototype("GrassTuftShort");
        const Entity* grassProtoBiome1 = findColumnPrototype("GrassTuftMeadow");
        const Entity* shortGrassProtoBiome1 = findColumnPrototype("GrassTuftShortMeadow");
        const Entity* grassProtoBiome3 = findColumnPrototype("GrassTuftJungle");
        const Entity* flowerProto = findColumnPrototype("Flower");
        const Entity* grassCoverProtoX = findColumnPrototype("GrassCoverTexX");
        const Entity* grassCoverProtoZ = findColumnPrototype("GrassCoverTexZ");
        const Entity* grassCoverMeadowProtoX = findColumnPrototype("GrassCoverMeadowTexX");
        const Entity* grassCoverMeadowProtoZ = findColumnPrototype("GrassCoverMeadowTexZ");
        const Entity* grassCoverJungleProtoX = findColumnPrototype("GrassCoverJungleTexX");
        const Entity* grassCoverJungleProtoZ = findColumnPrototype("GrassCoverJungleTexZ");
        const Entity* grassCoverBareProtoX = findColumnPrototype("GrassCoverBareTexX");
        const Entity* grassCoverBareProtoZ = findColumnPrototype("GrassCoverBareTexZ");
        const Entity* grassCoverDesertProtoX = findColumnPrototype("GrassCoverDesertTexX");
        const Entity* grassCoverDesertProtoZ = findColumnPrototype("GrassCoverDesertTexZ");
        const Entity* waterProto = findColumnPrototype("Water");
        const int waterPrototypeID = waterProto ? waterProto->prototypeID : -1;

        const Entity* cactusProtoA = findColumnPrototype("Cactus1Tex");
        const Entity* cactusProtoB = findColumnPrototype("Cactus2Tex");
        const Entity* cactusProtoAX = findColumnPrototype("Cactus1TexX");
        const Entity* cactusProtoAZ = findColumnPrototype("Cactus1TexZ");
        const Entity* cactusProtoBX = findColumnPrototype("Cactus2TexX");
        const Entity* cactusProtoBZ = findColumnPrototype("Cactus2TexZ");
        const Entity* cactusProtoAJunctionX = findColumnPrototype("Cactus1TexJunctionX");
        const Entity* cactusProtoAJunctionZ = findColumnPrototype("Cactus1TexJunctionZ");
        const Entity* cactusProtoBJunctionX = findColumnPrototype("Cactus2TexJunctionX");
        const Entity* cactusProtoBJunctionZ = findColumnPrototype("Cactus2TexJunctionZ");

        std::array<int, kJungleLeafVariantCount> jungleLeafPrototypeIDs{};
        int jungleLeafPrototypeCount = 0;
        {
            const std::array<const char*, kJungleLeafVariantCount> kJungleLeafNames = {
                "LeafJungleV001", "LeafJungleV002", "LeafJungleV003", "LeafJungleV004",
                "LeafJungleV005", "LeafJungleV006", "LeafJungleV007", "LeafJungleV008",
                "LeafJungleV009", "LeafJungleV010", "LeafJungleV011", "LeafJungleV012",
                "LeafJungleV013", "LeafJungleV014", "LeafJungleV015", "LeafJungleV016"
            };
            for (const char* name : kJungleLeafNames) {
                if (const Entity* proto = findColumnPrototype(name)) {
                    jungleLeafPrototypeIDs[static_cast<size_t>(jungleLeafPrototypeCount)] = proto->prototypeID;
                    jungleLeafPrototypeCount += 1;
                }
            }
        }

        FoliageSpec foliageSpec;
        foliageSpec.enabled = getRegistryBool(baseSystem, "FoliageGenerationEnabled", true);
        foliageSpec.grassEnabled = getRegistryBool(baseSystem, "GrassGenerationEnabled", true);
        foliageSpec.grassCoverEnabled = getRegistryBool(baseSystem, "GrassCoverGenerationEnabled", true);
        foliageSpec.flowerEnabled = getRegistryBool(baseSystem, "FlowerGenerationEnabled", true);
        foliageSpec.temperateOnly = getRegistryBool(baseSystem, "FoliageTemperateOnly", true);
        foliageSpec.grassSpawnModulo = std::max(1, getRegistryInt(baseSystem, "GrassSpawnModulo", foliageSpec.grassSpawnModulo));
        foliageSpec.grassCoverPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "GrassCoverPercent", foliageSpec.grassCoverPercent)));
        foliageSpec.flowerSpawnModulo = std::max(1, getRegistryInt(baseSystem, "FlowerSpawnModulo", foliageSpec.flowerSpawnModulo));
        foliageSpec.shortGrassPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "ShortGrassPercent", foliageSpec.shortGrassPercent)));
        foliageSpec.grassTuftPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "GrassTuftPercent", foliageSpec.grassTuftPercent)));

        PineSpec baseSpec;
        const int pineCanopyBaseHeight = std::max(2, getRegistryInt(baseSystem, "PineCanopyBaseHeight", 10));
        baseSpec.canopyOffset = std::max(1, baseSpec.trunkHeight - pineCanopyBaseHeight);
        const int pineMinTrunkHeight = std::max(6, getRegistryInt(baseSystem, "PineTrunkHeightMin", 15));
        const int pineMaxTrunkHeight = std::max(
            pineMinTrunkHeight,
            getRegistryInt(baseSystem, "PineTrunkHeightMax", baseSpec.trunkHeight)
        );
        const int pineBeeHiveSpawnModulo = std::max(1, getRegistryInt(baseSystem, "PineBeeHiveSpawnModulo", 18));
        const int jungleTreeSpawnModulo = std::max(1, getRegistryInt(baseSystem, "JungleTreeSpawnModulo", 1000));
        const bool meadowTreeGenerationEnabled = getRegistryBool(baseSystem, "MeadowTreeGenerationEnabled", true);
        const int meadowTreeSpawnModulo = std::max(1, getRegistryInt(baseSystem, "MeadowTreeSpawnModulo", 140));
        const int jungleTreeTrunkMin = std::max(4, getRegistryInt(baseSystem, "JungleTreeTrunkHeightMin", 6));
        const int jungleTreeTrunkMax = std::max(jungleTreeTrunkMin, getRegistryInt(baseSystem, "JungleTreeTrunkHeightMax", 9));
        const int jungleTreeCanopyRadius = std::max(2, std::min(8, getRegistryInt(baseSystem, "JungleTreeCanopyRadius", 4)));
        const int bareTreeSpawnModulo = std::max(1, getRegistryInt(baseSystem, "BareTreeSpawnModulo", 380));
        const int bareTreeTrunkMin = std::max(4, getRegistryInt(baseSystem, "BareTreeTrunkHeightMin", 7));
        const int bareTreeTrunkMax = std::max(bareTreeTrunkMin, getRegistryInt(baseSystem, "BareTreeTrunkHeightMax", 12));
        const int desertCactusSpawnModulo = std::max(1, getRegistryInt(baseSystem, "DesertCactusSpawnModulo", 128));
        const bool desertCactusEnabled = getRegistryBool(baseSystem, "DesertCactusGenerationEnabled", true);
        const bool treeGenerationEnabled = getRegistryBool(baseSystem, "TreeGenerationEnabled", true);

        const uint32_t trunkColor = packColor(glm::vec3(0.29f, 0.21f, 0.13f));
        const uint32_t leafColor = packColor(glm::vec3(0.07f, 0.46f, 0.34f));
        bool modified = false;
        bool unresolvedDependencies = false;
        std::unordered_set<glm::ivec3, IVec3Hash> touchedSections;

        const bool previousColumnWriteActive = voxelWorld.columnFeatureWritesActive;
        const VoxelColumnKey previousColumnWriteOwner = voxelWorld.columnFeatureOwner;
        voxelWorld.beginColumnFeatureWrites(columnKey);

        int scannedColumns = 0;
        int candidateColumns = 0;
        int placedTrees = 0;
        int skippedNonLand = 0;
        int skippedMissingGround = 0;
        int blockedByColumn = 0;
        int blockedBySpacing = 0;

        auto placeColumnGroundFoliage = [&](int worldX, int groundY, int worldZ, int biomeID, uint32_t seed) {
            if (!foliageSpec.enabled) return;
            const glm::ivec3 groundCell(worldX, groundY, worldZ);
            const glm::ivec3 placeCell(worldX, groundY + 1, worldZ);
            if (getBlockAt(voxelWorld, placeCell) != 0u) return;
            const uint32_t groundID = getBlockAt(voxelWorld, groundCell);
            if (!isFoliageGroundPrototypeID(prototypes, groundID, waterPrototypeID)) return;

            bool spawnFlower = foliageSpec.flowerEnabled
                && flowerProto
                && ((seed % static_cast<uint32_t>(foliageSpec.flowerSpawnModulo)) == 0u);
            if (foliageSpec.temperateOnly && biomeID != 0 && biomeID != 1) {
                spawnFlower = false;
            }
            if (spawnFlower) {
                if (voxelWorld.setBlockIfEmpty(
                        placeCell,
                        static_cast<uint32_t>(flowerProto->prototypeID),
                        flowerColorForCell(worldX, worldZ),
                        false)) {
                    modified = true;
                }
                return;
            }

            if (!foliageSpec.grassEnabled) return;
            if (((seed >> 1u) % static_cast<uint32_t>(foliageSpec.grassSpawnModulo)) != 0u) return;
            const int clampedGrassTuftPercent = std::max(0, std::min(100, foliageSpec.grassTuftPercent));
            if (clampedGrassTuftPercent <= 0) return;
            if (clampedGrassTuftPercent < 100) {
                const uint32_t grassTuftSeed = hash3D(worldX + 1847, groundY + 97, worldZ - 563);
                if (static_cast<int>(grassTuftSeed % 100u) >= clampedGrassTuftPercent) return;
            }

            int coverX = grassCoverProtoX ? grassCoverProtoX->prototypeID : -1;
            int coverZ = grassCoverProtoZ ? grassCoverProtoZ->prototypeID : -1;
            int tallGrassID = grassProto ? grassProto->prototypeID : -1;
            int shortGrassID = shortGrassProto ? shortGrassProto->prototypeID : -1;
            if (biomeID == 1) {
                if (grassProtoBiome1) tallGrassID = grassProtoBiome1->prototypeID;
                if (shortGrassProtoBiome1) shortGrassID = shortGrassProtoBiome1->prototypeID;
                if (grassCoverMeadowProtoX) coverX = grassCoverMeadowProtoX->prototypeID;
                if (grassCoverMeadowProtoZ) coverZ = grassCoverMeadowProtoZ->prototypeID;
            } else if (biomeID == 3) {
                if (grassProtoBiome3) tallGrassID = grassProtoBiome3->prototypeID;
                shortGrassID = -1;
                if (grassCoverJungleProtoX) coverX = grassCoverJungleProtoX->prototypeID;
                if (grassCoverJungleProtoZ) coverZ = grassCoverJungleProtoZ->prototypeID;
            } else if (biomeID == 4) {
                shortGrassID = -1;
                if (grassProtoBiome3) tallGrassID = grassProtoBiome3->prototypeID;
                if (grassCoverBareProtoX) coverX = grassCoverBareProtoX->prototypeID;
                if (grassCoverBareProtoZ) coverZ = grassCoverBareProtoZ->prototypeID;
            } else if (biomeID == 2) {
                tallGrassID = -1;
                shortGrassID = -1;
                if (grassCoverDesertProtoX) coverX = grassCoverDesertProtoX->prototypeID;
                if (grassCoverDesertProtoZ) coverZ = grassCoverDesertProtoZ->prototypeID;
            }

            if (foliageSpec.grassCoverEnabled && biomeID != 2 && (coverX >= 0 || coverZ >= 0)) {
                const uint32_t coverSeed = hash3D(worldX + 913, groundY + 37, worldZ - 211);
                if (static_cast<int>(coverSeed % 100u) < foliageSpec.grassCoverPercent) {
                    int coverID = ((seed >> 13u) & 1u) == 0u ? coverX : coverZ;
                    if (coverID < 0) coverID = coverX >= 0 ? coverX : coverZ;
                    if (coverID >= 0) {
                        if (voxelWorld.setBlockIfEmpty(
                                placeCell,
                                static_cast<uint32_t>(coverID),
                                grassColorForCell(worldX, worldZ),
                                false)) {
                            modified = true;
                        }
                        return;
                    }
                }
            }

            int grassID = tallGrassID;
            if (shortGrassID >= 0) {
                const int shortPercent = std::max(0, std::min(100, foliageSpec.shortGrassPercent));
                if (grassID < 0 || static_cast<int>((seed >> 11u) % 100u) < shortPercent) {
                    grassID = shortGrassID;
                }
            }
            if (grassID < 0) return;
            if (voxelWorld.setBlockIfEmpty(
                    placeCell,
                    static_cast<uint32_t>(grassID),
                    grassColorForCell(worldX, worldZ),
                    false)) {
                modified = true;
            }
        };

        const bool islandQuadrants =
            (worldCtx.expanse.islandRadius > 0.0f) && worldCtx.expanse.secondaryBiomeEnabled;
        for (int worldZ = minZ; worldZ <= maxZ; ++worldZ) {
            for (int worldX = minX; worldX <= maxX; ++worldX) {
                scannedColumns += 1;
                const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(
                    worldCtx,
                    static_cast<float>(worldX),
                    static_cast<float>(worldZ)
                );
                if (biomeID == 5 || isWithinJungleVolcano(worldCtx.expanse, biomeID, worldX, worldZ)) {
                    continue;
                }

                float terrainHeight = 0.0f;
                const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                    worldCtx,
                    static_cast<float>(worldX),
                    static_cast<float>(worldZ),
                    terrainHeight
                );
                if (!isLand) {
                    skippedNonLand += 1;
                    continue;
                }

                const int groundY = static_cast<int>(std::floor(terrainHeight));
                if (groundY < voxelWorld.columnMinY || groundY + 1 >= voxelWorld.columnMaxYExclusive) {
                    skippedMissingGround += 1;
                    continue;
                }
                const glm::ivec3 groundCell(worldX, groundY, worldZ);
                if (getBlockAt(voxelWorld, groundCell) == 0u) {
                    skippedMissingGround += 1;
                    continue;
                }

                const uint32_t seed = hash2D(worldX, worldZ);

                if (!treeGenerationEnabled || !trunkProtoA || !trunkProtoB || !leafProto) {
                    placeColumnGroundFoliage(worldX, groundY, worldZ, biomeID, seed);
                    continue;
                }
                if (biomeID == 2 || biomeID == 5) {
                    placeColumnGroundFoliage(worldX, groundY, worldZ, biomeID, seed);
                    continue;
                }

                const bool wantsPineTree = (biomeID == 0) && shouldSpawnPine(worldX, worldZ, baseSpec);
                const bool wantsMeadowTree =
                    meadowTreeGenerationEnabled
                    && (biomeID == 1)
                    && ((hash2D(worldX + 571, worldZ - 313) % static_cast<uint32_t>(meadowTreeSpawnModulo)) == 0u);
                const bool wantsJungleTree =
                    islandQuadrants && (biomeID == 0 || biomeID == 3)
                    && ((hash2D(worldX + 173, worldZ - 911) % static_cast<uint32_t>(jungleTreeSpawnModulo)) == 0u);
                const bool wantsBareTree =
                    (biomeID == 4)
                    && ((hash2D(worldX - 433, worldZ + 1259) % static_cast<uint32_t>(bareTreeSpawnModulo)) == 0u);
                if (!wantsPineTree && !wantsMeadowTree && !wantsJungleTree && !wantsBareTree) {
                    placeColumnGroundFoliage(worldX, groundY, worldZ, biomeID, seed);
                    continue;
                }
                candidateColumns += 1;

                if (wantsPineTree || wantsMeadowTree) {
                    PineSpec treeSpec = pineSpecForCell(
                        baseSpec,
                        worldX,
                        worldZ,
                        pineMinTrunkHeight,
                        pineMaxTrunkHeight
                    );
                    const int trunkPrototypeID = ((hash2D(worldX, worldZ) >> 8u) & 1u) == 0u
                        ? trunkProtoA->prototypeID
                        : trunkProtoB->prototypeID;
                    const int nubPrototypeID = (trunkPrototypeID == trunkProtoA->prototypeID)
                        ? trunkProtoB->prototypeID
                        : trunkProtoA->prototypeID;
                    const int topPrototypeID = topLogPrototypeFor(prototypes, nubPrototypeID);
                    if (!trunkColumnCanExist(
                            voxelWorld,
                            sectionTier,
                            rootSectionCoord,
                            sectionSize,
                            trunkProtoA->prototypeID,
                            trunkProtoB->prototypeID,
                            worldX,
                            groundY,
                            worldZ,
                            treeSpec.trunkHeight)) {
                        blockedByColumn += 1;
                        continue;
                    }
                    if (hasNearbyConflictingTrunk(
                            voxelWorld,
                            sectionTier,
                            trunkProtoA->prototypeID,
                            trunkProtoB->prototypeID,
                            worldX,
                            groundY + 1,
                            worldZ,
                            treeSpec.trunkExclusionRadius)) {
                        blockedBySpacing += 1;
                        continue;
                    }
                    writeTreeToWorld(
                        voxelWorld,
                        prototypes,
                        sectionTier,
                        sectionSize,
                        rootSectionCoord,
                        trunkPrototypeID,
                        topPrototypeID,
                        nubPrototypeID,
                        leafProto->prototypeID,
                        leafFanPineProto ? leafFanPineProto->prototypeID : -1,
                        (pineBeeHiveProto
                            && wantsPineTree
                            && ((hash2D(worldX - 6481, worldZ + 2207)
                                 % static_cast<uint32_t>(pineBeeHiveSpawnModulo)) == 0u))
                            ? pineBeeHiveProto->prototypeID
                            : -1,
                        trunkColor,
                        leafColor,
                        worldX,
                        groundY,
                        worldZ,
                        treeSpec,
                        touchedSections,
                        modified
                    );
                    placedTrees += 1;
                } else if (wantsBareTree) {
                    const int bareTrunkID = bareTrunkProto ? bareTrunkProto->prototypeID : trunkProtoA->prototypeID;
                    const uint32_t bareSeed = hash2D(worldX + 6097, worldZ - 2953);
                    const int bareHeightSpan = bareTreeTrunkMax - bareTreeTrunkMin + 1;
                    const int bareTrunkHeight = bareTreeTrunkMin + static_cast<int>(
                        bareSeed % static_cast<uint32_t>(bareHeightSpan)
                    );
                    if (!trunkColumnCanExist(
                            voxelWorld,
                            sectionTier,
                            rootSectionCoord,
                            sectionSize,
                            bareTrunkID,
                            bareTrunkID,
                            worldX,
                            groundY,
                            worldZ,
                            bareTrunkHeight)) {
                        blockedByColumn += 1;
                        continue;
                    }
                    if (hasNearbyConflictingTrunk(
                            voxelWorld,
                            sectionTier,
                            bareTrunkID,
                            bareTrunkID,
                            worldX,
                            groundY + 1,
                            worldZ,
                            2)) {
                        blockedBySpacing += 1;
                        continue;
                    }
                    writeBareTreeToWorld(
                        voxelWorld,
                        sectionTier,
                        sectionSize,
                        rootSectionCoord,
                        bareTrunkID,
                        -1,
                        -1,
                        wallBranchLongProtoPosX ? wallBranchLongProtoPosX->prototypeID : -1,
                        wallBranchLongProtoNegX ? wallBranchLongProtoNegX->prototypeID : -1,
                        wallBranchLongProtoPosZ ? wallBranchLongProtoPosZ->prototypeID : -1,
                        wallBranchLongProtoNegZ ? wallBranchLongProtoNegZ->prototypeID : -1,
                        wallBranchLongTipProtoPosX ? wallBranchLongTipProtoPosX->prototypeID : -1,
                        wallBranchLongTipProtoNegX ? wallBranchLongTipProtoNegX->prototypeID : -1,
                        wallBranchLongTipProtoPosZ ? wallBranchLongTipProtoPosZ->prototypeID : -1,
                        wallBranchLongTipProtoNegZ ? wallBranchLongTipProtoNegZ->prototypeID : -1,
                        packColor(glm::vec3(0.35f, 0.29f, 0.23f)),
                        packColor(glm::vec3(0.42f, 0.35f, 0.27f)),
                        worldX,
                        groundY,
                        worldZ,
                        bareTrunkHeight,
                        bareSeed,
                        touchedSections,
                        modified
                    );
                    placedTrees += 1;
                } else {
                    const int jungleTrunkID = jungleTrunkProto ? jungleTrunkProto->prototypeID : trunkProtoA->prototypeID;
                    const uint32_t jungleSeed = hash2D(worldX + 4041, worldZ - 7927);
                    const int jungleHeightSpan = jungleTreeTrunkMax - jungleTreeTrunkMin + 1;
                    const int jungleTrunkHeight = jungleTreeTrunkMin
                        + static_cast<int>(jungleSeed % static_cast<uint32_t>(jungleHeightSpan));
                    if (!trunkColumnCanExist(
                            voxelWorld,
                            sectionTier,
                            rootSectionCoord,
                            sectionSize,
                            jungleTrunkID,
                            jungleTrunkID,
                            worldX,
                            groundY,
                            worldZ,
                            jungleTrunkHeight)) {
                        blockedByColumn += 1;
                        continue;
                    }
                    if (hasNearbyConflictingTrunk(
                            voxelWorld,
                            sectionTier,
                            jungleTrunkID,
                            jungleTrunkID,
                            worldX,
                            groundY + 1,
                            worldZ,
                            2)) {
                        blockedBySpacing += 1;
                        continue;
                    }
                    writeJungleTreeToWorld(
                        voxelWorld,
                        sectionTier,
                        sectionSize,
                        rootSectionCoord,
                        jungleTrunkID,
                        leafProto->prototypeID,
                        leafFanOakProto ? leafFanOakProto->prototypeID : -1,
                        jungleLeafPrototypeIDs,
                        jungleLeafPrototypeCount,
                        packColor(glm::vec3(0.36f, 0.24f, 0.15f)),
                        leafColor,
                        worldX,
                        groundY,
                        worldZ,
                        jungleTrunkHeight,
                        jungleTreeCanopyRadius,
                        touchedSections,
                        modified
                    );
                    placedTrees += 1;
                }
            }
        }

        if (desertCactusEnabled) {
            writeDesertCactusToSection(
                prototypes,
                worldCtx,
                voxelWorld,
                sectionTier,
                rootSectionCoord,
                sectionSize,
                sectionScale,
                cactusProtoA ? cactusProtoA->prototypeID : -1,
                cactusProtoB ? cactusProtoB->prototypeID : -1,
                cactusProtoAX ? cactusProtoAX->prototypeID : -1,
                cactusProtoAZ ? cactusProtoAZ->prototypeID : -1,
                cactusProtoBX ? cactusProtoBX->prototypeID : -1,
                cactusProtoBZ ? cactusProtoBZ->prototypeID : -1,
                cactusProtoAJunctionX ? cactusProtoAJunctionX->prototypeID : -1,
                cactusProtoAJunctionZ ? cactusProtoAJunctionZ->prototypeID : -1,
                cactusProtoBJunctionX ? cactusProtoBJunctionX->prototypeID : -1,
                cactusProtoBJunctionZ ? cactusProtoBJunctionZ->prototypeID : -1,
                waterPrototypeID,
                desertCactusSpawnModulo,
                unresolvedDependencies,
                modified
            );
        }

        const size_t appliedPending = voxelWorld.applyPendingColumnWrites(columnKey, false);
        voxelWorld.columnFeatureWritesActive = previousColumnWriteActive;
        voxelWorld.columnFeatureOwner = previousColumnWriteOwner;

        g_treePerfStats.pendingSections = 0;
        g_treePerfStats.pendingDependencies = voxelWorld.pendingColumnWrites.size();
        g_treePerfStats.backfillVisited = 0;
        g_treePerfStats.selectedSections = 0;
        g_treePerfStats.processedSections = 0;
        g_treePerfStats.deferredByTimeBudget = 0;
        g_treePerfStats.backfillAppended = 0;
        g_treePerfStats.scannedColumns = scannedColumns;
        g_treePerfStats.candidateColumns = candidateColumns;
        g_treePerfStats.placedTrees = placedTrees;
        g_treePerfStats.skippedOutOfSection = 0;
        g_treePerfStats.skippedNonLand = skippedNonLand;
        g_treePerfStats.skippedMissingGround = skippedMissingGround;
        g_treePerfStats.blockedByDependencies = 0;
        g_treePerfStats.blockedByColumn = blockedByColumn;
        g_treePerfStats.blockedBySpacing = blockedBySpacing;
        g_treePerfStats.backfillRan = false;
        g_treePerfStats.updateMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - columnFoliageStart
        ).count();

        return modified || appliedPending > 0u;
    }

    void UpdateExpanseTrees(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt;
        (void)win;
        if (!baseSystem.world || !baseSystem.voxelWorld || !baseSystem.registry) return;
        if (!baseSystem.voxelWorld->enabled) return;

        WorldContext& worldCtx = *baseSystem.world;
        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
        if (!worldCtx.expanse.loaded) return;
        g_treeFrameCounter += 1;
        auto updateStart = std::chrono::steady_clock::now();

        auto isFoliageEligibleSection = [&](const VoxelSectionKey& key) -> bool {
            return voxelWorld.sections.find(key) != voxelWorld.sections.end();
        };
        auto updateSurfaceFoliageLifecycle = [&](const VoxelSectionKey& key, bool surfaceReady) {
            VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
            state.surfaceFoliageComplete = surfaceReady;
            if (!surfaceReady) {
                g_treeSurfaceAppliedVersion.erase(key);
            }
            if (!state.generated) return;
            state.stage = state.isFullyReady()
                ? VoxelChunkLifecycleStage::Ready
                : VoxelChunkLifecycleStage::BaseGenerated;
        };
        const bool terrainOwnsSectionWorldgen = getRegistryBool(
            baseSystem,
            "TerrainOwnsSectionWorldgen",
            true
        );

        std::string levelKey;
        auto levelIt = baseSystem.registry->find("level");
        if (levelIt != baseSystem.registry->end() && std::holds_alternative<std::string>(levelIt->second)) {
            levelKey = std::get<std::string>(levelIt->second);
        }
        if (g_treeLevelKey != levelKey) {
            g_treeLevelKey = levelKey;
            g_treeAppliedVersion.clear();
            g_treeSurfaceAppliedVersion.clear();
            g_treeBackfillVisited.clear();
            g_treePendingDependencies.clear();
            g_treePendingSections.clear();
            g_treeSectionProgress.clear();
            clearImmediateFoliageQueue();
            g_pendingPineLogRemovals.clear();
            g_treeFoliageSignature = 0;
        }
        if (levelKey == "the_depths") {
            return;
        }
        // Streaming unload/reload can recreate a section with the same key and reset editVersion.
        // Prune cache entries for currently unloaded keys so first-pass foliage/tree generation
        // cannot be incorrectly skipped when those sections stream back in.
        for (auto it = g_treeAppliedVersion.begin(); it != g_treeAppliedVersion.end(); ) {
            if (!isFoliageEligibleSection(it->first)) {
                it = g_treeAppliedVersion.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_treeSurfaceAppliedVersion.begin(); it != g_treeSurfaceAppliedVersion.end(); ) {
            if (!isFoliageEligibleSection(it->first)) {
                it = g_treeSurfaceAppliedVersion.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_treeBackfillVisited.begin(); it != g_treeBackfillVisited.end(); ) {
            if (!isFoliageEligibleSection(*it)) {
                it = g_treeBackfillVisited.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_treePendingDependencies.begin(); it != g_treePendingDependencies.end(); ) {
            if (!isFoliageEligibleSection(*it)) {
                g_treeSurfaceAppliedVersion.erase(*it);
                g_treeSectionProgress.erase(*it);
                it = g_treePendingDependencies.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_treePendingSections.begin(); it != g_treePendingSections.end(); ) {
            if (!isFoliageEligibleSection(*it)) {
                g_treeSurfaceAppliedVersion.erase(*it);
                g_treeSectionProgress.erase(*it);
                it = g_treePendingSections.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = g_treeSectionProgress.begin(); it != g_treeSectionProgress.end(); ) {
            if (!isFoliageEligibleSection(it->first)) {
                g_treeSurfaceAppliedVersion.erase(it->first);
                it = g_treeSectionProgress.erase(it);
            } else {
                ++it;
            }
        }

        const Entity* trunkProtoA = HostLogic::findPrototype("FirLog1Tex", prototypes);
        const Entity* trunkProtoB = HostLogic::findPrototype("FirLog2Tex", prototypes);
        if (!trunkProtoA) trunkProtoA = HostLogic::findPrototype("Branch", prototypes);
        if (!trunkProtoA) trunkProtoA = HostLogic::findPrototype("Block", prototypes);
        if (!trunkProtoB) trunkProtoB = trunkProtoA;
        const Entity* leafProto = HostLogic::findPrototype("Leaf", prototypes);
        const Entity* leafFanOakProto = HostLogic::findPrototype("GrassTuftLeafFanOak", prototypes);
        const Entity* leafFanPineProto = HostLogic::findPrototype("GrassTuftLeafFanPine", prototypes);
        const Entity* grassProto = HostLogic::findPrototype("GrassTuft", prototypes);
        const Entity* shortGrassProto = HostLogic::findPrototype("GrassTuftShort", prototypes);
        const Entity* grassProtoBiome1 = HostLogic::findPrototype("GrassTuftMeadow", prototypes);
        const Entity* shortGrassProtoBiome1 = HostLogic::findPrototype("GrassTuftShortMeadow", prototypes);
        const Entity* grassProtoBiome3 = HostLogic::findPrototype("GrassTuftJungle", prototypes);
        std::array<int, 4> grassPrototypeBiome4IDs{};
        int grassPrototypeBiome4Count = 0;
        {
            const std::array<const char*, 4> kBareGrassNames = {
                "GrassTuftBareV001",
                "GrassTuftBareV002",
                "GrassTuftBareV003",
                "GrassTuftBareV004"
            };
            for (const char* name : kBareGrassNames) {
                if (const Entity* proto = HostLogic::findPrototype(name, prototypes)) {
                    grassPrototypeBiome4IDs[static_cast<size_t>(grassPrototypeBiome4Count)] = proto->prototypeID;
                    grassPrototypeBiome4Count += 1;
                }
            }
        }
        const Entity* flowerProto = HostLogic::findPrototype("Flower", prototypes);
        const Entity* blueRareFlowerProto = HostLogic::findPrototype("FlowerBlueRare", prototypes);
        const Entity* flaxRareFlowerProto = HostLogic::findPrototype("FlowerFlaxRare", prototypes);
        const Entity* hempRareFlowerProto = HostLogic::findPrototype("FlowerHempRare", prototypes);
        const Entity* fernRareFlowerProto = HostLogic::findPrototype("FlowerFernRare", prototypes);
        const Entity* succulentProto = HostLogic::findPrototype("FlowerSucculentV001", prototypes);
        const Entity* succulentProtoV2 = HostLogic::findPrototype("FlowerSucculentV002", prototypes);
        const Entity* jungleOrangeUnderLeafProto = HostLogic::findPrototype("FlowerOrangeUnderLeafV001", prototypes);
        const Entity* pineBeeHiveProto = HostLogic::findPrototype("FlowerBeeHivePineV001", prototypes);
        std::array<int, 3> coniferMushroomPrototypeIDs{};
        int coniferMushroomPrototypeCount = 0;
        {
            const std::array<const char*, 3> kConiferMushroomNames = {
                "FlowerMushroomConiferV001",
                "FlowerMushroomConiferV002",
                "FlowerMushroomConiferV003"
            };
            for (const char* name : kConiferMushroomNames) {
                if (const Entity* proto = HostLogic::findPrototype(name, prototypes)) {
                    coniferMushroomPrototypeIDs[static_cast<size_t>(coniferMushroomPrototypeCount)] = proto->prototypeID;
                    coniferMushroomPrototypeCount += 1;
                }
            }
        }
        std::array<int, 4> jungleCoffeePrototypeIDs{};
        int jungleCoffeePrototypeCount = 0;
        {
            const std::array<const char*, 4> kJungleCoffeeNames = {
                "FlowerCoffeeJungleV001",
                "FlowerCoffeeJungleV002",
                "FlowerCoffeeJungleV003",
                "FlowerCoffeeJungleV004"
            };
            for (const char* name : kJungleCoffeeNames) {
                if (const Entity* proto = HostLogic::findPrototype(name, prototypes)) {
                    jungleCoffeePrototypeIDs[static_cast<size_t>(jungleCoffeePrototypeCount)] = proto->prototypeID;
                    jungleCoffeePrototypeCount += 1;
                }
            }
        }
        std::array<int, 6> anyBiomePlantPrototypeIDs{};
        int anyBiomePlantPrototypeCount = 0;
        {
            const std::array<const char*, 6> kAnyBiomePlantNames = {
                "FlowerMiscAnyBiomeV001",
                "FlowerMiscAnyBiomeV002",
                "FlowerMiscAnyBiomeV003",
                "FlowerMiscAnyBiomeV004",
                "FlowerMiscAnyBiomeV005",
                "FlowerMiscAnyBiomeV006"
            };
            for (const char* name : kAnyBiomePlantNames) {
                if (const Entity* proto = HostLogic::findPrototype(name, prototypes)) {
                    anyBiomePlantPrototypeIDs[static_cast<size_t>(anyBiomePlantPrototypeCount)] = proto->prototypeID;
                    anyBiomePlantPrototypeCount += 1;
                }
            }
        }
        std::array<int, 3> meadowPlantPrototypeIDs{};
        int meadowPlantPrototypeCount = 0;
        {
            const std::array<const char*, 3> kMeadowPlantNames = {
                "FlowerMeadowMiscV001",
                "FlowerMeadowMiscV002",
                "FlowerMeadowMiscV003"
            };
            for (const char* name : kMeadowPlantNames) {
                if (const Entity* proto = HostLogic::findPrototype(name, prototypes)) {
                    meadowPlantPrototypeIDs[static_cast<size_t>(meadowPlantPrototypeCount)] = proto->prototypeID;
                    meadowPlantPrototypeCount += 1;
                }
            }
        }
        std::array<int, 16> blueFlowerPrototypeIDs{};
        int blueFlowerPrototypeCount = 0;
        {
            const std::array<const char*, 13> kBlueFlowerNames = {
                "FlowerBlueV001",
                "FlowerBlueV002",
                "FlowerBlueV003",
                "FlowerBlueV004",
                "FlowerBlueV005",
                "FlowerBlueV006",
                "FlowerBlueV007",
                "FlowerBlueV008",
                "FlowerLittleBluestemV001",
                "FlowerLittleBluestemV002",
                "FlowerLittleBluestemV003",
                "FlowerLittleBluestemV004",
                "FlowerLittleBluestemV005"
            };
            for (const char* name : kBlueFlowerNames) {
                if (const Entity* proto = HostLogic::findPrototype(name, prototypes)) {
                    blueFlowerPrototypeIDs[static_cast<size_t>(blueFlowerPrototypeCount)] = proto->prototypeID;
                    blueFlowerPrototypeCount += 1;
                }
            }
        }
        std::array<int, kJungleLeafVariantCount> jungleLeafPrototypeIDs{};
        int jungleLeafPrototypeCount = 0;
        {
            const std::array<const char*, kJungleLeafVariantCount> kJungleLeafNames = {
                "LeafJungleV001",
                "LeafJungleV002",
                "LeafJungleV003",
                "LeafJungleV004",
                "LeafJungleV005",
                "LeafJungleV006",
                "LeafJungleV007",
                "LeafJungleV008",
                "LeafJungleV009",
                "LeafJungleV010",
                "LeafJungleV011",
                "LeafJungleV012",
                "LeafJungleV013",
                "LeafJungleV014",
                "LeafJungleV015",
                "LeafJungleV016"
            };
            for (const char* name : kJungleLeafNames) {
                if (const Entity* proto = HostLogic::findPrototype(name, prototypes)) {
                    jungleLeafPrototypeIDs[static_cast<size_t>(jungleLeafPrototypeCount)] = proto->prototypeID;
                    jungleLeafPrototypeCount += 1;
                }
            }
        }
        const Entity* stickProtoX = HostLogic::findPrototype("StickTexX", prototypes);
        const Entity* stickProtoZ = HostLogic::findPrototype("StickTexZ", prototypes);
        const Entity* stickWinterProtoX = HostLogic::findPrototype("StickWinterTexX", prototypes);
        const Entity* stickWinterProtoZ = HostLogic::findPrototype("StickWinterTexZ", prototypes);
        const Entity* grassCoverProtoX = HostLogic::findPrototype("GrassCoverTexX", prototypes);
        const Entity* grassCoverProtoZ = HostLogic::findPrototype("GrassCoverTexZ", prototypes);
        const Entity* grassCoverMeadowProtoX = HostLogic::findPrototype("GrassCoverMeadowTexX", prototypes);
        const Entity* grassCoverMeadowProtoZ = HostLogic::findPrototype("GrassCoverMeadowTexZ", prototypes);
        const Entity* grassCoverForestProtoX = HostLogic::findPrototype("GrassCoverForestTexX", prototypes);
        const Entity* grassCoverForestProtoZ = HostLogic::findPrototype("GrassCoverForestTexZ", prototypes);
        const Entity* grassCoverJungleProtoX = HostLogic::findPrototype("GrassCoverJungleTexX", prototypes);
        const Entity* grassCoverJungleProtoZ = HostLogic::findPrototype("GrassCoverJungleTexZ", prototypes);
        const Entity* grassCoverBareProtoX = HostLogic::findPrototype("GrassCoverBareTexX", prototypes);
        const Entity* grassCoverBareProtoZ = HostLogic::findPrototype("GrassCoverBareTexZ", prototypes);
        const Entity* grassCoverDesertProtoX = HostLogic::findPrototype("GrassCoverDesertTexX", prototypes);
        const Entity* grassCoverDesertProtoZ = HostLogic::findPrototype("GrassCoverDesertTexZ", prototypes);
        const Entity* miniPineBottomProto = HostLogic::findPrototype("GrassTuftMiniPineBottom", prototypes);
        const Entity* miniPineTopProto = HostLogic::findPrototype("GrassTuftMiniPineTop", prototypes);
        const Entity* miniPineTripleBottomProto = HostLogic::findPrototype("GrassTuftMiniPineTripleBottom", prototypes);
        const Entity* miniPineTripleMiddleProto = HostLogic::findPrototype("GrassTuftMiniPineTripleMiddle", prototypes);
        const Entity* miniPineTripleTopProto = HostLogic::findPrototype("GrassTuftMiniPineTripleTop", prototypes);
        const Entity* flaxDoubleBottomProto = HostLogic::findPrototype("GrassTuftFlaxDoubleBottom", prototypes);
        const Entity* flaxDoubleTopProto = HostLogic::findPrototype("GrassTuftFlaxDoubleTop", prototypes);
        const Entity* hempDoubleBottomProto = HostLogic::findPrototype("GrassTuftHempDoubleBottom", prototypes);
        const Entity* hempDoubleTopProto = HostLogic::findPrototype("GrassTuftHempDoubleTop", prototypes);
        const Entity* pebblePatchProtoX = HostLogic::findPrototype("StonePebblePatchTexX", prototypes);
        const Entity* pebblePatchProtoZ = HostLogic::findPrototype("StonePebblePatchTexZ", prototypes);
        const Entity* autumnLeafPatchProtoX = HostLogic::findPrototype("StonePebbleLeafTexX", prototypes);
        const Entity* autumnLeafPatchProtoZ = HostLogic::findPrototype("StonePebbleLeafTexZ", prototypes);
        const Entity* deadLeafPatchProtoX = HostLogic::findPrototype("StonePebbleDeadLeafTexX", prototypes);
        const Entity* deadLeafPatchProtoZ = HostLogic::findPrototype("StonePebbleDeadLeafTexZ", prototypes);
        const Entity* bareBranchProtoX = HostLogic::findPrototype("StonePebbleBareBranchTexX", prototypes);
        const Entity* bareBranchProtoZ = HostLogic::findPrototype("StonePebbleBareBranchTexZ", prototypes);
        const Entity* lilypadPatchProtoX = HostLogic::findPrototype("StonePebbleLilypadTexX", prototypes);
        const Entity* lilypadPatchProtoZ = HostLogic::findPrototype("StonePebbleLilypadTexZ", prototypes);
        const Entity* kelpProto = HostLogic::findPrototype("GrassTuftKelp", prototypes);
        const int kelpTileIndex = kelpProto
            ? RenderInitSystemLogic::FaceTileIndexFor(baseSystem.world.get(), *kelpProto, 0)
            : -1;
        const Entity* seaUrchinProtoX = HostLogic::findPrototype("StonePebbleSeaUrchinTexX", prototypes);
        const Entity* seaUrchinProtoZ = HostLogic::findPrototype("StonePebbleSeaUrchinTexZ", prototypes);
        const Entity* sandDollarProtoX = HostLogic::findPrototype("StonePebbleSandDollarTexX", prototypes);
        const Entity* sandDollarProtoZ = HostLogic::findPrototype("StonePebbleSandDollarTexZ", prototypes);
        const Entity* jungleTrunkProto = HostLogic::findPrototype("OakLogTex", prototypes);
        const Entity* bareTrunkProto = HostLogic::findPrototype("BareLogTex", prototypes);
        const Entity* wallBranchLongProtoPosX = HostLogic::findPrototype("WallBranchLongTexPosX", prototypes);
        const Entity* wallBranchLongProtoNegX = HostLogic::findPrototype("WallBranchLongTexNegX", prototypes);
        const Entity* wallBranchLongProtoPosZ = HostLogic::findPrototype("WallBranchLongTexPosZ", prototypes);
        const Entity* wallBranchLongProtoNegZ = HostLogic::findPrototype("WallBranchLongTexNegZ", prototypes);
        const Entity* wallBranchLongTipProtoPosX = HostLogic::findPrototype("WallBranchLongTipTexPosX", prototypes);
        const Entity* wallBranchLongTipProtoNegX = HostLogic::findPrototype("WallBranchLongTipTexNegX", prototypes);
        const Entity* wallBranchLongTipProtoPosZ = HostLogic::findPrototype("WallBranchLongTipTexPosZ", prototypes);
        const Entity* wallBranchLongTipProtoNegZ = HostLogic::findPrototype("WallBranchLongTipTexNegZ", prototypes);
        const Entity* cactusProtoA = HostLogic::findPrototype("Cactus1Tex", prototypes);
        const Entity* cactusProtoB = HostLogic::findPrototype("Cactus2Tex", prototypes);
        const Entity* cactusProtoAX = HostLogic::findPrototype("Cactus1TexX", prototypes);
        const Entity* cactusProtoAZ = HostLogic::findPrototype("Cactus1TexZ", prototypes);
        const Entity* cactusProtoBX = HostLogic::findPrototype("Cactus2TexX", prototypes);
        const Entity* cactusProtoBZ = HostLogic::findPrototype("Cactus2TexZ", prototypes);
        const Entity* cactusProtoAJunctionX = HostLogic::findPrototype("Cactus1TexJunctionX", prototypes);
        const Entity* cactusProtoAJunctionZ = HostLogic::findPrototype("Cactus1TexJunctionZ", prototypes);
        const Entity* cactusProtoBJunctionX = HostLogic::findPrototype("Cactus2TexJunctionX", prototypes);
        const Entity* cactusProtoBJunctionZ = HostLogic::findPrototype("Cactus2TexJunctionZ", prototypes);
        const Entity* waterProto = HostLogic::findPrototype("Water", prototypes);
        if (!trunkProtoA || !trunkProtoB || !leafProto) return;
        if (!grassProtoBiome1) grassProtoBiome1 = grassProto;
        if (!shortGrassProtoBiome1) shortGrassProtoBiome1 = shortGrassProto;
        if (!grassProtoBiome3) grassProtoBiome3 = grassProto;
        if (grassPrototypeBiome4Count <= 0 && grassProtoBiome3) {
            grassPrototypeBiome4IDs[0] = grassProtoBiome3->prototypeID;
            grassPrototypeBiome4Count = 1;
        }
        if (!grassCoverMeadowProtoX) grassCoverMeadowProtoX = grassCoverProtoX;
        if (!grassCoverMeadowProtoZ) grassCoverMeadowProtoZ = grassCoverProtoZ;
        if (!grassCoverForestProtoX) grassCoverForestProtoX = grassCoverProtoX;
        if (!grassCoverForestProtoZ) grassCoverForestProtoZ = grassCoverProtoZ;
        if (!grassCoverJungleProtoX) grassCoverJungleProtoX = grassCoverProtoX;
        if (!grassCoverJungleProtoZ) grassCoverJungleProtoZ = grassCoverProtoZ;
        if (!grassCoverBareProtoX) grassCoverBareProtoX = grassCoverForestProtoX;
        if (!grassCoverBareProtoZ) grassCoverBareProtoZ = grassCoverForestProtoZ;
        if (!grassCoverDesertProtoX) grassCoverDesertProtoX = grassCoverProtoX;
        if (!grassCoverDesertProtoZ) grassCoverDesertProtoZ = grassCoverProtoZ;
        if (!deadLeafPatchProtoX) deadLeafPatchProtoX = autumnLeafPatchProtoX;
        if (!deadLeafPatchProtoZ) deadLeafPatchProtoZ = autumnLeafPatchProtoZ;
        if (!bareBranchProtoX) bareBranchProtoX = stickWinterProtoX ? stickWinterProtoX : stickProtoX;
        if (!bareBranchProtoZ) bareBranchProtoZ = stickWinterProtoZ ? stickWinterProtoZ : stickProtoZ;

        const uint32_t trunkColor = packColor(glm::vec3(0.29f, 0.21f, 0.13f));
        const uint32_t leafColor = packColor(glm::vec3(0.07f, 0.46f, 0.34f));
        PineSpec baseSpec;
        const int pineCanopyBaseHeight = std::max(2, getRegistryInt(baseSystem, "PineCanopyBaseHeight", 10));
        baseSpec.canopyOffset = std::max(1, baseSpec.trunkHeight - pineCanopyBaseHeight);
        const int pineMinTrunkHeight = std::max(6, getRegistryInt(baseSystem, "PineTrunkHeightMin", 15));
        const int pineMaxTrunkHeight = std::max(
            pineMinTrunkHeight,
            getRegistryInt(baseSystem, "PineTrunkHeightMax", baseSpec.trunkHeight)
        );
        const int pineBeeHiveSpawnModulo = std::max(1, getRegistryInt(baseSystem, "PineBeeHiveSpawnModulo", 18));
        const int jungleTreeSpawnModulo = std::max(1, getRegistryInt(baseSystem, "JungleTreeSpawnModulo", 1000));
        const bool meadowTreeGenerationEnabled = getRegistryBool(baseSystem, "MeadowTreeGenerationEnabled", true);
        const int meadowTreeSpawnModulo = std::max(1, getRegistryInt(baseSystem, "MeadowTreeSpawnModulo", 140));
        const int jungleTreeTrunkMin = std::max(4, getRegistryInt(baseSystem, "JungleTreeTrunkHeightMin", 6));
        const int jungleTreeTrunkMax = std::max(jungleTreeTrunkMin, getRegistryInt(baseSystem, "JungleTreeTrunkHeightMax", 9));
        const int jungleTreeCanopyRadius = std::max(2, std::min(8, getRegistryInt(baseSystem, "JungleTreeCanopyRadius", 4)));
        const int bareTreeSpawnModulo = std::max(1, getRegistryInt(baseSystem, "BareTreeSpawnModulo", 380));
        const int bareTreeTrunkMin = std::max(4, getRegistryInt(baseSystem, "BareTreeTrunkHeightMin", 7));
        const int bareTreeTrunkMax = std::max(bareTreeTrunkMin, getRegistryInt(baseSystem, "BareTreeTrunkHeightMax", 12));
        const int desertCactusSpawnModulo = std::max(1, getRegistryInt(baseSystem, "DesertCactusSpawnModulo", 128));
        const bool desertCactusEnabled = getRegistryBool(baseSystem, "DesertCactusGenerationEnabled", true);
        FoliageSpec foliageSpec;
        foliageSpec.enabled = getRegistryBool(baseSystem, "FoliageGenerationEnabled", true);
        foliageSpec.grassEnabled = getRegistryBool(baseSystem, "GrassGenerationEnabled", true);
        foliageSpec.grassCoverEnabled = getRegistryBool(baseSystem, "GrassCoverGenerationEnabled", true);
        foliageSpec.pebblePatchEnabled = getRegistryBool(baseSystem, "PebblePatchGenerationEnabled", true);
        foliageSpec.autumnLeafPatchEnabled = getRegistryBool(baseSystem, "AutumnLeafPatchGenerationEnabled", true);
        foliageSpec.lilypadPatchEnabled = getRegistryBool(baseSystem, "LilypadPatchGenerationEnabled", true);
        foliageSpec.flowerEnabled = getRegistryBool(baseSystem, "FlowerGenerationEnabled", true);
        foliageSpec.stickEnabled = getRegistryBool(baseSystem, "StickGenerationEnabled", true);
        foliageSpec.waterFoliageEnabled = getRegistryBool(baseSystem, "WaterFoliageGenerationEnabled", true);
        foliageSpec.temperateOnly = getRegistryBool(baseSystem, "FoliageTemperateOnly", true);
        foliageSpec.grassSpawnModulo = std::max(1, getRegistryInt(baseSystem, "GrassSpawnModulo", foliageSpec.grassSpawnModulo));
        foliageSpec.grassCoverPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "GrassCoverPercent", foliageSpec.grassCoverPercent)));
        foliageSpec.pebblePatchPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "PebblePatchPercent", foliageSpec.pebblePatchPercent)));
        foliageSpec.pebblePatchNearWaterRadius = std::max(1, std::min(64, getRegistryInt(baseSystem, "PebblePatchNearWaterRadius", foliageSpec.pebblePatchNearWaterRadius)));
        foliageSpec.pebblePatchNearWaterVerticalRange = std::max(0, std::min(32, getRegistryInt(baseSystem, "PebblePatchNearWaterVerticalRange", foliageSpec.pebblePatchNearWaterVerticalRange)));
        foliageSpec.autumnLeafPatchPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "AutumnLeafPatchPercent", foliageSpec.autumnLeafPatchPercent)));
        foliageSpec.lilypadPatchPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "LilypadPatchPercent", foliageSpec.lilypadPatchPercent)));
        foliageSpec.flowerSpawnModulo = std::max(1, getRegistryInt(baseSystem, "FlowerSpawnModulo", foliageSpec.flowerSpawnModulo));
        foliageSpec.miniPineSpawnModulo = std::max(1, getRegistryInt(baseSystem, "MiniPineSpawnModulo", foliageSpec.miniPineSpawnModulo));
        foliageSpec.shortGrassPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "ShortGrassPercent", foliageSpec.shortGrassPercent)));
        foliageSpec.grassTuftPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "GrassTuftPercent", foliageSpec.grassTuftPercent)));
        foliageSpec.stickSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "StickSpawnPercent", foliageSpec.stickSpawnPercent)));
        foliageSpec.stickCanopySearchRadius = std::max(0, std::min(4, getRegistryInt(baseSystem, "StickCanopySearchRadius", foliageSpec.stickCanopySearchRadius)));
        foliageSpec.stickCanopySearchHeight = std::max(2, std::min(96, getRegistryInt(baseSystem, "StickCanopySearchHeight", foliageSpec.stickCanopySearchHeight)));
        foliageSpec.kelpSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "KelpSpawnPercent", foliageSpec.kelpSpawnPercent)));
        foliageSpec.seaUrchinSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "SeaUrchinSpawnPercent", foliageSpec.seaUrchinSpawnPercent)));
        foliageSpec.sandDollarSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "SandDollarSpawnPercent", foliageSpec.sandDollarSpawnPercent)));
        if (!grassProto
            && !shortGrassProto
            && !grassProtoBiome1
            && !shortGrassProtoBiome1
            && !grassProtoBiome3
            && grassPrototypeBiome4Count <= 0) {
            foliageSpec.grassEnabled = false;
        }
        if (!grassCoverProtoX && !grassCoverProtoZ
            && !grassCoverMeadowProtoX && !grassCoverMeadowProtoZ
            && !grassCoverForestProtoX && !grassCoverForestProtoZ
            && !grassCoverJungleProtoX && !grassCoverJungleProtoZ
            && !grassCoverBareProtoX && !grassCoverBareProtoZ
            && !grassCoverDesertProtoX && !grassCoverDesertProtoZ) {
            foliageSpec.grassCoverEnabled = false;
            foliageSpec.pebblePatchEnabled = false;
        }
        if (!pebblePatchProtoX && !pebblePatchProtoZ) {
            foliageSpec.pebblePatchEnabled = false;
        }
        if (!autumnLeafPatchProtoX && !autumnLeafPatchProtoZ
            && !deadLeafPatchProtoX && !deadLeafPatchProtoZ) {
            foliageSpec.autumnLeafPatchEnabled = false;
        }
        if (!lilypadPatchProtoX && !lilypadPatchProtoZ) {
            foliageSpec.lilypadPatchEnabled = false;
        }
        if (!flowerProto
            && blueFlowerPrototypeCount <= 0
            && !succulentProto
            && !succulentProtoV2
            && coniferMushroomPrototypeCount <= 0
            && jungleCoffeePrototypeCount <= 0
            && anyBiomePlantPrototypeCount <= 0
            && meadowPlantPrototypeCount <= 0
            && !jungleOrangeUnderLeafProto) {
            foliageSpec.flowerEnabled = false;
        }
        if (!stickProtoX && !stickProtoZ && !stickWinterProtoX && !stickWinterProtoZ) foliageSpec.stickEnabled = false;
        if (!kelpProto && !seaUrchinProtoX && !seaUrchinProtoZ && !sandDollarProtoX && !sandDollarProtoZ) foliageSpec.waterFoliageEnabled = false;
        if (!foliageSpec.grassEnabled
            && !foliageSpec.grassCoverEnabled
            && !foliageSpec.flowerEnabled
            && !foliageSpec.waterFoliageEnabled) {
            foliageSpec.enabled = false;
        }
        const int waterPrototypeID = waterProto ? waterProto->prototypeID : -1;

        // Re-run first-pass foliage when generation rules/prototypes change.
        uint64_t foliageSignature = 1469598103934665603ull;
        auto mixSig = [&](uint64_t v) {
            foliageSignature ^= v;
            foliageSignature *= 1099511628211ull;
        };
        mixSig(0xCAFEA16u); // include winter bare biome foliage/tree prototype revisions
        mixSig(static_cast<uint64_t>(foliageSpec.waterFoliageEnabled ? 1u : 0u));
        mixSig(static_cast<uint64_t>(foliageSpec.kelpSpawnPercent));
        mixSig(static_cast<uint64_t>(foliageSpec.seaUrchinSpawnPercent));
        mixSig(static_cast<uint64_t>(foliageSpec.sandDollarSpawnPercent));
        mixSig(static_cast<uint64_t>(kelpProto ? std::max(0, kelpProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(seaUrchinProtoX ? std::max(0, seaUrchinProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(seaUrchinProtoZ ? std::max(0, seaUrchinProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(sandDollarProtoX ? std::max(0, sandDollarProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(sandDollarProtoZ ? std::max(0, sandDollarProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(miniPineBottomProto ? std::max(0, miniPineBottomProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(miniPineTopProto ? std::max(0, miniPineTopProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(miniPineTripleBottomProto ? std::max(0, miniPineTripleBottomProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(miniPineTripleMiddleProto ? std::max(0, miniPineTripleMiddleProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(miniPineTripleTopProto ? std::max(0, miniPineTripleTopProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(jungleOrangeUnderLeafProto ? std::max(0, jungleOrangeUnderLeafProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(pineBeeHiveProto ? std::max(0, pineBeeHiveProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(pineBeeHiveSpawnModulo));
        mixSig(static_cast<uint64_t>(grassPrototypeBiome4Count));
        for (int i = 0; i < grassPrototypeBiome4Count; ++i) {
            mixSig(static_cast<uint64_t>(std::max(0, grassPrototypeBiome4IDs[static_cast<size_t>(i)])));
        }
        mixSig(static_cast<uint64_t>(grassCoverBareProtoX ? std::max(0, grassCoverBareProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(grassCoverBareProtoZ ? std::max(0, grassCoverBareProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(deadLeafPatchProtoX ? std::max(0, deadLeafPatchProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(deadLeafPatchProtoZ ? std::max(0, deadLeafPatchProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(bareBranchProtoX ? std::max(0, bareBranchProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(bareBranchProtoZ ? std::max(0, bareBranchProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(stickWinterProtoX ? std::max(0, stickWinterProtoX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(stickWinterProtoZ ? std::max(0, stickWinterProtoZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(bareTrunkProto ? std::max(0, bareTrunkProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(meadowTreeGenerationEnabled ? 1u : 0u));
        mixSig(static_cast<uint64_t>(meadowTreeSpawnModulo));
        mixSig(static_cast<uint64_t>(jungleTreeSpawnModulo));
        mixSig(static_cast<uint64_t>(bareTreeSpawnModulo));
        mixSig(static_cast<uint64_t>(wallBranchLongProtoPosX ? std::max(0, wallBranchLongProtoPosX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongProtoNegX ? std::max(0, wallBranchLongProtoNegX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongProtoPosZ ? std::max(0, wallBranchLongProtoPosZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongProtoNegZ ? std::max(0, wallBranchLongProtoNegZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongTipProtoPosX ? std::max(0, wallBranchLongTipProtoPosX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongTipProtoNegX ? std::max(0, wallBranchLongTipProtoNegX->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongTipProtoPosZ ? std::max(0, wallBranchLongTipProtoPosZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(wallBranchLongTipProtoNegZ ? std::max(0, wallBranchLongTipProtoNegZ->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(flaxDoubleBottomProto ? std::max(0, flaxDoubleBottomProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(flaxDoubleTopProto ? std::max(0, flaxDoubleTopProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(hempDoubleBottomProto ? std::max(0, hempDoubleBottomProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(hempDoubleTopProto ? std::max(0, hempDoubleTopProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(leafFanOakProto ? std::max(0, leafFanOakProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(leafFanPineProto ? std::max(0, leafFanPineProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(succulentProto ? std::max(0, succulentProto->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(succulentProtoV2 ? std::max(0, succulentProtoV2->prototypeID) : 0));
        mixSig(static_cast<uint64_t>(coniferMushroomPrototypeCount));
        for (int i = 0; i < coniferMushroomPrototypeCount; ++i) {
            mixSig(static_cast<uint64_t>(std::max(0, coniferMushroomPrototypeIDs[static_cast<size_t>(i)])));
        }
        mixSig(static_cast<uint64_t>(jungleCoffeePrototypeCount));
        for (int i = 0; i < jungleCoffeePrototypeCount; ++i) {
            mixSig(static_cast<uint64_t>(std::max(0, jungleCoffeePrototypeIDs[static_cast<size_t>(i)])));
        }
        mixSig(static_cast<uint64_t>(anyBiomePlantPrototypeCount));
        for (int i = 0; i < anyBiomePlantPrototypeCount; ++i) {
            mixSig(static_cast<uint64_t>(std::max(0, anyBiomePlantPrototypeIDs[static_cast<size_t>(i)])));
        }
        mixSig(static_cast<uint64_t>(meadowPlantPrototypeCount));
        for (int i = 0; i < meadowPlantPrototypeCount; ++i) {
            mixSig(static_cast<uint64_t>(std::max(0, meadowPlantPrototypeIDs[static_cast<size_t>(i)])));
        }
        if (g_treeFoliageSignature != foliageSignature) {
            g_treeFoliageSignature = foliageSignature;
            g_treeAppliedVersion.clear();
            g_treeSurfaceAppliedVersion.clear();
            g_treeBackfillVisited.clear();
            g_treePendingDependencies.clear();
            g_treePendingSections.clear();
            g_treeSectionProgress.clear();
            clearImmediateFoliageQueue();
        }

        if (!g_pendingPineLogRemovals.empty()) {
            std::vector<std::pair<glm::ivec3, int>> pending;
            pending.swap(g_pendingPineLogRemovals);
            std::vector<std::pair<glm::ivec3, int>> deferred;
            deferred.reserve(pending.size());
            std::unordered_set<glm::ivec3, IVec3Hash> seenCells;
            int processed = 0;
            constexpr int kMaxFallsPerFrame = 2;
            for (const auto& event : pending) {
                if (!isPineVerticalLogPrototypeID(prototypes, event.second)) continue;
                if (seenCells.count(event.first) > 0) continue;
                seenCells.insert(event.first);
                if (processed >= kMaxFallsPerFrame) {
                    deferred.push_back(event);
                    continue;
                }
                processSingleTreeFall(baseSystem, prototypes, event.first);
                processed += 1;
            }
            if (!deferred.empty()) {
                g_pendingPineLogRemovals.insert(g_pendingPineLogRemovals.end(), deferred.begin(), deferred.end());
            }
        }

        if (!terrainOwnsSectionWorldgen) {
            for (const auto& key : voxelWorld.dirtySections) {
                if (isFoliageEligibleSection(key)) {
                    g_treePendingSections.insert(key);
                    updateSurfaceFoliageLifecycle(key, false);
                } else {
                    g_treePendingSections.erase(key);
                    g_treePendingDependencies.erase(key);
                    g_treeSurfaceAppliedVersion.erase(key);
                    g_treeBackfillVisited.erase(key);
                    g_treeSectionProgress.erase(key);
                }
            }
        } else if (g_treeForceCompleteSections.empty() && g_treeImmediateQueue.empty()) {
            g_treePerfStats.pendingSections = g_treePendingSections.size();
            g_treePerfStats.pendingDependencies = g_treePendingDependencies.size();
            g_treePerfStats.backfillVisited = g_treeBackfillVisited.size();
            g_treePerfStats.selectedSections = 0;
            g_treePerfStats.processedSections = 0;
            g_treePerfStats.deferredByTimeBudget = 0;
            g_treePerfStats.backfillAppended = 0;
            g_treePerfStats.backfillRan = false;
            g_treePerfStats.updateMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - updateStart
            ).count();
            return;
        }

        std::vector<VoxelSectionKey> dirtySections;
        dirtySections.reserve(g_treePendingSections.size() + voxelWorld.dirtySections.size());
        std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> selected;
        std::vector<VoxelSectionKey> immediateSections;
        const bool forceCompleteActive = !g_treeForceCompleteSections.empty();
        const int immediateSectionBudget = std::max(
            0,
            getRegistryInt(baseSystem, "TreeFoliageImmediateSectionsPerFrame", 8)
        );
        if (immediateSectionBudget > 0 && !g_treeImmediateQueue.empty()) {
            const size_t immediateScanCount = g_treeImmediateQueue.size();
            for (size_t i = 0;
                 i < immediateScanCount && static_cast<int>(immediateSections.size()) < immediateSectionBudget;
                 ++i) {
                const VoxelSectionKey key = g_treeImmediateQueue.front();
                g_treeImmediateQueue.pop_front();
                g_treeImmediateQueued.erase(key);
                if (!isFoliageEligibleSection(key)) {
                    g_treePendingSections.erase(key);
                    g_treePendingDependencies.erase(key);
                    g_treeSurfaceAppliedVersion.erase(key);
                    g_treeBackfillVisited.erase(key);
                    g_treeSectionProgress.erase(key);
                    continue;
                }
                if (!TerrainSystemLogic::IsSectionTerrainReady(key)) {
                    if (g_treeImmediateQueued.insert(key).second) {
                        g_treeImmediateQueue.push_back(key);
                    }
                    continue;
                }
                if (selected.insert(key).second) {
                    dirtySections.push_back(key);
                    immediateSections.push_back(key);
                }
            }
        }
        if (!g_treeForceCompleteSections.empty()) {
            dirtySections.clear();
            selected.clear();
            immediateSections.clear();
            for (const auto& key : g_treeForceCompleteSections) {
                if (!isFoliageEligibleSection(key)) continue;
                if (!TerrainSystemLogic::IsSectionTerrainReady(key)) continue;
                if (selected.insert(key).second) {
                    dirtySections.push_back(key);
                    immediateSections.push_back(key);
                }
            }
        }

        int backfillAppended = 0;
        bool backfillRan = false;
        int cameraSurfaceWorldY = 0;
        bool terrainBacklogged = false;
        bool backfillLoaded = false;
        if (!forceCompleteActive) {
            // Process all pending sections each frame (selection budget is applied later). This
            // avoids starvation where newly ready sections can wait many frames before even being
            // considered for foliage/tree passes.
            for (auto it = g_treePendingSections.begin(); it != g_treePendingSections.end(); ) {
                const VoxelSectionKey key = *it;
                if (!isFoliageEligibleSection(key)) {
                    g_treePendingDependencies.erase(key);
                    g_treeSurfaceAppliedVersion.erase(key);
                    g_treeBackfillVisited.erase(key);
                    g_treeSectionProgress.erase(key);
                    it = g_treePendingSections.erase(it);
                    continue;
                }
                if (!TerrainSystemLogic::IsSectionTerrainReady(key)) {
                    ++it;
                    continue;
                }
                if (selected.insert(key).second) {
                    dirtySections.push_back(key);
                }
                ++it;
            }

            backfillLoaded = getRegistryBool(baseSystem, "TreeFoliageBackfillAllLoaded", true);
            int backfillBudget = std::max(1, getRegistryInt(baseSystem, "TreeFoliageBackfillSectionsPerFrame", 12));
            const int backfillIntervalFrames = std::max(1, getRegistryInt(baseSystem, "TreeFoliageBackfillIntervalFrames", 4));
            const float backfillMaxDistance = std::max(0.0f, getRegistryFloat(baseSystem, "TreeFoliageBackfillMaxDistance", 0.0f));
            const float backfillMaxDistanceSq = backfillMaxDistance > 0.0f ? backfillMaxDistance * backfillMaxDistance : 0.0f;
            size_t terrainPending = 0, terrainDesired = 0, terrainGenerated = 0, terrainJobs = 0;
            int terrainStepped = 0, terrainBuilt = 0, terrainConsumed = 0, terrainSkipped = 0, terrainFiltered = 0;
            int terrainRescueSurface = 0, terrainRescueMissing = 0, terrainCapDrop = 0, terrainReprio = 0;
            float terrainPrepMs = 0.0f;
            float terrainGenMs = 0.0f;
            float terrainDesiredMs = 0.0f;
            float terrainBaseMs = 0.0f;
	            float terrainFeatureMs = 0.0f;
	            float terrainSurfaceMs = 0.0f;
	            float terrainCaveFieldMs = 0.0f;
                int terrainSchedulerPressure = 0;
                int terrainDesiredBudget = 0;
                int terrainBaseBudget = 0;
                int terrainFeatureBudget = 0;
                int terrainSurfaceBudget = 0;
                float terrainBaseBudgetMs = 0.0f;
                float terrainFeatureBudgetMs = 0.0f;
                float terrainSurfaceBudgetMs = 0.0f;
                size_t terrainDownstreamDirty = 0;
                size_t terrainDownstreamPrepared = 0;
                size_t terrainDownstreamUpload = 0;
	            uint64_t terrainCaveFieldCellsBuilt = 0;
	            uint64_t terrainCaveSamples = 0;
            TerrainSystemLogic::GetVoxelStreamingPerfStats(
                terrainPending,
                terrainDesired,
                terrainGenerated,
                terrainJobs,
                terrainStepped,
                terrainBuilt,
                terrainConsumed,
                terrainSkipped,
                terrainFiltered,
                terrainRescueSurface,
                terrainRescueMissing,
                terrainCapDrop,
                terrainReprio,
                terrainPrepMs,
                terrainGenMs,
                terrainDesiredMs,
                terrainBaseMs,
	                terrainFeatureMs,
	                terrainSurfaceMs,
	                terrainCaveFieldMs,
                    terrainSchedulerPressure,
                    terrainDesiredBudget,
                    terrainBaseBudget,
                    terrainFeatureBudget,
                    terrainSurfaceBudget,
                    terrainBaseBudgetMs,
                    terrainFeatureBudgetMs,
                    terrainSurfaceBudgetMs,
                    terrainDownstreamDirty,
                    terrainDownstreamPrepared,
                    terrainDownstreamUpload,
	                terrainCaveFieldCellsBuilt,
	                terrainCaveSamples
	            );
            (void)terrainDesired;
            (void)terrainGenerated;
            (void)terrainStepped;
            (void)terrainBuilt;
            (void)terrainConsumed;
            (void)terrainSkipped;
            (void)terrainFiltered;
            (void)terrainRescueSurface;
            (void)terrainRescueMissing;
            (void)terrainCapDrop;
            (void)terrainReprio;
            (void)terrainPrepMs;
            (void)terrainGenMs;
            (void)terrainDesiredMs;
            (void)terrainBaseMs;
	            (void)terrainFeatureMs;
	            (void)terrainSurfaceMs;
	            (void)terrainCaveFieldMs;
                (void)terrainSchedulerPressure;
                (void)terrainDesiredBudget;
                (void)terrainBaseBudget;
                (void)terrainFeatureBudget;
                (void)terrainSurfaceBudget;
                (void)terrainBaseBudgetMs;
                (void)terrainFeatureBudgetMs;
                (void)terrainSurfaceBudgetMs;
                (void)terrainDownstreamDirty;
                (void)terrainDownstreamPrepared;
                (void)terrainDownstreamUpload;
	            (void)terrainCaveFieldCellsBuilt;
            (void)terrainCaveSamples;
            const bool throttleByTerrainBacklog = getRegistryBool(baseSystem, "TreeFoliageThrottleByTerrainBacklog", true);
            const int terrainPendingThreshold = std::max(0, getRegistryInt(baseSystem, "TreeFoliageTerrainPendingThreshold", 4096));
            const int terrainJobsThreshold = std::max(0, getRegistryInt(baseSystem, "TreeFoliageTerrainJobsThreshold", 24));
            terrainBacklogged = throttleByTerrainBacklog
                && ((terrainPendingThreshold > 0 && static_cast<int>(terrainPending) >= terrainPendingThreshold)
                    || (terrainJobsThreshold > 0 && static_cast<int>(terrainJobs) >= terrainJobsThreshold));
            const int throttledBackfillBudget = std::max(1, getRegistryInt(baseSystem, "TreeFoliageBackfillSectionsPerFrameBacklogged", 1));
            if (terrainBacklogged) {
                backfillBudget = std::min(backfillBudget, throttledBackfillBudget);
            }
            bool runBackfillThisFrame = backfillLoaded
                && baseSystem.player
                && ((g_treeFrameCounter % static_cast<uint64_t>(backfillIntervalFrames)) == 0u
                    || !g_treePendingDependencies.empty());
            if (terrainBacklogged && getRegistryBool(baseSystem, "TreeFoliageSkipBackfillWhenBacklogged", true)
                && g_treePendingDependencies.empty()) {
                runBackfillThisFrame = false;
            }
            if (baseSystem.player) {
                float cameraSurface = 0.0f;
                const bool cameraOnLand = ExpanseBiomeSystemLogic::SampleTerrain(
                    worldCtx,
                    baseSystem.player->cameraPosition.x,
                    baseSystem.player->cameraPosition.z,
                    cameraSurface
                );
                cameraSurfaceWorldY = cameraOnLand
                    ? (static_cast<int>(std::floor(cameraSurface)) + 1)
                    : (static_cast<int>(std::floor(worldCtx.expanse.waterSurface)) + 1);
            }
            if (runBackfillThisFrame) {
                backfillRan = true;
                struct BackfillCandidate {
                    VoxelSectionKey key;
                    float dist2 = 0.0f;
                    int yDistToSurface = 0;
                    bool pendingDependencies = false;
                };
                std::vector<BackfillCandidate> candidates;
                candidates.reserve(voxelWorld.sections.size());
                const glm::vec3 camPos = baseSystem.player->cameraPosition;
                for (const auto& [key, section] : voxelWorld.sections) {
                    if (selected.count(key) > 0) continue;
                    if (!isFoliageEligibleSection(key)) continue;
                    if (!TerrainSystemLogic::IsSectionTerrainReady(key)) continue;
                    const bool waitingDependencies = g_treePendingDependencies.count(key) > 0;
                    if (!waitingDependencies && g_treeBackfillVisited.count(key) > 0) continue;
                    const int sectionScale = sectionScaleForTier(0);
                    const float worldSectionSpan = static_cast<float>(section.size * sectionScale);
                    float centerX = (static_cast<float>(key.coord.x) + 0.5f) * worldSectionSpan;
                    float centerZ = (static_cast<float>(key.coord.z) + 0.5f) * worldSectionSpan;
                    float dx = centerX - camPos.x;
                    float dz = centerZ - camPos.z;
                    float dist2 = dx * dx + dz * dz;
                    if (backfillMaxDistanceSq > 0.0f && dist2 > backfillMaxDistanceSq) continue;
                    const int cameraSurfaceTierY = floorDivInt(cameraSurfaceWorldY, sectionScale);
                    const int cameraSurfaceSectionY = floorDivInt(cameraSurfaceTierY, section.size);
                    candidates.push_back({
                        key,
                        dist2,
                        std::abs(key.coord.y - cameraSurfaceSectionY),
                        waitingDependencies
                    });
                }
                std::sort(candidates.begin(), candidates.end(), [](const BackfillCandidate& a, const BackfillCandidate& b) {
                    if (a.pendingDependencies != b.pendingDependencies) return a.pendingDependencies > b.pendingDependencies;
                    if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
                    if (a.yDistToSurface != b.yDistToSurface) return a.yDistToSurface < b.yDistToSurface;
                    if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
                    if (a.key.coord.y != b.key.coord.y) return a.key.coord.y < b.key.coord.y;
                    return a.key.coord.z < b.key.coord.z;
                });
                int appended = 0;
                for (const auto& candidate : candidates) {
                    if (appended >= backfillBudget) break;
                    dirtySections.push_back(candidate.key);
                    selected.insert(candidate.key);
                    appended += 1;
                }
                backfillAppended = appended;
            }
        }
        const int immediateForcedCount = static_cast<int>(immediateSections.size());
        if (dirtySections.empty()) {
            g_treePerfStats.pendingSections = g_treePendingSections.size();
            g_treePerfStats.pendingDependencies = g_treePendingDependencies.size();
            g_treePerfStats.backfillVisited = g_treeBackfillVisited.size();
            g_treePerfStats.selectedSections = 0;
            g_treePerfStats.processedSections = 0;
            g_treePerfStats.deferredByTimeBudget = 0;
            g_treePerfStats.backfillAppended = backfillAppended;
            g_treePerfStats.backfillRan = backfillRan;
            g_treePerfStats.updateMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - updateStart
            ).count();
            return;
        }
        {
            int sectionBudget = std::max(1, getRegistryInt(baseSystem, "TreeFoliageSectionsPerFrame", 4));
            if (terrainBacklogged) {
                const int throttledBudget = std::max(1, getRegistryInt(baseSystem, "TreeFoliageSectionsPerFrameBacklogged", 1));
                sectionBudget = std::min(sectionBudget, throttledBudget);
            }
            const bool prioritizeContinuations = getRegistryBool(
                baseSystem,
                "TreeFoliagePrioritizeContinuations",
                true
            );
            auto dist2ToCam = [&](const VoxelSectionKey& k) {
                if (!baseSystem.player) return std::numeric_limits<float>::max();
                auto it = voxelWorld.sections.find(k);
                if (it == voxelWorld.sections.end()) return std::numeric_limits<float>::max();
                const int sectionScale = sectionScaleForTier(0);
                const float worldSectionSpan = static_cast<float>(it->second.size * sectionScale);
                const float cx = (static_cast<float>(k.coord.x) + 0.5f) * worldSectionSpan;
                const float cz = (static_cast<float>(k.coord.z) + 0.5f) * worldSectionSpan;
                const float dx = cx - baseSystem.player->cameraPosition.x;
                const float dz = cz - baseSystem.player->cameraPosition.z;
                return dx * dx + dz * dz;
            };
            auto yDistToSurface = [&](const VoxelSectionKey& k) {
                auto it = voxelWorld.sections.find(k);
                if (it == voxelWorld.sections.end()) return std::numeric_limits<int>::max();
                const int sectionScale = sectionScaleForTier(0);
                const int cameraSurfaceTierY = floorDivInt(cameraSurfaceWorldY, sectionScale);
                const int cameraSurfaceSectionY = floorDivInt(cameraSurfaceTierY, it->second.size);
                return std::abs(k.coord.y - cameraSurfaceSectionY);
            };
            auto continuationPriority = [&](const VoxelSectionKey& k) {
                auto progressIt = g_treeSectionProgress.find(k);
                if (progressIt != g_treeSectionProgress.end()) {
                    const int phase = progressIt->second.phase;
                    if (phase > 0) return 2;
                }
                return g_treePendingDependencies.count(k) > 0 ? 1 : 0;
            };

            if (!immediateSections.empty()) {
                std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> immediateSet;
                immediateSet.reserve(immediateSections.size());
                for (const auto& key : immediateSections) immediateSet.insert(key);
                dirtySections.erase(
                    std::remove_if(
                        dirtySections.begin(),
                        dirtySections.end(),
                        [&](const VoxelSectionKey& k) { return immediateSet.count(k) > 0; }
                    ),
                    dirtySections.end()
                );
            }

            const int regularBudget = std::max(0, sectionBudget);
            if (regularBudget == 0) {
                dirtySections.clear();
            } else if (static_cast<int>(dirtySections.size()) > regularBudget && baseSystem.player) {
                std::nth_element(
                    dirtySections.begin(),
                    dirtySections.begin() + regularBudget,
                    dirtySections.end(),
                    [&](const VoxelSectionKey& a, const VoxelSectionKey& b) {
                        if (prioritizeContinuations) {
                            const int aPriority = continuationPriority(a);
                            const int bPriority = continuationPriority(b);
                            if (aPriority != bPriority) return aPriority > bPriority;
                        }
                        const float ad2 = dist2ToCam(a);
                        const float bd2 = dist2ToCam(b);
                        if (ad2 != bd2) return ad2 < bd2;
                        const int ay = yDistToSurface(a);
                        const int by = yDistToSurface(b);
                        if (ay != by) return ay < by;
                        if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
                        if (a.coord.y != b.coord.y) return a.coord.y < b.coord.y;
                        return a.coord.z < b.coord.z;
                    }
                );
                dirtySections.resize(static_cast<size_t>(regularBudget));
            } else if (static_cast<int>(dirtySections.size()) > regularBudget) {
                dirtySections.resize(static_cast<size_t>(regularBudget));
            }

            if (!immediateSections.empty()) {
                immediateSections.insert(
                    immediateSections.end(),
                    dirtySections.begin(),
                    dirtySections.end()
                );
                dirtySections.swap(immediateSections);
            }
        }

        const float foliageTimeBudgetMs = std::max(0.0f, getRegistryFloat(baseSystem, "TreeFoliageMaxMsPerFrame", 0.0f));
        const float foliageSectionBudgetMs = std::max(0.0f, getRegistryFloat(baseSystem, "TreeFoliageMaxMsPerSection", 1.25f));
        const int foliageMinSectionsBeforeTimeCap = std::max(1, getRegistryInt(baseSystem, "TreeFoliageMinSectionsPerFrame", 1));
        const bool foliageImmediateExemptFromTimeBudget = getRegistryBool(
            baseSystem,
            "TreeFoliageImmediateExemptFromTimeBudget",
            false
        );
        const int foliageImmediateExemptSections = foliageImmediateExemptFromTimeBudget
            ? immediateForcedCount
            : 0;
        std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> immediateSectionSet;
        immediateSectionSet.reserve(immediateSections.size());
        for (const auto& immediateKey : immediateSections) {
            immediateSectionSet.insert(immediateKey);
        }
        int processedSections = 0;
        int deferredByTimeBudget = 0;
        int scannedColumns = 0;
        int candidateColumns = 0;
        int placedTrees = 0;
        int skippedOutOfSection = 0;
        int skippedNonLand = 0;
        int skippedMissingGround = 0;
        int blockedByDependencies = 0;
        int blockedByColumn = 0;
        int blockedBySpacing = 0;
        for (size_t keyIndex = 0; keyIndex < dirtySections.size(); ++keyIndex) {
            if (!forceCompleteActive
                && foliageTimeBudgetMs > 0.0f
                && processedSections >= (foliageMinSectionsBeforeTimeCap + foliageImmediateExemptSections)) {
                float elapsedMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - updateStart
                ).count();
                if (elapsedMs >= foliageTimeBudgetMs) {
                    for (size_t deferredIndex = keyIndex; deferredIndex < dirtySections.size(); ++deferredIndex) {
                        const VoxelSectionKey deferredKey = dirtySections[deferredIndex];
                        g_treePendingSections.insert(deferredKey);
                        deferredByTimeBudget += 1;
                    }
                    break;
                }
            }
            const auto& key = dirtySections[keyIndex];
            if (!isFoliageEligibleSection(key)) {
                g_treePendingSections.erase(key);
                g_treePendingDependencies.erase(key);
                g_treeSurfaceAppliedVersion.erase(key);
                g_treeBackfillVisited.erase(key);
                g_treeSectionProgress.erase(key);
                continue;
            }
            processedSections += 1;
            const bool forceCompleteSection = (g_treeForceCompleteSections.count(key) > 0);
            if (!TerrainSystemLogic::IsSectionTerrainReady(key)) {
                g_treePendingSections.insert(key);
                continue;
            }
            const bool queuedFromDirty = g_treePendingSections.count(key) > 0;
            TreeSectionProgress& progress = g_treeSectionProgress[key];
            const bool forceBackfill = backfillLoaded
                && !queuedFromDirty
                && progress.phase <= 0
                && !progress.startedAsBackfill;
            if (forceBackfill) {
                progress.startedAsBackfill = true;
            }
            auto sectionIt = voxelWorld.sections.find(key);
            if (sectionIt == voxelWorld.sections.end()) continue;
            const int sectionTier = 0;
            const int sectionScale = sectionScaleForTier(0);

            auto appliedIt = g_treeAppliedVersion.find(key);
            const bool waitingDependencies = g_treePendingDependencies.count(key) > 0;
            // Pending sections should retry when they are explicitly dirtied by dependency updates.
            // Do not force full retries just because backfill selected them.
            const bool retryPendingNow = waitingDependencies && queuedFromDirty;
            if (!forceBackfill
                && !retryPendingNow
                && progress.phase <= 0
                && appliedIt != g_treeAppliedVersion.end()
                && appliedIt->second == sectionIt->second.editVersion) {
                g_treeSurfaceAppliedVersion[key] = sectionIt->second.editVersion;
                updateSurfaceFoliageLifecycle(key, true);
                if (queuedFromDirty) {
                    g_treePendingSections.erase(key);
                }
                g_treeSectionProgress.erase(key);
                continue;
            }

            // Once a section has already been processed by this system, treat any newer
            // editVersion as user/gameplay edits and do not auto-regrow trees/foliage into it.
            // First-time sections are still allowed even if editVersion > 1, because
            // neighboring tree writes can bump version before the section's first pass.
            if (!forceBackfill
                && !retryPendingNow
                && progress.phase <= 0
                && appliedIt != g_treeAppliedVersion.end()
                && sectionIt->second.editVersion > appliedIt->second) {
                g_treeAppliedVersion[key] = sectionIt->second.editVersion;
                g_treeSurfaceAppliedVersion[key] = sectionIt->second.editVersion;
                updateSurfaceFoliageLifecycle(key, true);
                if (queuedFromDirty) {
                    g_treePendingSections.erase(key);
                }
                g_treeSectionProgress.erase(key);
                continue;
            }

            bool modified = false;
            bool unresolvedDependencies = false;
            std::unordered_set<glm::ivec3, IVec3Hash> touchedSections;
            const int sectionSize = sectionIt->second.size;
            const glm::ivec3 sectionCoord = key.coord;
            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;
            const int scanMinX = minX;
            const int scanMinZ = minZ;
            const int scanMaxX = maxX;
            const int scanMaxZ = maxZ;
            // Keep the scan resumable so one tree-heavy section cannot monopolize the frame.
            const long long scanSpanX = static_cast<long long>(scanMaxX) - static_cast<long long>(scanMinX) + 1ll;
            const long long scanSpanZ = static_cast<long long>(scanMaxZ) - static_cast<long long>(scanMinZ) + 1ll;
            const long long fullColumns = std::max(1ll, scanSpanX * scanSpanZ);
            const long long maxInt = static_cast<long long>(std::numeric_limits<int>::max());
            const int configuredTreeColumnsPerSlice = getRegistryInt(
                baseSystem,
                "TreeFoliageTreeColumnsPerSlice",
                static_cast<int>(std::min(fullColumns, maxInt))
            );
            int treeColumnsPerSlice = std::max(1, configuredTreeColumnsPerSlice);
            if (forceCompleteSection) {
                treeColumnsPerSlice = std::max(
                    treeColumnsPerSlice,
                    getRegistryInt(baseSystem, "TreeFoliageForceCompleteTreeColumnsPerSlice", treeColumnsPerSlice)
                );
            }
            treeColumnsPerSlice = static_cast<int>(std::min<long long>(treeColumnsPerSlice, fullColumns));
            const long long surfaceColumns = static_cast<long long>(sectionSize) * static_cast<long long>(sectionSize);
            int configuredGroundColumnsPerSlice = std::max(
                1,
                getRegistryInt(baseSystem, "TreeFoliageGroundColumnsPerSlice", 32)
            );
            if (forceCompleteSection) {
                configuredGroundColumnsPerSlice = std::max(
                    configuredGroundColumnsPerSlice,
                    getRegistryInt(
                        baseSystem,
                        "TreeFoliageForceCompleteGroundColumnsPerSlice",
                        static_cast<int>(std::min(surfaceColumns, maxInt))
                    )
                );
            }
            const int groundFoliageColumnsPerSlice = static_cast<int>(std::min<long long>(
                configuredGroundColumnsPerSlice,
                std::max(1ll, surfaceColumns)
            ));

            auto commitTouchedSections = [&]() {
                if (!modified) return;
                touchedSections.insert(sectionCoord);
                for (const auto& touched : touchedSections) {
                    VoxelSectionKey touchedKey{touched};
                    auto touchedIt = voxelWorld.sections.find(touchedKey);
                    if (touchedIt == voxelWorld.sections.end()) continue;
                    touchedIt->second.editVersion += 1;
                    touchedIt->second.dirty = true;
                    voxelWorld.markSectionDirty(touchedKey);
                }
                if (getRegistryBool(baseSystem, "TreeFoliagePriorityRemesh", true)) {
                    const glm::ivec3 requestCell =
                        (sectionCoord * sectionSize + glm::ivec3(sectionSize / 2)) * sectionScale;
                    VoxelMeshingSystemLogic::RequestPriorityVoxelRemesh(baseSystem, prototypes, requestCell);
                }
                modified = false;
                touchedSections.clear();
            };

            const auto sectionStart = std::chrono::steady_clock::now();
            auto sectionTimeExceeded = [&]() -> bool {
                if (forceCompleteSection) return false;
                if (foliageSectionBudgetMs <= 0.0f) return false;
                const float elapsedMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - sectionStart
                ).count();
                return elapsedMs >= foliageSectionBudgetMs;
            };
            auto deferSection = [&]() {
                progress.unresolvedDependencies = unresolvedDependencies;
                g_treePendingSections.insert(key);
                deferredByTimeBudget += 1;
            };
            auto markSurfaceReadyIfComplete = [&]() {
                if (unresolvedDependencies) return;
                VoxelChunkLifecycleState* statePtr = voxelWorld.findChunkState(key);
                if (statePtr && statePtr->surfaceFoliageComplete) return;
                auto currentSectionIt = voxelWorld.sections.find(key);
                if (currentSectionIt != voxelWorld.sections.end()) {
                    g_treeSurfaceAppliedVersion[key] = currentSectionIt->second.editVersion;
                }
                updateSurfaceFoliageLifecycle(key, true);
            };
            auto runGroundFoliagePass = [&]() -> bool {
                const bool complete = writeGroundFoliageToSection(
                    prototypes,
                    worldCtx,
                    voxelWorld,
                    sectionTier,
                    sectionCoord,
                    sectionSize,
                    sectionScale,
                    grassProto ? grassProto->prototypeID : -1,
                    shortGrassProto ? shortGrassProto->prototypeID : -1,
                    grassProtoBiome1 ? grassProtoBiome1->prototypeID : -1,
                    shortGrassProtoBiome1 ? shortGrassProtoBiome1->prototypeID : -1,
                    grassProtoBiome3 ? grassProtoBiome3->prototypeID : -1,
                    grassPrototypeBiome4IDs,
                    grassPrototypeBiome4Count,
                    grassCoverProtoX ? grassCoverProtoX->prototypeID : -1,
                    grassCoverProtoZ ? grassCoverProtoZ->prototypeID : -1,
                    grassCoverMeadowProtoX ? grassCoverMeadowProtoX->prototypeID : -1,
                    grassCoverMeadowProtoZ ? grassCoverMeadowProtoZ->prototypeID : -1,
                    grassCoverForestProtoX ? grassCoverForestProtoX->prototypeID : -1,
                    grassCoverForestProtoZ ? grassCoverForestProtoZ->prototypeID : -1,
                    grassCoverJungleProtoX ? grassCoverJungleProtoX->prototypeID : -1,
                    grassCoverJungleProtoZ ? grassCoverJungleProtoZ->prototypeID : -1,
                    grassCoverBareProtoX ? grassCoverBareProtoX->prototypeID : -1,
                    grassCoverBareProtoZ ? grassCoverBareProtoZ->prototypeID : -1,
                    grassCoverDesertProtoX ? grassCoverDesertProtoX->prototypeID : -1,
                    grassCoverDesertProtoZ ? grassCoverDesertProtoZ->prototypeID : -1,
                    pebblePatchProtoX ? pebblePatchProtoX->prototypeID : -1,
                    pebblePatchProtoZ ? pebblePatchProtoZ->prototypeID : -1,
                    autumnLeafPatchProtoX ? autumnLeafPatchProtoX->prototypeID : -1,
                    autumnLeafPatchProtoZ ? autumnLeafPatchProtoZ->prototypeID : -1,
                    deadLeafPatchProtoX ? deadLeafPatchProtoX->prototypeID : -1,
                    deadLeafPatchProtoZ ? deadLeafPatchProtoZ->prototypeID : -1,
                    lilypadPatchProtoX ? lilypadPatchProtoX->prototypeID : -1,
                    lilypadPatchProtoZ ? lilypadPatchProtoZ->prototypeID : -1,
                    bareBranchProtoX ? bareBranchProtoX->prototypeID : -1,
                    bareBranchProtoZ ? bareBranchProtoZ->prototypeID : -1,
                    miniPineBottomProto ? miniPineBottomProto->prototypeID : -1,
                    miniPineTopProto ? miniPineTopProto->prototypeID : -1,
                    miniPineTripleBottomProto ? miniPineTripleBottomProto->prototypeID : -1,
                    miniPineTripleMiddleProto ? miniPineTripleMiddleProto->prototypeID : -1,
                    miniPineTripleTopProto ? miniPineTripleTopProto->prototypeID : -1,
                    flaxDoubleBottomProto ? flaxDoubleBottomProto->prototypeID : -1,
                    flaxDoubleTopProto ? flaxDoubleTopProto->prototypeID : -1,
                    hempDoubleBottomProto ? hempDoubleBottomProto->prototypeID : -1,
                    hempDoubleTopProto ? hempDoubleTopProto->prototypeID : -1,
                    blueFlowerPrototypeIDs,
                    blueFlowerPrototypeCount,
                    blueRareFlowerProto ? blueRareFlowerProto->prototypeID : -1,
                    flaxRareFlowerProto ? flaxRareFlowerProto->prototypeID : -1,
                    hempRareFlowerProto ? hempRareFlowerProto->prototypeID : -1,
                    fernRareFlowerProto ? fernRareFlowerProto->prototypeID : -1,
                    coniferMushroomPrototypeIDs,
                    coniferMushroomPrototypeCount,
                    jungleCoffeePrototypeIDs,
                    jungleCoffeePrototypeCount,
                    anyBiomePlantPrototypeIDs,
                    anyBiomePlantPrototypeCount,
                    meadowPlantPrototypeIDs,
                    meadowPlantPrototypeCount,
                    flowerProto ? flowerProto->prototypeID : -1,
                    succulentProto ? succulentProto->prototypeID : -1,
                    succulentProtoV2 ? succulentProtoV2->prototypeID : -1,
                    jungleOrangeUnderLeafProto ? jungleOrangeUnderLeafProto->prototypeID : -1,
                    stickProtoX ? stickProtoX->prototypeID : -1,
                    stickProtoZ ? stickProtoZ->prototypeID : -1,
                    stickWinterProtoX ? stickWinterProtoX->prototypeID : -1,
                    stickWinterProtoZ ? stickWinterProtoZ->prototypeID : -1,
                    leafProto ? leafProto->prototypeID : -1,
                    waterPrototypeID,
                    foliageSpec,
                    groundFoliageColumnsPerSlice,
                    progress.surfaceScanTierX,
                    progress.surfaceScanTierZ,
                    unresolvedDependencies,
                    modified
                );
                commitTouchedSections();
                if (complete) {
                    progress.surfaceFoliageSeeded = true;
                    progress.surfaceScanTierX = std::numeric_limits<int>::min();
                    progress.surfaceScanTierZ = std::numeric_limits<int>::min();
                }
                return complete;
            };

            bool finishedSection = false;
            while (!finishedSection) {
                if (progress.phase <= 0) {
                    int tierZ = progress.scanTierZ;
                    int tierX = progress.scanTierX;
                    if (tierZ == std::numeric_limits<int>::min() || tierX == std::numeric_limits<int>::min()) {
                        tierZ = scanMinZ;
                        tierX = scanMinX;
                    }
                    int columnsProcessed = 0;
                    bool treeScanDone = false;
                        while (tierZ <= scanMaxZ && columnsProcessed < treeColumnsPerSlice) {
                            while (tierX <= scanMaxX && columnsProcessed < treeColumnsPerSlice) {
                                columnsProcessed += 1;
                                scannedColumns += 1;
                                const int worldX = tierX * sectionScale;
                                const int worldZ = tierZ * sectionScale;
                            const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(
                                worldCtx,
                                static_cast<float>(worldX),
                                static_cast<float>(worldZ)
                            );
                            const bool islandQuadrants =
                                (worldCtx.expanse.islandRadius > 0.0f) && worldCtx.expanse.secondaryBiomeEnabled;
                            if (isWithinJungleVolcano(worldCtx.expanse, biomeID, worldX, worldZ)) {
                                tierX += 1;
                                continue;
                            }
                            if (biomeID == 2 || biomeID == 5) {
                                tierX += 1;
                                continue;
                            }

                            const bool wantsPineTree = (biomeID == 0) && shouldSpawnPine(worldX, worldZ, baseSpec);
                            const bool wantsMeadowTree =
                                meadowTreeGenerationEnabled
                                && (biomeID == 1)
                                && ((hash2D(worldX + 571, worldZ - 313) % static_cast<uint32_t>(meadowTreeSpawnModulo)) == 0u);
                            const bool wantsJungleTree =
                                islandQuadrants && (biomeID == 0 || biomeID == 3)
                                && ((hash2D(worldX + 173, worldZ - 911) % static_cast<uint32_t>(jungleTreeSpawnModulo)) == 0u);
                            const bool wantsBareTree =
                                (biomeID == 4)
                                && ((hash2D(worldX - 433, worldZ + 1259) % static_cast<uint32_t>(bareTreeSpawnModulo)) == 0u);
                            if (!wantsPineTree && !wantsMeadowTree && !wantsJungleTree && !wantsBareTree) {
                                tierX += 1;
                                continue;
                            }
                            candidateColumns += 1;

                            float terrainHeight = 0.0f;
                            const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                                worldCtx,
                                static_cast<float>(worldX),
                                static_cast<float>(worldZ),
                                terrainHeight
                            );
                            if (!isLand) {
                                skippedNonLand += 1;
                                tierX += 1;
                                continue;
                            }
                            const int groundY = floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                            const glm::ivec3 groundCell(tierX, groundY, tierZ);
                            if (!cellBelongsToSection(groundCell, sectionCoord, sectionSize)) {
                                skippedOutOfSection += 1;
                                tierX += 1;
                                continue;
                            }

                            if (wantsPineTree || wantsMeadowTree) {
                                PineSpec treeSpec = pineSpecForCell(
                                    baseSpec,
                                    worldX,
                                    worldZ,
                                    pineMinTrunkHeight,
                                    pineMaxTrunkHeight
                                );
                                treeSpec = scaledPineSpecForTier(treeSpec, sectionScale);

                                const int trunkPrototypeID = ((hash2D(worldX, worldZ) >> 8u) & 1u) == 0u
                                    ? trunkProtoA->prototypeID
                                    : trunkProtoB->prototypeID;
                                const int nubPrototypeID = (trunkPrototypeID == trunkProtoA->prototypeID)
                                    ? trunkProtoB->prototypeID
                                    : trunkProtoA->prototypeID;
                                const int topPrototypeID = topLogPrototypeFor(prototypes, nubPrototypeID);
                                const int pineTreeMinY = groundY + 1;
                                const int pineCanopyTopY = groundY + treeSpec.trunkHeight - treeSpec.canopyOffset
                                    + treeSpec.canopyLayers - 1;
                                const int pineTreeMaxY = std::max(groundY + treeSpec.trunkHeight, pineCanopyTopY);
                                if (getBlockAt(voxelWorld, groundCell) == 0u) {
                                    skippedMissingGround += 1;
                                    tierX += 1;
                                    continue;
                                }

                                if (!arePineTreeSectionsReady(
                                        sectionSize,
                                        tierX,
                                            groundY,
                                            tierZ,
                                            treeSpec)) {
                                    unresolvedDependencies = true;
                                    blockedByDependencies += 1;
                                    tierX += 1;
                                    continue;
                                }

                                if (!trunkColumnCanExist(voxelWorld,
                                                         sectionTier,
                                                         sectionCoord,
                                                         sectionSize,
                                                         trunkProtoA->prototypeID,
                                                         trunkProtoB->prototypeID,
                                                         tierX,
                                                         groundY,
                                                         tierZ,
                                                         treeSpec.trunkHeight)) {
                                    blockedByColumn += 1;
                                    tierX += 1;
                                    continue;
                                }

                                if (hasNearbyConflictingTrunk(voxelWorld,
                                                              sectionTier,
                                                              trunkProtoA->prototypeID,
                                                              trunkProtoB->prototypeID,
                                                              tierX,
                                                              groundY + 1,
                                                              tierZ,
                                                              treeSpec.trunkExclusionRadius)) {
                                    blockedBySpacing += 1;
                                    tierX += 1;
                                    continue;
                                }

                                writeTreeToWorld(voxelWorld,
                                                 prototypes,
                                                 sectionTier,
                                                 sectionSize,
                                                 sectionCoord,
                                                 trunkPrototypeID,
                                                 topPrototypeID,
                                                 nubPrototypeID,
                                                 leafProto->prototypeID,
                                                 leafFanPineProto ? leafFanPineProto->prototypeID : -1,
                                                 (pineBeeHiveProto
                                                     && wantsPineTree
                                                     && ((hash2D(worldX - 6481, worldZ + 2207)
                                                          % static_cast<uint32_t>(pineBeeHiveSpawnModulo)) == 0u))
                                                     ? pineBeeHiveProto->prototypeID
                                                     : -1,
                                                 trunkColor,
                                                 leafColor,
                                                 tierX,
                                                 groundY,
                                                 tierZ,
                                                 treeSpec,
                                                 touchedSections,
                                                 modified);
                                placedTrees += 1;
                            } else if (wantsBareTree) {
                                const int bareTrunkID = bareTrunkProto ? bareTrunkProto->prototypeID : trunkProtoA->prototypeID;
                                const uint32_t bareSeed = hash2D(worldX + 6097, worldZ - 2953);
                                const int bareHeightSpan = bareTreeTrunkMax - bareTreeTrunkMin + 1;
                                const int bareTrunkHeight = bareTreeTrunkMin + static_cast<int>(
                                    bareSeed % static_cast<uint32_t>(bareHeightSpan)
                                );
                                const int bareTreeMinY = groundY + 1;
                                const int bareTreeMaxY = groundY + bareTrunkHeight + 2;
                                if (getBlockAt(voxelWorld, groundCell) == 0u) {
                                    skippedMissingGround += 1;
                                    tierX += 1;
                                    continue;
                                }

                                if (!areBareTreeSectionsReady(
                                        sectionSize,
                                        tierX,
                                        groundY,
                                            tierZ,
                                            bareTrunkHeight,
                                            bareSeed)) {
                                    unresolvedDependencies = true;
                                    blockedByDependencies += 1;
                                    tierX += 1;
                                    continue;
                                }

                                if (!trunkColumnCanExist(voxelWorld,
                                                         sectionTier,
                                                         sectionCoord,
                                                         sectionSize,
                                                         bareTrunkID,
                                                         bareTrunkID,
                                                         tierX,
                                                         groundY,
                                                         tierZ,
                                                         bareTrunkHeight)) {
                                    blockedByColumn += 1;
                                    tierX += 1;
                                    continue;
                                }

                                if (hasNearbyConflictingTrunk(voxelWorld,
                                                              sectionTier,
                                                              bareTrunkID,
                                                              bareTrunkID,
                                                              tierX,
                                                              groundY + 1,
                                                              tierZ,
                                                              2)) {
                                    blockedBySpacing += 1;
                                    tierX += 1;
                                    continue;
                                }

                                const uint32_t bareTrunkColor = packColor(glm::vec3(0.35f, 0.29f, 0.23f));
                                const uint32_t bareBranchColor = packColor(glm::vec3(0.42f, 0.35f, 0.27f));
                                writeBareTreeToWorld(
                                    voxelWorld,
                                    sectionTier,
                                    sectionSize,
                                    sectionCoord,
                                    bareTrunkID,
                                    bareBranchProtoX ? bareBranchProtoX->prototypeID : -1,
                                    bareBranchProtoZ ? bareBranchProtoZ->prototypeID : -1,
                                    wallBranchLongProtoPosX ? wallBranchLongProtoPosX->prototypeID : -1,
                                    wallBranchLongProtoNegX ? wallBranchLongProtoNegX->prototypeID : -1,
                                    wallBranchLongProtoPosZ ? wallBranchLongProtoPosZ->prototypeID : -1,
                                    wallBranchLongProtoNegZ ? wallBranchLongProtoNegZ->prototypeID : -1,
                                    wallBranchLongTipProtoPosX ? wallBranchLongTipProtoPosX->prototypeID : -1,
                                    wallBranchLongTipProtoNegX ? wallBranchLongTipProtoNegX->prototypeID : -1,
                                    wallBranchLongTipProtoPosZ ? wallBranchLongTipProtoPosZ->prototypeID : -1,
                                    wallBranchLongTipProtoNegZ ? wallBranchLongTipProtoNegZ->prototypeID : -1,
                                    bareTrunkColor,
                                    bareBranchColor,
                                    tierX,
                                    groundY,
                                    tierZ,
                                    bareTrunkHeight,
                                    bareSeed,
                                    touchedSections,
                                    modified
                                );
                                placedTrees += 1;
                            } else {
                                const int jungleTrunkID = jungleTrunkProto ? jungleTrunkProto->prototypeID : trunkProtoA->prototypeID;
                                const uint32_t jungleSeed = hash2D(worldX + 4041, worldZ - 7927);
                                const int jungleHeightSpan = jungleTreeTrunkMax - jungleTreeTrunkMin + 1;
                                const int jungleTrunkHeight = jungleTreeTrunkMin
                                    + static_cast<int>(jungleSeed % static_cast<uint32_t>(jungleHeightSpan));
                                const int jungleCanopyRadius = std::max(2, jungleTreeCanopyRadius);
                                const int jungleTreeMinY = groundY + 1;
                                const int jungleTreeMaxY = groundY + jungleTrunkHeight + 2 + jungleCanopyRadius;
                                if (getBlockAt(voxelWorld, groundCell) == 0u) {
                                    skippedMissingGround += 1;
                                    tierX += 1;
                                    continue;
                                }

                                if (!areJungleTreeSectionsReady(
                                        sectionSize,
                                        tierX,
                                        groundY,
                                            tierZ,
                                            jungleTrunkHeight,
                                            jungleTreeCanopyRadius)) {
                                    unresolvedDependencies = true;
                                    blockedByDependencies += 1;
                                    tierX += 1;
                                    continue;
                                }

                                if (!trunkColumnCanExist(voxelWorld,
                                                         sectionTier,
                                                         sectionCoord,
                                                         sectionSize,
                                                         jungleTrunkID,
                                                         jungleTrunkID,
                                                         tierX,
                                                         groundY,
                                                         tierZ,
                                                         jungleTrunkHeight)) {
                                    blockedByColumn += 1;
                                    tierX += 1;
                                    continue;
                                }

                                if (hasNearbyConflictingTrunk(voxelWorld,
                                                              sectionTier,
                                                              jungleTrunkID,
                                                              jungleTrunkID,
                                                              tierX,
                                                              groundY + 1,
                                                              tierZ,
                                                              2)) {
                                    blockedBySpacing += 1;
                                    tierX += 1;
                                    continue;
                                }

                                const uint32_t jungleTrunkColor = packColor(glm::vec3(0.36f, 0.24f, 0.15f));
                                writeJungleTreeToWorld(
                                    voxelWorld,
                                    sectionTier,
                                    sectionSize,
                                    sectionCoord,
                                    jungleTrunkID,
                                    leafProto->prototypeID,
                                    leafFanOakProto ? leafFanOakProto->prototypeID : -1,
                                    jungleLeafPrototypeIDs,
                                    jungleLeafPrototypeCount,
                                    jungleTrunkColor,
                                    leafColor,
                                    tierX,
                                    groundY,
                                    tierZ,
                                    jungleTrunkHeight,
                                    jungleTreeCanopyRadius,
                                    touchedSections,
                                    modified
                                );
                                placedTrees += 1;
                            }

                            tierX += 1;
                        }
                        if (tierX > scanMaxX) {
                            tierX = scanMinX;
                            tierZ += 1;
                        }
                    }

                    if (tierZ > scanMaxZ) {
                        treeScanDone = true;
                    }
                    commitTouchedSections();
                    if (!treeScanDone) {
                        progress.phase = 0;
                        progress.scanTierX = tierX;
                        progress.scanTierZ = tierZ;
                        deferSection();
                        finishedSection = true;
                        continue;
                    }
                    progress.phase = 1;
                    progress.scanTierX = std::numeric_limits<int>::min();
                    progress.scanTierZ = std::numeric_limits<int>::min();
                    if (sectionTimeExceeded()) {
                        deferSection();
                        finishedSection = true;
                        continue;
                    }
                }

                if (progress.phase <= 1) {
                    convertExposedLeafShellInSection(
                        voxelWorld,
                        prototypes,
                        sectionTier,
                        sectionCoord,
                        sectionSize,
                        leafProto ? leafProto->prototypeID : -1,
                        leafFanPineProto ? leafFanPineProto->prototypeID : -1,
                        leafFanOakProto ? leafFanOakProto->prototypeID : -1,
                        leafColor,
                        touchedSections,
                        modified
                    );
                    commitTouchedSections();
                    progress.phase = 2;
                    if (sectionTimeExceeded()) {
                        deferSection();
                        finishedSection = true;
                        continue;
                    }
                }
                if (progress.phase <= 2) {
                    if (!progress.surfaceFoliageSeeded) {
                        if (!runGroundFoliagePass()) {
                            deferSection();
                            finishedSection = true;
                            continue;
                        }
                    }
                    progress.phase = 3;
                    if (sectionTimeExceeded()) {
                        deferSection();
                        finishedSection = true;
                        continue;
                    }
                }
                if (progress.phase <= 3) {
                    writeWaterFoliageToSection(
                        prototypes,
                        voxelWorld,
                        sectionTier,
                        sectionCoord,
                        sectionSize,
                        sectionScale,
                        kelpProto ? kelpProto->prototypeID : -1,
                        kelpTileIndex,
                        seaUrchinProtoX ? seaUrchinProtoX->prototypeID : -1,
                        seaUrchinProtoZ ? seaUrchinProtoZ->prototypeID : -1,
                        sandDollarProtoX ? sandDollarProtoX->prototypeID : -1,
                        sandDollarProtoZ ? sandDollarProtoZ->prototypeID : -1,
                        waterPrototypeID,
                        foliageSpec,
                        modified
                    );
                    commitTouchedSections();
                    progress.phase = 4;
                    if (sectionTimeExceeded()) {
                        deferSection();
                        finishedSection = true;
                        continue;
                    }
                }
                if (progress.phase <= 4) {
                    if (desertCactusEnabled) {
                        writeDesertCactusToSection(
                            prototypes,
                            worldCtx,
                            voxelWorld,
                            sectionTier,
                            sectionCoord,
                            sectionSize,
                            sectionScale,
                            cactusProtoA ? cactusProtoA->prototypeID : -1,
                            cactusProtoB ? cactusProtoB->prototypeID : -1,
                            cactusProtoAX ? cactusProtoAX->prototypeID : -1,
                            cactusProtoAZ ? cactusProtoAZ->prototypeID : -1,
                            cactusProtoBX ? cactusProtoBX->prototypeID : -1,
                            cactusProtoBZ ? cactusProtoBZ->prototypeID : -1,
                            cactusProtoAJunctionX ? cactusProtoAJunctionX->prototypeID : -1,
                            cactusProtoAJunctionZ ? cactusProtoAJunctionZ->prototypeID : -1,
                            cactusProtoBJunctionX ? cactusProtoBJunctionX->prototypeID : -1,
                            cactusProtoBJunctionZ ? cactusProtoBJunctionZ->prototypeID : -1,
                            waterPrototypeID,
                            desertCactusSpawnModulo,
                            unresolvedDependencies,
                            modified
                        );
                    }
                    commitTouchedSections();
                    progress.phase = 5;
                    markSurfaceReadyIfComplete();
                    if (sectionTimeExceeded()) {
                        deferSection();
                        finishedSection = true;
                        continue;
                    }
                }

                if (progress.phase >= 5) {
                    markSurfaceReadyIfComplete();
                    progress.phase = 10;
                }

                const bool canFinalizeSection = !unresolvedDependencies;
                auto finalSectionIt = voxelWorld.sections.find(key);
                if (canFinalizeSection && finalSectionIt != voxelWorld.sections.end()) {
                    g_treeAppliedVersion[key] = finalSectionIt->second.editVersion;
                    g_treeSurfaceAppliedVersion[key] = finalSectionIt->second.editVersion;
                }
                if (canFinalizeSection) {
                    g_treePendingDependencies.erase(key);
                    g_treePendingSections.erase(key);
                    if (progress.startedAsBackfill) {
                        g_treeBackfillVisited.insert(key);
                    }
                } else {
                    g_treePendingDependencies.insert(key);
                    g_treePendingSections.insert(key);
                }
                g_treeSectionProgress.erase(key);
                finishedSection = true;
            }
        }

        g_treePerfStats.pendingSections = g_treePendingSections.size();
        g_treePerfStats.pendingDependencies = g_treePendingDependencies.size();
        g_treePerfStats.backfillVisited = g_treeBackfillVisited.size();
        g_treePerfStats.selectedSections = static_cast<int>(dirtySections.size());
        g_treePerfStats.processedSections = processedSections;
        g_treePerfStats.deferredByTimeBudget = deferredByTimeBudget;
        g_treePerfStats.backfillAppended = backfillAppended;
        g_treePerfStats.scannedColumns = scannedColumns;
        g_treePerfStats.candidateColumns = candidateColumns;
        g_treePerfStats.placedTrees = placedTrees;
        g_treePerfStats.skippedOutOfSection = skippedOutOfSection;
        g_treePerfStats.skippedNonLand = skippedNonLand;
        g_treePerfStats.skippedMissingGround = skippedMissingGround;
        g_treePerfStats.blockedByDependencies = blockedByDependencies;
        g_treePerfStats.blockedByColumn = blockedByColumn;
        g_treePerfStats.blockedBySpacing = blockedBySpacing;
        g_treePerfStats.backfillRan = backfillRan;
        g_treePerfStats.updateMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - updateStart
        ).count();
    }

    void UpdateTreeCanopyGeneration(BaseSystem&, std::vector<Entity>&, float, PlatformWindowHandle) {
    }
}
