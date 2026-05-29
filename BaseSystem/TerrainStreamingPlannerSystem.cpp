#pragma once

#include <array>
#include <numeric>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <random>
#include <cmath>
#include <chrono>
#include <limits>
#include <memory>
#include <glm/glm.hpp>

namespace HostLogic { const Entity* findPrototype(const std::string& name, const std::vector<Entity>& prototypes); EntityInstance CreateInstance(BaseSystem& baseSystem, int prototypeID, glm::vec3 position, glm::vec3 color); }
namespace ExpanseBiomeSystemLogic {
    bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight);
    int ResolveBiome(const WorldContext& worldCtx, float x, float z);
}
namespace TreeGenerationSystemLogic {
    bool ProcessSectionSurfaceFoliageNow(BaseSystem& baseSystem,
                                         std::vector<Entity>& prototypes,
                                         const VoxelSectionKey& key,
                                         int maxPasses);
}


namespace TerrainSystemLogic {

    constexpr int kBinaryCaveTileBits = 64;
    using BinaryCaveTile = std::array<uint64_t, kBinaryCaveTileBits>;

    class PerlinNoise3DLocal {
    public:
        explicit PerlinNoise3DLocal(int seed) {
            std::iota(permutation.begin(), permutation.begin() + 256, 0);
            std::mt19937 rng(seed);
            std::shuffle(permutation.begin(), permutation.begin() + 256, rng);
            for (int i = 0; i < 256; ++i) permutation[256 + i] = permutation[i];
        }

        float noise(float x, float y, float z) const {
            int X = static_cast<int>(std::floor(x)) & 255;
            int Y = static_cast<int>(std::floor(y)) & 255;
            int Z = static_cast<int>(std::floor(z)) & 255;

            x -= std::floor(x);
            y -= std::floor(y);
            z -= std::floor(z);

            float u = fade(x);
            float v = fade(y);
            float w = fade(z);

            int A = permutation[X] + Y;
            int AA = permutation[A] + Z;
            int AB = permutation[A + 1] + Z;
            int B = permutation[X + 1] + Y;
            int BA = permutation[B] + Z;
            int BB = permutation[B + 1] + Z;

            return lerp(w,
                lerp(v,
                    lerp(u, grad(permutation[AA], x, y, z),
                            grad(permutation[BA], x - 1, y, z)),
                    lerp(u, grad(permutation[AB], x, y - 1, z),
                            grad(permutation[BB], x - 1, y - 1, z))),
                lerp(v,
                    lerp(u, grad(permutation[AA + 1], x, y, z - 1),
                            grad(permutation[BA + 1], x - 1, y, z - 1)),
                    lerp(u, grad(permutation[AB + 1], x, y - 1, z - 1),
                            grad(permutation[BB + 1], x - 1, y - 1, z - 1))));
        }

    private:
        std::array<int, 512> permutation{};

        static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
        static float lerp(float t, float a, float b) { return a + t * (b - a); }
        static float grad(int hash, float x, float y, float z) {
            int h = hash & 15;
            float u = h < 8 ? x : y;
            float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
            return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
        }
    };

    struct BinaryCaveTileKey {
        int plane = 0;
        int x = 0;
        int y = 0;

        bool operator==(const BinaryCaveTileKey& other) const noexcept {
            return plane == other.plane && x == other.x && y == other.y;
        }
    };

    struct BinaryCaveTileKeyHash {
        std::size_t operator()(const BinaryCaveTileKey& key) const noexcept {
            std::size_t h = std::hash<int>()(key.plane);
            h ^= std::hash<int>()(key.x + 0x9e3779b9) + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(key.y - 0x7f4a7c15) + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct CaveFieldLocal {
        bool ready = false;
        bool enabled = true;
        bool building = false;
        int mode = 0; // 0=binary CA, 1=precomputed Perlin field
        int seedA = 0;
        int seedB = 0;
        int iterations = 3;
        int fillPercent = 50;
        int openThreshold = 5;
        int cellScale = 1;
        size_t maxCachedTiles = 512;
        glm::vec3 origin{0.0f};
        int step = 4;
        int dimX = 0;
        int dimY = 0;
        int dimZ = 0;
        size_t buildCursor = 0;
        size_t totalCount = 0;
        uint64_t lastBuildFrame = std::numeric_limits<uint64_t>::max();
        std::unique_ptr<PerlinNoise3DLocal> noiseA;
        std::unique_ptr<PerlinNoise3DLocal> noiseB;
        std::vector<uint8_t> a;
        std::vector<uint8_t> b;
        std::unordered_map<BinaryCaveTileKey, BinaryCaveTile, BinaryCaveTileKeyHash> tiles;
    };

    namespace {
        glm::vec3 GetColorLocal(WorldContext& worldCtx, const std::string& name, const glm::vec3& fallback) {
            if (worldCtx.colorLibrary.count(name)) return worldCtx.colorLibrary[name];
            return fallback;
        }

        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        struct VoxelStreamingState {
            std::vector<VoxelSectionKey> baseReady;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> baseReadySet;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> baseInProgress;
            std::vector<VoxelSectionKey> featureReady;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> featureReadySet;
            std::deque<VoxelSectionKey> featureContinuationReady;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> featureContinuationReadySet;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> featureInProgress;
            std::deque<VoxelSectionKey> surfaceReady;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> surfaceReadySet;
            size_t featureReadyHead = 0;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> desired;
            glm::ivec3 lastCenterSection = glm::ivec3(std::numeric_limits<int>::min());
            int lastRadius = std::numeric_limits<int>::min();
            int lastCpuViewYawBucket = std::numeric_limits<int>::min();
            bool lastCpuViewCullingEnabled = false;
            uint64_t frameCounter = 0;
            bool pendingDesiredRebuild = true;
        };

        struct VoxelDesiredRebuildState {
            bool active = false;
            int prevRadius = 0;
            bool tierPrepared = false;
            int radius = 0;
            int size = 0;
            int scale = 1;
            int minSectionY = 0;
            int maxSectionY = -1;
            int tierSurfaceCenterY = 0;
            std::vector<glm::ivec3> sectionOrderXZ;
            size_t orderCursor = 0;
            std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> desired;
            std::vector<VoxelSectionKey> desiredOrder;
        };

        struct VoxelSectionGenerationJob {
            int nextColumn = 0;
            bool wroteAny = false;
        };

        struct VoxelColumnGenerationJob {
            int nextColumn = 0;
            int nextFeatureSectionY = std::numeric_limits<int>::min();
            int nextSurfaceSectionY = std::numeric_limits<int>::min();
            int phase = 0; // 0 base, 1 decorators, 2 foliage, 3 publish render bridge
            bool wroteAny = false;
        };

        struct VoxelColumnStreamingState {
            std::vector<VoxelColumnKey> pending;
            std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> pendingSet;
            std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> inProgress;
            std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> desired;
            std::vector<VoxelColumnKey> desiredOrder;
            std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> generated;
            std::unordered_map<VoxelColumnKey, VoxelColumnGenerationJob, VoxelColumnKeyHash> jobs;
            glm::ivec2 lastCenterColumn = glm::ivec2(std::numeric_limits<int>::min());
            int lastRadius = std::numeric_limits<int>::min();
            int lastCpuViewYawBucket = std::numeric_limits<int>::min();
            bool lastCpuViewCullingEnabled = false;
            bool pendingDesiredRebuild = true;
        };

        struct VoxelStreamingPerfStats {
            size_t pending = 0;
            size_t desired = 0;
            size_t generated = 0;
            size_t jobs = 0;
            int stepped = 0;
            int built = 0;
            int consumed = 0;
            int skippedExisting = 0;
            int filteredOut = 0;
            int rescueSurfaceQueued = 0;
            int rescueMissingQueued = 0;
            int droppedByCap = 0;
            int reprioritized = 0;
            float prepMs = 0.0f;
            float generationMs = 0.0f;
            float desiredMs = 0.0f;
            float baseGenMs = 0.0f;
            float featureMs = 0.0f;
            float surfaceMs = 0.0f;
            float caveFieldMs = 0.0f;
            int schedulerPressure = 0;
            int desiredBudget = 0;
            int baseBudget = 0;
            int featureBudget = 0;
            int surfaceBudget = 0;
            float baseBudgetMs = 0.0f;
            float featureBudgetMs = 0.0f;
            float surfaceBudgetMs = 0.0f;
            size_t downstreamDirty = 0;
            size_t downstreamPrepared = 0;
            size_t downstreamUpload = 0;
            uint64_t caveFieldCellsBuilt = 0;
            uint64_t caveSamples = 0;
        };

        static VoxelStreamingState g_voxelStreaming;
        static VoxelColumnStreamingState g_voxelColumnStreaming;
        static VoxelDesiredRebuildState g_voxelDesiredRebuild;
        static std::unordered_map<VoxelSectionKey, VoxelSectionGenerationJob, VoxelSectionKeyHash> g_voxelSectionJobs;
        static VoxelStreamingPerfStats g_voxelStreamingPerfStats;
        static std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> g_voxelTerrainGenerated;
        static std::chrono::steady_clock::time_point g_lastVoxelPerf = std::chrono::steady_clock::now();
        static std::chrono::steady_clock::time_point g_lastTerrainSnapshot = std::chrono::steady_clock::now();
        static uint64_t g_voxelStreamingTotalStepped = 0;
        static uint64_t g_voxelStreamingTotalBuilt = 0;
        static uint64_t g_voxelStreamingTotalConsumed = 0;
        static uint64_t g_lastSnapshotBuiltTotal = 0;
        static size_t g_lastSnapshotGeneratedCount = 0;
        static float g_snapshotMaxPrepMs = 0.0f;
        static float g_snapshotMaxGenerationMs = 0.0f;
        static int g_snapshotLongGenerationCount = 0;
        static double g_snapshotStallSeconds = 0.0;
        static double g_snapshotMaxStallSeconds = 0.0;
        static uint64_t g_snapshotCount = 0;
        static uint64_t g_snapshotZeroBuildCount = 0;
        static uint64_t g_snapshotStalledBuildCount = 0;
        static uint64_t g_snapshotStallBurstCount = 0;
        static bool g_snapshotWasStalled = false;
        static double g_snapshotBuiltPerSecMin = 0.0;
        static double g_snapshotBuiltPerSecMax = 0.0;
        static size_t g_pendingResortCursor = 0;
        static std::vector<VoxelSectionKey> g_tier0RescueScanList;
        static size_t g_tier0RescueScanHead = 0;
        static int g_verticalDomainMode = 0; // 0=unset/off, 1=upper(expanse), 2=lower(depths)
        static std::string g_voxelLevelKey;
        static std::string g_caveConfigKey;
        static CaveFieldLocal g_caveField;
        static float g_caveFieldFrameMs = 0.0f;
        static uint64_t g_caveFieldFrameCellsBuilt = 0;
        static uint64_t g_caveFieldFrameSampleCount = 0;
        

        int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback);
        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback);
        std::string getRegistryString(const BaseSystem& baseSystem,
                                      const std::string& key,
                                      const std::string& fallback);

        inline uint8_t quantizeNoise01(float v) {
            return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
        }

        inline bool samplePerlinCaveNoiseDirect(float worldX, float worldY, float worldZ, float& outA, float& outB) {
            if (!g_caveField.noiseA || !g_caveField.noiseB) return false;
            const float v1 = (g_caveField.noiseA->noise(worldX / 64.0f, worldY / 48.0f, worldZ / 64.0f) + 1.0f) * 0.5f;
            const float v2 = (g_caveField.noiseB->noise(worldX / 128.0f, worldY / 128.0f, worldZ / 128.0f) + 1.0f) * 0.5f;
            outA = static_cast<float>(quantizeNoise01(v1)) / 255.0f;
            outB = static_cast<float>(quantizeNoise01(v2)) / 255.0f;
            return true;
        }

        inline uint64_t mixCaveHash(uint64_t v) {
            v += 0x9e3779b97f4a7c15ULL;
            v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
            v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
            return v ^ (v >> 31);
        }

        inline uint64_t caveCoordWord(int v) {
            return static_cast<uint64_t>(static_cast<int64_t>(v));
        }

        inline uint64_t binaryCaveColumnHash(int plane, int tileX, int tileY, int localX) {
            const int seed = (plane == 0) ? g_caveField.seedA : g_caveField.seedB;
            const int worldX = tileX * kBinaryCaveTileBits + localX;
            uint64_t h = caveCoordWord(worldX);
            h ^= caveCoordWord(tileY * kBinaryCaveTileBits) + 0x517cc1b727220a95ULL + (h << 6) + (h >> 2);
            h ^= static_cast<uint64_t>(seed) * 0x9e3779b185ebca87ULL;
            h ^= static_cast<uint64_t>(plane + 1) * 0x94d049bb133111ebULL;
            return mixCaveHash(h);
        }

        uint64_t makeInitialBinaryCaveColumn(int plane, int tileX, int tileY, int localX) {
            const uint64_t baseHash = binaryCaveColumnHash(plane, tileX, tileY, localX);
            if (g_caveField.fillPercent == 50) {
                return baseHash;
            }

            const uint64_t threshold = static_cast<uint64_t>(
                std::clamp(g_caveField.fillPercent, 1, 99) * 255 / 100
            );
            uint64_t column = 0;
            for (int y = 0; y < kBinaryCaveTileBits; ++y) {
                const uint64_t h = mixCaveHash(baseHash ^ (static_cast<uint64_t>(y) * 0xd2b74407b1ce6e93ULL));
                if ((h & 0xffULL) < threshold) {
                    column |= (1ULL << y);
                }
            }
            return column;
        }

        uint64_t caveCountAtLeastMask(const uint64_t count[4], int threshold) {
            const uint64_t b0 = count[0];
            const uint64_t b1 = count[1];
            const uint64_t b2 = count[2];
            const uint64_t b3 = count[3];
            switch (std::clamp(threshold, 1, 9)) {
                case 1: return b0 | b1 | b2 | b3;
                case 2: return b1 | b2 | b3;
                case 3: return b3 | b2 | (b1 & b0);
                case 4: return b2 | b3;
                case 5: return b3 | (b2 & (b1 | b0));
                case 6: return b3 | (b2 & b1);
                case 7: return b3 | (b2 & b1 & b0);
                case 8: return b3;
                case 9: return b3 & b0;
                default: return b3 | (b2 & (b1 | b0));
            }
        }

        size_t binaryCaveGridIndex(int x, int y) {
            return static_cast<size_t>(x + y * 3);
        }

        void smoothBinaryCaveGrid(std::array<BinaryCaveTile, 9>& grid) {
            std::array<BinaryCaveTile, 9> next{};
            for (int gy = 0; gy < 3; ++gy) {
                for (int gx = 0; gx < 3; ++gx) {
                    const size_t idx = binaryCaveGridIndex(gx, gy);
                    const BinaryCaveTile& tile = grid[idx];
                    BinaryCaveTile& outTile = next[idx];
                    const bool hasLeft = gx > 0;
                    const bool hasRight = gx < 2;
                    const bool hasAbove = gy > 0;
                    const bool hasBelow = gy < 2;
                    const BinaryCaveTile* leftTile = hasLeft ? &grid[binaryCaveGridIndex(gx - 1, gy)] : nullptr;
                    const BinaryCaveTile* rightTile = hasRight ? &grid[binaryCaveGridIndex(gx + 1, gy)] : nullptr;
                    const BinaryCaveTile* aboveTile = hasAbove ? &grid[binaryCaveGridIndex(gx, gy - 1)] : nullptr;
                    const BinaryCaveTile* belowTile = hasBelow ? &grid[binaryCaveGridIndex(gx, gy + 1)] : nullptr;

                    for (int x = 0; x < kBinaryCaveTileBits; ++x) {
                        const uint64_t left = (x > 0)
                            ? tile[static_cast<size_t>(x - 1)]
                            : (leftTile ? (*leftTile)[kBinaryCaveTileBits - 1] : ~0ULL);
                        const uint64_t center = tile[static_cast<size_t>(x)];
                        const uint64_t right = (x < kBinaryCaveTileBits - 1)
                            ? tile[static_cast<size_t>(x + 1)]
                            : (rightTile ? (*rightTile)[0] : ~0ULL);

                        const uint64_t aboveL = aboveTile
                            ? ((x > 0) ? (*aboveTile)[static_cast<size_t>(x - 1)] : (leftTile ? grid[binaryCaveGridIndex(gx - 1, gy - 1)][kBinaryCaveTileBits - 1] : 0ULL))
                            : 0ULL;
                        const uint64_t aboveC = aboveTile ? (*aboveTile)[static_cast<size_t>(x)] : 0ULL;
                        const uint64_t aboveR = aboveTile
                            ? ((x < kBinaryCaveTileBits - 1) ? (*aboveTile)[static_cast<size_t>(x + 1)] : (rightTile ? grid[binaryCaveGridIndex(gx + 1, gy - 1)][0] : 0ULL))
                            : 0ULL;
                        const uint64_t belowL = belowTile
                            ? ((x > 0) ? (*belowTile)[static_cast<size_t>(x - 1)] : (leftTile ? grid[binaryCaveGridIndex(gx - 1, gy + 1)][kBinaryCaveTileBits - 1] : 0ULL))
                            : 0ULL;
                        const uint64_t belowC = belowTile ? (*belowTile)[static_cast<size_t>(x)] : 0ULL;
                        const uint64_t belowR = belowTile
                            ? ((x < kBinaryCaveTileBits - 1) ? (*belowTile)[static_cast<size_t>(x + 1)] : (rightTile ? grid[binaryCaveGridIndex(gx + 1, gy + 1)][0] : 0ULL))
                            : 0ULL;

                        const uint64_t upperInjectL = aboveTile ? (aboveL >> 63) : 1ULL;
                        const uint64_t upperInjectC = aboveTile ? (aboveC >> 63) : 1ULL;
                        const uint64_t upperInjectR = aboveTile ? (aboveR >> 63) : 1ULL;
                        const uint64_t lowerInjectL = belowTile ? ((belowL & 1ULL) << 63) : (1ULL << 63);
                        const uint64_t lowerInjectC = belowTile ? ((belowC & 1ULL) << 63) : (1ULL << 63);
                        const uint64_t lowerInjectR = belowTile ? ((belowR & 1ULL) << 63) : (1ULL << 63);

                        const uint64_t neighbors[9] = {
                            (left << 1) | upperInjectL,
                            (center << 1) | upperInjectC,
                            (right << 1) | upperInjectR,
                            left,
                            center,
                            right,
                            (left >> 1) | lowerInjectL,
                            (center >> 1) | lowerInjectC,
                            (right >> 1) | lowerInjectR
                        };
                        uint64_t count[4] = {0, 0, 0, 0};
                        for (uint64_t neighbor : neighbors) {
                            uint64_t carry = neighbor;
                            for (int bit = 0; bit < 4; ++bit) {
                                const uint64_t sum = count[bit] ^ carry;
                                carry = count[bit] & carry;
                                count[bit] = sum;
                                if (carry == 0ULL) break;
                            }
                        }
                        outTile[static_cast<size_t>(x)] = caveCountAtLeastMask(count, g_caveField.openThreshold);
                    }
                }
            }
            grid = next;
        }

        const BinaryCaveTile& getBinaryCaveTile(int plane, int tileX, int tileY) {
            const BinaryCaveTileKey key{plane, tileX, tileY};
            auto found = g_caveField.tiles.find(key);
            if (found != g_caveField.tiles.end()) return found->second;

            const auto start = std::chrono::steady_clock::now();
            std::array<BinaryCaveTile, 9> grid{};
            for (int gy = 0; gy < 3; ++gy) {
                for (int gx = 0; gx < 3; ++gx) {
                    BinaryCaveTile& tile = grid[binaryCaveGridIndex(gx, gy)];
                    const int sourceTileX = tileX + gx - 1;
                    const int sourceTileY = tileY + gy - 1;
                    for (int x = 0; x < kBinaryCaveTileBits; ++x) {
                        tile[static_cast<size_t>(x)] = makeInitialBinaryCaveColumn(
                            plane,
                            sourceTileX,
                            sourceTileY,
                            x
                        );
                    }
                }
            }
            for (int i = 0; i < g_caveField.iterations; ++i) {
                smoothBinaryCaveGrid(grid);
            }

            if (g_caveField.tiles.size() >= g_caveField.maxCachedTiles) {
                g_caveField.tiles.clear();
            }
            auto inserted = g_caveField.tiles.emplace(key, grid[binaryCaveGridIndex(1, 1)]);
            g_caveFieldFrameMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - start
            ).count();
            g_caveFieldFrameCellsBuilt += static_cast<uint64_t>(
                kBinaryCaveTileBits * kBinaryCaveTileBits
            );
            return inserted.first->second;
        }

        void ensureCaveField(const BaseSystem& baseSystem,
                             const ExpanseConfig& cfg,
                             int requestedMinY,
                             int requestedMaxY) {
            const bool enabled = getRegistryBool(baseSystem, "BinaryCaveEnabled", true);
            const std::string modeName = getRegistryString(baseSystem, "BinaryCaveMode", "binary");
            const int mode = (modeName == "perlin" || modeName == "old" || modeName == "legacy") ? 1 : 0;
            const int desiredSeedA = cfg.elevationSeed + 1337;
            const int desiredSeedB = cfg.ridgeSeed + 7331;
            const int iterations = std::clamp(
                getRegistryInt(baseSystem, "BinaryCaveIterations", 3),
                0,
                8
            );
            const int fillPercent = std::clamp(
                getRegistryInt(baseSystem, "BinaryCaveFillPercent", 50),
                1,
                99
            );
            const int openThreshold = std::clamp(
                getRegistryInt(baseSystem, "BinaryCaveOpenThreshold", 5),
                1,
                9
            );
            const int cellScale = std::clamp(
                getRegistryInt(baseSystem, "BinaryCaveCellScale", 3),
                1,
                16
            );
            const size_t maxCachedTiles = static_cast<size_t>(std::max(
                16,
                getRegistryInt(baseSystem, "BinaryCaveMaxCachedTiles", 512)
            ));

            if (mode == 1) {
                const int step = std::clamp(
                    getRegistryInt(baseSystem, "PerlinCaveFieldStep", 4),
                    1,
                    16
                );
                const int sizeXZ = std::max(
                    128,
                    getRegistryInt(baseSystem, "PerlinCaveFieldSizeXZ", 2304)
                );
                const int halfXZ = sizeXZ / 2;
                const int targetMinY = std::min(requestedMinY, requestedMaxY);
                const int targetMaxY = std::max(requestedMinY, requestedMaxY);
                const int minY = targetMinY - step;
                const int maxY = targetMaxY + step;
                const int heightY = std::max(step, maxY - minY);
                const int dimX = sizeXZ / step + 1;
                const int dimZ = sizeXZ / step + 1;
                const int dimY = heightY / step + 1;
                const size_t count = static_cast<size_t>(dimX)
                    * static_cast<size_t>(dimY)
                    * static_cast<size_t>(dimZ);

                const bool hasField = (g_caveField.ready || g_caveField.building);
                const bool sameBounds = hasField
                    && g_caveField.mode == mode
                    && g_caveField.enabled == enabled
                    && g_caveField.step == step
                    && static_cast<int>(std::floor(g_caveField.origin.x)) == -halfXZ
                    && static_cast<int>(std::floor(g_caveField.origin.y)) == minY
                    && static_cast<int>(std::floor(g_caveField.origin.z)) == -halfXZ
                    && g_caveField.dimX == dimX
                    && g_caveField.dimY == dimY
                    && g_caveField.dimZ == dimZ;
                const bool sameSeeds = g_caveField.seedA == desiredSeedA
                    && g_caveField.seedB == desiredSeedB
                    && g_caveField.noiseA
                    && g_caveField.noiseB;
                const bool needsRebuild = !sameBounds || !sameSeeds;

                if (!enabled) {
                    g_caveField.ready = false;
                    g_caveField.enabled = false;
                    g_caveField.building = false;
                    g_caveField.mode = mode;
                    g_caveField.tiles.clear();
                    return;
                }

                if (needsRebuild) {
                    g_caveField.ready = false;
                    g_caveField.enabled = enabled;
                    g_caveField.building = true;
                    g_caveField.mode = mode;
                    g_caveField.step = step;
                    g_caveField.origin = glm::vec3(-halfXZ, minY, -halfXZ);
                    g_caveField.dimX = dimX;
                    g_caveField.dimY = dimY;
                    g_caveField.dimZ = dimZ;
                    g_caveField.seedA = desiredSeedA;
                    g_caveField.seedB = desiredSeedB;
                    g_caveField.buildCursor = 0;
                    g_caveField.totalCount = count;
                    g_caveField.lastBuildFrame = std::numeric_limits<uint64_t>::max();
                    g_caveField.noiseA = std::make_unique<PerlinNoise3DLocal>(desiredSeedA);
                    g_caveField.noiseB = std::make_unique<PerlinNoise3DLocal>(desiredSeedB);
                    g_caveField.a.assign(count, 0);
                    g_caveField.b.assign(count, 0);
                    g_caveField.tiles.clear();
                    std::cout << "TerrainGeneration: started Perlin cave field precompute "
                              << g_caveField.dimX << "x" << g_caveField.dimY << "x" << g_caveField.dimZ
                              << " step=" << step << std::endl;
                }

                if (g_caveField.ready || !g_caveField.building) return;
                if (g_caveField.lastBuildFrame == g_voxelStreaming.frameCounter) return;
                g_caveField.lastBuildFrame = g_voxelStreaming.frameCounter;

                const auto startTime = std::chrono::steady_clock::now();
                const size_t cellsPerFrame = static_cast<size_t>(std::max(
                    1024,
                    getRegistryInt(baseSystem, "PerlinCaveFieldCellsPerFrame", 65536)
                ));
                const size_t start = g_caveField.buildCursor;
                const size_t end = std::min(g_caveField.totalCount, start + cellsPerFrame);
                for (size_t linear = start; linear < end; ++linear) {
                    size_t t = linear;
                    const int z = static_cast<int>(t % static_cast<size_t>(g_caveField.dimZ));
                    t /= static_cast<size_t>(g_caveField.dimZ);
                    const int y = static_cast<int>(t % static_cast<size_t>(g_caveField.dimY));
                    const int x = static_cast<int>(t / static_cast<size_t>(g_caveField.dimY));
                    const float wx = g_caveField.origin.x + static_cast<float>(x * step);
                    const float wy = g_caveField.origin.y + static_cast<float>(y * step);
                    const float wz = g_caveField.origin.z + static_cast<float>(z * step);
                    if (!g_caveField.noiseA || !g_caveField.noiseB) break;
                    const float v1 = (g_caveField.noiseA->noise(wx / 64.0f, wy / 48.0f, wz / 64.0f) + 1.0f) * 0.5f;
                    const float v2 = (g_caveField.noiseB->noise(wx / 128.0f, wy / 128.0f, wz / 128.0f) + 1.0f) * 0.5f;
                    g_caveField.a[linear] = quantizeNoise01(v1);
                    g_caveField.b[linear] = quantizeNoise01(v2);
                }
                g_caveField.buildCursor = end;
                g_caveFieldFrameMs += std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - startTime
                ).count();
                g_caveFieldFrameCellsBuilt += static_cast<uint64_t>(end - start);
                if (g_caveField.buildCursor >= g_caveField.totalCount) {
                    g_caveField.ready = true;
                    g_caveField.building = false;
                    std::cout << "TerrainGeneration: precomputed Perlin cave field "
                              << g_caveField.dimX << "x" << g_caveField.dimY << "x" << g_caveField.dimZ
                              << " step=" << step << std::endl;
                }
                return;
            }

            const bool changed = g_caveField.ready != enabled
                || g_caveField.enabled != enabled
                || g_caveField.mode != mode
                || g_caveField.seedA != desiredSeedA
                || g_caveField.seedB != desiredSeedB
                || g_caveField.iterations != iterations
                || g_caveField.fillPercent != fillPercent
                || g_caveField.openThreshold != openThreshold
                || g_caveField.cellScale != cellScale
                || g_caveField.maxCachedTiles != maxCachedTiles;
            if (!changed) return;

            g_caveField.ready = enabled;
            g_caveField.enabled = enabled;
            g_caveField.building = false;
            g_caveField.mode = mode;
            g_caveField.seedA = desiredSeedA;
            g_caveField.seedB = desiredSeedB;
            g_caveField.iterations = iterations;
            g_caveField.fillPercent = fillPercent;
            g_caveField.openThreshold = openThreshold;
            g_caveField.cellScale = cellScale;
            g_caveField.maxCachedTiles = maxCachedTiles;
            g_caveField.tiles.clear();
            g_caveField.noiseA.reset();
            g_caveField.noiseB.reset();
            g_caveField.a.clear();
            g_caveField.b.clear();
            if (enabled) {
                std::cout << "TerrainGeneration: binary cave CA enabled "
                          << "tile=" << kBinaryCaveTileBits
                          << " cellScale=" << cellScale
                          << " iterations=" << iterations
                          << " fill=" << fillPercent
                          << " threshold=" << openThreshold
                          << std::endl;
            }
        }

        bool sampleBinaryCavePlane(int plane, int worldA, int worldY) {
            const int tileA = floorDivInt(worldA, kBinaryCaveTileBits);
            const int tileY = floorDivInt(worldY, kBinaryCaveTileBits);
            const int localA = worldA - tileA * kBinaryCaveTileBits;
            const int localY = worldY - tileY * kBinaryCaveTileBits;
            const BinaryCaveTile& tile = getBinaryCaveTile(plane, tileA, tileY);
            return ((tile[static_cast<size_t>(localA)] >> localY) & 1ULL) != 0ULL;
        }

        inline bool sampleCaveField(float worldX, float worldY, float worldZ, float& outA, float& outB) {
            if (!g_caveField.enabled) return false;
            g_caveFieldFrameSampleCount += 1;
            if (g_caveField.mode == 1) {
                if (!g_caveField.ready) {
                    return samplePerlinCaveNoiseDirect(worldX, worldY, worldZ, outA, outB);
                }
                const float fx = (worldX - g_caveField.origin.x) / static_cast<float>(g_caveField.step);
                const float fy = (worldY - g_caveField.origin.y) / static_cast<float>(g_caveField.step);
                const float fz = (worldZ - g_caveField.origin.z) / static_cast<float>(g_caveField.step);
                const int ix = static_cast<int>(std::round(fx));
                const int iy = static_cast<int>(std::round(fy));
                const int iz = static_cast<int>(std::round(fz));
                if (ix < 0 || iy < 0 || iz < 0
                    || ix >= g_caveField.dimX
                    || iy >= g_caveField.dimY
                    || iz >= g_caveField.dimZ) {
                    return samplePerlinCaveNoiseDirect(worldX, worldY, worldZ, outA, outB);
                }
                const size_t idx = (static_cast<size_t>(ix) * static_cast<size_t>(g_caveField.dimY)
                    + static_cast<size_t>(iy)) * static_cast<size_t>(g_caveField.dimZ)
                    + static_cast<size_t>(iz);
                if (idx >= g_caveField.a.size() || idx >= g_caveField.b.size()) {
                    return samplePerlinCaveNoiseDirect(worldX, worldY, worldZ, outA, outB);
                }
                outA = static_cast<float>(g_caveField.a[idx]) / 255.0f;
                outB = static_cast<float>(g_caveField.b[idx]) / 255.0f;
                return true;
            }
            if (!g_caveField.ready) return false;
            const float invCellScale = 1.0f / static_cast<float>(std::max(1, g_caveField.cellScale));
            const int x = static_cast<int>(std::floor(worldX * invCellScale));
            const int y = static_cast<int>(std::floor(worldY * invCellScale));
            const int z = static_cast<int>(std::floor(worldZ * invCellScale));
            const bool xyOpen = sampleBinaryCavePlane(0, x, y);
            const bool zyOpen = sampleBinaryCavePlane(1, z, y);
            const bool open = xyOpen && zyOpen;
            outA = open ? 1.0f : 0.0f;
            outB = 0.0f;
            return true;
        }

        

        int findWorldIndexByName(const LevelContext& level, const std::string& name) {
            for (size_t i = 0; i < level.worlds.size(); ++i) {
                if (level.worlds[i].name == name) return static_cast<int>(i);
            }
            return -1;
        }

        glm::ivec3 instanceToBlockPos(const EntityInstance& inst) {
            return glm::ivec3(
                static_cast<int>(std::round(inst.position.x)),
                static_cast<int>(std::round(inst.position.y)),
                static_cast<int>(std::round(inst.position.z))
            );
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

        constexpr uint8_t kWaterWaveClassUnknown = 0u;
        constexpr uint8_t kWaterWaveClassPond = 1u;
        constexpr uint8_t kWaterWaveClassLake = 2u;
        constexpr uint8_t kWaterWaveClassRiver = 3u;
        constexpr uint8_t kWaterWaveClassOcean = 4u;
        constexpr uint8_t kWaterFoliageMarkerNone = 0u;
        constexpr uint8_t kWaterFoliageMarkerSandDollarX = 4u;
        constexpr uint8_t kWaterFoliageMarkerSandDollarZ = 5u;

        uint32_t withWaterWaveClass(uint32_t packedColorRgb, uint8_t waveClass) {
            const uint32_t rgb = packedColorRgb & 0x00ffffffu;
            const uint8_t encoded = static_cast<uint8_t>((waveClass & 0x0fu) << 4u);
            return rgb | (static_cast<uint32_t>(encoded) << 24u);
        }

        uint8_t waterWaveClassFromPackedColor(uint32_t packedColor) {
            const uint8_t encoded = static_cast<uint8_t>((packedColor >> 24) & 0xffu);
            const uint8_t marker = static_cast<uint8_t>(encoded & 0x0fu);
            const uint8_t waveClass = static_cast<uint8_t>((encoded >> 4u) & 0x0fu);
            if (marker <= kWaterFoliageMarkerSandDollarZ && waveClass <= kWaterWaveClassOcean) {
                return waveClass;
            }
            return kWaterWaveClassUnknown;
        }

        uint8_t waterFoliageMarkerFromPackedColor(uint32_t packedColor) {
            const uint8_t encoded = static_cast<uint8_t>((packedColor >> 24) & 0xffu);
            const uint8_t marker = static_cast<uint8_t>(encoded & 0x0fu);
            const uint8_t waveClass = static_cast<uint8_t>((encoded >> 4u) & 0x0fu);
            if (marker <= kWaterFoliageMarkerSandDollarZ && waveClass <= kWaterWaveClassOcean) {
                return marker;
            }
            return kWaterFoliageMarkerNone;
        }

        uint32_t withWaterFoliageMarker(uint32_t packedColor, uint8_t marker) {
            const uint32_t rgb = packedColor & 0x00ffffffu;
            const uint8_t waveClass = waterWaveClassFromPackedColor(packedColor);
            const uint8_t encoded = static_cast<uint8_t>(((waveClass & 0x0fu) << 4u) | (marker & 0x0fu));
            return rgb | (static_cast<uint32_t>(encoded) << 24u);
        }

        const Entity* findNonZeroBlockProto(const std::vector<Entity>& prototypes) {
            for (const auto& proto : prototypes) {
                if (!proto.isBlock) continue;
                if (proto.prototypeID == 0) continue;
                if (proto.name == "Water") continue;
                return &proto;
            }
            return nullptr;
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

        bool levelContainsWorldNamed(const LevelContext& level, const std::string& worldName) {
            if (worldName.empty()) return false;
            for (const Entity& world : level.worlds) {
                if (world.name == worldName) return true;
            }
            return false;
        }

        uint32_t hash2DInt(int x, int z) {
            uint32_t ux = static_cast<uint32_t>(x) * 73856093u;
            uint32_t uz = static_cast<uint32_t>(z) * 19349663u;
            uint32_t h = ux ^ uz;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
        }

        uint32_t hash3DInt(int x, int y, int z) {
            uint32_t ux = static_cast<uint32_t>(x) * 73856093u;
            uint32_t uy = static_cast<uint32_t>(y) * 19349663u;
            uint32_t uz = static_cast<uint32_t>(z) * 83492791u;
            uint32_t h = ux ^ uy ^ uz;
            h ^= (h >> 13);
            h *= 1274126177u;
            h ^= (h >> 16);
            return h;
        }

        float hashToUnitFloat01(uint32_t h) {
            return static_cast<float>(h & 0x00ffffffu) / 16777215.0f;
        }

        int positiveMod(int value, int modulus) {
            if (modulus <= 0) return 0;
            const int m = value % modulus;
            return (m < 0) ? (m + modulus) : m;
        }

        float smoothstep01(float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float valueNoise2D(int seed, float x, float z) {
            const int ix = static_cast<int>(std::floor(x));
            const int iz = static_cast<int>(std::floor(z));
            const float fx = x - static_cast<float>(ix);
            const float fz = z - static_cast<float>(iz);
            const float u = smoothstep01(fx);
            const float v = smoothstep01(fz);
            auto sample = [&](int gx, int gz) -> float {
                return hashToUnitFloat01(hash2DInt(gx + seed * 37, gz - seed * 53));
            };
            const float n00 = sample(ix, iz);
            const float n10 = sample(ix + 1, iz);
            const float n01 = sample(ix, iz + 1);
            const float n11 = sample(ix + 1, iz + 1);
            const float nx0 = n00 + (n10 - n00) * u;
            const float nx1 = n01 + (n11 - n01) * u;
            return nx0 + (nx1 - nx0) * v;
        }

        float fbmValueNoise2D(int seed,
                              float x,
                              float z,
                              int octaves,
                              float lacunarity = 2.0f,
                              float gain = 0.5f) {
            const int oct = std::max(1, octaves);
            float sum = 0.0f;
            float amplitude = 1.0f;
            float frequency = 1.0f;
            float norm = 0.0f;
            for (int i = 0; i < oct; ++i) {
                sum += valueNoise2D(seed + i * 911, x * frequency, z * frequency) * amplitude;
                norm += amplitude;
                amplitude *= gain;
                frequency *= lacunarity;
            }
            if (norm <= 0.0f) return 0.0f;
            return sum / norm;
        }

        int sectionSizeForSection(const VoxelWorldContext& voxelWorld) {
            return voxelWorld.sectionSize > 0 ? voxelWorld.sectionSize : 1;
        }

        int streamRadiusWorld(const VoxelWorldContext& voxelWorld) {
            constexpr int kStreamRadiusChunks = 6;
            const long long span = static_cast<long long>(sectionSizeForSection(voxelWorld));
            long long radius = span * static_cast<long long>(kStreamRadiusChunks);
            if (radius > 2000000000LL) radius = 2000000000LL;
            return static_cast<int>(radius);
        }

        struct VoxelCpuStreamingView {
            bool enabled = false;
            glm::vec2 cameraXZ{0.0f};
            glm::vec2 forwardXZ{0.0f, -1.0f};
            float nearRadiusSq = 0.0f;
            float cosHalfAngle = -1.0f;
        };

        int angleBucket(float degrees, float bucketDegrees, float offset) {
            const float safeBucket = std::max(1.0f, bucketDegrees);
            return static_cast<int>(std::floor((degrees + offset) / safeBucket));
        }

        VoxelCpuStreamingView makeVoxelCpuStreamingView(const BaseSystem& baseSystem,
                                                        const VoxelWorldContext& voxelWorld,
                                                        const glm::vec3& cameraPos) {
            VoxelCpuStreamingView view{};
            view.enabled = getRegistryBool(baseSystem, "voxelCpuViewCullingEnabled", true);
            view.cameraXZ = glm::vec2(cameraPos.x, cameraPos.z);
            const int sectionSize = sectionSizeForSection(voxelWorld);
            const float defaultNearRadius = static_cast<float>(sectionSize) * 2.0f;
            const float nearRadius = std::max(
                static_cast<float>(sectionSize),
                getRegistryFloat(baseSystem, "voxelCpuViewNearRadiusBlocks", defaultNearRadius)
            );
            const float halfAngleDegrees = glm::clamp(
                getRegistryFloat(baseSystem, "voxelCpuViewHalfAngleDegrees", 65.0f),
                45.0f,
                180.0f
            );
            const float yawRadians = glm::radians(baseSystem.player ? baseSystem.player->cameraYaw : -90.0f);
            glm::vec2 forward(std::cos(yawRadians), std::sin(yawRadians));
            const float len2 = glm::dot(forward, forward);
            if (len2 > 1e-6f) {
                forward /= std::sqrt(len2);
            } else {
                forward = glm::vec2(0.0f, -1.0f);
            }
            view.forwardXZ = forward;
            view.nearRadiusSq = nearRadius * nearRadius;
            view.cosHalfAngle = std::cos(glm::radians(halfAngleDegrees));
            return view;
        }

        bool pointPassesCpuStreamingView(const VoxelCpuStreamingView& view, const glm::vec2& point) {
            if (!view.enabled) return true;
            const glm::vec2 delta = point - view.cameraXZ;
            const float dist2 = glm::dot(delta, delta);
            if (dist2 <= view.nearRadiusSq) return true;
            if (dist2 <= 1e-6f) return true;
            const float invDist = 1.0f / std::sqrt(dist2);
            return glm::dot(delta * invDist, view.forwardXZ) >= view.cosHalfAngle;
        }

        bool sectionColumnPassesCpuStreamingView(const VoxelCpuStreamingView& view,
                                                const glm::ivec3& sectionCoord,
                                                int sectionSize,
                                                int scale) {
            if (!view.enabled) return true;
            const float minX = static_cast<float>(sectionCoord.x * sectionSize * scale);
            const float minZ = static_cast<float>(sectionCoord.z * sectionSize * scale);
            const float maxX = minX + static_cast<float>(sectionSize * scale);
            const float maxZ = minZ + static_cast<float>(sectionSize * scale);
            const glm::vec2 center((minX + maxX) * 0.5f, (minZ + maxZ) * 0.5f);
            if (pointPassesCpuStreamingView(view, center)) return true;
            if (pointPassesCpuStreamingView(view, glm::vec2(minX, minZ))) return true;
            if (pointPassesCpuStreamingView(view, glm::vec2(maxX, minZ))) return true;
            if (pointPassesCpuStreamingView(view, glm::vec2(minX, maxZ))) return true;
            return pointPassesCpuStreamingView(view, glm::vec2(maxX, maxZ));
        }

        int computeExpanseMaxY(const BaseSystem& baseSystem,
                               const WorldContext& worldCtx,
                               const ExpanseConfig& cfg) {
            int maxY = static_cast<int>(std::ceil(cfg.baseElevation + cfg.mountainElevation));
            if (cfg.islandRadius > 0.0f) {
                // Island height uses a blended elevation + ridge term that can exceed islandNoiseAmp.
                // Use a conservative bound so high ridges never clip top sections out of streaming.
                maxY = static_cast<int>(std::ceil(cfg.waterSurface + cfg.islandMaxHeight + (cfg.islandNoiseAmp * 2.0f)));
                if (cfg.jungleVolcanoEnabled) {
                    maxY = std::max(
                        maxY,
                        static_cast<int>(std::ceil(cfg.waterSurface + cfg.islandMaxHeight + cfg.jungleVolcanoHeight))
                    );
                }
            }
            maxY = std::max(maxY, static_cast<int>(std::ceil(cfg.waterSurface)));
            if (worldCtx.leyLines.enabled && worldCtx.leyLines.loaded) {
                float upliftBudget = std::max(0.0f, worldCtx.leyLines.upliftMax);
                if (worldCtx.leyLines.mountainLayerEnabled && worldCtx.leyLines.mountainLayerStrength > 0.0f) {
                    upliftBudget += upliftBudget * worldCtx.leyLines.mountainLayerStrength;
                }
                maxY += static_cast<int>(std::ceil(upliftBudget));
            }
            // Keep vertical streaming/generation range above terrain so tall pines can exist
            // on uplifted ridges without clipped tops or missing foliage passes.
            maxY += std::max(0, getRegistryInt(baseSystem, "ExpanseVerticalHeadroom", 48));
            maxY = std::max(maxY, getRegistryInt(baseSystem, "ExpanseAbsoluteMaxY", 334));
            if (std::isfinite(cfg.maxSurfaceY)) {
                maxY = std::min(maxY, static_cast<int>(std::ceil(cfg.maxSurfaceY)));
            }
            return maxY;
        }

        glm::ivec3 floorDivVec(const glm::ivec3& v, int divisor) {
            return glm::ivec3(
                floorDivInt(v.x, divisor),
                floorDivInt(v.y, divisor),
                floorDivInt(v.z, divisor)
            );
        }

        bool GenerateExpanseSectionBase(BaseSystem& baseSystem,
                                        std::vector<Entity>& prototypes,
                                        WorldContext& worldCtx,
                                        const ExpanseConfig& cfg,
                                        const glm::ivec3& sectionCoord,
                                        int startColumn,
                                        int maxColumns,
                                        bool& inOutWroteAny,
                                        int& outNextColumn,
                                        bool& outCompleted,
                                        bool& outPostFeaturesCompleted);

        bool GenerateExpanseColumnBase(BaseSystem& baseSystem,
                                       std::vector<Entity>& prototypes,
                                       WorldContext& worldCtx,
                                       const ExpanseConfig& cfg,
                                       const VoxelColumnKey& columnKey,
                                       int minY,
                                       int maxY,
                                       int startColumn,
                                       int maxColumns,
                                       bool& inOutWroteAny,
                                       int& outNextColumn,
                                       bool& outCompleted,
                                       bool& outPostFeaturesCompleted);

        bool RunExpanseSectionDecorators(BaseSystem& baseSystem,
                                         std::vector<Entity>& prototypes,
                                         WorldContext& worldCtx,
                                         const ExpanseConfig& cfg,
                                         const glm::ivec3& sectionCoord,
                                         bool& inOutWroteAny,
                                         bool& outPostFeaturesCompleted);

        int computeExpanseMinY(const BaseSystem& baseSystem,
                               const ExpanseConfig& cfg,
                               const std::string& currentLevel) {
            const bool isExpanseLevel = (currentLevel == "the_expanse");
            const bool isDepthLevel = (currentLevel == "the_depths");
            const int waterFloorY = static_cast<int>(std::floor(cfg.waterFloor));
            const int streamPortalY = isDepthLevel ? (cfg.minY + 1) : (waterFloorY - 2);
            int minY = std::min(std::min(cfg.minY, waterFloorY), streamPortalY);
            if (isExpanseLevel && getRegistryBool(baseSystem, "UnifiedDepthsEnabled", true)) {
                minY = getRegistryInt(baseSystem, "UnifiedDepthsMinY", -70);
            }
            return minY;
        }

        void pruneColumnSectionLifecycleOutsideProfile(VoxelWorldContext& voxelWorld, int size) {
            std::vector<VoxelSectionKey> removeKeys;
            removeKeys.reserve(voxelWorld.chunkStates.size());
            for (const auto& [key, _] : voxelWorld.chunkStates) {
                const int sectionMinY = key.coord.y * size;
                const int sectionMaxY = sectionMinY + size - 1;
                if (sectionMaxY < voxelWorld.columnMinY
                    || sectionMinY >= voxelWorld.columnMaxYExclusive) {
                    removeKeys.push_back(key);
                }
            }
            for (const VoxelSectionKey& key : removeKeys) {
                voxelWorld.releaseSection(key);
                voxelWorld.eraseChunkState(key);
                g_voxelTerrainGenerated.erase(key);
            }
        }

        void clearGeneratedColumnSections(const VoxelColumnKey& columnKey, int minSectionY, int maxSectionY) {
            for (int sy = minSectionY; sy <= maxSectionY; ++sy) {
                g_voxelTerrainGenerated.erase(VoxelSectionKey{glm::ivec3(columnKey.coord.x, sy, columnKey.coord.y)});
            }
        }

        void markColumnSectionLifecycle(VoxelWorldContext& voxelWorld,
                                        const VoxelColumnKey& columnKey,
                                        int minSectionY,
                                        int maxSectionY,
                                        bool needsDecorators,
                                        bool needsSurfaceFoliage) {
            for (int sy = minSectionY; sy <= maxSectionY; ++sy) {
                const VoxelSectionKey sectionKey{glm::ivec3(columnKey.coord.x, sy, columnKey.coord.y)};
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(sectionKey);
                state.desired = true;
                state.generated = true;
                state.postFeaturesComplete = !needsDecorators;
                state.hasSection = (voxelWorld.sections.count(sectionKey) > 0);
                state.surfaceFoliageComplete = !needsSurfaceFoliage || !state.hasSection;
                state.featureDependencyReadyMask = 0;
                state.featureDependencyMaskInitialized = false;
                state.touchFrame = g_voxelStreaming.frameCounter;
                if (state.hasSection) {
                    g_voxelTerrainGenerated.insert(sectionKey);
                } else {
                    g_voxelTerrainGenerated.erase(sectionKey);
                }
                if (needsDecorators) {
                    state.stage = VoxelChunkLifecycleStage::FeatureQueued;
                } else if (state.isFullyReady()) {
                    state.stage = VoxelChunkLifecycleStage::Ready;
                } else {
                    state.stage = VoxelChunkLifecycleStage::BaseGenerated;
                }
            }
        }

        bool runColumnDecoratorStep(BaseSystem& baseSystem,
                                    std::vector<Entity>& prototypes,
                                    WorldContext& worldCtx,
                                    const ExpanseConfig& cfg,
                                    VoxelWorldContext& voxelWorld,
                                    const VoxelColumnKey& columnKey,
                                    VoxelColumnGenerationJob& job,
                                    int minSectionY,
                                    int maxSectionY,
                                    bool needsSurfaceFoliage) {
            if (job.nextFeatureSectionY == std::numeric_limits<int>::min()) {
                job.nextFeatureSectionY = minSectionY;
            }
            const int sectionCount = std::max(1, maxSectionY - minSectionY + 1);
            const int sectionsPerStep = std::clamp(
                getRegistryInt(baseSystem, "voxelColumnFeatureSectionsPerStep", 4),
                1,
                sectionCount
            );
            int processed = 0;
            while (job.nextFeatureSectionY <= maxSectionY && processed < sectionsPerStep) {
                const int sy = job.nextFeatureSectionY;
                const VoxelSectionKey sectionKey{glm::ivec3(columnKey.coord.x, sy, columnKey.coord.y)};
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(sectionKey);
                state.desired = true;
                state.generated = true;
                state.postFeaturesComplete = false;
                state.surfaceFoliageComplete = false;
                state.hasSection = (voxelWorld.sections.count(sectionKey) > 0);
                state.stage = VoxelChunkLifecycleStage::FeatureInProgress;
                state.touchFrame = g_voxelStreaming.frameCounter;
                if (state.hasSection) {
                    g_voxelTerrainGenerated.insert(sectionKey);
                }

                bool wroteAny = false;
                bool postFeaturesCompleted = true;
                const bool resolved = RunExpanseSectionDecorators(
                    baseSystem,
                    prototypes,
                    worldCtx,
                    cfg,
                    sectionKey.coord,
                    wroteAny,
                    postFeaturesCompleted
                );
                job.wroteAny = job.wroteAny || wroteAny;
                state.hasSection = (voxelWorld.sections.count(sectionKey) > 0);
                if (state.hasSection) {
                    g_voxelTerrainGenerated.insert(sectionKey);
                } else {
                    g_voxelTerrainGenerated.erase(sectionKey);
                }

                if (!resolved || !postFeaturesCompleted) {
                    state.postFeaturesComplete = false;
                    state.surfaceFoliageComplete = false;
                    state.stage = VoxelChunkLifecycleStage::FeatureQueued;
                    state.touchFrame = g_voxelStreaming.frameCounter;
                    return false;
                }

                state.postFeaturesComplete = true;
                state.surfaceFoliageComplete = !needsSurfaceFoliage || !state.hasSection;
                state.stage = state.isFullyReady()
                    ? VoxelChunkLifecycleStage::Ready
                    : VoxelChunkLifecycleStage::BaseGenerated;
                state.touchFrame = g_voxelStreaming.frameCounter;
                job.nextFeatureSectionY += 1;
                processed += 1;
            }
            return job.nextFeatureSectionY > maxSectionY;
        }

        bool runColumnSurfaceFoliageStep(BaseSystem& baseSystem,
                                         std::vector<Entity>& prototypes,
                                         VoxelWorldContext& voxelWorld,
                                         const VoxelColumnKey& columnKey,
                                         VoxelColumnGenerationJob& job,
                                         int minSectionY,
                                         int maxSectionY) {
            if (job.nextSurfaceSectionY == std::numeric_limits<int>::min()) {
                job.nextSurfaceSectionY = minSectionY;
            }
            const int sectionCount = std::max(1, maxSectionY - minSectionY + 1);
            const int sectionsPerStep = std::clamp(
                getRegistryInt(baseSystem, "voxelColumnSurfaceSectionsPerStep", 1),
                1,
                sectionCount
            );
            const int passesPerSection = std::max(
                1,
                getRegistryInt(baseSystem, "voxelColumnSurfaceFoliagePassesPerSection", 1)
            );
            int processed = 0;
            while (job.nextSurfaceSectionY <= maxSectionY && processed < sectionsPerStep) {
                const int sy = job.nextSurfaceSectionY;
                const VoxelSectionKey sectionKey{glm::ivec3(columnKey.coord.x, sy, columnKey.coord.y)};
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(sectionKey);
                state.desired = true;
                state.generated = true;
                state.postFeaturesComplete = true;
                state.hasSection = (voxelWorld.sections.count(sectionKey) > 0);
                state.touchFrame = g_voxelStreaming.frameCounter;
                if (!state.hasSection) {
                    state.surfaceFoliageComplete = true;
                    state.stage = VoxelChunkLifecycleStage::Ready;
                    g_voxelTerrainGenerated.erase(sectionKey);
                    job.nextSurfaceSectionY += 1;
                    processed += 1;
                    continue;
                }

                // TreeGeneration's immediate path requires terrain readiness before it will
                // accept a section. Publish that readiness before asking it to decorate.
                g_voxelTerrainGenerated.insert(sectionKey);
                state.surfaceFoliageComplete = false;
                state.stage = VoxelChunkLifecycleStage::BaseGenerated;
                const bool surfaceReady = TreeGenerationSystemLogic::ProcessSectionSurfaceFoliageNow(
                    baseSystem,
                    prototypes,
                    sectionKey,
                    passesPerSection
                );
                state.hasSection = (voxelWorld.sections.count(sectionKey) > 0);
                state.surfaceFoliageComplete = surfaceReady || !state.hasSection;
                state.stage = state.isFullyReady()
                    ? VoxelChunkLifecycleStage::Ready
                    : VoxelChunkLifecycleStage::BaseGenerated;
                state.touchFrame = g_voxelStreaming.frameCounter;
                if (state.hasSection) {
                    g_voxelTerrainGenerated.insert(sectionKey);
                } else {
                    g_voxelTerrainGenerated.erase(sectionKey);
                }
                if (!state.surfaceFoliageComplete) {
                    return false;
                }
                job.nextSurfaceSectionY += 1;
                processed += 1;
            }
            return job.nextSurfaceSectionY > maxSectionY;
        }

        void finalizeColumnRenderBridge(BaseSystem& baseSystem,
                                        std::vector<Entity>& prototypes,
                                        VoxelWorldContext& voxelWorld,
                                        const VoxelColumnKey& columnKey,
                                        int minSectionY,
                                        int maxSectionY) {
            (void)baseSystem;
            (void)prototypes;
            for (int sy = minSectionY; sy <= maxSectionY; ++sy) {
                const VoxelSectionKey sectionKey{glm::ivec3(columnKey.coord.x, sy, columnKey.coord.y)};
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(sectionKey);
                state.desired = true;
                state.generated = true;
                state.postFeaturesComplete = true;
                state.hasSection = (voxelWorld.sections.count(sectionKey) > 0);
                state.touchFrame = g_voxelStreaming.frameCounter;
                state.surfaceFoliageComplete = true;
                state.stage = VoxelChunkLifecycleStage::Ready;
                if (state.hasSection) {
                    g_voxelTerrainGenerated.insert(sectionKey);
                } else {
                    g_voxelTerrainGenerated.erase(sectionKey);
                }
            }
        }

        bool stepColumnGenerationJob(BaseSystem& baseSystem,
                                     std::vector<Entity>& prototypes,
                                     WorldContext& worldCtx,
                                     const ExpanseConfig& cfg,
                                     const VoxelColumnKey& columnKey,
                                     VoxelColumnGenerationJob& job,
                                     int columnMinY,
                                     int columnMaxY,
                                     int minSectionY,
                                     int maxSectionY,
                                     int localColumnsPerStep,
                                     int& outColumnSteps) {
            if (!baseSystem.voxelWorld) return true;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelChunkLifecycleState& columnState = voxelWorld.ensureColumnState(columnKey);
            columnState.desired = true;
            columnState.touchFrame = g_voxelStreaming.frameCounter;

            if (job.phase == 0) {
                columnState.stage = VoxelChunkLifecycleStage::BaseInProgress;
                bool completed = false;
                bool postFeaturesCompleted = true;
                int nextColumn = job.nextColumn;
                bool wroteAny = job.wroteAny;
                (void)GenerateExpanseColumnBase(
                    baseSystem,
                    prototypes,
                    worldCtx,
                    cfg,
                    columnKey,
                    columnMinY,
                    columnMaxY,
                    nextColumn,
                    localColumnsPerStep,
                    wroteAny,
                    nextColumn,
                    completed,
                    postFeaturesCompleted
                );
                outColumnSteps += 1;
                job.nextColumn = nextColumn;
                job.wroteAny = wroteAny;
                if (!completed) return false;
                const bool runDecorators =
                    !postFeaturesCompleted
                    && getRegistryBool(baseSystem, "VoxelColumnRunDeferredDecorators", false);
                const bool runSurfaceFoliage =
                    getRegistryBool(baseSystem, "VoxelColumnRunSurfaceFoliage", false);
                markColumnSectionLifecycle(
                    voxelWorld,
                    columnKey,
                    minSectionY,
                    maxSectionY,
                    runDecorators,
                    runSurfaceFoliage
                );
                columnState.generated = true;
                columnState.postFeaturesComplete = !runDecorators;
                columnState.surfaceFoliageComplete = !runSurfaceFoliage;
                columnState.hasSection = (voxelWorld.columns.count(columnKey) > 0);
                columnState.stage = runDecorators
                    ? VoxelChunkLifecycleStage::FeatureQueued
                    : VoxelChunkLifecycleStage::BaseGenerated;
                job.phase = runDecorators ? 1 : (runSurfaceFoliage ? 2 : 3);
                return false;
            }

            if (job.phase == 1) {
                columnState.stage = VoxelChunkLifecycleStage::FeatureInProgress;
                const bool runSurfaceFoliage =
                    getRegistryBool(baseSystem, "VoxelColumnRunSurfaceFoliage", false);
                const bool completed = runColumnDecoratorStep(
                    baseSystem,
                    prototypes,
                    worldCtx,
                    cfg,
                    voxelWorld,
                    columnKey,
                    job,
                    minSectionY,
                    maxSectionY,
                    runSurfaceFoliage
                );
                outColumnSteps += 1;
                if (!completed) return false;
                columnState.postFeaturesComplete = true;
                columnState.surfaceFoliageComplete = !runSurfaceFoliage;
                columnState.stage = runSurfaceFoliage
                    ? VoxelChunkLifecycleStage::BaseGenerated
                    : VoxelChunkLifecycleStage::Ready;
                columnState.touchFrame = g_voxelStreaming.frameCounter;
                job.phase = runSurfaceFoliage ? 2 : 3;
                return false;
            }

            if (job.phase == 2) {
                columnState.stage = VoxelChunkLifecycleStage::BaseGenerated;
                const bool completed = runColumnSurfaceFoliageStep(
                    baseSystem,
                    prototypes,
                    voxelWorld,
                    columnKey,
                    job,
                    minSectionY,
                    maxSectionY
                );
                outColumnSteps += 1;
                if (!completed) return false;
                columnState.surfaceFoliageComplete = true;
                columnState.stage = VoxelChunkLifecycleStage::Ready;
                columnState.touchFrame = g_voxelStreaming.frameCounter;
                job.phase = 3;
                return false;
            }

            if (job.phase == 3) {
                finalizeColumnRenderBridge(
                    baseSystem,
                    prototypes,
                    voxelWorld,
                    columnKey,
                    minSectionY,
                    maxSectionY
                );
                columnState.generated = true;
                columnState.postFeaturesComplete = true;
                columnState.surfaceFoliageComplete = true;
                columnState.hasSection = (voxelWorld.columns.count(columnKey) > 0);
                columnState.stage = VoxelChunkLifecycleStage::Ready;
                columnState.touchFrame = g_voxelStreaming.frameCounter;
                return true;
            }

            return false;
        }

        void rebuildDesiredColumns(BaseSystem& baseSystem,
                                   VoxelWorldContext& voxelWorld,
                                   const ExpanseConfig& cfg,
                                   const glm::vec3& cameraPos,
                                   const VoxelCpuStreamingView& cpuStreamingView,
                                   int cpuViewYawBucket) {
            const int radius = streamRadiusWorld(voxelWorld);
            const int size = sectionSizeForSection(voxelWorld);
            const glm::ivec2 cameraColumn(
                floorDivInt(static_cast<int>(std::floor(cameraPos.x)), size),
                floorDivInt(static_cast<int>(std::floor(cameraPos.z)), size)
            );
            const bool moved =
                g_voxelColumnStreaming.lastCenterColumn != cameraColumn
                || g_voxelColumnStreaming.lastRadius != radius
                || g_voxelColumnStreaming.lastCpuViewYawBucket != cpuViewYawBucket
                || g_voxelColumnStreaming.lastCpuViewCullingEnabled != cpuStreamingView.enabled;
            if (!moved
                && !g_voxelColumnStreaming.pendingDesiredRebuild
                && !g_voxelColumnStreaming.desired.empty()) {
                return;
            }

            g_voxelColumnStreaming.lastCenterColumn = cameraColumn;
            g_voxelColumnStreaming.lastRadius = radius;
            g_voxelColumnStreaming.lastCpuViewYawBucket = cpuViewYawBucket;
            g_voxelColumnStreaming.lastCpuViewCullingEnabled = cpuStreamingView.enabled;
            g_voxelColumnStreaming.pendingDesiredRebuild = false;

            std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> nextDesired;
            std::vector<VoxelColumnKey> nextOrder;
            if (radius > 0) {
                const int columnRadius = static_cast<int>(std::ceil(
                    static_cast<float>(radius) / static_cast<float>(std::max(1, size))
                ));
                nextDesired.reserve(static_cast<size_t>((columnRadius * 2 + 1) * (columnRadius * 2 + 1)));
                nextOrder.reserve(nextDesired.size());
                const glm::vec2 camXZ(cameraPos.x, cameraPos.z);
                for (int ring = 0; ring <= columnRadius; ++ring) {
                    for (int dz = -ring; dz <= ring; ++dz) {
                        for (int dx = -ring; dx <= ring; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                            const glm::ivec2 coord = cameraColumn + glm::ivec2(dx, dz);
                            const glm::ivec3 sectionCoord(coord.x, 0, coord.y);
                            if (!sectionColumnPassesCpuStreamingView(cpuStreamingView, sectionCoord, size, 1)) {
                                continue;
                            }
                            const glm::vec2 minB(coord.x * size, coord.y * size);
                            const glm::vec2 maxB = minB + glm::vec2(size);
                            float distX = 0.0f;
                            if (camXZ.x < minB.x) distX = minB.x - camXZ.x;
                            else if (camXZ.x > maxB.x) distX = camXZ.x - maxB.x;
                            float distZ = 0.0f;
                            if (camXZ.y < minB.y) distZ = minB.y - camXZ.y;
                            else if (camXZ.y > maxB.y) distZ = camXZ.y - maxB.y;
                            if (std::sqrt(distX * distX + distZ * distZ) > static_cast<float>(radius)) continue;
                            const VoxelColumnKey key{coord};
                            if (nextDesired.insert(key).second) {
                                nextOrder.push_back(key);
                            }
                        }
                    }
                }
            }

            const int desiredHardCap = std::max(0, getRegistryInt(baseSystem, "voxelDesiredColumnHardCap", 0));
            if (desiredHardCap > 0 && static_cast<int>(nextOrder.size()) > desiredHardCap) {
                nextOrder.resize(static_cast<size_t>(desiredHardCap));
                nextDesired.clear();
                for (const VoxelColumnKey& key : nextOrder) {
                    nextDesired.insert(key);
                }
            }

            for (const VoxelColumnKey& key : nextOrder) {
                if (g_voxelColumnStreaming.generated.count(key) > 0) continue;
                if (g_voxelColumnStreaming.jobs.count(key) > 0) continue;
                if (g_voxelColumnStreaming.pendingSet.insert(key).second) {
                    g_voxelColumnStreaming.pending.push_back(key);
                    voxelWorld.ensureColumnState(key).stage = VoxelChunkLifecycleStage::BaseQueued;
                }
            }

            std::vector<VoxelColumnKey> releaseColumns;
            releaseColumns.reserve(voxelWorld.columns.size());
            for (const auto& [key, _] : voxelWorld.columns) {
                if (nextDesired.count(key) == 0) {
                    releaseColumns.push_back(key);
                }
            }
            const int minSectionY = floorDivInt(voxelWorld.columnMinY, size);
            const int maxSectionY = floorDivInt(voxelWorld.columnMaxYExclusive - 1, size);
            for (const VoxelColumnKey& key : releaseColumns) {
                voxelWorld.releaseColumn(key);
                clearGeneratedColumnSections(key, minSectionY, maxSectionY);
                g_voxelColumnStreaming.generated.erase(key);
                g_voxelColumnStreaming.jobs.erase(key);
                g_voxelColumnStreaming.pendingSet.erase(key);
                g_voxelColumnStreaming.inProgress.erase(key);
            }

            if (!g_voxelColumnStreaming.pending.empty()) {
                std::vector<VoxelColumnKey> filtered;
                filtered.reserve(g_voxelColumnStreaming.pending.size());
                for (const VoxelColumnKey& key : g_voxelColumnStreaming.pending) {
                    if (nextDesired.count(key) > 0
                        && g_voxelColumnStreaming.generated.count(key) == 0) {
                        filtered.push_back(key);
                    } else {
                        g_voxelColumnStreaming.pendingSet.erase(key);
                    }
                }
                g_voxelColumnStreaming.pending.swap(filtered);
            }

            g_voxelColumnStreaming.desired.swap(nextDesired);
            g_voxelColumnStreaming.desiredOrder.swap(nextOrder);
        }

        void UpdateExpanseVoxelColumns(BaseSystem& baseSystem,
                                       std::vector<Entity>& prototypes,
                                       WorldContext& worldCtx,
                                       const ExpanseConfig& cfg,
                                       const std::string& currentLevel,
                                       const VoxelCpuStreamingView& cpuStreamingView,
                                       int cpuViewYawBucket) {
            if (!baseSystem.voxelWorld || !baseSystem.player) return;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const int size = sectionSizeForSection(voxelWorld);
            const int minY = computeExpanseMinY(baseSystem, cfg, currentLevel);
            const int maxY = computeExpanseMaxY(baseSystem, worldCtx, cfg);
            voxelWorld.columnMinY = minY;
            voxelWorld.columnMaxYExclusive = maxY + 1;
            const int minSectionY = floorDivInt(minY, size);
            const int maxSectionY = floorDivInt(maxY, size);

            glm::vec3 cameraPos = baseSystem.player->cameraPosition;
            rebuildDesiredColumns(baseSystem, voxelWorld, cfg, cameraPos, cpuStreamingView, cpuViewYawBucket);

            const int fullColumnsPerSection = size * size;
            const int localColumnsPerStep = std::clamp(
                getRegistryInt(
                    baseSystem,
                    "voxelColumnLocalColumnsPerStep",
                    getRegistryInt(baseSystem, "voxelGenerationColumnsPerStep", fullColumnsPerSection)
                ),
                1,
                std::max(1, fullColumnsPerSection)
            );
            const int generationBudget = std::max(
                1,
                getRegistryInt(baseSystem, "voxelColumnGenerationStepsPerFrame", 1)
            );
            const float generationTimeBudgetMs = std::max(
                0.1f,
                getRegistryFloat(baseSystem, "voxelColumnGenerationMaxMsPerFrame", 8.0f)
            );
            const auto genStart = std::chrono::steady_clock::now();
            int columnSteps = 0;
            int columnsBuilt = 0;
            int columnsConsumed = 0;
            int skipped = 0;

            while (!g_voxelColumnStreaming.pending.empty() && columnSteps < generationBudget) {
                if (columnsConsumed > 0 && generationTimeBudgetMs > 0.0f) {
                    const float elapsedMs = std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - genStart
                    ).count();
                    if (elapsedMs >= generationTimeBudgetMs) break;
                }
                const VoxelColumnKey key = g_voxelColumnStreaming.pending.front();
                g_voxelColumnStreaming.pending.erase(g_voxelColumnStreaming.pending.begin());
                g_voxelColumnStreaming.pendingSet.erase(key);
                columnsConsumed += 1;

                if (g_voxelColumnStreaming.desired.count(key) == 0) {
                    skipped += 1;
                    continue;
                }
                if (g_voxelColumnStreaming.generated.count(key) > 0) {
                    skipped += 1;
                    continue;
                }

                g_voxelColumnStreaming.inProgress.insert(key);

                auto jobIt = g_voxelColumnStreaming.jobs.find(key);
                if (jobIt == g_voxelColumnStreaming.jobs.end()) {
                    voxelWorld.releaseColumn(key);
                    VoxelColumnGenerationJob job;
                    auto inserted = g_voxelColumnStreaming.jobs.emplace(key, job);
                    jobIt = inserted.first;
                }

                VoxelChunkLifecycleState& columnState = voxelWorld.ensureColumnState(key);
                columnState.desired = true;
                columnState.stage = VoxelChunkLifecycleStage::BaseInProgress;
                columnState.touchFrame = g_voxelStreaming.frameCounter;

                bool completed = false;
                while (columnSteps < generationBudget) {
                    completed = stepColumnGenerationJob(
                        baseSystem,
                        prototypes,
                        worldCtx,
                        cfg,
                        key,
                        jobIt->second,
                        minY,
                        maxY,
                        minSectionY,
                        maxSectionY,
                        localColumnsPerStep,
                        columnSteps
                    );
                    if (completed) break;
                    if (generationTimeBudgetMs > 0.0f) {
                        const float elapsedMs = std::chrono::duration<float, std::milli>(
                            std::chrono::steady_clock::now() - genStart
                        ).count();
                        if (elapsedMs >= generationTimeBudgetMs) break;
                    }
                }

                g_voxelColumnStreaming.inProgress.erase(key);
                if (completed) {
                    g_voxelColumnStreaming.jobs.erase(key);
                    g_voxelColumnStreaming.generated.insert(key);
                    columnState.generated = true;
                    columnState.postFeaturesComplete = true;
                    columnState.surfaceFoliageComplete = true;
                    columnState.hasSection = (voxelWorld.columns.count(key) > 0);
                    columnState.stage = VoxelChunkLifecycleStage::Ready;
                    columnState.touchFrame = g_voxelStreaming.frameCounter;
                    columnsBuilt += 1;
                } else if (g_voxelColumnStreaming.desired.count(key) > 0
                           && g_voxelColumnStreaming.pendingSet.insert(key).second) {
                    switch (jobIt->second.phase) {
                        case 0:
                            columnState.stage = VoxelChunkLifecycleStage::BaseQueued;
                            break;
                        case 1:
                            columnState.stage = VoxelChunkLifecycleStage::FeatureQueued;
                            break;
                        case 2:
                        case 3:
                            columnState.stage = VoxelChunkLifecycleStage::BaseGenerated;
                            break;
                        default:
                            columnState.stage = VoxelChunkLifecycleStage::Desired;
                            break;
                    }
                    g_voxelColumnStreaming.pending.insert(g_voxelColumnStreaming.pending.begin(), key);
                }
            }

            pruneColumnSectionLifecycleOutsideProfile(voxelWorld, size);

            const auto end = std::chrono::steady_clock::now();
            const float generationMs = std::chrono::duration<float, std::milli>(end - genStart).count();
            g_voxelStreamingPerfStats.pending = g_voxelColumnStreaming.pending.size()
                + g_voxelColumnStreaming.jobs.size()
                + g_voxelColumnStreaming.inProgress.size();
            g_voxelStreamingPerfStats.desired = g_voxelColumnStreaming.desired.size();
            g_voxelStreamingPerfStats.generated = g_voxelColumnStreaming.generated.size();
            g_voxelStreamingPerfStats.jobs = g_voxelColumnStreaming.jobs.size();
            g_voxelStreamingPerfStats.stepped = columnSteps;
            g_voxelStreamingPerfStats.built = columnsBuilt;
            g_voxelStreamingPerfStats.consumed = columnsConsumed;
            g_voxelStreamingPerfStats.skippedExisting = skipped;
            g_voxelStreamingPerfStats.filteredOut = 0;
            g_voxelStreamingPerfStats.rescueSurfaceQueued = 0;
            g_voxelStreamingPerfStats.rescueMissingQueued = 0;
            g_voxelStreamingPerfStats.droppedByCap = 0;
            g_voxelStreamingPerfStats.reprioritized = 0;
            g_voxelStreamingPerfStats.prepMs = 0.0f;
            g_voxelStreamingPerfStats.generationMs = generationMs;
            g_voxelStreamingPerfStats.desiredMs = 0.0f;
            g_voxelStreamingPerfStats.baseGenMs = generationMs;
            g_voxelStreamingPerfStats.featureMs = 0.0f;
            g_voxelStreamingPerfStats.surfaceMs = 0.0f;
            g_voxelStreamingPerfStats.caveFieldMs = g_caveFieldFrameMs;
            g_voxelStreamingPerfStats.schedulerPressure = 0;
            g_voxelStreamingPerfStats.desiredBudget = static_cast<int>(g_voxelColumnStreaming.desired.size());
            g_voxelStreamingPerfStats.baseBudget = generationBudget;
            g_voxelStreamingPerfStats.featureBudget = 0;
            g_voxelStreamingPerfStats.surfaceBudget = 0;
            g_voxelStreamingPerfStats.baseBudgetMs = generationTimeBudgetMs;
            g_voxelStreamingPerfStats.featureBudgetMs = 0.0f;
            g_voxelStreamingPerfStats.surfaceBudgetMs = 0.0f;
            g_voxelStreamingPerfStats.downstreamDirty = voxelWorld.dirtySections.size();
            g_voxelStreamingPerfStats.downstreamPrepared = baseSystem.voxelRender
                ? baseSystem.voxelRender->preparedMeshes.size()
                : 0u;
            g_voxelStreamingPerfStats.downstreamUpload = baseSystem.voxelRender
                ? baseSystem.voxelRender->renderBuffersDirty.size()
                : 0u;
            g_voxelStreamingPerfStats.caveFieldCellsBuilt = g_caveFieldFrameCellsBuilt;
            g_voxelStreamingPerfStats.caveSamples = g_caveFieldFrameSampleCount;

            g_voxelStreamingTotalStepped += static_cast<uint64_t>(std::max(0, columnSteps));
            g_voxelStreamingTotalBuilt += static_cast<uint64_t>(std::max(0, columnsBuilt));
            g_voxelStreamingTotalConsumed += static_cast<uint64_t>(std::max(0, columnsConsumed));
        }


        void UpdateExpanseVoxelWorld(BaseSystem& baseSystem,
                                     std::vector<Entity>& prototypes,
                                     WorldContext& worldCtx,
                                     const ExpanseConfig& cfg) {
            if (!baseSystem.voxelWorld || !baseSystem.player) return;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            glm::vec3 cameraPos = baseSystem.player->cameraPosition;
            const std::string currentLevel = getRegistryString(baseSystem, "level", "the_expanse");
            const bool isExpanseLevel = (currentLevel == "the_expanse");
            const bool isDepthLevel = (currentLevel == "the_depths");
            const bool unifiedDepthsEnabled = isExpanseLevel && getRegistryBool(baseSystem, "UnifiedDepthsEnabled", true);
            const int unifiedDepthsMinY = getRegistryInt(baseSystem, "UnifiedDepthsMinY", -70);
            const bool verticalDomainGateEnabled = isExpanseLevel
                && unifiedDepthsEnabled
                && getRegistryBool(baseSystem, "voxelVerticalDomainGateEnabled", true);
            const int verticalDomainCutoffY = getRegistryInt(baseSystem, "voxelVerticalDomainCutoffY", -20);
            const int verticalDomainSwitchHysteresisY = std::max(
                0,
                getRegistryInt(baseSystem, "voxelVerticalDomainSwitchHysteresisY", 8)
            );
            const int verticalDomainPrewarmY = std::max(
                0,
                getRegistryInt(baseSystem, "voxelVerticalDomainPrewarmY", 16)
            );
            if (verticalDomainGateEnabled) {
                if (g_verticalDomainMode != 1 && g_verticalDomainMode != 2) {
                    g_verticalDomainMode = (cameraPos.y >= static_cast<float>(verticalDomainCutoffY)) ? 1 : 2;
                } else if (g_verticalDomainMode == 1) {
                    if (cameraPos.y < static_cast<float>(verticalDomainCutoffY - verticalDomainSwitchHysteresisY)) {
                        g_verticalDomainMode = 2;
                    }
                } else {
                    if (cameraPos.y > static_cast<float>(verticalDomainCutoffY + verticalDomainSwitchHysteresisY)) {
                        g_verticalDomainMode = 1;
                    }
                }
            } else {
                g_verticalDomainMode = 0;
            }
            const int waterFloorY = static_cast<int>(std::floor(cfg.waterFloor));
            const int streamPortalY = isDepthLevel ? (cfg.minY + 1) : (waterFloorY - 2);
            int streamMinYTier0 = std::min(std::min(cfg.minY, waterFloorY), streamPortalY);
            if (unifiedDepthsEnabled) {
                streamMinYTier0 = unifiedDepthsMinY;
            }
            g_voxelStreaming.frameCounter += 1;
            const VoxelCpuStreamingView cpuStreamingView =
                makeVoxelCpuStreamingView(baseSystem, voxelWorld, cameraPos);
            const float cpuViewYawBucketDegrees = getRegistryFloat(
                baseSystem,
                "voxelCpuViewYawBucketDegrees",
                12.0f
            );
            const int cpuViewYawBucket = cpuStreamingView.enabled
                ? angleBucket(baseSystem.player->cameraYaw, cpuViewYawBucketDegrees, 180.0f)
                : 0;
            if (getRegistryBool(baseSystem, "VoxelColumnStreamingEnabled", true)) {
                UpdateExpanseVoxelColumns(
                    baseSystem,
                    prototypes,
                    worldCtx,
                    cfg,
                    currentLevel,
                    cpuStreamingView,
                    cpuViewYawBucket
                );
                return;
            }

            // Keep completion markers independent from section allocation.
            // Some generated sections are intentionally empty and never allocate voxel storage.
            // They still must remain "generated" so we do not keep re-queuing them every frame.
            // Do not purge in-progress jobs just because their backing section is currently absent.
            // Sections are materialized lazily when the first non-air cell is written; clearing the
            // job early resets nextColumn to 0 every frame and can stall generation indefinitely.
            auto markChunkDesired = [&](const VoxelSectionKey& key, bool desired) {
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                state.desired = desired;
                state.touchFrame = g_voxelStreaming.frameCounter;
                if (!desired && !state.hasSection && !state.generated) {
                    state.stage = VoxelChunkLifecycleStage::Unknown;
                } else if (desired && state.stage == VoxelChunkLifecycleStage::Unknown) {
                    state.stage = VoxelChunkLifecycleStage::Desired;
                }
            };
            auto markChunkStage = [&](const VoxelSectionKey& key,
                                      VoxelChunkLifecycleStage stage) {
                voxelWorld.setChunkStage(key, stage, g_voxelStreaming.frameCounter);
            };
            auto enqueuePendingKey = [&](const VoxelSectionKey& key) {
                if (!g_voxelStreaming.baseReadySet.insert(key).second) return false;
                g_voxelStreaming.baseReady.push_back(key);
                g_voxelStreaming.baseInProgress.erase(key);
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                state.desired = true;
                state.generated = false;
                state.postFeaturesComplete = false;
                state.surfaceFoliageComplete = false;
                state.hasSection = (voxelWorld.sections.count(key) > 0);
                state.stage = VoxelChunkLifecycleStage::BaseQueued;
                state.touchFrame = g_voxelStreaming.frameCounter;
                return true;
            };
            auto shouldQueueKey = [&](const VoxelSectionKey& key) {
                return g_voxelTerrainGenerated.count(key) == 0;
            };
            auto isSectionBaseReady = [&](const VoxelSectionKey& key) {
                return g_voxelTerrainGenerated.count(key) > 0
                    && g_voxelSectionJobs.count(key) == 0;
            };
            struct FeatureDependencyDescriptor {
                glm::ivec3 offset;
                uint16_t bit = 0;
            };
            constexpr uint16_t kFeatureDepPosX = static_cast<uint16_t>(1u << 0u);
            constexpr uint16_t kFeatureDepNegX = static_cast<uint16_t>(1u << 1u);
            constexpr uint16_t kFeatureDepPosZ = static_cast<uint16_t>(1u << 2u);
            constexpr uint16_t kFeatureDepNegZ = static_cast<uint16_t>(1u << 3u);
            constexpr uint16_t kFeatureDepPosXPosZ = static_cast<uint16_t>(1u << 4u);
            constexpr uint16_t kFeatureDepPosXNegZ = static_cast<uint16_t>(1u << 5u);
            constexpr uint16_t kFeatureDepNegXPosZ = static_cast<uint16_t>(1u << 6u);
            constexpr uint16_t kFeatureDepNegXNegZ = static_cast<uint16_t>(1u << 7u);
            constexpr uint16_t kFeatureDepPosY = static_cast<uint16_t>(1u << 8u);
            constexpr uint16_t kFeatureDepNegY = static_cast<uint16_t>(1u << 9u);
            const std::array<FeatureDependencyDescriptor, 10> featureDeps = {{
                {glm::ivec3( 1,  0,  0), kFeatureDepPosX},
                {glm::ivec3(-1,  0,  0), kFeatureDepNegX},
                {glm::ivec3( 0,  0,  1), kFeatureDepPosZ},
                {glm::ivec3( 0,  0, -1), kFeatureDepNegZ},
                {glm::ivec3( 1,  0,  1), kFeatureDepPosXPosZ},
                {glm::ivec3( 1,  0, -1), kFeatureDepPosXNegZ},
                {glm::ivec3(-1,  0,  1), kFeatureDepNegXPosZ},
                {glm::ivec3(-1,  0, -1), kFeatureDepNegXNegZ},
                {glm::ivec3( 0,  1,  0), kFeatureDepPosY},
                {glm::ivec3( 0, -1,  0), kFeatureDepNegY}
            }};
            auto featureDependencyCount = [&]() -> size_t {
                const bool requireDiagonals = getRegistryBool(baseSystem, "voxelFeatureNeighborRequireDiagonals", false);
                const bool requireVertical = getRegistryBool(baseSystem, "voxelFeatureNeighborRequireVertical", false);
                size_t count = 4;
                if (requireDiagonals) count += 4;
                if (requireVertical) count += 2;
                return std::min(count, featureDeps.size());
            };
            auto computeFeatureReadyMask = [&](const VoxelSectionKey& key) -> uint16_t {
                const size_t depCount = featureDependencyCount();
                uint16_t mask = 0;
                for (size_t i = 0; i < depCount; ++i) {
                    const auto& dep = featureDeps[i];
                    const VoxelSectionKey neighborKey{key.coord + dep.offset};
                    if (isSectionBaseReady(neighborKey)) {
                        mask = static_cast<uint16_t>(mask | dep.bit);
                    }
                }
                return mask;
            };
            auto computeFeatureRequiredMask = [&](const VoxelSectionKey& key) -> uint16_t {
                const bool requireDesiredNeighbors = getRegistryBool(baseSystem, "voxelFeatureNeighborRequireDesired", true);
                const size_t depCount = featureDependencyCount();
                uint16_t requiredMask = 0;
                for (size_t i = 0; i < depCount; ++i) {
                    const auto& dep = featureDeps[i];
                    const VoxelSectionKey neighborKey{key.coord + dep.offset};
                    if (requireDesiredNeighbors && g_voxelStreaming.desired.count(neighborKey) == 0) {
                        continue;
                    }
                    requiredMask = static_cast<uint16_t>(requiredMask | dep.bit);
                }
                return requiredMask;
            };
            auto areFeatureDependenciesReady = [&](const VoxelSectionKey& key) {
                const bool gateEnabled = getRegistryBool(baseSystem, "voxelFeatureNeighborGateEnabled", false);
                if (!gateEnabled) return true;
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                if (!state.featureDependencyMaskInitialized) {
                    state.featureDependencyReadyMask = computeFeatureReadyMask(key);
                    state.featureDependencyMaskInitialized = true;
                }
                const uint16_t requiredMask = computeFeatureRequiredMask(key);
                return (state.featureDependencyReadyMask & requiredMask) == requiredMask;
            };
            auto queueFeatureIfReady = [&](const VoxelSectionKey& key) {
                VoxelChunkLifecycleState* statePtr = voxelWorld.findChunkState(key);
                if (!statePtr) return;
                VoxelChunkLifecycleState& state = *statePtr;
                if (!state.desired || !state.generated || state.postFeaturesComplete) return;
                if (!isSectionBaseReady(key)) return;
                if (g_voxelStreaming.featureInProgress.count(key) > 0) return;
                if (g_voxelStreaming.featureContinuationReadySet.count(key) > 0) return;
                if (g_voxelStreaming.featureReadySet.count(key) > 0) return;
                if (!areFeatureDependenciesReady(key)) {
                    state.stage = VoxelChunkLifecycleStage::FeatureQueued;
                    state.touchFrame = g_voxelStreaming.frameCounter;
                    return;
                }
                state.stage = VoxelChunkLifecycleStage::FeatureQueued;
                state.touchFrame = g_voxelStreaming.frameCounter;
                if (g_voxelStreaming.featureReadySet.insert(key).second) {
                    g_voxelStreaming.featureReady.push_back(key);
                }
            };
            auto queueSurfaceIfNeeded = [&](const VoxelSectionKey& key, bool front = false) {
                VoxelChunkLifecycleState* statePtr = voxelWorld.findChunkState(key);
                if (!statePtr) return;
                VoxelChunkLifecycleState& state = *statePtr;
                if (!state.desired || !state.generated || !state.postFeaturesComplete) return;
                state.hasSection = (voxelWorld.sections.count(key) > 0);
                if (!state.hasSection) {
                    state.surfaceFoliageComplete = true;
                    markChunkStage(
                        key,
                        state.isFullyReady()
                            ? VoxelChunkLifecycleStage::Ready
                            : VoxelChunkLifecycleStage::BaseGenerated
                    );
                    g_voxelStreaming.surfaceReadySet.erase(key);
                    return;
                }
                if (state.surfaceFoliageComplete) {
                    g_voxelStreaming.surfaceReadySet.erase(key);
                    return;
                }
                if (g_voxelStreaming.surfaceReadySet.insert(key).second) {
                    if (front) {
                        g_voxelStreaming.surfaceReady.push_front(key);
                    } else {
                        g_voxelStreaming.surfaceReady.push_back(key);
                    }
                }
            };
            auto tryFinalizeSurfaceFoliage = [&](const VoxelSectionKey& key, bool queueOnFailure, bool frontOnFailure) {
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                state.generated = true;
                state.postFeaturesComplete = true;
                state.hasSection = (voxelWorld.sections.count(key) > 0);
                state.touchFrame = g_voxelStreaming.frameCounter;
                if (!state.hasSection) {
                    state.surfaceFoliageComplete = true;
                    markChunkStage(
                        key,
                        state.isFullyReady()
                            ? VoxelChunkLifecycleStage::Ready
                            : VoxelChunkLifecycleStage::BaseGenerated
                    );
                    g_voxelStreaming.surfaceReadySet.erase(key);
                    return true;
                }

            constexpr int kSyncFoliagePasses = 2;
                const int syncFoliagePasses = kSyncFoliagePasses;
                const bool surfaceReady = TreeGenerationSystemLogic::ProcessSectionSurfaceFoliageNow(
                    baseSystem,
                    prototypes,
                    key,
                    syncFoliagePasses
                );
                state.surfaceFoliageComplete = surfaceReady;
                state.hasSection = (voxelWorld.sections.count(key) > 0);
                state.touchFrame = g_voxelStreaming.frameCounter;
                markChunkStage(
                    key,
                    state.isFullyReady()
                        ? VoxelChunkLifecycleStage::Ready
                        : VoxelChunkLifecycleStage::BaseGenerated
                );
                if (surfaceReady) {
                    g_voxelStreaming.surfaceReadySet.erase(key);
                } else if (queueOnFailure) {
                    queueSurfaceIfNeeded(key, frontOnFailure);
                }
                return surfaceReady;
            };
            auto onBaseReadinessChanged = [&](const VoxelSectionKey& baseKey) {
                const bool baseReady = isSectionBaseReady(baseKey);
                for (size_t i = 0; i < featureDeps.size(); ++i) {
                    const auto& dep = featureDeps[i];
                    const VoxelSectionKey targetKey{
                        baseKey.coord - dep.offset
                    };
                    VoxelChunkLifecycleState* targetStatePtr = voxelWorld.findChunkState(targetKey);
                    if (!targetStatePtr) continue;
                    VoxelChunkLifecycleState& targetState = *targetStatePtr;
                    if (baseReady) {
                        targetState.featureDependencyReadyMask = static_cast<uint16_t>(
                            targetState.featureDependencyReadyMask | dep.bit
                        );
                    } else {
                        targetState.featureDependencyReadyMask = static_cast<uint16_t>(
                            targetState.featureDependencyReadyMask & static_cast<uint16_t>(~dep.bit)
                        );
                    }
                    targetState.featureDependencyMaskInitialized = true;
                    queueFeatureIfReady(targetKey);
                }
                queueFeatureIfReady(baseKey);
            };
            auto onGeneratedSectionAvailable = [&](const VoxelSectionKey& key) {
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                state.generated = true;
                state.desired = (g_voxelStreaming.desired.count(key) > 0);
                state.hasSection = (voxelWorld.sections.count(key) > 0);
                state.surfaceFoliageComplete = false;
                state.touchFrame = g_voxelStreaming.frameCounter;
                state.featureDependencyMaskInitialized = false;
                if (state.stage == VoxelChunkLifecycleStage::BaseInProgress) {
                    state.stage = VoxelChunkLifecycleStage::BaseGenerated;
                }
                onBaseReadinessChanged(key);
            };
            auto onGeneratedSectionUnavailable = [&](const VoxelSectionKey& key) {
                // Drop stale feature-continuation membership as soon as a generated section
                // becomes unavailable, so it can be re-enqueued cleanly if regenerated later.
                g_voxelStreaming.featureReadySet.erase(key);
                g_voxelStreaming.featureContinuationReadySet.erase(key);
                g_voxelStreaming.featureInProgress.erase(key);
                g_voxelStreaming.surfaceReadySet.erase(key);
                g_voxelStreaming.baseInProgress.erase(key);
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                state.generated = false;
                state.postFeaturesComplete = false;
                state.surfaceFoliageComplete = false;
                state.desired = (g_voxelStreaming.desired.count(key) > 0);
                state.hasSection = (voxelWorld.sections.count(key) > 0);
                state.featureDependencyReadyMask = 0;
                state.featureDependencyMaskInitialized = false;
                state.touchFrame = g_voxelStreaming.frameCounter;
                state.stage = state.desired
                    ? VoxelChunkLifecycleStage::Desired
                    : VoxelChunkLifecycleStage::Unknown;
                onBaseReadinessChanged(key);
            };
            auto minDistToAabbXZ = [&](const glm::vec2& p, const glm::vec2& minB, const glm::vec2& maxB) {
                float dx = 0.0f;
                if (p.x < minB.x) dx = minB.x - p.x;
                else if (p.x > maxB.x) dx = p.x - maxB.x;
                float dz = 0.0f;
                if (p.y < minB.y) dz = minB.y - p.y;
                else if (p.y > maxB.y) dz = p.y - maxB.y;
                return std::sqrt(dx * dx + dz * dz);
            };
            auto maxDistToAabbXZ = [&](const glm::vec2& p, const glm::vec2& minB, const glm::vec2& maxB) {
                float dx = std::max(std::abs(p.x - minB.x), std::abs(p.x - maxB.x));
                float dz = std::max(std::abs(p.y - minB.y), std::abs(p.y - maxB.y));
                return std::sqrt(dx * dx + dz * dz);
            };
            auto sectionInActiveVerticalDomain = [&](int sy) {
                if (!verticalDomainGateEnabled || g_verticalDomainMode == 0) return true;
                const int size = sectionSizeForSection(voxelWorld);
                const int scale = 1;
                const int worldMinY = sy * size * scale;
                const int worldMaxY = worldMinY + (size * scale) - 1;
                if (g_verticalDomainMode == 1) {
                    return worldMaxY >= (verticalDomainCutoffY - verticalDomainPrewarmY);
                }
                return worldMinY <= (verticalDomainCutoffY + verticalDomainPrewarmY);
            };

            auto updateWorkStart = std::chrono::steady_clock::now();

            if (g_voxelStreaming.lastCenterSection.x == std::numeric_limits<int>::min()
                || g_voxelStreaming.lastRadius == std::numeric_limits<int>::min()) {
                g_voxelStreaming.desired.clear();
                g_voxelStreaming.pendingDesiredRebuild = true;
            }

            int superChunkSize = 1;
            const int centerYHysteresisSections = std::max(
                0,
                getRegistryInt(baseSystem, "voxelDesiredCenterYHysteresisSections", 2)
            );
            if (superChunkSize < 1) superChunkSize = 1;
            bool movedDesired = false;
            {
                const int radius = streamRadiusWorld(voxelWorld);
                const int size = sectionSizeForSection(voxelWorld);
                const int scale = 1;
                glm::ivec3 cameraCell = glm::ivec3(glm::floor(cameraPos / static_cast<float>(scale)));
                glm::ivec3 centerSection = floorDivVec(cameraCell, size);
                if (centerYHysteresisSections > 0) {
                    const glm::ivec3 prevCenter = g_voxelStreaming.lastCenterSection;
                    if (prevCenter.x != std::numeric_limits<int>::min()
                        && std::abs(centerSection.y - prevCenter.y) <= centerYHysteresisSections) {
                        centerSection.y = prevCenter.y;
                    }
                }
                if (g_voxelStreaming.lastCenterSection != centerSection
                    || g_voxelStreaming.lastRadius != radius
                    || g_voxelStreaming.lastCpuViewYawBucket != cpuViewYawBucket
                    || g_voxelStreaming.lastCpuViewCullingEnabled != cpuStreamingView.enabled) {
                    movedDesired = true;
                }
                g_voxelStreaming.lastCenterSection = centerSection;
                g_voxelStreaming.lastRadius = radius;
                g_voxelStreaming.lastCpuViewYawBucket = cpuViewYawBucket;
                g_voxelStreaming.lastCpuViewCullingEnabled = cpuStreamingView.enabled;
            }
            if (movedDesired) {
                if (!g_voxelDesiredRebuild.active) {
                    g_voxelStreaming.pendingDesiredRebuild = true;
                }
            }
            const int desiredRebuildIntervalFrames = std::max(
                1,
                getRegistryInt(baseSystem, "voxelDesiredRebuildIntervalFrames", 4)
            );
            const int desiredRebuildColumnsPerFrame = std::max(
                16,
                getRegistryInt(baseSystem, "voxelDesiredRebuildColumnsPerFrame", 64)
            );
            const bool schedulerBackpressureEnabled = getRegistryBool(
                baseSystem,
                "voxelSchedulerBackpressureEnabled",
                true
            );
            const size_t schedulerDirtyBacklog = voxelWorld.dirtySections.size();
            const size_t schedulerPreparedBacklog = baseSystem.voxelRender
                ? baseSystem.voxelRender->preparedMeshes.size()
                : 0u;
            const size_t schedulerUploadBacklog = baseSystem.voxelRender
                ? baseSystem.voxelRender->renderBuffersDirty.size()
                : 0u;
            const size_t schedulerDirtySoft = static_cast<size_t>(std::max(
                1,
                getRegistryInt(baseSystem, "voxelSchedulerDirtyBacklogSoft", 24)
            ));
            const size_t schedulerDirtyHard = static_cast<size_t>(std::max(
                static_cast<int>(schedulerDirtySoft),
                getRegistryInt(baseSystem, "voxelSchedulerDirtyBacklogHard", 64)
            ));
            const size_t schedulerPreparedSoft = static_cast<size_t>(std::max(
                1,
                getRegistryInt(baseSystem, "voxelSchedulerPreparedBacklogSoft", 2)
            ));
            const size_t schedulerPreparedHard = static_cast<size_t>(std::max(
                static_cast<int>(schedulerPreparedSoft),
                getRegistryInt(baseSystem, "voxelSchedulerPreparedBacklogHard", 8)
            ));
            const size_t schedulerUploadSoft = static_cast<size_t>(std::max(
                1,
                getRegistryInt(baseSystem, "voxelSchedulerUploadBacklogSoft", 2)
            ));
            const size_t schedulerUploadHard = static_cast<size_t>(std::max(
                static_cast<int>(schedulerUploadSoft),
                getRegistryInt(baseSystem, "voxelSchedulerUploadBacklogHard", 8)
            ));
            int schedulerPressure = 0;
            if (schedulerBackpressureEnabled) {
                const bool hardPressure = schedulerDirtyBacklog >= schedulerDirtyHard
                    || schedulerPreparedBacklog >= schedulerPreparedHard
                    || schedulerUploadBacklog >= schedulerUploadHard;
                const bool softPressure = schedulerDirtyBacklog >= schedulerDirtySoft
                    || schedulerPreparedBacklog >= schedulerPreparedSoft
                    || schedulerUploadBacklog >= schedulerUploadSoft;
                schedulerPressure = hardPressure ? 2 : (softPressure ? 1 : 0);
            }
            const int schedulerBudgetDivisor = schedulerPressure >= 2
                ? 4
                : (schedulerPressure == 1 ? 2 : 1);
            auto throttleIntBudget = [&](int budget, int floorBudget) {
                if (!schedulerBackpressureEnabled || schedulerPressure <= 0 || budget <= 0) {
                    return budget;
                }
                return std::max(floorBudget, budget / schedulerBudgetDivisor);
            };
            auto throttleMsBudget = [&](float budgetMs, float floorBudgetMs) {
                if (!schedulerBackpressureEnabled || schedulerPressure <= 0 || budgetMs <= 0.0f) {
                    return budgetMs;
                }
                return std::max(floorBudgetMs, budgetMs / static_cast<float>(schedulerBudgetDivisor));
            };
            bool rebuildDesired = false;
            std::vector<VoxelSectionKey> committedDesiredOrder;
            if ((g_voxelStreaming.pendingDesiredRebuild || g_voxelStreaming.desired.empty())
                && !g_voxelDesiredRebuild.active) {
                if (g_voxelStreaming.desired.empty()
                    || desiredRebuildIntervalFrames <= 1
                    || (g_voxelStreaming.frameCounter % static_cast<uint64_t>(desiredRebuildIntervalFrames) == 0u)) {
                    g_voxelStreaming.pendingDesiredRebuild = false;
                    g_voxelDesiredRebuild.active = true;
                    g_voxelDesiredRebuild.prevRadius = 0;
                    g_voxelDesiredRebuild.tierPrepared = false;
                    g_voxelDesiredRebuild.orderCursor = 0;
                    g_voxelDesiredRebuild.sectionOrderXZ.clear();
                    g_voxelDesiredRebuild.desired.clear();
                    g_voxelDesiredRebuild.desired.reserve(std::max<size_t>(2048, g_voxelStreaming.desired.size()));
                    g_voxelDesiredRebuild.desiredOrder.clear();
                    g_voxelDesiredRebuild.desiredOrder.reserve(std::max<size_t>(2048, g_voxelStreaming.desired.size()));
                }
            }

            auto enqueueDesiredRebuildKey = [&](const glm::ivec3& fullCoord) {
                const VoxelSectionKey key{fullCoord};
                const bool inserted = g_voxelDesiredRebuild.desired.insert(key).second;
                if (!inserted) return;
                g_voxelDesiredRebuild.desiredOrder.push_back(key);
            };

            const int desiredColumnsFrameBudget = throttleIntBudget(desiredRebuildColumnsPerFrame, 8);
            int desiredColumnsBudget = desiredColumnsFrameBudget;
            bool desiredRebuildFinished = false;
            while (g_voxelDesiredRebuild.active && desiredColumnsBudget > 0) {
                if (!g_voxelDesiredRebuild.tierPrepared) {
                    const int radius = streamRadiusWorld(voxelWorld);
                    if (radius <= 0) {
                        g_voxelDesiredRebuild.prevRadius = radius;
                        g_voxelDesiredRebuild.active = false;
                        g_voxelDesiredRebuild.tierPrepared = false;
                        desiredRebuildFinished = true;
                        break;
                    }

                    const int size = sectionSizeForSection(voxelWorld);
                    const int scale = 1;
                    const glm::ivec3 cameraCell = glm::ivec3(glm::floor(cameraPos / static_cast<float>(scale)));
                    glm::ivec3 centerSection = g_voxelStreaming.lastCenterSection;
                    if (centerSection.x == std::numeric_limits<int>::min()) {
                        centerSection = floorDivVec(cameraCell, size);
                    }
                    const int sectionRadius = static_cast<int>(std::ceil(
                        static_cast<float>(radius) / static_cast<float>(size * scale)
                    ));
                    const int minY = streamMinYTier0;
                    const int maxY = computeExpanseMaxY(baseSystem, worldCtx, cfg);
                    const int minSectionY = floorDivInt(minY, scale * size);
                    const int maxSectionY = floorDivInt(maxY, scale * size);
                    if (minSectionY > maxSectionY) {
                        g_voxelDesiredRebuild.prevRadius = radius;
                        g_voxelDesiredRebuild.active = false;
                        g_voxelDesiredRebuild.tierPrepared = false;
                        desiredRebuildFinished = true;
                        break;
                    }

                    float cameraSurface = 0.0f;
                    const bool cameraOnLand = ExpanseBiomeSystemLogic::SampleTerrain(
                        worldCtx,
                        cameraPos.x,
                        cameraPos.z,
                        cameraSurface
                    );
                    const int targetY = cameraOnLand
                        ? static_cast<int>(std::floor(cameraSurface))
                        : static_cast<int>(std::floor(cfg.waterSurface));
                    const int tierSurfaceCenterY = floorDivInt(targetY, scale * size);

                    g_voxelDesiredRebuild.radius = radius;
                    g_voxelDesiredRebuild.size = size;
                    g_voxelDesiredRebuild.scale = scale;
                    g_voxelDesiredRebuild.minSectionY = minSectionY;
                    g_voxelDesiredRebuild.maxSectionY = maxSectionY;
                    g_voxelDesiredRebuild.tierSurfaceCenterY = tierSurfaceCenterY;
                    g_voxelDesiredRebuild.orderCursor = 0;
                    g_voxelDesiredRebuild.sectionOrderXZ.clear();
                    g_voxelDesiredRebuild.sectionOrderXZ.reserve(static_cast<size_t>((sectionRadius * 2 + 1) * (sectionRadius * 2 + 1)));
                    for (int ring = 0; ring <= sectionRadius; ++ring) {
                        for (int dz = -ring; dz <= ring; ++dz) {
                            for (int dx = -ring; dx <= ring; ++dx) {
                                if (std::max(std::abs(dx), std::abs(dz)) != ring) continue;
                                const glm::ivec3 sectionCoord = centerSection + glm::ivec3(dx, 0, dz);
                                const glm::vec2 minB = glm::vec2(sectionCoord.x * size * scale, sectionCoord.z * size * scale);
                                const glm::vec2 maxB = minB + glm::vec2(size * scale);
                                const glm::vec2 camXZ(cameraPos.x, cameraPos.z);
                                const float minDist = minDistToAabbXZ(camXZ, minB, maxB);
                                const float maxDist = maxDistToAabbXZ(camXZ, minB, maxB);
                                if (minDist > static_cast<float>(radius)) continue;
                                if (!sectionColumnPassesCpuStreamingView(
                                        cpuStreamingView,
                                        sectionCoord,
                                        size,
                                        scale
                                    )) {
                                    continue;
                                }
                                if (g_voxelDesiredRebuild.prevRadius > 0
                                    && maxDist <= static_cast<float>(g_voxelDesiredRebuild.prevRadius)) continue;
                                g_voxelDesiredRebuild.sectionOrderXZ.push_back(sectionCoord);
                            }
                        }
                    }
                    g_voxelDesiredRebuild.tierPrepared = true;
                }

                if (!g_voxelDesiredRebuild.tierPrepared) continue;

                const int size = g_voxelDesiredRebuild.size;
                const int scale = g_voxelDesiredRebuild.scale;
                const int minSectionY = g_voxelDesiredRebuild.minSectionY;
                const int maxSectionY = g_voxelDesiredRebuild.maxSectionY;
                const int tierSurfaceCenterY = g_voxelDesiredRebuild.tierSurfaceCenterY;
                const int treeVerticalHeadroomBlocks = std::max(
                    0,
                    getRegistryInt(
                        baseSystem,
                        "TreeGenerationVerticalHeadroom",
                        getRegistryInt(baseSystem, "ExpanseVerticalHeadroom", 48)
                    )
                );
                const int treeVerticalHeadroomSections =
                    (treeVerticalHeadroomBlocks + size * scale - 1) / (size * scale);

                {
                    auto enqueueDesiredSection = [&](const glm::ivec3& sectionCoord, int sy) {
                        if (!sectionInActiveVerticalDomain(sy)) return;
                        if (superChunkSize > 1) {
                            const glm::ivec3 anchorCoord(
                                floorDivInt(sectionCoord.x, superChunkSize) * superChunkSize,
                                sy,
                                floorDivInt(sectionCoord.z, superChunkSize) * superChunkSize
                            );
                            for (int oz = 0; oz < superChunkSize; ++oz) {
                                for (int ox = 0; ox < superChunkSize; ++ox) {
                                    enqueueDesiredRebuildKey(glm::ivec3(anchorCoord.x + ox, sy, anchorCoord.z + oz));
                                }
                            }
                        } else {
                            enqueueDesiredRebuildKey(glm::ivec3(sectionCoord.x, sy, sectionCoord.z));
                        }
                    };

                    while (desiredColumnsBudget > 0
                           && g_voxelDesiredRebuild.orderCursor < g_voxelDesiredRebuild.sectionOrderXZ.size()) {
                        const glm::ivec3 sectionCoord = g_voxelDesiredRebuild.sectionOrderXZ[g_voxelDesiredRebuild.orderCursor++];
                        std::vector<int> yOrder;
                        yOrder.reserve(static_cast<size_t>(5 * (treeVerticalHeadroomSections + 1)));
                        auto pushUniqueInRange = [&](int sy) {
                            if (sy < minSectionY || sy > maxSectionY) return;
                            for (int existing : yOrder) {
                                if (existing == sy) return;
                            }
                            yOrder.push_back(sy);
                        };
                        const float minWX = static_cast<float>(sectionCoord.x * size * scale);
                        const float minWZ = static_cast<float>(sectionCoord.z * size * scale);
                        const float maxWX = minWX + static_cast<float>(size * scale) - 1.0f;
                        const float maxWZ = minWZ + static_cast<float>(size * scale) - 1.0f;
                        const std::array<glm::vec2, 5> terrainSamples = {
                            glm::vec2((minWX + maxWX) * 0.5f, (minWZ + maxWZ) * 0.5f),
                            glm::vec2(minWX + 0.5f, minWZ + 0.5f),
                            glm::vec2(maxWX - 0.5f, minWZ + 0.5f),
                            glm::vec2(minWX + 0.5f, maxWZ - 0.5f),
                            glm::vec2(maxWX - 0.5f, maxWZ - 0.5f)
                        };
                        for (const glm::vec2& sampleXZ : terrainSamples) {
                            float terrainHeight = 0.0f;
                            const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, sampleXZ.x, sampleXZ.y, terrainHeight);
                            const int targetY = isLand
                                ? static_cast<int>(std::floor(terrainHeight))
                                : static_cast<int>(std::floor(cfg.waterSurface));
                            const int surfaceSectionY = floorDivInt(targetY, scale * size);
                            pushUniqueInRange(surfaceSectionY);
                            for (int headroom = 1; headroom <= treeVerticalHeadroomSections; ++headroom) {
                                pushUniqueInRange(surfaceSectionY + headroom);
                            }
                        }
                        if (unifiedDepthsEnabled && g_verticalDomainMode == 2) {
                            const int unifiedTopSectionY = floorDivInt(streamPortalY - 1, scale * size);
                            const int unifiedMinSectionY = floorDivInt(unifiedDepthsMinY, scale * size);
                            for (int sy = unifiedTopSectionY; sy >= unifiedMinSectionY; --sy) {
                                pushUniqueInRange(sy);
                            }
                        }
                        for (int sy : yOrder) {
                            enqueueDesiredSection(sectionCoord, sy);
                        }
                        desiredColumnsBudget -= 1;
                    }
                }

                const bool rebuildPassDone =
                    (g_voxelDesiredRebuild.orderCursor >= g_voxelDesiredRebuild.sectionOrderXZ.size());
                if (rebuildPassDone) {
                    g_voxelDesiredRebuild.prevRadius = g_voxelDesiredRebuild.radius;
                    g_voxelDesiredRebuild.tierPrepared = false;
                    g_voxelDesiredRebuild.orderCursor = 0;
                    g_voxelDesiredRebuild.sectionOrderXZ.clear();
                    g_voxelDesiredRebuild.active = false;
                    desiredRebuildFinished = true;
                }
            }

            if (desiredRebuildFinished) {
                const int desiredHardCap = std::max(
                    0,
                    getRegistryInt(baseSystem, "voxelDesiredHardCap", 0)
                );
                if (desiredHardCap > 0
                    && static_cast<int>(g_voxelDesiredRebuild.desiredOrder.size()) > desiredHardCap) {
                    struct DesiredCandidate {
                        VoxelSectionKey key;
                        float dist2 = 0.0f;
                        float yDist = 0.0f;
                    };
                    std::vector<DesiredCandidate> candidates;
                    candidates.reserve(g_voxelDesiredRebuild.desiredOrder.size());
                    for (const auto& key : g_voxelDesiredRebuild.desiredOrder) {
                        const int sectionSize = sectionSizeForSection(voxelWorld);
                        const int scale = 1;
                        const float span = static_cast<float>(sectionSize * scale);
                        const glm::vec3 center(
                            (static_cast<float>(key.coord.x) + 0.5f) * span,
                            (static_cast<float>(key.coord.y) + 0.5f) * span,
                            (static_cast<float>(key.coord.z) + 0.5f) * span
                        );
                        const glm::vec3 d = center - cameraPos;
                        candidates.push_back(DesiredCandidate{
                            key,
                            (d.x * d.x + d.z * d.z),
                            std::abs(d.y)
                        });
                    }
                    std::nth_element(
                        candidates.begin(),
                        candidates.begin() + desiredHardCap,
                        candidates.end(),
                        [](const DesiredCandidate& a, const DesiredCandidate& b) {
                            if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
                            if (a.yDist != b.yDist) return a.yDist < b.yDist;
                            if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
                            if (a.key.coord.y != b.key.coord.y) return a.key.coord.y < b.key.coord.y;
                            return a.key.coord.z < b.key.coord.z;
                        }
                    );
                    candidates.resize(static_cast<size_t>(desiredHardCap));
                    std::sort(
                        candidates.begin(),
                        candidates.end(),
                        [](const DesiredCandidate& a, const DesiredCandidate& b) {
                            if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
                            if (a.yDist != b.yDist) return a.yDist < b.yDist;
                            if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
                            if (a.key.coord.y != b.key.coord.y) return a.key.coord.y < b.key.coord.y;
                            return a.key.coord.z < b.key.coord.z;
                        }
                    );

                    std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> cappedDesired;
                    cappedDesired.reserve(candidates.size());
                    std::vector<VoxelSectionKey> cappedOrder;
                    cappedOrder.reserve(candidates.size());
                    for (const auto& c : candidates) {
                        if (cappedDesired.insert(c.key).second) {
                            cappedOrder.push_back(c.key);
                        }
                    }
                    g_voxelDesiredRebuild.desired.swap(cappedDesired);
                    g_voxelDesiredRebuild.desiredOrder.swap(cappedOrder);

                }
                g_voxelStreaming.desired.swap(g_voxelDesiredRebuild.desired);
                committedDesiredOrder.swap(g_voxelDesiredRebuild.desiredOrder);
                g_voxelDesiredRebuild = {};
                rebuildDesired = true;
                std::vector<VoxelSectionKey> stateEraseList;
                stateEraseList.reserve(voxelWorld.chunkStates.size());
                for (auto& entry : voxelWorld.chunkStates) {
                    const VoxelSectionKey& stateKey = entry.first;
                    VoxelChunkLifecycleState& state = entry.second;
                    const bool isDesiredNow = (g_voxelStreaming.desired.count(stateKey) > 0);
                    state.desired = isDesiredNow;
                    state.touchFrame = g_voxelStreaming.frameCounter;
                    if (!isDesiredNow
                        && !state.generated
                        && !state.hasSection
                        && g_voxelStreaming.baseReadySet.count(stateKey) == 0
                        && g_voxelStreaming.baseInProgress.count(stateKey) == 0
                        && g_voxelSectionJobs.count(stateKey) == 0
                        && g_voxelStreaming.featureReadySet.count(stateKey) == 0
                        && g_voxelStreaming.featureContinuationReadySet.count(stateKey) == 0
                        && g_voxelStreaming.featureInProgress.count(stateKey) == 0) {
                        stateEraseList.push_back(stateKey);
                    } else if (!isDesiredNow
                               && state.stage == VoxelChunkLifecycleStage::Desired) {
                        state.stage = VoxelChunkLifecycleStage::Unknown;
                    }
                }
                for (const auto& key : stateEraseList) {
                    voxelWorld.eraseChunkState(key);
                }
            }

            if (rebuildDesired) {
                for (const auto& key : committedDesiredOrder) {
                    markChunkDesired(key, true);
                    if (!shouldQueueKey(key)) continue;
                    enqueuePendingKey(key);
                }
                // Event-driven feature scheduling: when desired set changes, re-evaluate
                // already-generated sections against current neighbor requirements.
                for (const auto& key : g_voxelTerrainGenerated) {
                    queueFeatureIfReady(key);
                }
            }

            if (rebuildDesired) {
                std::vector<VoxelSectionKey> toRemove;
                toRemove.reserve(voxelWorld.sections.size());
                glm::vec2 camXZ(cameraPos.x, cameraPos.z);
                const int radius = streamRadiusWorld(voxelWorld);
                const int size = sectionSizeForSection(voxelWorld);
                const int scale = 1;
                const float keepRadius = static_cast<float>(radius + size * scale);
                for (const auto& [key, _] : voxelWorld.sections) {
                    if (g_voxelStreaming.desired.count(key) > 0) continue;
                    if (radius <= 0) {
                        toRemove.push_back(key);
                        continue;
                    }
                    if (cpuStreamingView.enabled) {
                        toRemove.push_back(key);
                    } else {
                        glm::vec2 minB = glm::vec2(key.coord.x * size * scale, key.coord.z * size * scale);
                        glm::vec2 maxB = minB + glm::vec2(size * scale);
                        float minDist = minDistToAabbXZ(camXZ, minB, maxB);
                        if (minDist > keepRadius) {
                            toRemove.push_back(key);
                        }
                    }
                }
                for (const auto& key : toRemove) {
                    voxelWorld.releaseSection(key);
                    if (g_voxelTerrainGenerated.erase(key) > 0) {
                        onGeneratedSectionUnavailable(key);
                    }
                    g_voxelSectionJobs.erase(key);
                    g_voxelStreaming.baseInProgress.erase(key);
                    g_voxelStreaming.featureInProgress.erase(key);
                    markChunkDesired(key, false);
                    if (g_voxelStreaming.baseReadySet.count(key) == 0
                        && g_voxelStreaming.baseInProgress.count(key) == 0
                        && g_voxelSectionJobs.count(key) == 0
                        && g_voxelTerrainGenerated.count(key) == 0
                        && g_voxelStreaming.featureReadySet.count(key) == 0
                        && g_voxelStreaming.featureContinuationReadySet.count(key) == 0
                        && g_voxelStreaming.featureInProgress.count(key) == 0) {
                        voxelWorld.eraseChunkState(key);
                    }
                }
            }

            if (rebuildDesired && !g_voxelTerrainGenerated.empty()) {
                for (auto it = g_voxelTerrainGenerated.begin(); it != g_voxelTerrainGenerated.end(); ) {
                    if (g_voxelStreaming.desired.count(*it) == 0) {
                        const VoxelSectionKey key = *it;
                        it = g_voxelTerrainGenerated.erase(it);
                        onGeneratedSectionUnavailable(key);
                        markChunkDesired(key, false);
                        if (g_voxelStreaming.baseReadySet.count(key) == 0
                            && g_voxelStreaming.baseInProgress.count(key) == 0
                            && g_voxelSectionJobs.count(key) == 0
                            && g_voxelStreaming.featureReadySet.count(key) == 0
                            && g_voxelStreaming.featureContinuationReadySet.count(key) == 0
                            && g_voxelStreaming.featureInProgress.count(key) == 0
                            && voxelWorld.sections.count(key) == 0) {
                            voxelWorld.eraseChunkState(key);
                        }
                    } else {
                        ++it;
                    }
                }
            }

            int filteredOut = 0;
            int rescueSurfaceQueued = 0;
            int rescueMissingQueued = 0;

            // Drop pending entries that are no longer desired.
            if (rebuildDesired && !g_voxelStreaming.baseReady.empty()) {
                std::vector<VoxelSectionKey> filtered;
                filtered.reserve(g_voxelStreaming.baseReady.size());
                for (const auto& key : g_voxelStreaming.baseReady) {
                    if (g_voxelStreaming.desired.count(key) > 0
                        && shouldQueueKey(key)) {
                        filtered.push_back(key);
                    } else {
                        g_voxelStreaming.baseReadySet.erase(key);
                        g_voxelSectionJobs.erase(key);
                        g_voxelStreaming.baseInProgress.erase(key);
                        g_voxelStreaming.featureInProgress.erase(key);
                        markChunkDesired(key, false);
                        if (g_voxelTerrainGenerated.count(key) == 0
                            && g_voxelStreaming.featureReadySet.count(key) == 0
                            && g_voxelStreaming.featureContinuationReadySet.count(key) == 0
                            && g_voxelStreaming.featureInProgress.count(key) == 0
                            && voxelWorld.sections.count(key) == 0) {
                            voxelWorld.eraseChunkState(key);
                        }
                        filteredOut += 1;
                    }
                }
                g_voxelStreaming.baseReady.swap(filtered);
            }

            // Additional TIER0 surface rescue: explicitly queue missing near-camera surface bands.
            // This protects against persistent checkerboard holes when the player is moving while
            // generation budgets are tight.
            {
                const int tier0SurfaceRescuePerFrame = 12;
                const int tier0SurfaceRescueIntervalFrames = 8;
                const int tier0SurfaceRescueSkipPendingAbove = 384;
                const bool surfaceRescueBacklogged =
                    tier0SurfaceRescueSkipPendingAbove > 0
                    && static_cast<int>(g_voxelStreaming.baseReady.size()) >= tier0SurfaceRescueSkipPendingAbove;
                const bool runSurfaceRescue =
                    rebuildDesired
                    || (g_voxelStreaming.frameCounter % static_cast<uint64_t>(tier0SurfaceRescueIntervalFrames) == 0u);
                if (tier0SurfaceRescuePerFrame > 0 && runSurfaceRescue && !surfaceRescueBacklogged) {
                    const int size = sectionSizeForSection(voxelWorld);
                    const int scale = 1;
                    const int radius = streamRadiusWorld(voxelWorld);
                    if (radius > 0) {
                        const int sectionRadius = static_cast<int>(std::ceil(static_cast<float>(radius) / static_cast<float>(size * scale)));
                        glm::ivec3 cameraCell = glm::ivec3(glm::floor(cameraPos / static_cast<float>(scale)));
                        glm::ivec3 centerSection = floorDivVec(cameraCell, size);
                        const int tier0SurfaceDepthSections = std::max(
                            0,
                            getRegistryInt(baseSystem, "voxelSurfaceDepthSections", 1)
                        );
                        const int tier0SurfaceUpSections = std::max(
                            0,
                            getRegistryInt(baseSystem, "voxelSurfaceUpSections", 0)
                        );
                        const int tier0CameraVerticalPadSections = std::max(
                            0,
                            getRegistryInt(baseSystem, "voxelSurfaceCameraPadSections", 0)
                        );
                        const int minY = streamMinYTier0;
                        const int maxY = computeExpanseMaxY(baseSystem, worldCtx, cfg);
                        const int minSectionY = floorDivInt(minY, scale * size);
                        const int maxSectionY = floorDivInt(maxY, scale * size);
                        glm::vec2 camXZ(cameraPos.x, cameraPos.z);
                        struct RescueCandidate {
                            VoxelSectionKey key;
                            float dist2 = 0.0f;
                            int yPriority = 0;
                        };
                        std::vector<RescueCandidate> candidates;
                        candidates.reserve(256);

                        auto enqueueRescue = [&](const glm::ivec3& sectionCoord, int sy, float dist2, int yPriority) {
                            if (sy < minSectionY || sy > maxSectionY) return;
                            if (!sectionInActiveVerticalDomain(sy)) return;
                            VoxelSectionKey key{glm::ivec3(sectionCoord.x, sy, sectionCoord.z)};
                            if (!shouldQueueKey(key)) return;
                            if (g_voxelStreaming.baseReadySet.count(key) > 0) return;
                            if (g_voxelStreaming.baseInProgress.count(key) > 0) return;
                            candidates.push_back(RescueCandidate{key, dist2, yPriority});
                        };

                        for (int dz = -sectionRadius; dz <= sectionRadius; ++dz) {
                            for (int dx = -sectionRadius; dx <= sectionRadius; ++dx) {
                                glm::ivec3 sectionCoord = centerSection + glm::ivec3(dx, 0, dz);
                                glm::vec2 minB = glm::vec2(sectionCoord.x * size * scale, sectionCoord.z * size * scale);
                                glm::vec2 maxB = minB + glm::vec2(size * scale);
                                float minDist = minDistToAabbXZ(camXZ, minB, maxB);
                                if (minDist > static_cast<float>(radius)) continue;
                                if (!sectionColumnPassesCpuStreamingView(
                                        cpuStreamingView,
                                        sectionCoord,
                                        size,
                                        scale
                                    )) {
                                    continue;
                                }

                                float centerX = (static_cast<float>(sectionCoord.x) + 0.5f) * static_cast<float>(size * scale);
                                float centerZ = (static_cast<float>(sectionCoord.z) + 0.5f) * static_cast<float>(size * scale);
                                float dxC = centerX - cameraPos.x;
                                float dzC = centerZ - cameraPos.z;
                                float dist2 = dxC * dxC + dzC * dzC;

                                const float minWX = static_cast<float>(sectionCoord.x * size * scale);
                                const float minWZ = static_cast<float>(sectionCoord.z * size * scale);
                                const float maxWX = minWX + static_cast<float>(size * scale) - 1.0f;
                                const float maxWZ = minWZ + static_cast<float>(size * scale) - 1.0f;
                                const std::array<glm::vec2, 5> terrainSamples = {
                                    glm::vec2((minWX + maxWX) * 0.5f, (minWZ + maxWZ) * 0.5f),
                                    glm::vec2(minWX + 0.5f, minWZ + 0.5f),
                                    glm::vec2(maxWX - 0.5f, minWZ + 0.5f),
                                    glm::vec2(minWX + 0.5f, maxWZ - 0.5f),
                                    glm::vec2(maxWX - 0.5f, maxWZ - 0.5f)
                                };

                                for (const glm::vec2& sampleXZ : terrainSamples) {
                                    float terrainHeight = 0.0f;
                                    bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, sampleXZ.x, sampleXZ.y, terrainHeight);
                                    int targetY = isLand
                                        ? static_cast<int>(std::floor(terrainHeight))
                                        : static_cast<int>(std::floor(cfg.waterSurface));
                                    int surfaceSectionY = floorDivInt(targetY, scale * size);

                                    enqueueRescue(sectionCoord, surfaceSectionY, dist2, 0);
                                    for (int up = 1; up <= tier0SurfaceUpSections; ++up) {
                                        enqueueRescue(sectionCoord, surfaceSectionY + up, dist2, up);
                                    }
                                    for (int down = 1; down <= tier0SurfaceDepthSections; ++down) {
                                        enqueueRescue(sectionCoord, surfaceSectionY - down, dist2, down);
                                    }
                                }

                                for (int pad = -tier0CameraVerticalPadSections; pad <= tier0CameraVerticalPadSections; ++pad) {
                                    enqueueRescue(sectionCoord, centerSection.y + pad, dist2, std::abs(pad) + 1);
                                }
                            }
                        }

                        if (!candidates.empty()) {
                            std::sort(candidates.begin(), candidates.end(), [](const RescueCandidate& a, const RescueCandidate& b) {
                                if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
                                if (a.yPriority != b.yPriority) return a.yPriority < b.yPriority;
                                if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
                                if (a.key.coord.y != b.key.coord.y) return a.key.coord.y < b.key.coord.y;
                                return a.key.coord.z < b.key.coord.z;
                            });

                            const int rescueCount = std::min<int>(tier0SurfaceRescuePerFrame, static_cast<int>(candidates.size()));
                            for (int i = 0; i < rescueCount; ++i) {
                                const VoxelSectionKey key = candidates[static_cast<size_t>(i)].key;
                                if (g_voxelStreaming.baseReadySet.count(key) > 0) continue;
                                if (g_voxelStreaming.baseInProgress.count(key) > 0) continue;
                                enqueuePendingKey(key);
                                g_voxelStreaming.desired.insert(key);
                                markChunkDesired(key, true);
                                rescueSurfaceQueued += 1;
                            }
                        }
                    }
                }
            }

            // Safety net: if near-camera TIER0 sections are missing and not queued, re-queue them
            // even when desired-set rebuild is not triggered this frame.
            {
                const int rescueBudget = 16;
                const int tier0RescueIntervalFrames = 12;
                const int tier0RescueSkipPendingAbove = 384;
                const bool rescueBacklogged =
                    tier0RescueSkipPendingAbove > 0
                    && static_cast<int>(g_voxelStreaming.baseReady.size()) >= tier0RescueSkipPendingAbove;
                const int tier0RescueScanMaxDesired = 512;
                const bool runMissingRescue =
                    rebuildDesired
                    || (g_voxelStreaming.frameCounter % static_cast<uint64_t>(tier0RescueIntervalFrames) == 0u);
                if (rescueBudget > 0 && runMissingRescue && !rescueBacklogged && !g_voxelStreaming.desired.empty()) {
                    if (rebuildDesired
                        || g_tier0RescueScanList.empty()
                        || g_tier0RescueScanHead >= g_tier0RescueScanList.size()) {
                        g_tier0RescueScanList.clear();
                        g_tier0RescueScanList.reserve(g_voxelStreaming.desired.size());
                        for (const auto& desiredKey : g_voxelStreaming.desired) {
                            g_tier0RescueScanList.push_back(desiredKey);
                        }
                        g_tier0RescueScanHead = 0;
                    }
                    struct MissingTier0 {
                        VoxelSectionKey key;
                        float dist2 = 0.0f;
                        float yDist = 0.0f;
                    };
                    std::vector<MissingTier0> missing;
                    missing.reserve(256);

                    const int size = sectionSizeForSection(voxelWorld);
                    const int scale = 1;
                    glm::ivec3 cameraCell = glm::ivec3(glm::floor(cameraPos / static_cast<float>(scale)));
                    glm::ivec3 centerSection = floorDivVec(cameraCell, size);
                    int tierSurfaceCenterY = centerSection.y;
                    {
                        float cameraSurface = 0.0f;
                        bool cameraOnLand = ExpanseBiomeSystemLogic::SampleTerrain(worldCtx, cameraPos.x, cameraPos.z, cameraSurface);
                        int targetY = cameraOnLand
                            ? static_cast<int>(std::floor(cameraSurface))
                            : static_cast<int>(std::floor(cfg.waterSurface));
                        tierSurfaceCenterY = floorDivInt(targetY, scale * size);
                    }
                    const int radius = streamRadiusWorld(voxelWorld);
                    const int tier0RescueScanPerFrame = 256;

                    int scannedDesired = 0;
                    while (g_tier0RescueScanHead < g_tier0RescueScanList.size()) {
                        if (tier0RescueScanMaxDesired > 0 && scannedDesired >= tier0RescueScanMaxDesired) break;
                        if (scannedDesired >= tier0RescueScanPerFrame) break;
                        const VoxelSectionKey key = g_tier0RescueScanList[g_tier0RescueScanHead++];
                        scannedDesired += 1;
                        if (!sectionInActiveVerticalDomain(key.coord.y)) continue;
                        if (!shouldQueueKey(key)) continue;
                        if (g_voxelStreaming.baseReadySet.count(key) > 0) continue;
                        if (g_voxelStreaming.baseInProgress.count(key) > 0) continue;

                        glm::vec2 minB = glm::vec2(key.coord.x * size * scale, key.coord.z * size * scale);
                        glm::vec2 maxB = minB + glm::vec2(size * scale);
                        glm::vec2 camXZ(cameraPos.x, cameraPos.z);
                        float minDist = minDistToAabbXZ(camXZ, minB, maxB);
                        if (radius > 0 && minDist > static_cast<float>(radius)) continue;

                        float centerX = (static_cast<float>(key.coord.x) + 0.5f) * static_cast<float>(size * scale);
                        float centerZ = (static_cast<float>(key.coord.z) + 0.5f) * static_cast<float>(size * scale);
                        float dx = centerX - cameraPos.x;
                        float dz = centerZ - cameraPos.z;
                        missing.push_back(MissingTier0{
                            key,
                            dx * dx + dz * dz,
                            static_cast<float>(std::abs(key.coord.y - tierSurfaceCenterY))
                        });
                    }
                    if (g_tier0RescueScanHead >= g_tier0RescueScanList.size()) {
                        g_tier0RescueScanHead = 0;
                        g_tier0RescueScanList.clear();
                    }

                    if (!missing.empty()) {
                        std::sort(missing.begin(), missing.end(), [](const MissingTier0& a, const MissingTier0& b) {
                            if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
                            if (a.yDist != b.yDist) return a.yDist < b.yDist;
                            if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
                            if (a.key.coord.y != b.key.coord.y) return a.key.coord.y < b.key.coord.y;
                            return a.key.coord.z < b.key.coord.z;
                        });

                        const int limit = std::min<int>(rescueBudget, static_cast<int>(missing.size()));
                        for (int i = 0; i < limit; ++i) {
                            const VoxelSectionKey key = missing[static_cast<size_t>(i)].key;
                            if (g_voxelStreaming.baseReadySet.count(key) > 0) continue;
                            if (g_voxelStreaming.baseInProgress.count(key) > 0) continue;
                            enqueuePendingKey(key);
                            rescueMissingQueued += 1;
                        }
                    }
                }
            }

            auto pendingNearCameraLess = [&](const VoxelSectionKey& a, const VoxelSectionKey& b) {
                int aSize = sectionSizeForSection(voxelWorld);
                int bSize = sectionSizeForSection(voxelWorld);
                int aScale = 1;
                int bScale = 1;

                float aCenterX = (static_cast<float>(a.coord.x) + 0.5f) * static_cast<float>(aSize * aScale);
                float aCenterZ = (static_cast<float>(a.coord.z) + 0.5f) * static_cast<float>(aSize * aScale);
                float bCenterX = (static_cast<float>(b.coord.x) + 0.5f) * static_cast<float>(bSize * bScale);
                float bCenterZ = (static_cast<float>(b.coord.z) + 0.5f) * static_cast<float>(bSize * bScale);

                float aDx = aCenterX - cameraPos.x;
                float aDz = aCenterZ - cameraPos.z;
                float bDx = bCenterX - cameraPos.x;
                float bDz = bCenterZ - cameraPos.z;
                float aDist2 = aDx * aDx + aDz * aDz;
                float bDist2 = bDx * bDx + bDz * bDz;
                if (aDist2 != bDist2) return aDist2 < bDist2;

                float aCenterY = (static_cast<float>(a.coord.y) + 0.5f) * static_cast<float>(aSize * aScale);
                float bCenterY = (static_cast<float>(b.coord.y) + 0.5f) * static_cast<float>(bSize * bScale);
                float aDy = std::abs(aCenterY - cameraPos.y);
                float bDy = std::abs(bCenterY - cameraPos.y);
                if (aDy != bDy) return aDy < bDy;

                if (a.coord.y != b.coord.y) return a.coord.y < b.coord.y;
                if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
                return a.coord.z < b.coord.z;
            };

            int reprioritizedCount = 0;
            int droppedByCap = 0;
            const int pendingHardCap = std::max(0, getRegistryInt(baseSystem, "voxelPendingHardCap", 8192));
            const int pendingResortIntervalFrames = std::max(1, getRegistryInt(baseSystem, "voxelPendingResortIntervalFrames", 6));
            const int pendingResortWhenAbove = std::max(0, getRegistryInt(baseSystem, "voxelPendingResortWhenAbove", 1024));
            const int pendingResortWindow = std::max(0, getRegistryInt(baseSystem, "voxelPendingResortWindow", 4096));
            const int pendingResortSliceWindow = std::max(
                128,
                getRegistryInt(baseSystem, "voxelPendingResortSliceWindow", 4096)
            );
            const bool pendingResortRotateSlices = getRegistryBool(
                baseSystem,
                "voxelPendingResortRotateSlices",
                false
            );
            const bool shouldResortPending =
                rebuildDesired
                || (pendingHardCap > 0 && static_cast<int>(g_voxelStreaming.baseReady.size()) > pendingHardCap)
                || ((static_cast<int>(g_voxelStreaming.baseReady.size()) > pendingResortWhenAbove)
                    && (g_voxelStreaming.frameCounter % static_cast<uint64_t>(pendingResortIntervalFrames) == 0u));
            if (shouldResortPending && !g_voxelStreaming.baseReady.empty()) {
                size_t sortCount = g_voxelStreaming.baseReady.size();
                if (pendingResortWindow > 0 && sortCount > static_cast<size_t>(pendingResortWindow)) {
                    sortCount = static_cast<size_t>(pendingResortWindow);
                }
                if (sortCount > static_cast<size_t>(pendingResortSliceWindow)) {
                    sortCount = static_cast<size_t>(pendingResortSliceWindow);
                }
                if (sortCount == g_voxelStreaming.baseReady.size()) {
                    std::stable_sort(g_voxelStreaming.baseReady.begin(), g_voxelStreaming.baseReady.end(), pendingNearCameraLess);
                    g_pendingResortCursor = 0;
                } else {
                    if (!pendingResortRotateSlices) {
                        std::stable_sort(
                            g_voxelStreaming.baseReady.begin(),
                            g_voxelStreaming.baseReady.begin() + static_cast<std::ptrdiff_t>(sortCount),
                            pendingNearCameraLess
                        );
                        g_pendingResortCursor = 0;
                    } else {
                        const size_t pendingSize = g_voxelStreaming.baseReady.size();
                        const size_t start = pendingSize > 0 ? (g_pendingResortCursor % pendingSize) : 0u;
                        const size_t endExclusive = std::min(pendingSize, start + sortCount);
                        std::stable_sort(
                            g_voxelStreaming.baseReady.begin() + static_cast<std::ptrdiff_t>(start),
                            g_voxelStreaming.baseReady.begin() + static_cast<std::ptrdiff_t>(endExclusive),
                            pendingNearCameraLess
                        );
                        if (start + sortCount > pendingSize) {
                            const size_t wrapped = (start + sortCount) - pendingSize;
                            if (wrapped > 1u) {
                                std::stable_sort(
                                    g_voxelStreaming.baseReady.begin(),
                                    g_voxelStreaming.baseReady.begin() + static_cast<std::ptrdiff_t>(wrapped),
                                    pendingNearCameraLess
                                );
                            }
                        }
                        if (pendingSize > 0) {
                            g_pendingResortCursor = (start + sortCount) % pendingSize;
                        }
                    }
                }
                reprioritizedCount = static_cast<int>(sortCount);
            }
            if (pendingHardCap > 0 && static_cast<int>(g_voxelStreaming.baseReady.size()) > pendingHardCap) {
                auto begin = g_voxelStreaming.baseReady.begin();
                auto keepEnd = begin + pendingHardCap;
                auto end = g_voxelStreaming.baseReady.end();
                std::nth_element(begin, keepEnd, end, pendingNearCameraLess);
                std::sort(begin, keepEnd, pendingNearCameraLess);
                for (auto it = keepEnd; it != end; ++it) {
                    g_voxelStreaming.baseReadySet.erase(*it);
                    g_voxelSectionJobs.erase(*it);
                    g_voxelStreaming.baseInProgress.erase(*it);
                    g_voxelStreaming.featureInProgress.erase(*it);
                    g_voxelStreaming.featureContinuationReadySet.erase(*it);
                    if (g_voxelTerrainGenerated.erase(*it) > 0) {
                        onGeneratedSectionUnavailable(*it);
                    }
                    voxelWorld.releaseSection(*it);
                    markChunkDesired(*it, false);
                    if (g_voxelStreaming.featureReadySet.count(*it) == 0
                        && g_voxelStreaming.featureContinuationReadySet.count(*it) == 0
                        && g_voxelStreaming.featureInProgress.count(*it) == 0) {
                        voxelWorld.eraseChunkState(*it);
                    }
                }
                droppedByCap = static_cast<int>(std::distance(keepEnd, end));
                g_voxelStreaming.baseReady.erase(keepEnd, end);
            }

            constexpr int kGenerationStepsPerFrame = 16;
            constexpr int kMinSectionsBeforeTimeCap = 2;
            constexpr float kGenerationTimeBudgetMs = 8.0f;
            const int generationBudget = throttleIntBudget(
                std::max(1, getRegistryInt(baseSystem, "voxelGenerationStepsPerFrame", kGenerationStepsPerFrame)),
                1
            );
            const int configuredMinSectionsBeforeTimeCap = std::max(
                1,
                getRegistryInt(baseSystem, "voxelGenerationMinSectionsBeforeTimeCap", kMinSectionsBeforeTimeCap)
            );
            const int minSectionsBeforeTimeCap = schedulerPressure > 0
                ? 1
                : configuredMinSectionsBeforeTimeCap;
            const float generationTimeBudgetMs = throttleMsBudget(
                std::max(0.1f, getRegistryFloat(baseSystem, "voxelGenerationMaxMsPerFrame", kGenerationTimeBudgetMs)),
                1.5f
            );
            const int sectionSizeForGeneration = sectionSizeForSection(voxelWorld);
            const int fullColumnsPerSection = sectionSizeForGeneration * sectionSizeForGeneration;
            int sectionColumnsPerStep = std::clamp(
                getRegistryInt(baseSystem, "voxelGenerationColumnsPerStep", fullColumnsPerSection),
                1,
                std::max(1, fullColumnsPerSection)
            );
            if (schedulerPressure > 0) {
                const int pressureColumnsPerStep = std::max(
                    16,
                    fullColumnsPerSection / std::max(1, schedulerBudgetDivisor)
                );
                sectionColumnsPerStep = std::min(sectionColumnsPerStep, pressureColumnsPerStep);
            }
            constexpr bool kResumeInProgressFirst = true;
            constexpr int kCompletionFocusFrontWindow = 4;
            constexpr int kCompletionFocusRepeats = 1;
            const bool resumeInProgressFirst = kResumeInProgressFirst;
            const int completionFocusFrontWindow = kCompletionFocusFrontWindow;
            const int completionFocusRepeats = kCompletionFocusRepeats;
            auto genStart = std::chrono::steady_clock::now();
            const auto baseStageStart = genStart;
            int stepped = 0;
            int built = 0;
            int skippedExisting = 0;
            int consumed = 0;
            std::vector<VoxelSectionKey> requeueFront;
            std::vector<VoxelSectionKey> requeueBack;
            const int pendingCountAtStart = static_cast<int>(g_voxelStreaming.baseReady.size());
            while (consumed < pendingCountAtStart
                   && (generationBudget <= 0 || stepped < generationBudget)) {
                if (generationTimeBudgetMs > 0.0f && consumed >= minSectionsBeforeTimeCap) {
                    float elapsedMs = std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - genStart
                    ).count();
                    if (elapsedMs >= generationTimeBudgetMs) {
                        break;
                    }
                }
                const auto key = g_voxelStreaming.baseReady[static_cast<size_t>(consumed)];
                g_voxelStreaming.baseReadySet.erase(key);
                if (shouldQueueKey(key)) {
                    g_voxelStreaming.baseInProgress.insert(key);
                    markChunkDesired(key, true);
                    markChunkStage(key, VoxelChunkLifecycleStage::BaseInProgress);
                    auto jobIt = g_voxelSectionJobs.find(key);
                    if (jobIt == g_voxelSectionJobs.end()) {
                        voxelWorld.releaseSection(key);
                        auto inserted = g_voxelSectionJobs.emplace(key, VoxelSectionGenerationJob{});
                        jobIt = inserted.first;
                    }
                    bool jobWroteAny = jobIt->second.wroteAny;
                    int nextColumn = jobIt->second.nextColumn;
                    bool completed = false;
                    bool postFeaturesCompleted = true;
                    bool resolved = false;
                    const bool focusCandidate = consumed < completionFocusFrontWindow;
                    const int attemptBudget = focusCandidate ? completionFocusRepeats : 1;
                    int attempts = 0;
                    while (attempts < attemptBudget) {
                        resolved = GenerateExpanseSectionBase(
                            baseSystem,
                            prototypes,
                            worldCtx,
                            cfg,
                            key.coord,
                            nextColumn,
                            sectionColumnsPerStep,
                            jobWroteAny,
                            nextColumn,
                            completed,
                            postFeaturesCompleted
                        );
                        stepped += 1;
                        attempts += 1;
                        if (completed) break;
                        if (generationBudget > 0 && stepped >= generationBudget) break;
                        if (generationTimeBudgetMs > 0.0f) {
                            const float elapsedMs = std::chrono::duration<float, std::milli>(
                                std::chrono::steady_clock::now() - genStart
                            ).count();
                            if (elapsedMs >= generationTimeBudgetMs) {
                                break;
                            }
                        }
                    }
                    if (completed) {
                        g_voxelSectionJobs.erase(key);
                        if (resolved) {
                            const bool insertedGenerated = g_voxelTerrainGenerated.insert(key).second;
                            if (insertedGenerated) {
                                onGeneratedSectionAvailable(key);
                            }
                            if (!postFeaturesCompleted) {
                                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                                state.generated = true;
                                state.postFeaturesComplete = false;
                                state.surfaceFoliageComplete = false;
                                state.hasSection = (voxelWorld.sections.count(key) > 0);
                                state.touchFrame = g_voxelStreaming.frameCounter;
                                state.featureDependencyMaskInitialized = false;
                                markChunkStage(key, VoxelChunkLifecycleStage::BaseGenerated);
                                queueFeatureIfReady(key);
                            } else {
                                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                                state.generated = true;
                                state.postFeaturesComplete = true;
                                state.hasSection = (voxelWorld.sections.count(key) > 0);
                                state.touchFrame = g_voxelStreaming.frameCounter;
                                state.surfaceFoliageComplete = false;
                                tryFinalizeSurfaceFoliage(key, true, true);
                            }
                        } else {
                            if (g_voxelTerrainGenerated.erase(key) > 0) {
                                onGeneratedSectionUnavailable(key);
                            }
                            markChunkStage(key, VoxelChunkLifecycleStage::Desired);
                        }
                        g_voxelStreaming.baseInProgress.erase(key);
                        built += 1;
                    } else {
                        jobIt = g_voxelSectionJobs.find(key);
                        if (jobIt != g_voxelSectionJobs.end()) {
                            jobIt->second.nextColumn = nextColumn;
                            jobIt->second.wroteAny = jobWroteAny;
                        }
                        if (g_voxelStreaming.baseReadySet.insert(key).second) {
                            markChunkStage(key, VoxelChunkLifecycleStage::BaseQueued);
                            const int sectionSize = sectionSizeForSection(voxelWorld);
                            const int totalColumns = sectionSize * sectionSize;
                            const bool postFeatureOnlyWork = (nextColumn >= totalColumns);
                            const bool prioritizePostFeature = getRegistryBool(
                                baseSystem,
                                "voxelSectionGenPrioritizePostFeature",
                                true
                            );
                            // With sliced post-feature passes, starvation hurts chunk completion more than
                            // occasional near-front continuation work. Keep progress flowing by allowing
                            // post-feature continuations to re-enter the front when configured.
                            if (resumeInProgressFirst && (!postFeatureOnlyWork || prioritizePostFeature)) {
                                requeueFront.push_back(key);
                            } else {
                                requeueBack.push_back(key);
                            }
                        }
                        g_voxelStreaming.baseInProgress.erase(key);
                    }
                } else {
                    g_voxelSectionJobs.erase(key);
                    g_voxelStreaming.baseInProgress.erase(key);
                    VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                    state.generated = true;
                    state.postFeaturesComplete = true;
                    state.surfaceFoliageComplete = false;
                    state.hasSection = (voxelWorld.sections.count(key) > 0);
                    state.touchFrame = g_voxelStreaming.frameCounter;
                    tryFinalizeSurfaceFoliage(key, true, true);
                    skippedExisting += 1;
                }
                consumed += 1;
            }
            if (consumed > 0) {
                g_voxelStreaming.baseReady.erase(g_voxelStreaming.baseReady.begin(),
                                               g_voxelStreaming.baseReady.begin() + consumed);
            }
            if (!requeueFront.empty()) {
                g_voxelStreaming.baseReady.insert(g_voxelStreaming.baseReady.begin(),
                                                requeueFront.begin(),
                                                requeueFront.end());
            }
            if (!requeueBack.empty()) {
                g_voxelStreaming.baseReady.insert(g_voxelStreaming.baseReady.end(),
                                                requeueBack.begin(),
                                                requeueBack.end());
            }
            const auto baseStageEnd = std::chrono::steady_clock::now();

            // Continue heavy post-feature terrain phases (waterfalls/chalk/lava/obsidian)
            // outside the base-generation queue so section generation can keep progressing.
            constexpr int kFeatureSectionsPerFrame = 16;
            constexpr float kFeatureTimeBudgetMs = 6.0f;
            const int featureSectionsPerFrame = throttleIntBudget(
                std::max(1, getRegistryInt(baseSystem, "voxelFeatureSectionsPerFrame", kFeatureSectionsPerFrame)),
                1
            );
            const float featureTimeBudgetMs = throttleMsBudget(
                std::max(0.1f, getRegistryFloat(baseSystem, "voxelFeatureMaxMsPerFrame", kFeatureTimeBudgetMs)),
                0.75f
            );
            const auto featureStageStart = std::chrono::steady_clock::now();
            int featureSectionsProcessed = 0;
            int featureDeferredByDeps = 0;
            const bool featureResortEnabled = getRegistryBool(baseSystem, "voxelFeatureResortEnabled", true);
            const int featureResortIntervalFrames = std::max(
                1,
                getRegistryInt(baseSystem, "voxelFeatureResortIntervalFrames", 4)
            );
            const bool featurePrioritizeContinuations = getRegistryBool(
                baseSystem,
                "voxelFeaturePrioritizeContinuations",
                true
            );
            const int featureContinuationBurst = std::max(
                1,
                getRegistryInt(baseSystem, "voxelFeatureContinuationBurst", 3)
            );
            if (featureSectionsPerFrame > 0
                && (g_voxelStreaming.featureReadyHead < g_voxelStreaming.featureReady.size()
                    || !g_voxelStreaming.featureContinuationReady.empty())) {
                if (featureResortEnabled
                    && (g_voxelStreaming.frameCounter % static_cast<uint64_t>(featureResortIntervalFrames) == 0u)
                    && !g_voxelStreaming.featureReady.empty()) {
                    if (g_voxelStreaming.featureReadyHead > 0
                        && g_voxelStreaming.featureReadyHead <= g_voxelStreaming.featureReady.size()) {
                        g_voxelStreaming.featureReady.erase(
                            g_voxelStreaming.featureReady.begin(),
                            g_voxelStreaming.featureReady.begin() + static_cast<std::ptrdiff_t>(g_voxelStreaming.featureReadyHead)
                        );
                        g_voxelStreaming.featureReadyHead = 0;
                    }
                    std::stable_sort(
                        g_voxelStreaming.featureReady.begin(),
                        g_voxelStreaming.featureReady.end(),
                        pendingNearCameraLess
                    );
                }
                const auto featureStart = std::chrono::steady_clock::now();
                int continuationBurstUsed = 0;
                auto popContinuationKey = [&](VoxelSectionKey& outKey) -> bool {
                    while (!g_voxelStreaming.featureContinuationReady.empty()) {
                        const VoxelSectionKey key = g_voxelStreaming.featureContinuationReady.front();
                        g_voxelStreaming.featureContinuationReady.pop_front();
                        if (g_voxelStreaming.featureContinuationReadySet.erase(key) == 0) continue;
                        outKey = key;
                        return true;
                    }
                    return false;
                };
                auto popFreshKey = [&](VoxelSectionKey& outKey) -> bool {
                    while (g_voxelStreaming.featureReadyHead < g_voxelStreaming.featureReady.size()) {
                        const VoxelSectionKey key = g_voxelStreaming.featureReady[g_voxelStreaming.featureReadyHead++];
                        if (g_voxelStreaming.featureReadySet.erase(key) == 0) continue;
                        outKey = key;
                        return true;
                    }
                    return false;
                };
                while (featureSectionsProcessed < featureSectionsPerFrame
                    && (g_voxelStreaming.featureReadyHead < g_voxelStreaming.featureReady.size()
                        || !g_voxelStreaming.featureContinuationReady.empty())) {
                    if (featureTimeBudgetMs > 0.0f && featureSectionsProcessed > 0) {
                        const float elapsedMs = std::chrono::duration<float, std::milli>(
                            std::chrono::steady_clock::now() - featureStart
                        ).count();
                        if (elapsedMs >= featureTimeBudgetMs) break;
                    }
                    VoxelSectionKey key{};
                    bool hasKey = false;
                    if (featurePrioritizeContinuations) {
                        if (continuationBurstUsed < featureContinuationBurst) {
                            hasKey = popContinuationKey(key);
                            if (hasKey) {
                                continuationBurstUsed += 1;
                            }
                        }
                        if (!hasKey) {
                            hasKey = popFreshKey(key);
                            if (hasKey) {
                                continuationBurstUsed = 0;
                            }
                        }
                        if (!hasKey) {
                            hasKey = popContinuationKey(key);
                            if (hasKey) {
                                continuationBurstUsed += 1;
                            }
                        }
                    } else {
                        hasKey = popFreshKey(key);
                        if (!hasKey) hasKey = popContinuationKey(key);
                    }
                    if (!hasKey) break;
                    featureSectionsProcessed += 1;

                    if (g_voxelTerrainGenerated.count(key) == 0) continue;
                    if (g_voxelStreaming.desired.count(key) == 0) continue;
                    if (!areFeatureDependenciesReady(key)) {
                        // Event-driven dependency gating: leave it waiting, and let
                        // future base-readiness transitions re-queue it when ready.
                        markChunkStage(key, VoxelChunkLifecycleStage::FeatureQueued);
                        featureDeferredByDeps += 1;
                        continue;
                    }
                    g_voxelStreaming.featureInProgress.insert(key);
                    markChunkStage(key, VoxelChunkLifecycleStage::FeatureInProgress);

                    bool wroteAny = false;
                    bool postFeaturesCompleted = true;
                    const bool resolved = RunExpanseSectionDecorators(
                        baseSystem,
                        prototypes,
                        worldCtx,
                        cfg,
                        key.coord,
                        wroteAny,
                        postFeaturesCompleted
                    );
                    if (!resolved || !postFeaturesCompleted) {
                        g_voxelStreaming.featureInProgress.erase(key);
                        markChunkStage(key, VoxelChunkLifecycleStage::FeatureQueued);
                        VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                        state.generated = true;
                        state.postFeaturesComplete = false;
                        state.surfaceFoliageComplete = false;
                        state.hasSection = (voxelWorld.sections.count(key) > 0);
                        state.touchFrame = g_voxelStreaming.frameCounter;
                        if (g_voxelStreaming.featureContinuationReadySet.insert(key).second) {
                            g_voxelStreaming.featureContinuationReady.push_back(key);
                        }
                    } else {
                        g_voxelStreaming.featureInProgress.erase(key);
                        g_voxelStreaming.featureContinuationReadySet.erase(key);
                        VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(key);
                        state.generated = true;
                        state.postFeaturesComplete = true;
                        state.surfaceFoliageComplete = false;
                        state.hasSection = (voxelWorld.sections.count(key) > 0);
                        state.touchFrame = g_voxelStreaming.frameCounter;
                        tryFinalizeSurfaceFoliage(key, true, true);
                    }
                }

                if (g_voxelStreaming.featureReadyHead > 0
                    && (g_voxelStreaming.featureReadyHead * 2 >= g_voxelStreaming.featureReady.size()
                        || g_voxelStreaming.featureReadyHead == g_voxelStreaming.featureReady.size())) {
                    g_voxelStreaming.featureReady.erase(
                        g_voxelStreaming.featureReady.begin(),
                        g_voxelStreaming.featureReady.begin() + static_cast<std::ptrdiff_t>(g_voxelStreaming.featureReadyHead)
                    );
                    g_voxelStreaming.featureReadyHead = 0;
                }
            }
            const auto featureStageEnd = std::chrono::steady_clock::now();

            constexpr int kSurfaceSectionsPerFrame = 16;
            constexpr float kSurfaceTimeBudgetMs = 4.0f;
            const int surfaceSectionsPerFrame = throttleIntBudget(
                std::max(1, getRegistryInt(baseSystem, "voxelSurfaceSectionsPerFrame", kSurfaceSectionsPerFrame)),
                1
            );
            const float surfaceTimeBudgetMs = throttleMsBudget(
                std::max(0.1f, getRegistryFloat(baseSystem, "voxelSurfaceMaxMsPerFrame", kSurfaceTimeBudgetMs)),
                0.5f
            );
            const auto surfaceStageStart = std::chrono::steady_clock::now();
            if (surfaceSectionsPerFrame > 0 && !g_voxelStreaming.surfaceReady.empty()) {
                const auto surfaceStart = std::chrono::steady_clock::now();
                int surfaceProcessed = 0;
                const size_t surfaceScanCount = g_voxelStreaming.surfaceReady.size();
                for (size_t scan = 0;
                     scan < surfaceScanCount && surfaceProcessed < surfaceSectionsPerFrame;
                     ++scan) {
                    if (surfaceTimeBudgetMs > 0.0f && surfaceProcessed > 0) {
                        const float elapsedMs = std::chrono::duration<float, std::milli>(
                            std::chrono::steady_clock::now() - surfaceStart
                        ).count();
                        if (elapsedMs >= surfaceTimeBudgetMs) break;
                    }

                    const VoxelSectionKey key = g_voxelStreaming.surfaceReady.front();
                    g_voxelStreaming.surfaceReady.pop_front();
                    if (g_voxelStreaming.surfaceReadySet.erase(key) == 0) continue;

                    VoxelChunkLifecycleState* statePtr = voxelWorld.findChunkState(key);
                    if (!statePtr) continue;
                    VoxelChunkLifecycleState& state = *statePtr;
                    if (!state.desired || !state.generated || !state.postFeaturesComplete) continue;

                    tryFinalizeSurfaceFoliage(key, true, false);
                    surfaceProcessed += 1;
                }
            }
            const auto surfaceStageEnd = std::chrono::steady_clock::now();
            float prepMs = std::chrono::duration<float, std::milli>(
                genStart - updateWorkStart
            ).count();
            float generationMs = std::chrono::duration<float, std::milli>(
                surfaceStageEnd - genStart
            ).count();
            const float desiredMs = prepMs;
            const float baseGenMs = std::chrono::duration<float, std::milli>(
                baseStageEnd - baseStageStart
            ).count();
            const float featureMs = std::chrono::duration<float, std::milli>(
                featureStageEnd - featureStageStart
            ).count();
            const float surfaceMs = std::chrono::duration<float, std::milli>(
                surfaceStageEnd - surfaceStageStart
            ).count();
            const size_t featureReadyPending = g_voxelStreaming.featureReady.size() > g_voxelStreaming.featureReadyHead
                ? (g_voxelStreaming.featureReady.size() - g_voxelStreaming.featureReadyHead)
                : 0u;
            const size_t featureContinuationPending = g_voxelStreaming.featureContinuationReadySet.size();
            const size_t totalPendingQueue = g_voxelStreaming.baseReady.size()
                + g_voxelStreaming.baseInProgress.size()
                + featureReadyPending
                + featureContinuationPending
                + g_voxelStreaming.featureInProgress.size();

            g_voxelStreamingPerfStats.pending = totalPendingQueue;
            g_voxelStreamingPerfStats.desired = g_voxelStreaming.desired.size();
            g_voxelStreamingPerfStats.generated = g_voxelTerrainGenerated.size();
            g_voxelStreamingPerfStats.jobs = g_voxelSectionJobs.size();
            g_voxelStreamingPerfStats.stepped = stepped;
            g_voxelStreamingPerfStats.built = built;
            g_voxelStreamingPerfStats.consumed = consumed;
            g_voxelStreamingPerfStats.skippedExisting = skippedExisting;
            g_voxelStreamingPerfStats.filteredOut = filteredOut;
            g_voxelStreamingPerfStats.rescueSurfaceQueued = rescueSurfaceQueued;
            g_voxelStreamingPerfStats.rescueMissingQueued = rescueMissingQueued;
            g_voxelStreamingPerfStats.droppedByCap = droppedByCap;
            g_voxelStreamingPerfStats.reprioritized = reprioritizedCount;
            g_voxelStreamingPerfStats.prepMs = prepMs;
            g_voxelStreamingPerfStats.generationMs = generationMs;
            g_voxelStreamingPerfStats.desiredMs = desiredMs;
            g_voxelStreamingPerfStats.baseGenMs = baseGenMs;
            g_voxelStreamingPerfStats.featureMs = featureMs;
            g_voxelStreamingPerfStats.surfaceMs = surfaceMs;
            g_voxelStreamingPerfStats.caveFieldMs = g_caveFieldFrameMs;
            g_voxelStreamingPerfStats.schedulerPressure = schedulerPressure;
            g_voxelStreamingPerfStats.desiredBudget = desiredColumnsFrameBudget;
            g_voxelStreamingPerfStats.baseBudget = generationBudget;
            g_voxelStreamingPerfStats.featureBudget = featureSectionsPerFrame;
            g_voxelStreamingPerfStats.surfaceBudget = surfaceSectionsPerFrame;
            g_voxelStreamingPerfStats.baseBudgetMs = generationTimeBudgetMs;
            g_voxelStreamingPerfStats.featureBudgetMs = featureTimeBudgetMs;
            g_voxelStreamingPerfStats.surfaceBudgetMs = surfaceTimeBudgetMs;
            g_voxelStreamingPerfStats.downstreamDirty = schedulerDirtyBacklog;
            g_voxelStreamingPerfStats.downstreamPrepared = schedulerPreparedBacklog;
            g_voxelStreamingPerfStats.downstreamUpload = schedulerUploadBacklog;
            g_voxelStreamingPerfStats.caveFieldCellsBuilt = g_caveFieldFrameCellsBuilt;
            g_voxelStreamingPerfStats.caveSamples = g_caveFieldFrameSampleCount;

            g_voxelStreamingTotalStepped += static_cast<uint64_t>(std::max(0, stepped));
            g_voxelStreamingTotalBuilt += static_cast<uint64_t>(std::max(0, built));
            g_voxelStreamingTotalConsumed += static_cast<uint64_t>(std::max(0, consumed));
            g_snapshotMaxPrepMs = std::max(g_snapshotMaxPrepMs, prepMs);
            g_snapshotMaxGenerationMs = std::max(g_snapshotMaxGenerationMs, generationMs);
            if (generationMs >= 50.0f) {
                g_snapshotLongGenerationCount += 1;
            }

            auto now = std::chrono::steady_clock::now();
            const bool verboseTerrainLog = getRegistryBool(baseSystem, "voxelTerrainVerboseLog", false);
            if (verboseTerrainLog && now - g_lastVoxelPerf >= std::chrono::seconds(1)) {
                std::cout << "TerrainGeneration: voxel sections generated "
                          << built << " (stepped " << stepped
                          << ", consumed " << consumed
                          << ", skipped " << skippedExisting
                          << ", filtered " << filteredOut
                          << ") in " << static_cast<int>(std::round(generationMs))
                          << " ms. Pending " << totalPendingQueue
                          << " desired " << g_voxelStreaming.desired.size()
                          << " generated " << g_voxelTerrainGenerated.size()
                          << " jobs " << g_voxelSectionJobs.size()
                          << " queues(baseR/baseIP/featR/featCR/featIP)="
                          << g_voxelStreaming.baseReady.size() << "/"
                          << g_voxelStreaming.baseInProgress.size() << "/"
                          << featureReadyPending << "/"
                          << featureContinuationPending << "/"
                          << g_voxelStreaming.featureInProgress.size()
                          << " rescue(surface/missing)=" << rescueSurfaceQueued << "/" << rescueMissingQueued
                          << " reprio " << reprioritizedCount
                          << " capDrop " << droppedByCap
                          << " featureDeferred " << featureDeferredByDeps
                          << " prep " << static_cast<int>(std::round(prepMs))
                          << "." << std::endl;
                g_lastVoxelPerf = now;
            }

            const int snapshotIntervalMs = std::max(
                250,
                getRegistryInt(baseSystem, "voxelPerfSnapshotIntervalMs", 2000)
            );
            if (now - g_lastTerrainSnapshot >= std::chrono::milliseconds(snapshotIntervalMs)) {
                const double dtSeconds = std::max(
                    0.0001,
                    std::chrono::duration<double>(now - g_lastTerrainSnapshot).count()
                );
                const size_t generatedNow = g_voxelTerrainGenerated.size();
                const uint64_t builtTotalNow = g_voxelStreamingTotalBuilt;
                size_t generatedDelta = 0;
                if (generatedNow >= g_lastSnapshotGeneratedCount) {
                    generatedDelta = generatedNow - g_lastSnapshotGeneratedCount;
                } else {
                    // Reset events (level/schema changes) can drop generated set size abruptly.
                    generatedDelta = generatedNow;
                }
                uint64_t builtDelta = builtTotalNow >= g_lastSnapshotBuiltTotal
                    ? (builtTotalNow - g_lastSnapshotBuiltTotal)
                    : builtTotalNow;
                const double builtPerSec = static_cast<double>(builtDelta) / dtSeconds;
                const bool stalled = (builtDelta == 0) && (totalPendingQueue > 0);
                g_snapshotStallSeconds = stalled ? (g_snapshotStallSeconds + dtSeconds) : 0.0;
                g_snapshotMaxStallSeconds = std::max(g_snapshotMaxStallSeconds, g_snapshotStallSeconds);
                g_snapshotCount += 1;
                if (builtDelta == 0) {
                    g_snapshotZeroBuildCount += 1;
                }
                if (stalled) {
                    g_snapshotStalledBuildCount += 1;
                    if (!g_snapshotWasStalled) {
                        g_snapshotStallBurstCount += 1;
                    }
                }
                g_snapshotWasStalled = stalled;
                if (g_snapshotCount == 1) {
                    g_snapshotBuiltPerSecMin = builtPerSec;
                    g_snapshotBuiltPerSecMax = builtPerSec;
                } else {
                    g_snapshotBuiltPerSecMin = std::min(g_snapshotBuiltPerSecMin, builtPerSec);
                    g_snapshotBuiltPerSecMax = std::max(g_snapshotBuiltPerSecMax, builtPerSec);
                }
                const size_t desiredGap = g_voxelStreaming.desired.size() > g_voxelTerrainGenerated.size()
                    ? (g_voxelStreaming.desired.size() - g_voxelTerrainGenerated.size())
                    : 0;

                std::cout << "TerrainSnapshot: dt " << dtSeconds
                          << "s, built " << builtDelta
                          << " (" << builtPerSec << "/s)"
                          << ", generatedDelta " << generatedDelta
                          << ", pending " << totalPendingQueue
                          << ", desiredGap " << desiredGap
                          << ", jobs " << g_voxelSectionJobs.size()
                          << ", prepMaxMs " << g_snapshotMaxPrepMs
                          << ", genMaxMs " << g_snapshotMaxGenerationMs
                          << ", genHitches50ms " << g_snapshotLongGenerationCount
                          << ", stallFor " << g_snapshotStallSeconds
                          << "s." << std::endl;

                g_lastTerrainSnapshot = now;
                g_lastSnapshotGeneratedCount = generatedNow;
                g_lastSnapshotBuiltTotal = builtTotalNow;
                g_snapshotMaxPrepMs = 0.0f;
                g_snapshotMaxGenerationMs = 0.0f;
                g_snapshotLongGenerationCount = 0;
            }

        }

    }

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
                                    uint64_t& caveSamples) {
        pending = g_voxelStreamingPerfStats.pending;
        desired = g_voxelStreamingPerfStats.desired;
        generated = g_voxelStreamingPerfStats.generated;
        jobs = g_voxelStreamingPerfStats.jobs;
        stepped = g_voxelStreamingPerfStats.stepped;
        built = g_voxelStreamingPerfStats.built;
        consumed = g_voxelStreamingPerfStats.consumed;
        skippedExisting = g_voxelStreamingPerfStats.skippedExisting;
        filteredOut = g_voxelStreamingPerfStats.filteredOut;
        rescueSurfaceQueued = g_voxelStreamingPerfStats.rescueSurfaceQueued;
        rescueMissingQueued = g_voxelStreamingPerfStats.rescueMissingQueued;
        droppedByCap = g_voxelStreamingPerfStats.droppedByCap;
        reprioritized = g_voxelStreamingPerfStats.reprioritized;
        prepMs = g_voxelStreamingPerfStats.prepMs;
        generationMs = g_voxelStreamingPerfStats.generationMs;
        desiredMs = g_voxelStreamingPerfStats.desiredMs;
        baseGenMs = g_voxelStreamingPerfStats.baseGenMs;
        featureMs = g_voxelStreamingPerfStats.featureMs;
        surfaceMs = g_voxelStreamingPerfStats.surfaceMs;
        caveFieldMs = g_voxelStreamingPerfStats.caveFieldMs;
        schedulerPressure = g_voxelStreamingPerfStats.schedulerPressure;
        desiredBudget = g_voxelStreamingPerfStats.desiredBudget;
        baseBudget = g_voxelStreamingPerfStats.baseBudget;
        featureBudget = g_voxelStreamingPerfStats.featureBudget;
        surfaceBudget = g_voxelStreamingPerfStats.surfaceBudget;
        baseBudgetMs = g_voxelStreamingPerfStats.baseBudgetMs;
        featureBudgetMs = g_voxelStreamingPerfStats.featureBudgetMs;
        surfaceBudgetMs = g_voxelStreamingPerfStats.surfaceBudgetMs;
        downstreamDirty = g_voxelStreamingPerfStats.downstreamDirty;
        downstreamPrepared = g_voxelStreamingPerfStats.downstreamPrepared;
        downstreamUpload = g_voxelStreamingPerfStats.downstreamUpload;
        caveFieldCellsBuilt = g_voxelStreamingPerfStats.caveFieldCellsBuilt;
        caveSamples = g_voxelStreamingPerfStats.caveSamples;
    }

    void GetTerrainStreamingRunStats(uint64_t& totalStepped,
                                     uint64_t& totalBuilt,
                                     uint64_t& totalConsumed,
                                     size_t& pending,
                                     size_t& desired,
                                     size_t& generated,
                                     size_t& jobs,
                                     double& stallForSeconds,
                                     double& maxStallSeconds,
                                     uint64_t& snapshotCount,
                                     uint64_t& snapshotZeroBuildCount,
                                     uint64_t& snapshotStalledBuildCount,
                                     uint64_t& snapshotStallBurstCount,
                                     double& snapshotBuiltPerSecMin,
                                     double& snapshotBuiltPerSecMax,
                                     float& lastPrepMs,
                                     float& lastGenerationMs) {
        totalStepped = g_voxelStreamingTotalStepped;
        totalBuilt = g_voxelStreamingTotalBuilt;
        totalConsumed = g_voxelStreamingTotalConsumed;
        const size_t featureReadyPending = g_voxelStreaming.featureReady.size() > g_voxelStreaming.featureReadyHead
            ? (g_voxelStreaming.featureReady.size() - g_voxelStreaming.featureReadyHead)
            : 0u;
        const size_t featureContinuationPending = g_voxelStreaming.featureContinuationReadySet.size();
        pending = g_voxelStreaming.baseReady.size()
            + g_voxelStreaming.baseInProgress.size()
            + featureReadyPending
            + featureContinuationPending
            + g_voxelStreaming.featureInProgress.size();
        desired = g_voxelStreaming.desired.size();
        generated = g_voxelTerrainGenerated.size();
        jobs = g_voxelSectionJobs.size();
        stallForSeconds = g_snapshotStallSeconds;
        maxStallSeconds = g_snapshotMaxStallSeconds;
        snapshotCount = g_snapshotCount;
        snapshotZeroBuildCount = g_snapshotZeroBuildCount;
        snapshotStalledBuildCount = g_snapshotStalledBuildCount;
        snapshotStallBurstCount = g_snapshotStallBurstCount;
        snapshotBuiltPerSecMin = g_snapshotBuiltPerSecMin;
        snapshotBuiltPerSecMax = g_snapshotBuiltPerSecMax;
        lastPrepMs = g_voxelStreamingPerfStats.prepMs;
        lastGenerationMs = g_voxelStreamingPerfStats.generationMs;
    }

    bool IsSectionTerrainReady(const VoxelSectionKey& key) {
        return (g_voxelSectionJobs.count(key) == 0)
            && (g_voxelTerrainGenerated.count(key) > 0);
    }

    void UpdateExpanseTerrain(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)dt; (void)win;
        if (!baseSystem.level || !baseSystem.instance || !baseSystem.world || !baseSystem.player) return;
        LevelContext& level = *baseSystem.level;
        WorldContext& worldCtx = *baseSystem.world;
        if (!worldCtx.expanse.loaded) return;
        g_caveFieldFrameMs = 0.0f;
        g_caveFieldFrameCellsBuilt = 0;
        g_caveFieldFrameSampleCount = 0;
        auto resetVoxelStreamingState = [&]() {
            if (!baseSystem.voxelWorld) return;
            baseSystem.voxelWorld->reset();
            g_voxelStreaming.baseReady.clear();
            g_voxelStreaming.baseReadySet.clear();
            g_voxelStreaming.baseInProgress.clear();
            g_voxelStreaming.desired.clear();
            g_voxelStreaming.pendingDesiredRebuild = true;
            g_voxelSectionJobs.clear();
            g_voxelTerrainGenerated.clear();
            g_voxelStreaming.featureReady.clear();
            g_voxelStreaming.featureReadySet.clear();
            g_voxelStreaming.featureContinuationReady.clear();
            g_voxelStreaming.featureContinuationReadySet.clear();
            g_voxelStreaming.featureInProgress.clear();
            g_voxelStreaming.surfaceReady.clear();
            g_voxelStreaming.surfaceReadySet.clear();
            g_voxelStreaming.featureReadyHead = 0;
            g_voxelColumnStreaming = VoxelColumnStreamingState{};
            g_pendingResortCursor = 0;
            g_tier0RescueScanList.clear();
            g_tier0RescueScanHead = 0;
            g_voxelStreaming.lastCenterSection = glm::ivec3(std::numeric_limits<int>::min());
            g_voxelStreaming.lastRadius = std::numeric_limits<int>::min();
            g_voxelStreaming.lastCpuViewYawBucket = std::numeric_limits<int>::min();
            g_voxelStreaming.lastCpuViewCullingEnabled = false;
            g_voxelDesiredRebuild.active = false;
            g_voxelDesiredRebuild.prevRadius = 0;
            g_voxelDesiredRebuild.tierPrepared = false;
            g_voxelDesiredRebuild.radius = 0;
            g_voxelDesiredRebuild.size = 0;
            g_voxelDesiredRebuild.scale = 1;
            g_voxelDesiredRebuild.minSectionY = 0;
            g_voxelDesiredRebuild.maxSectionY = -1;
            g_voxelDesiredRebuild.tierSurfaceCenterY = 0;
            g_voxelDesiredRebuild.sectionOrderXZ.clear();
            g_voxelDesiredRebuild.orderCursor = 0;
            g_voxelDesiredRebuild.desired.clear();
            g_voxelDesiredRebuild.desiredOrder.clear();
            g_caveField.ready = false;
            g_caveField.enabled = true;
            g_caveField.building = false;
            g_caveField.mode = 0;
            g_caveField.seedA = 0;
            g_caveField.seedB = 0;
            g_caveField.iterations = 3;
            g_caveField.fillPercent = 50;
            g_caveField.openThreshold = 5;
            g_caveField.cellScale = 1;
            g_caveField.maxCachedTiles = 512;
            g_caveField.origin = glm::vec3(0.0f);
            g_caveField.step = 4;
            g_caveField.dimX = 0;
            g_caveField.dimY = 0;
            g_caveField.dimZ = 0;
            g_caveField.buildCursor = 0;
            g_caveField.totalCount = 0;
            g_caveField.lastBuildFrame = std::numeric_limits<uint64_t>::max();
            g_caveField.noiseA.reset();
            g_caveField.noiseB.reset();
            g_caveField.a.clear();
            g_caveField.b.clear();
            g_caveField.tiles.clear();
            g_lastVoxelPerf = std::chrono::steady_clock::now();
            g_lastTerrainSnapshot = std::chrono::steady_clock::now();
            g_voxelStreamingTotalStepped = 0;
            g_voxelStreamingTotalBuilt = 0;
            g_voxelStreamingTotalConsumed = 0;
            g_lastSnapshotBuiltTotal = 0;
            g_lastSnapshotGeneratedCount = 0;
            g_snapshotMaxPrepMs = 0.0f;
            g_snapshotMaxGenerationMs = 0.0f;
            g_snapshotLongGenerationCount = 0;
            g_snapshotStallSeconds = 0.0;
            g_snapshotMaxStallSeconds = 0.0;
            g_snapshotCount = 0;
            g_snapshotZeroBuildCount = 0;
            g_snapshotStalledBuildCount = 0;
            g_snapshotStallBurstCount = 0;
            g_snapshotWasStalled = false;
            g_snapshotBuiltPerSecMin = 0.0;
            g_snapshotBuiltPerSecMax = 0.0;
            g_verticalDomainMode = 0;
            g_voxelStreamingPerfStats = {};
        };
        const bool usesExpanseVoxelWorld =
            levelContainsWorldNamed(level, worldCtx.expanse.terrainWorld) ||
            levelContainsWorldNamed(level, worldCtx.expanse.waterWorld) ||
            levelContainsWorldNamed(level, worldCtx.expanse.treesWorld);
        if (!usesExpanseVoxelWorld) {
            if (baseSystem.voxelWorld && baseSystem.voxelWorld->enabled) {
                resetVoxelStreamingState();
            }
            if (baseSystem.voxelWorld) {
                baseSystem.voxelWorld->enabled = false;
            }
            g_voxelLevelKey.clear();
            return;
        }
        if (baseSystem.voxelWorld) {
            baseSystem.voxelWorld->enabled = true;
        }
        if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return;
        if (baseSystem.registry) {
            const std::string terrainSchemaVersion = "terrain_schema_column_chunks_v22";
            auto it = baseSystem.registry->find("TerrainSchemaVersion");
            bool needsReset = true;
            if (it != baseSystem.registry->end() && std::holds_alternative<std::string>(it->second)) {
                needsReset = (std::get<std::string>(it->second) != terrainSchemaVersion);
            }
            if (needsReset) {
                resetVoxelStreamingState();
                (*baseSystem.registry)["TerrainSchemaVersion"] = terrainSchemaVersion;
            }
        }

        // Single storage mode: section streaming is fixed to one path.

        std::string levelKey;
        if (baseSystem.registry) {
            auto it = baseSystem.registry->find("level");
            if (it != baseSystem.registry->end() && std::holds_alternative<std::string>(it->second)) {
                levelKey = std::get<std::string>(it->second);
            }
        }
        if (g_voxelLevelKey != levelKey) {
            g_voxelLevelKey = levelKey;
            g_caveConfigKey.clear();
            resetVoxelStreamingState();
        }

        const std::string caveConfigKey =
            getRegistryString(baseSystem, "BinaryCaveMode", "binary")
            + "|scale=" + std::to_string(getRegistryInt(baseSystem, "BinaryCaveCellScale", 3))
            + "|iters=" + std::to_string(getRegistryInt(baseSystem, "BinaryCaveIterations", 3))
            + "|fill=" + std::to_string(getRegistryInt(baseSystem, "BinaryCaveFillPercent", 50))
            + "|threshold=" + std::to_string(getRegistryInt(baseSystem, "BinaryCaveOpenThreshold", 5))
            + "|perlinStep=" + std::to_string(getRegistryInt(baseSystem, "PerlinCaveFieldStep", 4))
            + "|perlinSize=" + std::to_string(getRegistryInt(baseSystem, "PerlinCaveFieldSizeXZ", 2304));
        if (g_caveConfigKey.empty()) {
            g_caveConfigKey = caveConfigKey;
        } else if (g_caveConfigKey != caveConfigKey) {
            g_caveConfigKey = caveConfigKey;
            resetVoxelStreamingState();
        }

        UpdateExpanseVoxelWorld(baseSystem, prototypes, worldCtx, worldCtx.expanse);
    }
}
