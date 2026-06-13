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
    bool GenerateColumnTerrainFoliage(BaseSystem& baseSystem,
                                      std::vector<Entity>& prototypes,
                                      WorldContext& worldCtx,
                                      VoxelWorldContext& voxelWorld,
                                      const VoxelColumnKey& columnKey);
}


namespace TerrainSystemLogic {

    constexpr int kBinaryCaveTileBits = 64;
    using BinaryCaveTile = std::array<uint64_t, kBinaryCaveTileBits>;
    constexpr int kPerlinCaveTileSamples = 8;
    constexpr int kPerlinCaveTileSampleCount =
        kPerlinCaveTileSamples * kPerlinCaveTileSamples * kPerlinCaveTileSamples;

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

    struct PerlinCaveTileKey {
        int x = 0;
        int y = 0;
        int z = 0;

        bool operator==(const PerlinCaveTileKey& other) const noexcept {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct PerlinCaveTileKeyHash {
        std::size_t operator()(const PerlinCaveTileKey& key) const noexcept {
            std::size_t h = std::hash<int>()(key.x + 0x9e3779b9);
            h ^= std::hash<int>()(key.y - 0x7f4a7c15) + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(key.z + 0x94d049bb) + (h << 6) + (h >> 2);
            return h;
        }
    };

    struct PerlinCaveTile {
        std::array<uint8_t, kPerlinCaveTileSampleCount> a{};
        std::array<uint8_t, kPerlinCaveTileSampleCount> b{};
    };

    struct CaveFieldLocal {
        bool ready = false;
        bool enabled = true;
        bool building = false;
        int mode = 0; // 0=binary CA, 1=tiled Perlin field
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
        std::unordered_map<PerlinCaveTileKey, PerlinCaveTile, PerlinCaveTileKeyHash> perlinTiles;
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
            uint64_t frameCounter = 0;
        };

        struct VoxelColumnGenerationJob {
            int nextColumn = 0;
            int nextFeatureSectionY = std::numeric_limits<int>::min();
            int nextPublishSectionY = std::numeric_limits<int>::min();
            int phase = 0; // 0 base voxels, 1 terrain features, 2 column foliage/trees, 3 publish render bridge
            bool wroteAny = false;
            uint64_t startedFrame = 0;
            uint64_t lastStepFrame = 0;
        };

        struct VoxelColumnStreamingState {
            std::vector<VoxelColumnKey> pending;
            std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> pendingSet;
            std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> inProgress;
            std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> desired;
            std::vector<VoxelColumnKey> desiredOrder;
            std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> generated;
            std::unordered_map<VoxelColumnKey, VoxelColumnGenerationJob, VoxelColumnKeyHash> jobs;
            std::unordered_map<VoxelColumnKey, uint64_t, VoxelColumnKeyHash> lastDesiredFrame;
            std::unordered_map<VoxelColumnKey, uint64_t, VoxelColumnKeyHash> completedFrame;
            glm::ivec2 lastCenterColumn = glm::ivec2(std::numeric_limits<int>::min());
            int lastRadius = std::numeric_limits<int>::min();
            int lastCpuViewYawBucket = std::numeric_limits<int>::min();
            bool lastCpuViewCullingEnabled = false;
            bool lastCpuViewMotionLookaheadEnabled = false;
            glm::ivec2 lastCpuViewMotionColumn = glm::ivec2(std::numeric_limits<int>::min());
            bool pendingDesiredRebuild = true;
        };

        struct VoxelColumnFrameMaintenanceStats {
            int released = 0;
            int releasedBeforeComplete = 0;
            int releasedAfterComplete = 0;
            int retained = 0;
            int evictable = 0;
            int pendingFiltered = 0;
            int activeRequeued = 0;
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
            size_t columnResident = 0;
            size_t columnRetained = 0;
            size_t columnEvictable = 0;
            int columnStarted = 0;
            int columnLoaded = 0;
            int columnCompleted = 0;
            int columnReleased = 0;
            int columnReleasedBeforeComplete = 0;
            int columnReleasedAfterComplete = 0;
            int columnActiveRequeued = 0;
            int columnPendingFiltered = 0;
            int columnPhase0 = 0;
            int columnPhase1 = 0;
            int columnPhase2 = 0;
            int columnPhase3 = 0;
        };

        static VoxelStreamingState g_voxelStreaming;
        static VoxelColumnStreamingState g_voxelColumnStreaming;
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
        static std::string g_voxelLevelKey;
        static std::string g_voxelDimensionKey;
        static std::string g_caveConfigKey;
        static CaveFieldLocal g_caveField;
        static float g_caveFieldFrameMs = 0.0f;
        static uint64_t g_caveFieldFrameCellsBuilt = 0;
        static uint64_t g_caveFieldFrameSampleCount = 0;
        static uint64_t g_caveFieldTotalTilesBuilt = 0;
        static uint64_t g_caveFieldTotalCellsBuilt = 0;
        static float g_terrainWorkerSetupFrameMs = 0.0f;
        static float g_terrainWorkerColumnFrameMs = 0.0f;
        static float g_terrainPublishFrameMs = 0.0f;
        static float g_terrainMaintenanceFrameMs = 0.0f;
        static float g_terrainPendingPopFrameMs = 0.0f;
        static float g_terrainSaveProbeFrameMs = 0.0f;
        static float g_terrainReleaseFrameMs = 0.0f;
        static float g_terrainStepFrameMs = 0.0f;
        static float g_terrainRequeueFrameMs = 0.0f;
        static float g_terrainPhase0FrameMs = 0.0f;
        static float g_terrainPhase1FrameMs = 0.0f;
        static float g_terrainPhase2FrameMs = 0.0f;
        static float g_terrainPhase3FrameMs = 0.0f;

        struct TerrainBaseBreakdownFrameStats {
            float setupMs = 0.0f;
            float caveFieldPrepMs = 0.0f;
            float surfaceBiomeMs = 0.0f;
            float hydrologyMs = 0.0f;
            float caveMs = 0.0f;
            float oreDecorMs = 0.0f;
            float memoryMs = 0.0f;
            float blockWriteMs = 0.0f;
            float caveRampMs = 0.0f;
            float bookkeepingMs = 0.0f;
            float materializeMs = 0.0f;
            float fillDepthMs = 0.0f;
            float fillRegularMs = 0.0f;
            float fillRegularCaveScanMs = 0.0f;
            float fillRangeWriteMs = 0.0f;
            float detailScanMs = 0.0f;
            float fillPassMs = 0.0f;
            float detailPassMs = 0.0f;
            float postFeaturePassMs = 0.0f;
            uint64_t workerCalls = 0;
            uint64_t fillPassCalls = 0;
            uint64_t detailPassCalls = 0;
            uint64_t postFeatureCalls = 0;
            uint64_t columnsVisited = 0;
            uint64_t verticalCellsVisited = 0;
            uint64_t terrainSampleCalls = 0;
            uint64_t terrainSampleMisses = 0;
            uint64_t biomeResolveCalls = 0;
            uint64_t caveSampleCalls = 0;
            uint64_t hydrologyColumns = 0;
            uint64_t oreDecorColumnCalls = 0;
            uint64_t oreDecorCellCalls = 0;
            uint64_t directColumnEnsureCalls = 0;
            uint64_t directColumnCreateCalls = 0;
            uint64_t writeVoxelCalls = 0;
            uint64_t writeRunCalls = 0;
            uint64_t writeCellsChanged = 0;
            uint64_t caveRampScanCalls = 0;
            uint64_t materializeCalls = 0;
            uint64_t detailAirSkipped = 0;
            uint64_t detailDepthCells = 0;
            uint64_t detailSurfaceCells = 0;
            uint64_t detailSurfaceNeighborChecks = 0;
            uint64_t detailWrites = 0;
        };

        static TerrainBaseBreakdownFrameStats g_terrainBaseBreakdownFrame;
        static constexpr bool kTerrainBaseHotLoopTimers = false;

        inline float elapsedMsSince(std::chrono::steady_clock::time_point start) {
            return std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - start
            ).count();
        }

        inline std::chrono::steady_clock::time_point startTerrainBaseHotLoopTimer() {
            if constexpr (kTerrainBaseHotLoopTimers) {
                return std::chrono::steady_clock::now();
            }
            return {};
        }

        inline void addTerrainBaseHotLoopMs(bool enabled,
                                            float& targetMs,
                                            std::chrono::steady_clock::time_point start) {
            if constexpr (kTerrainBaseHotLoopTimers) {
                if (enabled) {
                    targetMs += elapsedMsSince(start);
                }
            }
        }

        struct ScopedTerrainBaseMs {
            bool enabled = false;
            float& targetMs;
            std::chrono::steady_clock::time_point startTime;

            ScopedTerrainBaseMs(bool enabledIn, float& target)
                : enabled(enabledIn),
                  targetMs(target),
                  startTime(std::chrono::steady_clock::now()) {}

            void stop() {
                if (enabled) {
                    targetMs += elapsedMsSince(startTime);
                    enabled = false;
                }
            }

            ~ScopedTerrainBaseMs() {
                stop();
            }
        };
        

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
            g_caveFieldTotalTilesBuilt += 1;
            g_caveFieldTotalCellsBuilt += static_cast<uint64_t>(
                kBinaryCaveTileBits * kBinaryCaveTileBits
            );
            return inserted.first->second;
        }

        size_t perlinCaveTileIndex(int x, int y, int z) {
            return (static_cast<size_t>(x) * static_cast<size_t>(kPerlinCaveTileSamples)
                + static_cast<size_t>(y)) * static_cast<size_t>(kPerlinCaveTileSamples)
                + static_cast<size_t>(z);
        }

        const PerlinCaveTile& getPerlinCaveTile(int tileX, int tileY, int tileZ) {
            const PerlinCaveTileKey key{tileX, tileY, tileZ};
            auto found = g_caveField.perlinTiles.find(key);
            if (found != g_caveField.perlinTiles.end()) return found->second;

            const auto start = std::chrono::steady_clock::now();
            PerlinCaveTile tile{};
            const int step = std::max(1, g_caveField.step);
            const int baseGridX = tileX * kPerlinCaveTileSamples;
            const int baseGridY = tileY * kPerlinCaveTileSamples;
            const int baseGridZ = tileZ * kPerlinCaveTileSamples;
            if (g_caveField.noiseA && g_caveField.noiseB) {
                for (int x = 0; x < kPerlinCaveTileSamples; ++x) {
                    const float wx = static_cast<float>((baseGridX + x) * step);
                    for (int y = 0; y < kPerlinCaveTileSamples; ++y) {
                        const float wy = static_cast<float>((baseGridY + y) * step);
                        for (int z = 0; z < kPerlinCaveTileSamples; ++z) {
                            const float wz = static_cast<float>((baseGridZ + z) * step);
                            const size_t idx = perlinCaveTileIndex(x, y, z);
                            const float v1 = (g_caveField.noiseA->noise(wx / 64.0f, wy / 48.0f, wz / 64.0f) + 1.0f) * 0.5f;
                            const float v2 = (g_caveField.noiseB->noise(wx / 128.0f, wy / 128.0f, wz / 128.0f) + 1.0f) * 0.5f;
                            tile.a[idx] = quantizeNoise01(v1);
                            tile.b[idx] = quantizeNoise01(v2);
                        }
                    }
                }
            }

            if (g_caveField.perlinTiles.size() >= g_caveField.maxCachedTiles) {
                g_caveField.perlinTiles.clear();
            }
            auto inserted = g_caveField.perlinTiles.emplace(key, tile);
            g_caveFieldFrameMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - start
            ).count();
            g_caveFieldFrameCellsBuilt += static_cast<uint64_t>(kPerlinCaveTileSampleCount);
            g_caveFieldTotalTilesBuilt += 1;
            g_caveFieldTotalCellsBuilt += static_cast<uint64_t>(kPerlinCaveTileSampleCount);
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
                const bool unchanged =
                    g_caveField.ready
                    && g_caveField.mode == mode
                    && g_caveField.enabled == enabled
                    && g_caveField.step == step
                    && g_caveField.seedA == desiredSeedA
                    && g_caveField.seedB == desiredSeedB
                    && g_caveField.maxCachedTiles == maxCachedTiles
                    && g_caveField.noiseA
                    && g_caveField.noiseB;

                if (!enabled) {
                    g_caveField.ready = false;
                    g_caveField.enabled = false;
                    g_caveField.building = false;
                    g_caveField.mode = mode;
                    g_caveField.tiles.clear();
                    g_caveField.perlinTiles.clear();
                    return;
                }

                if (!unchanged) {
                    g_caveField.ready = true;
                    g_caveField.enabled = true;
                    g_caveField.building = false;
                    g_caveField.mode = mode;
                    g_caveField.step = step;
                    g_caveField.seedA = desiredSeedA;
                    g_caveField.seedB = desiredSeedB;
                    g_caveField.maxCachedTiles = maxCachedTiles;
                    g_caveField.dimX = kPerlinCaveTileSamples;
                    g_caveField.dimY = kPerlinCaveTileSamples;
                    g_caveField.dimZ = kPerlinCaveTileSamples;
                    g_caveField.buildCursor = 0;
                    g_caveField.totalCount = 0;
                    g_caveField.lastBuildFrame = std::numeric_limits<uint64_t>::max();
                    g_caveField.noiseA = std::make_unique<PerlinNoise3DLocal>(desiredSeedA);
                    g_caveField.noiseB = std::make_unique<PerlinNoise3DLocal>(desiredSeedB);
                    g_caveField.a.clear();
                    g_caveField.b.clear();
                    g_caveField.tiles.clear();
                    g_caveField.perlinTiles.clear();
                    std::cout << "TerrainGeneration: Perlin cave tile cache enabled "
                              << "tile=" << kPerlinCaveTileSamples
                              << "^3 step=" << step << std::endl;
                }

                (void)requestedMinY;
                (void)requestedMaxY;
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
            g_caveField.perlinTiles.clear();
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

        inline void addCaveFieldLogicalSamples(uint64_t count) {
            g_caveFieldFrameSampleCount += count;
        }

        inline bool sampleBinaryCaveColumnTileBits(int worldX,
                                                   int worldZ,
                                                   int tileY,
                                                   uint64_t& outOpenBits) {
            if (!g_caveField.enabled || !g_caveField.ready || g_caveField.mode != 0) {
                outOpenBits = 0ULL;
                return false;
            }
            const int cellScale = std::max(1, g_caveField.cellScale);
            const int cellX = floorDivInt(worldX, cellScale);
            const int cellZ = floorDivInt(worldZ, cellScale);
            const int tileX = floorDivInt(cellX, kBinaryCaveTileBits);
            const int tileZ = floorDivInt(cellZ, kBinaryCaveTileBits);
            const int localX = cellX - tileX * kBinaryCaveTileBits;
            const int localZ = cellZ - tileZ * kBinaryCaveTileBits;
            const BinaryCaveTile& xyTile = getBinaryCaveTile(0, tileX, tileY);
            const BinaryCaveTile& zyTile = getBinaryCaveTile(1, tileZ, tileY);
            outOpenBits = xyTile[static_cast<size_t>(localX)]
                & zyTile[static_cast<size_t>(localZ)];
            return true;
        }

        inline bool sampleCaveField(float worldX, float worldY, float worldZ, float& outA, float& outB) {
            if (!g_caveField.enabled) return false;
            g_caveFieldFrameSampleCount += 1;
            if (g_caveField.mode == 1) {
                if (!g_caveField.ready || !g_caveField.noiseA || !g_caveField.noiseB) {
                    return samplePerlinCaveNoiseDirect(worldX, worldY, worldZ, outA, outB);
                }
                const int step = std::max(1, g_caveField.step);
                const int gridX = static_cast<int>(std::round(worldX / static_cast<float>(step)));
                const int gridY = static_cast<int>(std::round(worldY / static_cast<float>(step)));
                const int gridZ = static_cast<int>(std::round(worldZ / static_cast<float>(step)));
                const int tileX = floorDivInt(gridX, kPerlinCaveTileSamples);
                const int tileY = floorDivInt(gridY, kPerlinCaveTileSamples);
                const int tileZ = floorDivInt(gridZ, kPerlinCaveTileSamples);
                const int localX = gridX - tileX * kPerlinCaveTileSamples;
                const int localY = gridY - tileY * kPerlinCaveTileSamples;
                const int localZ = gridZ - tileZ * kPerlinCaveTileSamples;
                const PerlinCaveTile& tile = getPerlinCaveTile(tileX, tileY, tileZ);
                const size_t idx = perlinCaveTileIndex(localX, localY, localZ);
                outA = static_cast<float>(tile.a[idx]) / 255.0f;
                outB = static_cast<float>(tile.b[idx]) / 255.0f;
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

        std::string activeDimensionId(const BaseSystem& baseSystem) {
            if (baseSystem.worldSave && !baseSystem.worldSave->activeDimensionId.empty()) {
                return baseSystem.worldSave->activeDimensionId;
            }
            return getRegistryString(baseSystem, "ActiveDimensionId", "overworld");
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

        int streamRadiusWorld(const BaseSystem& baseSystem, const VoxelWorldContext& voxelWorld) {
            const int streamRadiusChunks = std::clamp(
                getRegistryInt(baseSystem, "voxelStreamRadiusChunks", 6),
                0,
                64
            );
            const long long span = static_cast<long long>(sectionSizeForSection(voxelWorld));
            long long radius = span * static_cast<long long>(streamRadiusChunks);
            if (radius > 2000000000LL) radius = 2000000000LL;
            return static_cast<int>(radius);
        }

        struct VoxelCpuStreamingView {
            bool enabled = false;
            glm::vec2 cameraXZ{0.0f};
            glm::vec2 forwardXZ{0.0f, -1.0f};
            float nearRadiusSq = 0.0f;
            float cosHalfAngle = -1.0f;
            bool motionLookaheadEnabled = false;
            glm::vec2 motionLookaheadXZ{0.0f};
            glm::ivec2 motionLookaheadColumn{std::numeric_limits<int>::min()};
            float motionNearRadiusSq = 0.0f;
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
            if (view.enabled
                && baseSystem.player
                && getRegistryBool(baseSystem, "voxelCpuViewMotionLookaheadEnabled", true)) {
                const glm::vec2 previousXZ(
                    baseSystem.player->prevCameraPosition.x,
                    baseSystem.player->prevCameraPosition.z
                );
                const glm::vec2 frameDelta = view.cameraXZ - previousXZ;
                const float minDelta = std::max(
                    0.0f,
                    getRegistryFloat(baseSystem, "voxelCpuViewMotionMinFrameDeltaBlocks", 0.01f)
                );
                const float deltaLen2 = glm::dot(frameDelta, frameDelta);
                if (deltaLen2 >= minDelta * minDelta) {
                    const float leadFrames = std::max(
                        0.0f,
                        getRegistryFloat(baseSystem, "voxelCpuViewMotionLeadFrames", 90.0f)
                    );
                    const float maxLookahead = std::max(
                        0.0f,
                        getRegistryFloat(baseSystem, "voxelCpuViewMotionMaxBlocks", 96.0f)
                    );
                    glm::vec2 offset = frameDelta * leadFrames;
                    const float offsetLen2 = glm::dot(offset, offset);
                    if (maxLookahead > 0.0f && offsetLen2 > maxLookahead * maxLookahead) {
                        offset *= maxLookahead / std::sqrt(offsetLen2);
                    }
                    view.motionLookaheadEnabled = true;
                    view.motionLookaheadXZ = view.cameraXZ + offset;
                    const float motionNearRadius = std::max(
                        static_cast<float>(sectionSize),
                        getRegistryFloat(baseSystem, "voxelCpuViewMotionNearRadiusBlocks", nearRadius)
                    );
                    view.motionNearRadiusSq = motionNearRadius * motionNearRadius;
                    view.motionLookaheadColumn = glm::ivec2(
                        floorDivInt(static_cast<int>(std::floor(view.motionLookaheadXZ.x)), sectionSize),
                        floorDivInt(static_cast<int>(std::floor(view.motionLookaheadXZ.y)), sectionSize)
                    );
                }
            }
            return view;
        }

        bool pointPassesCpuStreamingView(const VoxelCpuStreamingView& view, const glm::vec2& point) {
            if (!view.enabled) return true;
            const glm::vec2 delta = point - view.cameraXZ;
            const float dist2 = glm::dot(delta, delta);
            if (dist2 <= view.nearRadiusSq) return true;
            if (view.motionLookaheadEnabled) {
                const glm::vec2 motionDelta = point - view.motionLookaheadXZ;
                if (glm::dot(motionDelta, motionDelta) <= view.motionNearRadiusSq) return true;
            }
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
            maxY = std::max(maxY, getRegistryInt(baseSystem, "ExpanseAbsoluteMaxY", 319));
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

        bool RunExpanseSectionTerrainFeatures(BaseSystem& baseSystem,
                                              std::vector<Entity>& prototypes,
                                              WorldContext& worldCtx,
                                              const ExpanseConfig& cfg,
                                              const glm::ivec3& sectionCoord,
                                              bool& inOutWroteAny,
                                              bool& outPostFeaturesCompleted);

        int computeExpanseMinY(const BaseSystem& baseSystem,
                               const ExpanseConfig& cfg,
                               const std::string& currentLevel) {
            const bool isExpanseLevel = (currentLevel == "the_expanse" || currentLevel == "menu");
            const bool isDepthLevel = (currentLevel == "the_depths");
            const bool isOverworldDimension = (activeDimensionId(baseSystem) == "overworld");
            const int waterFloorY = static_cast<int>(std::floor(cfg.waterFloor));
            const int streamPortalY = isDepthLevel ? (cfg.minY + 1) : (waterFloorY - 2);
            int minY = std::min(std::min(cfg.minY, waterFloorY), streamPortalY);
            if (isExpanseLevel && isOverworldDimension && getRegistryBool(baseSystem, "UnifiedDepthsEnabled", true)) {
                minY = getRegistryInt(baseSystem, "UnifiedDepthsMinY", -64);
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

        int columnChebyshevDistance(const VoxelColumnKey& key, const glm::ivec2& centerColumn) {
            return std::max(
                std::abs(key.coord.x - centerColumn.x),
                std::abs(key.coord.y - centerColumn.y)
            );
        }

        struct VoxelColumnPriorityContext {
            bool enabled = true;
            glm::vec2 cameraXZ{0.0f};
            glm::vec2 forwardXZ{0.0f, -1.0f};
            int sectionSize = 16;
            float nearRadiusSq = 0.0f;
            float viewWeight = 4096.0f;
            float distanceWeight = 0.001f;
            float behindPenalty = 1000000.0f;
        };

        VoxelColumnPriorityContext makeVoxelColumnPriorityContext(const BaseSystem& baseSystem,
                                                                  const VoxelWorldContext& voxelWorld,
                                                                  const glm::vec3& cameraPos) {
            VoxelColumnPriorityContext ctx{};
            ctx.enabled = getRegistryBool(baseSystem, "voxelColumnViewPriorityEnabled", true);
            ctx.sectionSize = sectionSizeForSection(voxelWorld);
            const float yawRadians = glm::radians(baseSystem.player ? baseSystem.player->cameraYaw : -90.0f);
            ctx.forwardXZ = glm::vec2(std::cos(yawRadians), std::sin(yawRadians));
            const float forwardLen2 = glm::dot(ctx.forwardXZ, ctx.forwardXZ);
            if (forwardLen2 > 1e-6f) {
                ctx.forwardXZ /= std::sqrt(forwardLen2);
            } else {
                ctx.forwardXZ = glm::vec2(0.0f, -1.0f);
            }
            const float lookaheadBlocks = std::max(
                0.0f,
                getRegistryFloat(baseSystem, "voxelColumnPriorityLookaheadBlocks", 0.0f)
            );
            ctx.cameraXZ = glm::vec2(cameraPos.x, cameraPos.z) + ctx.forwardXZ * lookaheadBlocks;
            if (baseSystem.player && getRegistryBool(baseSystem, "voxelColumnPriorityMotionLookaheadEnabled", true)) {
                const glm::vec2 previousXZ(
                    baseSystem.player->prevCameraPosition.x,
                    baseSystem.player->prevCameraPosition.z
                );
                const glm::vec2 frameDelta = glm::vec2(cameraPos.x, cameraPos.z) - previousXZ;
                const float minDelta = std::max(
                    0.0f,
                    getRegistryFloat(baseSystem, "voxelColumnPriorityMotionMinFrameDeltaBlocks", 0.01f)
                );
                const float deltaLen2 = glm::dot(frameDelta, frameDelta);
                if (deltaLen2 >= minDelta * minDelta) {
                    const float leadFrames = std::max(
                        0.0f,
                        getRegistryFloat(baseSystem, "voxelColumnPriorityMotionLeadFrames", 90.0f)
                    );
                    const float maxLookahead = std::max(
                        0.0f,
                        getRegistryFloat(baseSystem, "voxelColumnPriorityMotionMaxBlocks", 96.0f)
                    );
                    glm::vec2 offset = frameDelta * leadFrames;
                    const float offsetLen2 = glm::dot(offset, offset);
                    if (maxLookahead > 0.0f && offsetLen2 > maxLookahead * maxLookahead) {
                        offset *= maxLookahead / std::sqrt(offsetLen2);
                    }
                    ctx.cameraXZ += offset;
                }
            }
            const float nearRadius = std::max(
                static_cast<float>(ctx.sectionSize),
                getRegistryFloat(
                    baseSystem,
                    "voxelColumnPriorityNearRadiusBlocks",
                    static_cast<float>(ctx.sectionSize) * 2.0f
                )
            );
            ctx.nearRadiusSq = nearRadius * nearRadius;
            ctx.viewWeight = std::max(
                0.0f,
                getRegistryFloat(baseSystem, "voxelColumnPriorityViewWeight", 4096.0f)
            );
            ctx.distanceWeight = std::max(
                0.0f,
                getRegistryFloat(baseSystem, "voxelColumnPriorityDistanceWeight", 0.001f)
            );
            ctx.behindPenalty = std::max(
                0.0f,
                getRegistryFloat(baseSystem, "voxelColumnPriorityBehindPenalty", 1000000.0f)
            );
            return ctx;
        }

        float columnViewPriority(const VoxelColumnPriorityContext& ctx,
                                 const VoxelColumnKey& key) {
            if (!ctx.enabled) return 0.0f;
            const float span = static_cast<float>(std::max(1, ctx.sectionSize));
            const glm::vec2 center(
                (static_cast<float>(key.coord.x) + 0.5f) * span,
                (static_cast<float>(key.coord.y) + 0.5f) * span
            );
            const glm::vec2 delta = center - ctx.cameraXZ;
            const float dist2 = glm::dot(delta, delta);
            const float depth = glm::dot(delta, ctx.forwardXZ);
            const float lateral2 = std::max(0.0f, dist2 - depth * depth);
            const float positiveDepth = std::max(0.0f, depth);
            const float centeredViewScore = lateral2 / (positiveDepth * positiveDepth + span * span);
            const float behindPenalty =
                (dist2 > ctx.nearRadiusSq && depth < -span) ? ctx.behindPenalty : 0.0f;
            return behindPenalty + centeredViewScore * ctx.viewWeight + dist2 * ctx.distanceWeight;
        }

        bool columnPriorityLess(const VoxelColumnPriorityContext& priority,
                                const glm::ivec2& cameraColumn,
                                const VoxelColumnKey& a,
                                const VoxelColumnKey& b) {
            const float aPriority = columnViewPriority(priority, a);
            const float bPriority = columnViewPriority(priority, b);
            if (std::abs(aPriority - bPriority) > 0.0001f) return aPriority < bPriority;
            const int da = columnChebyshevDistance(a, cameraColumn);
            const int db = columnChebyshevDistance(b, cameraColumn);
            if (da != db) return da < db;
            if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
            return a.coord.y < b.coord.y;
        }

        bool columnWasDesiredRecently(const VoxelColumnKey& key, uint64_t frame, uint64_t retentionFrames) {
            auto it = g_voxelColumnStreaming.lastDesiredFrame.find(key);
            if (it == g_voxelColumnStreaming.lastDesiredFrame.end()) return false;
            return frame >= it->second && (frame - it->second) <= retentionFrames;
        }

        bool columnHasWorkOrResidency(const VoxelColumnKey& key, uint64_t frame, uint64_t retentionFrames) {
            if (g_voxelColumnStreaming.desired.count(key) > 0) return true;
            if (g_voxelColumnStreaming.jobs.count(key) > 0) {
                return columnWasDesiredRecently(key, frame, retentionFrames);
            }
            if (g_voxelColumnStreaming.generated.count(key) > 0) {
                return columnWasDesiredRecently(key, frame, retentionFrames);
            }
            return false;
        }

        void releaseStreamingColumn(BaseSystem& baseSystem,
                                    VoxelWorldContext& voxelWorld,
                                    const VoxelColumnKey& key,
                                    int minSectionY,
                                    int maxSectionY,
                                    VoxelColumnFrameMaintenanceStats& stats) {
            const bool hadJob = (g_voxelColumnStreaming.jobs.count(key) > 0);
            const bool hadGenerated = (g_voxelColumnStreaming.generated.count(key) > 0);
            WorldSaveSystemLogic::FlushColumnIfDirty(baseSystem, key);
            voxelWorld.releaseColumn(key);
            clearGeneratedColumnSections(key, minSectionY, maxSectionY);
            g_voxelColumnStreaming.generated.erase(key);
            g_voxelColumnStreaming.jobs.erase(key);
            g_voxelColumnStreaming.pendingSet.erase(key);
            g_voxelColumnStreaming.inProgress.erase(key);
            g_voxelColumnStreaming.completedFrame.erase(key);
            g_voxelColumnStreaming.lastDesiredFrame.erase(key);
            stats.released += 1;
            if (hadJob && !hadGenerated) {
                stats.releasedBeforeComplete += 1;
            } else if (hadGenerated) {
                stats.releasedAfterComplete += 1;
            }
        }

        void removeReleasedColumnFromPending(const VoxelColumnKey& releasedKey) {
            if (g_voxelColumnStreaming.pending.empty()) return;
            std::vector<VoxelColumnKey> filtered;
            filtered.reserve(g_voxelColumnStreaming.pending.size());
            for (const VoxelColumnKey& key : g_voxelColumnStreaming.pending) {
                if (!(key == releasedKey)) {
                    filtered.push_back(key);
                }
            }
            g_voxelColumnStreaming.pending.swap(filtered);
        }

        void prioritizeActiveColumnJobs(const glm::ivec2& cameraColumn,
                                        const VoxelColumnPriorityContext& priority,
                                        uint64_t frame,
                                        uint64_t retentionFrames,
                                        VoxelColumnFrameMaintenanceStats& stats) {
            for (const auto& [key, job] : g_voxelColumnStreaming.jobs) {
                (void)job;
                if (!columnWasDesiredRecently(key, frame, retentionFrames)
                    && g_voxelColumnStreaming.desired.count(key) == 0) {
                    continue;
                }
                if (g_voxelColumnStreaming.pendingSet.insert(key).second) {
                    g_voxelColumnStreaming.pending.push_back(key);
                    stats.activeRequeued += 1;
                }
            }
            if (g_voxelColumnStreaming.pending.empty()) return;
            std::stable_sort(
                g_voxelColumnStreaming.pending.begin(),
                g_voxelColumnStreaming.pending.end(),
                [&](const VoxelColumnKey& a, const VoxelColumnKey& b) {
                    const auto aJobIt = g_voxelColumnStreaming.jobs.find(a);
                    const auto bJobIt = g_voxelColumnStreaming.jobs.find(b);
                    const bool aActive = (aJobIt != g_voxelColumnStreaming.jobs.end());
                    const bool bActive = (bJobIt != g_voxelColumnStreaming.jobs.end());
                    if (aActive != bActive) return aActive;
                    if (aActive && bActive && aJobIt->second.phase != bJobIt->second.phase) {
                        return aJobIt->second.phase > bJobIt->second.phase;
                    }
                    return columnPriorityLess(priority, cameraColumn, a, b);
                }
            );
        }

        void markColumnSectionLifecycle(VoxelWorldContext& voxelWorld,
                                        const VoxelColumnKey& columnKey,
                                        int minSectionY,
                                        int maxSectionY,
                                        bool needsTerrainFeatures,
                                        bool needsSurfaceFoliage) {
            for (int sy = minSectionY; sy <= maxSectionY; ++sy) {
                const VoxelSectionKey sectionKey{glm::ivec3(columnKey.coord.x, sy, columnKey.coord.y)};
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(sectionKey);
                state.desired = true;
                state.generated = true;
                state.postFeaturesComplete = !needsTerrainFeatures;
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
                if (needsTerrainFeatures) {
                    state.stage = VoxelChunkLifecycleStage::FeatureQueued;
                } else if (state.isFullyReady()) {
                    state.stage = VoxelChunkLifecycleStage::Ready;
                } else {
                    state.stage = VoxelChunkLifecycleStage::BaseGenerated;
                }
            }
        }

        bool runColumnTerrainFeatureStep(BaseSystem& baseSystem,
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
            while (job.nextFeatureSectionY <= maxSectionY) {
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
                const bool resolved = RunExpanseSectionTerrainFeatures(
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
            }
            return job.nextFeatureSectionY > maxSectionY;
        }

        bool finalizeColumnRenderBridgeStep(BaseSystem& baseSystem,
                                            std::vector<Entity>& prototypes,
                                            VoxelWorldContext& voxelWorld,
                                            const VoxelColumnKey& columnKey,
                                            int minSectionY,
                                            int maxSectionY,
                                            int& nextPublishSectionY,
                                            int sectionsPerStep,
                                            bool requestSurfaceFoliage,
                                            bool blockOnSurfaceFoliage) {
            (void)baseSystem;
            (void)prototypes;
            (void)requestSurfaceFoliage;
            (void)blockOnSurfaceFoliage;
            if (nextPublishSectionY == std::numeric_limits<int>::min()) {
                nextPublishSectionY = minSectionY;
            }
            const auto publishStart = std::chrono::steady_clock::now();

            auto columnIt = voxelWorld.columns.find(columnKey);
            const bool hasColumn = columnIt != voxelWorld.columns.end() && columnIt->second.nonAirCount > 0;
            VoxelChunkLifecycleState& columnState = voxelWorld.ensureColumnState(columnKey);
            columnState.desired = true;
            columnState.generated = true;
            columnState.postFeaturesComplete = true;
            columnState.surfaceFoliageComplete = true;
            columnState.hasSection = hasColumn;
            columnState.stage = VoxelChunkLifecycleStage::Ready;
            columnState.touchFrame = g_voxelStreaming.frameCounter;
            if (hasColumn) {
                columnIt->second.editVersion += 1;
                voxelWorld.clearColumnDirty(columnKey);
            } else {
                voxelWorld.clearColumnDirty(columnKey);
            }

            const int sectionBudget = std::max(1, sectionsPerStep);
            int publishedSections = 0;
            const std::array<glm::ivec3, 4> horizontalNeighborOffsets = {{
                glm::ivec3(1, 0, 0),
                glm::ivec3(-1, 0, 0),
                glm::ivec3(0, 0, 1),
                glm::ivec3(0, 0, -1)
            }};
            while (nextPublishSectionY <= maxSectionY && publishedSections < sectionBudget) {
                const int sy = nextPublishSectionY;
                const VoxelSectionKey sectionKey{glm::ivec3(columnKey.coord.x, sy, columnKey.coord.y)};
                const bool hasRenderSection = hasColumn && voxelWorld.materializeSectionFromColumn(sectionKey);
                VoxelChunkLifecycleState& state = voxelWorld.ensureChunkState(sectionKey);
                state.desired = true;
                state.generated = true;
                state.postFeaturesComplete = true;
                state.hasSection = hasRenderSection;
                state.surfaceFoliageComplete = true;
                state.stage = VoxelChunkLifecycleStage::Ready;
                state.touchFrame = g_voxelStreaming.frameCounter;
                if (hasRenderSection) {
                    g_voxelTerrainGenerated.insert(sectionKey);
                    voxelWorld.markSectionDirty(sectionKey);
                    for (const glm::ivec3& offset : horizontalNeighborOffsets) {
                        const VoxelSectionKey neighborSectionKey{sectionKey.coord + offset};
                        const VoxelColumnKey neighborColumnKey{
                            glm::ivec2(neighborSectionKey.coord.x, neighborSectionKey.coord.z)
                        };
                        auto neighborColumnIt = voxelWorld.columns.find(neighborColumnKey);
                        if (neighborColumnIt == voxelWorld.columns.end()
                            || neighborColumnIt->second.nonAirCount <= 0) {
                            continue;
                        }
                        const VoxelChunkLifecycleState* neighborColumnState =
                            voxelWorld.findColumnState(neighborColumnKey);
                        if (!neighborColumnState || !neighborColumnState->isFullyReady()) {
                            continue;
                        }
                        if (voxelWorld.materializeSectionFromColumn(neighborSectionKey)) {
                            voxelWorld.markSectionDirty(neighborSectionKey);
                        }
                    }
                } else {
                    g_voxelTerrainGenerated.erase(sectionKey);
                }
                nextPublishSectionY += 1;
                publishedSections += 1;
            }
            g_terrainPublishFrameMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - publishStart
            ).count();
            return nextPublishSectionY > maxSectionY;
        }

        void finalizeColumnRenderBridge(BaseSystem& baseSystem,
                                        std::vector<Entity>& prototypes,
                                        VoxelWorldContext& voxelWorld,
                                        const VoxelColumnKey& columnKey,
                                        int minSectionY,
                                        int maxSectionY) {
            int cursor = minSectionY;
            const int sectionCount = std::max(1, maxSectionY - minSectionY + 1);
            (void)finalizeColumnRenderBridgeStep(
                baseSystem,
                prototypes,
                voxelWorld,
                columnKey,
                minSectionY,
                maxSectionY,
                cursor,
                sectionCount,
                false,
                false
            );
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
                                     int maxColumnStepUnits,
                                     int& outColumnSteps) {
            if (!baseSystem.voxelWorld) return true;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            VoxelChunkLifecycleState& columnState = voxelWorld.ensureColumnState(columnKey);
            columnState.desired = true;
            columnState.touchFrame = g_voxelStreaming.frameCounter;
            job.lastStepFrame = g_voxelStreaming.frameCounter;

            if (job.phase == 0) {
                columnState.stage = VoxelChunkLifecycleStage::BaseInProgress;
                bool completed = false;
                bool postFeaturesCompleted = true;
                int nextColumn = job.nextColumn;
                bool wroteAny = job.wroteAny;
                const int size = sectionSizeForSection(voxelWorld);
                const int fullColumnsPerSection = std::max(1, size * size);
                const int targetColumnCursor = fullColumnsPerSection * 2;
                const int stepUnits = std::max(1, maxColumnStepUnits);
                int consumedStepUnits = 0;
                while (nextColumn < targetColumnCursor && consumedStepUnits < stepUnits) {
                    const int previousColumn = nextColumn;
                    (void)GenerateExpanseColumnBase(
                        baseSystem,
                        prototypes,
                        worldCtx,
                        cfg,
                        columnKey,
                        columnMinY,
                        columnMaxY,
                        nextColumn,
                        fullColumnsPerSection,
                        wroteAny,
                        nextColumn,
                        completed,
                        postFeaturesCompleted
                    );
                    consumedStepUnits += 1;
                    if (completed) break;
                    if (nextColumn <= previousColumn) break;
                }
                outColumnSteps += std::max(1, consumedStepUnits);
                job.nextColumn = nextColumn;
                job.wroteAny = wroteAny;
                if (!completed) return false;
                const bool runPostFeatures = !postFeaturesCompleted;
                const bool blockOnSurfaceFoliage = false;
                markColumnSectionLifecycle(
                    voxelWorld,
                    columnKey,
                    minSectionY,
                    maxSectionY,
                    runPostFeatures,
                    blockOnSurfaceFoliage
                );
                columnState.generated = true;
                columnState.postFeaturesComplete = !runPostFeatures;
                columnState.surfaceFoliageComplete = !blockOnSurfaceFoliage;
                columnState.hasSection = (voxelWorld.columns.count(columnKey) > 0);
                columnState.stage = runPostFeatures
                    ? VoxelChunkLifecycleStage::FeatureQueued
                    : VoxelChunkLifecycleStage::BaseGenerated;
                job.phase = runPostFeatures ? 1 : 2;
                return false;
            }

            if (job.phase == 1) {
                columnState.stage = VoxelChunkLifecycleStage::FeatureInProgress;
                const bool blockOnSurfaceFoliage = false;
                const bool completed = runColumnTerrainFeatureStep(
                    baseSystem,
                    prototypes,
                    worldCtx,
                    cfg,
                    voxelWorld,
                    columnKey,
                    job,
                    minSectionY,
                    maxSectionY,
                    blockOnSurfaceFoliage
                );
                outColumnSteps += 1;
                if (!completed) return false;
                columnState.postFeaturesComplete = true;
                columnState.surfaceFoliageComplete = !blockOnSurfaceFoliage;
                columnState.stage = blockOnSurfaceFoliage
                    ? VoxelChunkLifecycleStage::BaseGenerated
                    : VoxelChunkLifecycleStage::Ready;
                columnState.touchFrame = g_voxelStreaming.frameCounter;
                job.phase = 2;
                return false;
            }

            if (job.phase == 2) {
                columnState.stage = VoxelChunkLifecycleStage::FeatureInProgress;
                const bool modified = TreeGenerationSystemLogic::GenerateColumnTerrainFoliage(
                    baseSystem,
                    prototypes,
                    worldCtx,
                    voxelWorld,
                    columnKey
                );
                job.wroteAny = job.wroteAny || modified;
                columnState.postFeaturesComplete = true;
                columnState.surfaceFoliageComplete = true;
                columnState.stage = VoxelChunkLifecycleStage::BaseGenerated;
                columnState.touchFrame = g_voxelStreaming.frameCounter;
                job.phase = 3;
                outColumnSteps += 1;
                return false;
            }

            if (job.phase == 3) {
                const int sectionCount = std::max(1, maxSectionY - minSectionY + 1);
                const bool blockOnSurfaceFoliage = false;
                const bool completed = finalizeColumnRenderBridgeStep(
                    baseSystem,
                    prototypes,
                    voxelWorld,
                    columnKey,
                    minSectionY,
                    maxSectionY,
                    job.nextPublishSectionY,
                    sectionCount,
                    false,
                    blockOnSurfaceFoliage
                );
                outColumnSteps += 1;
                if (!completed) return false;
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

        VoxelColumnFrameMaintenanceStats rebuildDesiredColumns(BaseSystem& baseSystem,
                                                               VoxelWorldContext& voxelWorld,
                                                               const ExpanseConfig& cfg,
                                                               const glm::vec3& cameraPos,
                                                               const VoxelCpuStreamingView& cpuStreamingView,
                                                               int cpuViewYawBucket) {
            VoxelColumnFrameMaintenanceStats stats;
            (void)cfg;
            const int radius = streamRadiusWorld(baseSystem, voxelWorld);
            const int size = sectionSizeForSection(voxelWorld);
            const glm::ivec2 cameraColumn(
                floorDivInt(static_cast<int>(std::floor(cameraPos.x)), size),
                floorDivInt(static_cast<int>(std::floor(cameraPos.z)), size)
            );
            const uint64_t frame = g_voxelStreaming.frameCounter;
            const uint64_t retentionFrames = static_cast<uint64_t>(std::max(
                0,
                getRegistryInt(baseSystem, "voxelColumnRetentionFrames", 180)
            ));
            const bool moved =
                g_voxelColumnStreaming.lastCenterColumn != cameraColumn
                || g_voxelColumnStreaming.lastRadius != radius
                || g_voxelColumnStreaming.lastCpuViewYawBucket != cpuViewYawBucket
                || g_voxelColumnStreaming.lastCpuViewCullingEnabled != cpuStreamingView.enabled
                || g_voxelColumnStreaming.lastCpuViewMotionLookaheadEnabled != cpuStreamingView.motionLookaheadEnabled
                || g_voxelColumnStreaming.lastCpuViewMotionColumn != cpuStreamingView.motionLookaheadColumn;
            if (!moved
                && !g_voxelColumnStreaming.pendingDesiredRebuild
                && !g_voxelColumnStreaming.desired.empty()) {
                for (const VoxelColumnKey& key : g_voxelColumnStreaming.desiredOrder) {
                    g_voxelColumnStreaming.lastDesiredFrame[key] = frame;
                }
                return stats;
            }

            g_voxelColumnStreaming.lastCenterColumn = cameraColumn;
            g_voxelColumnStreaming.lastRadius = radius;
            g_voxelColumnStreaming.lastCpuViewYawBucket = cpuViewYawBucket;
            g_voxelColumnStreaming.lastCpuViewCullingEnabled = cpuStreamingView.enabled;
            g_voxelColumnStreaming.lastCpuViewMotionLookaheadEnabled = cpuStreamingView.motionLookaheadEnabled;
            g_voxelColumnStreaming.lastCpuViewMotionColumn = cpuStreamingView.motionLookaheadColumn;
            g_voxelColumnStreaming.pendingDesiredRebuild = false;

            std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> nextDesired;
            std::vector<VoxelColumnKey> nextOrder;
            int columnRadius = 0;
            if (radius > 0) {
                columnRadius = static_cast<int>(std::ceil(
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

            if (getRegistryBool(baseSystem, "voxelColumnDesiredViewSortEnabled", true)
                && nextOrder.size() > 1u) {
                const VoxelColumnPriorityContext priority =
                    makeVoxelColumnPriorityContext(baseSystem, voxelWorld, cameraPos);
                std::stable_sort(
                    nextOrder.begin(),
                    nextOrder.end(),
                    [&](const VoxelColumnKey& a, const VoxelColumnKey& b) {
                        return columnPriorityLess(priority, cameraColumn, a, b);
                    }
                );
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
                g_voxelColumnStreaming.lastDesiredFrame[key] = frame;
                voxelWorld.ensureColumnState(key).desired = true;
                if (g_voxelColumnStreaming.generated.count(key) > 0) continue;
                if (g_voxelColumnStreaming.jobs.count(key) > 0) continue;
                if (g_voxelColumnStreaming.pendingSet.insert(key).second) {
                    g_voxelColumnStreaming.pending.push_back(key);
                    voxelWorld.ensureColumnState(key).stage = VoxelChunkLifecycleStage::BaseQueued;
                }
            }
            for (auto& [key, state] : voxelWorld.columnStates) {
                state.desired = (nextDesired.count(key) > 0);
            }

            std::vector<VoxelColumnKey> releaseColumns;
            releaseColumns.reserve(voxelWorld.columns.size());
            const int retentionRadiusChunks = std::max(
                0,
                getRegistryInt(baseSystem, "voxelColumnRetentionRadiusChunks", 3)
            );
            const bool releaseInProgress = getRegistryBool(
                baseSystem,
                "voxelColumnReleaseInProgress",
                false
            );
            const int retainedColumnRadius = columnRadius + retentionRadiusChunks;
            for (const auto& [key, _] : voxelWorld.columns) {
                if (nextDesired.count(key) > 0) {
                    continue;
                }
                const bool hasJob = (g_voxelColumnStreaming.jobs.count(key) > 0);
                const bool hasGenerated = (g_voxelColumnStreaming.generated.count(key) > 0);
                const bool recentlyDesired = columnWasDesiredRecently(key, frame, retentionFrames);
                const bool closeEnough = columnChebyshevDistance(key, cameraColumn) <= retainedColumnRadius;
                const bool retainActive = hasJob && !releaseInProgress && recentlyDesired;
                const bool retainGenerated = hasGenerated && (recentlyDesired || closeEnough);
                if (retainActive || retainGenerated) {
                    stats.retained += 1;
                    continue;
                }
                stats.evictable += 1;
                releaseColumns.push_back(key);
            }
            const int minSectionY = floorDivInt(voxelWorld.columnMinY, size);
            const int maxSectionY = floorDivInt(voxelWorld.columnMaxYExclusive - 1, size);
            for (const VoxelColumnKey& key : releaseColumns) {
                releaseStreamingColumn(baseSystem, voxelWorld, key, minSectionY, maxSectionY, stats);
                removeReleasedColumnFromPending(key);
            }

            const int residentHardCap = std::max(
                0,
                getRegistryInt(baseSystem, "voxelColumnResidentHardCap", 256)
            );
            if (residentHardCap > 0 && static_cast<int>(voxelWorld.columns.size()) > residentHardCap) {
                struct ReleaseCandidate {
                    VoxelColumnKey key;
                    int distance = 0;
                    uint64_t lastDesiredFrame = 0;
                    bool hasJob = false;
                    bool generated = false;
                };
                std::vector<ReleaseCandidate> candidates;
                candidates.reserve(voxelWorld.columns.size());
                for (const auto& [key, _] : voxelWorld.columns) {
                    if (nextDesired.count(key) > 0) continue;
                    const bool hasJob = (g_voxelColumnStreaming.jobs.count(key) > 0);
                    if (hasJob && !releaseInProgress) continue;
                    auto lastIt = g_voxelColumnStreaming.lastDesiredFrame.find(key);
                    candidates.push_back(ReleaseCandidate{
                        key,
                        columnChebyshevDistance(key, cameraColumn),
                        lastIt != g_voxelColumnStreaming.lastDesiredFrame.end() ? lastIt->second : 0u,
                        hasJob,
                        g_voxelColumnStreaming.generated.count(key) > 0
                    });
                }
                std::sort(
                    candidates.begin(),
                    candidates.end(),
                    [](const ReleaseCandidate& a, const ReleaseCandidate& b) {
                        if (a.hasJob != b.hasJob) return !a.hasJob;
                        if (a.lastDesiredFrame != b.lastDesiredFrame) {
                            return a.lastDesiredFrame < b.lastDesiredFrame;
                        }
                        if (a.distance != b.distance) return a.distance > b.distance;
                        return a.generated && !b.generated;
                    }
                );
                for (const ReleaseCandidate& candidate : candidates) {
                    if (static_cast<int>(voxelWorld.columns.size()) <= residentHardCap) break;
                    releaseStreamingColumn(
                        baseSystem,
                        voxelWorld,
                        candidate.key,
                        minSectionY,
                        maxSectionY,
                        stats
                    );
                    removeReleasedColumnFromPending(candidate.key);
                    stats.evictable += 1;
                }
            }

            if (!g_voxelColumnStreaming.pending.empty()) {
                std::vector<VoxelColumnKey> filtered;
                filtered.reserve(g_voxelColumnStreaming.pending.size());
                for (const VoxelColumnKey& key : g_voxelColumnStreaming.pending) {
                    const bool desiredNow = (nextDesired.count(key) > 0);
                    const bool activeJob = (g_voxelColumnStreaming.jobs.count(key) > 0);
                    if ((desiredNow || (activeJob && columnWasDesiredRecently(key, frame, retentionFrames)))
                        && g_voxelColumnStreaming.generated.count(key) == 0) {
                        filtered.push_back(key);
                    } else {
                        g_voxelColumnStreaming.pendingSet.erase(key);
                        stats.pendingFiltered += 1;
                    }
                }
                g_voxelColumnStreaming.pending.swap(filtered);
            }

            g_voxelColumnStreaming.desired.swap(nextDesired);
            g_voxelColumnStreaming.desiredOrder.swap(nextOrder);
            return stats;
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
            const auto maintenanceStart = std::chrono::steady_clock::now();
            VoxelColumnFrameMaintenanceStats maintenance = rebuildDesiredColumns(
                baseSystem,
                voxelWorld,
                cfg,
                cameraPos,
                cpuStreamingView,
                cpuViewYawBucket
            );
            g_terrainMaintenanceFrameMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - maintenanceStart
            ).count();
            const glm::ivec2 cameraColumn(
                floorDivInt(static_cast<int>(std::floor(cameraPos.x)), size),
                floorDivInt(static_cast<int>(std::floor(cameraPos.z)), size)
            );
            const VoxelColumnPriorityContext columnPriority =
                makeVoxelColumnPriorityContext(baseSystem, voxelWorld, cameraPos);
            const uint64_t retentionFrames = static_cast<uint64_t>(std::max(
                0,
                getRegistryInt(baseSystem, "voxelColumnRetentionFrames", 180)
            ));
            const auto priorityStart = std::chrono::steady_clock::now();
            prioritizeActiveColumnJobs(
                cameraColumn,
                columnPriority,
                g_voxelStreaming.frameCounter,
                retentionFrames,
                maintenance
            );
            g_terrainMaintenanceFrameMs += std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - priorityStart
            ).count();

            int generationBudget = std::max(
                1,
                getRegistryInt(baseSystem, "voxelColumnGenerationStepsPerFrame", 1)
            );
            float generationTimeBudgetMs = std::max(
                0.1f,
                getRegistryFloat(baseSystem, "voxelColumnGenerationMaxMsPerFrame", 8.0f)
            );
            const bool menuFlyoverPrewarm =
                currentLevel == "menu"
                && baseSystem.ui
                && baseSystem.ui->loadingActive
                && getRegistryBool(baseSystem, "IslandFlyoverEnabled", true)
                && getRegistryBool(baseSystem, "voxelColumnMenuPrewarmEnabled", true);
            if (menuFlyoverPrewarm) {
                generationBudget = std::max(
                    generationBudget,
                    getRegistryInt(baseSystem, "voxelColumnMenuPrewarmStepsPerFrame", 8)
                );
                generationTimeBudgetMs = std::max(
                    generationTimeBudgetMs,
                    getRegistryFloat(baseSystem, "voxelColumnMenuPrewarmMaxMsPerFrame", 14.0f)
                );
            }
            const auto genStart = std::chrono::steady_clock::now();
            int columnSteps = 0;
            int columnsBuilt = 0;
            int columnsConsumed = 0;
            int skipped = 0;
            int columnsStarted = 0;
            int columnsLoaded = 0;

            while (!g_voxelColumnStreaming.pending.empty() && columnSteps < generationBudget) {
                if (columnsConsumed > 0 && generationTimeBudgetMs > 0.0f) {
                    const float elapsedMs = std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - genStart
                    ).count();
                    if (elapsedMs >= generationTimeBudgetMs) break;
                }
                const auto popStart = std::chrono::steady_clock::now();
                const VoxelColumnKey key = g_voxelColumnStreaming.pending.front();
                g_voxelColumnStreaming.pending.erase(g_voxelColumnStreaming.pending.begin());
                g_voxelColumnStreaming.pendingSet.erase(key);
                g_terrainPendingPopFrameMs += std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - popStart
                ).count();
                columnsConsumed += 1;

                auto jobIt = g_voxelColumnStreaming.jobs.find(key);
                const bool desiredNow = (g_voxelColumnStreaming.desired.count(key) > 0);
                const bool activeJob = (jobIt != g_voxelColumnStreaming.jobs.end());
                if (!desiredNow && !activeJob) {
                    skipped += 1;
                    continue;
                }
                if (!columnHasWorkOrResidency(key, g_voxelStreaming.frameCounter, retentionFrames)) {
                    releaseStreamingColumn(
                        baseSystem,
                        voxelWorld,
                        key,
                        minSectionY,
                        maxSectionY,
                        maintenance
                    );
                    skipped += 1;
                    continue;
                }
                if (g_voxelColumnStreaming.generated.count(key) > 0) {
                    skipped += 1;
                    continue;
                }

                g_voxelColumnStreaming.inProgress.insert(key);

                if (jobIt == g_voxelColumnStreaming.jobs.end()) {
                    const auto saveProbeStart = std::chrono::steady_clock::now();
                    const bool loadedSavedColumn = WorldSaveSystemLogic::TryLoadSavedColumn(baseSystem, key);
                    g_terrainSaveProbeFrameMs += std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - saveProbeStart
                    ).count();
                    if (loadedSavedColumn) {
                        finalizeColumnRenderBridge(
                            baseSystem,
                            prototypes,
                            voxelWorld,
                            key,
                            minSectionY,
                            maxSectionY
                        );
                        VoxelChunkLifecycleState& loadedState = voxelWorld.ensureColumnState(key);
                        g_voxelColumnStreaming.generated.insert(key);
                        loadedState.generated = true;
                        loadedState.postFeaturesComplete = true;
                        loadedState.surfaceFoliageComplete = true;
                        loadedState.hasSection = (voxelWorld.columns.count(key) > 0);
                        loadedState.stage = VoxelChunkLifecycleStage::Ready;
                        loadedState.touchFrame = g_voxelStreaming.frameCounter;
                        g_voxelColumnStreaming.inProgress.erase(key);
                        columnSteps += 1;
                        columnsBuilt += 1;
                        columnsLoaded += 1;
                        g_voxelColumnStreaming.completedFrame[key] = g_voxelStreaming.frameCounter;
                        continue;
                    }
                    const auto releaseStart = std::chrono::steady_clock::now();
                    WorldSaveSystemLogic::FlushColumnIfDirty(baseSystem, key);
                    voxelWorld.releaseColumn(key);
                    g_terrainReleaseFrameMs += std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - releaseStart
                    ).count();
                    VoxelColumnGenerationJob job;
                    job.startedFrame = g_voxelStreaming.frameCounter;
                    job.lastStepFrame = g_voxelStreaming.frameCounter;
                    auto inserted = g_voxelColumnStreaming.jobs.emplace(key, job);
                    jobIt = inserted.first;
                    columnsStarted += 1;
                }

                VoxelChunkLifecycleState& columnState = voxelWorld.ensureColumnState(key);
                columnState.desired = desiredNow || activeJob;
                columnState.stage = VoxelChunkLifecycleStage::BaseInProgress;
                columnState.touchFrame = g_voxelStreaming.frameCounter;

                bool completed = false;
                while (columnSteps < generationBudget) {
                    const int phaseBeforeStep = jobIt->second.phase;
                    const auto stepStart = std::chrono::steady_clock::now();
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
                        generationBudget - columnSteps,
                        columnSteps
                    );
                    const float stepElapsedMs = std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - stepStart
                    ).count();
                    g_terrainStepFrameMs += stepElapsedMs;
                    switch (phaseBeforeStep) {
                        case 0:
                            g_terrainPhase0FrameMs += stepElapsedMs;
                            break;
                        case 1:
                            g_terrainPhase1FrameMs += stepElapsedMs;
                            break;
                        case 2:
                            g_terrainPhase2FrameMs += stepElapsedMs;
                            break;
                        case 3:
                            g_terrainPhase3FrameMs += stepElapsedMs;
                            break;
                        default:
                            break;
                    }
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
                    g_voxelColumnStreaming.completedFrame[key] = g_voxelStreaming.frameCounter;
                    columnsBuilt += 1;
                } else if ((g_voxelColumnStreaming.desired.count(key) > 0
                            || columnWasDesiredRecently(key, g_voxelStreaming.frameCounter, retentionFrames))
                           && g_voxelColumnStreaming.pendingSet.insert(key).second) {
                    const auto requeueStart = std::chrono::steady_clock::now();
                    switch (jobIt->second.phase) {
                        case 0:
                            columnState.stage = VoxelChunkLifecycleStage::BaseQueued;
                            break;
                        case 1:
                            columnState.stage = VoxelChunkLifecycleStage::FeatureQueued;
                            break;
                        case 3:
                            columnState.stage = VoxelChunkLifecycleStage::BaseGenerated;
                            break;
                        default:
                            columnState.stage = VoxelChunkLifecycleStage::Desired;
                            break;
                    }
                    g_voxelColumnStreaming.pending.insert(g_voxelColumnStreaming.pending.begin(), key);
                    g_terrainRequeueFrameMs += std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - requeueStart
                    ).count();
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
            g_voxelStreamingPerfStats.baseGenMs = g_terrainPhase0FrameMs;
            g_voxelStreamingPerfStats.featureMs = g_terrainPhase1FrameMs;
            g_voxelStreamingPerfStats.surfaceMs = g_terrainPhase2FrameMs;
            g_voxelStreamingPerfStats.caveFieldMs = g_caveFieldFrameMs;
            g_voxelStreamingPerfStats.schedulerPressure = 0;
            g_voxelStreamingPerfStats.desiredBudget = static_cast<int>(g_voxelColumnStreaming.desired.size());
            g_voxelStreamingPerfStats.baseBudget = generationBudget;
            g_voxelStreamingPerfStats.featureBudget = 0;
            g_voxelStreamingPerfStats.surfaceBudget = 0;
            g_voxelStreamingPerfStats.baseBudgetMs = generationTimeBudgetMs;
            g_voxelStreamingPerfStats.featureBudgetMs = 0.0f;
            g_voxelStreamingPerfStats.surfaceBudgetMs = 0.0f;
            g_voxelStreamingPerfStats.downstreamDirty =
                voxelWorld.dirtySections.size() + voxelWorld.dirtyColumns.size();
            g_voxelStreamingPerfStats.downstreamPrepared = baseSystem.voxelRender
                ? baseSystem.voxelRender->preparedMeshes.size()
                    + baseSystem.voxelRender->preparedColumnMeshes.size()
                : 0u;
            g_voxelStreamingPerfStats.downstreamUpload = baseSystem.voxelRender
                ? baseSystem.voxelRender->renderBuffersDirty.size()
                    + baseSystem.voxelRender->columnRenderBuffersDirty.size()
                : 0u;
            g_voxelStreamingPerfStats.caveFieldCellsBuilt = g_caveFieldFrameCellsBuilt;
            g_voxelStreamingPerfStats.caveSamples = g_caveFieldFrameSampleCount;
            g_voxelStreamingPerfStats.columnResident = voxelWorld.columns.size();
            g_voxelStreamingPerfStats.columnRetained = static_cast<size_t>(std::max(0, maintenance.retained));
            g_voxelStreamingPerfStats.columnEvictable = static_cast<size_t>(std::max(0, maintenance.evictable));
            g_voxelStreamingPerfStats.columnStarted = columnsStarted;
            g_voxelStreamingPerfStats.columnLoaded = columnsLoaded;
            g_voxelStreamingPerfStats.columnCompleted = columnsBuilt;
            g_voxelStreamingPerfStats.columnReleased = maintenance.released;
            g_voxelStreamingPerfStats.columnReleasedBeforeComplete = maintenance.releasedBeforeComplete;
            g_voxelStreamingPerfStats.columnReleasedAfterComplete = maintenance.releasedAfterComplete;
            g_voxelStreamingPerfStats.columnActiveRequeued = maintenance.activeRequeued;
            g_voxelStreamingPerfStats.columnPendingFiltered = maintenance.pendingFiltered;
            g_voxelStreamingPerfStats.columnPhase0 = 0;
            g_voxelStreamingPerfStats.columnPhase1 = 0;
            g_voxelStreamingPerfStats.columnPhase2 = 0;
            g_voxelStreamingPerfStats.columnPhase3 = 0;
            for (const auto& [key, job] : g_voxelColumnStreaming.jobs) {
                (void)key;
                switch (job.phase) {
                    case 0:
                        g_voxelStreamingPerfStats.columnPhase0 += 1;
                        break;
                    case 1:
                        g_voxelStreamingPerfStats.columnPhase1 += 1;
                        break;
                    case 2:
                        g_voxelStreamingPerfStats.columnPhase2 += 1;
                        break;
                    case 3:
                        g_voxelStreamingPerfStats.columnPhase3 += 1;
                        break;
                    default:
                        break;
                }
            }

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
            const glm::vec3 cameraPos = baseSystem.player->cameraPosition;
            const std::string currentLevel = getRegistryString(baseSystem, "level", "the_expanse");

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

            UpdateExpanseVoxelColumns(
                baseSystem,
                prototypes,
                worldCtx,
                cfg,
                currentLevel,
                cpuStreamingView,
                cpuViewYawBucket
            );
        }

    }

    void ResetExpanseVoxelStreaming(BaseSystem& baseSystem, bool flushActiveWorld) {
        if (flushActiveWorld) {
            WorldSaveSystemLogic::FlushActiveWorld(baseSystem, false);
        }
        if (baseSystem.voxelWorld) {
            baseSystem.voxelWorld->reset();
        }
        if (baseSystem.voxelRender) {
            baseSystem.voxelRender->renderClusters.clear();
            baseSystem.voxelRender->renderBuffers.clear();
            baseSystem.voxelRender->preparedMeshes.clear();
            baseSystem.voxelRender->wireframeMeshes.clear();
            baseSystem.voxelRender->renderBuffersDirty.clear();
            baseSystem.voxelRender->columnRenderClusters.clear();
            baseSystem.voxelRender->columnRenderBuffers.clear();
            baseSystem.voxelRender->preparedColumnMeshes.clear();
            baseSystem.voxelRender->wireframeColumnMeshes.clear();
            baseSystem.voxelRender->columnRenderBuffersDirty.clear();
        }
        if (baseSystem.farTerrain) {
            baseSystem.farTerrain->initialized = false;
            baseSystem.farTerrain->enabled = false;
            baseSystem.farTerrain->visibleFaceCount = 0;
            baseSystem.farTerrain->handoffVisibleFaceCount = 0;
            baseSystem.farTerrain->bodyVisibleFaceCount = 0;
            baseSystem.farTerrain->resolvedCellCache.clear();
            baseSystem.farTerrain->handoffCells.clear();
            baseSystem.farTerrain->bodyCells.clear();
            for (auto& faces : baseSystem.farTerrain->handoffFaces) faces.clear();
            for (auto& faces : baseSystem.farTerrain->bodyFaces) faces.clear();
            for (auto& faces : baseSystem.farTerrain->handoffWaterSurfaceFaces) faces.clear();
            for (auto& faces : baseSystem.farTerrain->bodyWaterSurfaceFaces) faces.clear();
            baseSystem.farTerrain->handoffRenderClusters.clear();
            baseSystem.farTerrain->bodyRenderClusters.clear();
            baseSystem.farTerrain->handoffRenderBuffersValid = false;
            baseSystem.farTerrain->handoffRenderBuffersDirty = false;
            baseSystem.farTerrain->bodyRenderBuffersValid = false;
            baseSystem.farTerrain->bodyRenderBuffersDirty = false;
        }
        g_voxelTerrainGenerated.clear();
        g_voxelColumnStreaming = VoxelColumnStreamingState{};
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
        g_voxelStreamingPerfStats = {};
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

    void GetVoxelTerrainDetailedPerfStats(float& workerSetupMs,
                                          float& workerColumnMs,
                                          float& publishMs,
                                          float& maintenanceMs,
                                          float& pendingPopMs,
                                          float& saveProbeMs,
                                          float& releaseMs,
                                          float& stepMs,
                                          float& requeueMs) {
        workerSetupMs = g_terrainWorkerSetupFrameMs;
        workerColumnMs = g_terrainWorkerColumnFrameMs;
        publishMs = g_terrainPublishFrameMs;
        maintenanceMs = g_terrainMaintenanceFrameMs;
        pendingPopMs = g_terrainPendingPopFrameMs;
        saveProbeMs = g_terrainSaveProbeFrameMs;
        releaseMs = g_terrainReleaseFrameMs;
        stepMs = g_terrainStepFrameMs;
        requeueMs = g_terrainRequeueFrameMs;
    }

    void GetVoxelTerrainBaseBreakdownPerfStats(float& setupMs,
                                               float& caveFieldPrepMs,
                                               float& surfaceBiomeMs,
                                               float& hydrologyMs,
                                               float& caveMs,
                                               float& oreDecorMs,
                                               float& memoryMs,
                                               float& blockWriteMs,
                                               float& caveRampMs,
                                               float& bookkeepingMs,
                                               float& materializeMs,
                                               float& fillPassMs,
                                               float& detailPassMs,
                                               float& postFeaturePassMs,
                                               uint64_t& workerCalls,
                                               uint64_t& fillPassCalls,
                                               uint64_t& detailPassCalls,
                                               uint64_t& postFeatureCalls,
                                               uint64_t& columnsVisited,
                                               uint64_t& verticalCellsVisited,
                                               uint64_t& terrainSampleCalls,
                                               uint64_t& terrainSampleMisses,
                                               uint64_t& biomeResolveCalls,
                                               uint64_t& caveSampleCalls,
                                               uint64_t& hydrologyColumns,
                                               uint64_t& oreDecorColumnCalls,
                                               uint64_t& oreDecorCellCalls,
                                               uint64_t& directColumnEnsureCalls,
                                               uint64_t& directColumnCreateCalls,
                                               uint64_t& writeVoxelCalls,
                                               uint64_t& writeRunCalls,
                                               uint64_t& writeCellsChanged,
                                               uint64_t& caveRampScanCalls,
                                               uint64_t& materializeCalls) {
        setupMs = g_terrainBaseBreakdownFrame.setupMs;
        caveFieldPrepMs = g_terrainBaseBreakdownFrame.caveFieldPrepMs;
        surfaceBiomeMs = g_terrainBaseBreakdownFrame.surfaceBiomeMs;
        hydrologyMs = g_terrainBaseBreakdownFrame.hydrologyMs;
        caveMs = g_terrainBaseBreakdownFrame.caveMs;
        oreDecorMs = g_terrainBaseBreakdownFrame.oreDecorMs;
        memoryMs = g_terrainBaseBreakdownFrame.memoryMs;
        blockWriteMs = g_terrainBaseBreakdownFrame.blockWriteMs;
        caveRampMs = g_terrainBaseBreakdownFrame.caveRampMs;
        bookkeepingMs = g_terrainBaseBreakdownFrame.bookkeepingMs;
        materializeMs = g_terrainBaseBreakdownFrame.materializeMs;
        fillPassMs = g_terrainBaseBreakdownFrame.fillPassMs;
        detailPassMs = g_terrainBaseBreakdownFrame.detailPassMs;
        postFeaturePassMs = g_terrainBaseBreakdownFrame.postFeaturePassMs;
        workerCalls = g_terrainBaseBreakdownFrame.workerCalls;
        fillPassCalls = g_terrainBaseBreakdownFrame.fillPassCalls;
        detailPassCalls = g_terrainBaseBreakdownFrame.detailPassCalls;
        postFeatureCalls = g_terrainBaseBreakdownFrame.postFeatureCalls;
        columnsVisited = g_terrainBaseBreakdownFrame.columnsVisited;
        verticalCellsVisited = g_terrainBaseBreakdownFrame.verticalCellsVisited;
        terrainSampleCalls = g_terrainBaseBreakdownFrame.terrainSampleCalls;
        terrainSampleMisses = g_terrainBaseBreakdownFrame.terrainSampleMisses;
        biomeResolveCalls = g_terrainBaseBreakdownFrame.biomeResolveCalls;
        caveSampleCalls = g_terrainBaseBreakdownFrame.caveSampleCalls;
        hydrologyColumns = g_terrainBaseBreakdownFrame.hydrologyColumns;
        oreDecorColumnCalls = g_terrainBaseBreakdownFrame.oreDecorColumnCalls;
        oreDecorCellCalls = g_terrainBaseBreakdownFrame.oreDecorCellCalls;
        directColumnEnsureCalls = g_terrainBaseBreakdownFrame.directColumnEnsureCalls;
        directColumnCreateCalls = g_terrainBaseBreakdownFrame.directColumnCreateCalls;
        writeVoxelCalls = g_terrainBaseBreakdownFrame.writeVoxelCalls;
        writeRunCalls = g_terrainBaseBreakdownFrame.writeRunCalls;
        writeCellsChanged = g_terrainBaseBreakdownFrame.writeCellsChanged;
        caveRampScanCalls = g_terrainBaseBreakdownFrame.caveRampScanCalls;
        materializeCalls = g_terrainBaseBreakdownFrame.materializeCalls;
    }

    void GetVoxelTerrainBaseCoarsePerfStats(float& fillDepthMs,
                                            float& fillRegularMs,
                                            float& fillRegularCaveScanMs,
                                            float& fillRangeWriteMs,
                                            float& detailScanMs,
                                            uint64_t& detailAirSkipped,
                                            uint64_t& detailDepthCells,
                                            uint64_t& detailSurfaceCells,
                                            uint64_t& detailSurfaceNeighborChecks,
                                            uint64_t& detailWrites) {
        fillDepthMs = g_terrainBaseBreakdownFrame.fillDepthMs;
        fillRegularMs = g_terrainBaseBreakdownFrame.fillRegularMs;
        fillRegularCaveScanMs = g_terrainBaseBreakdownFrame.fillRegularCaveScanMs;
        fillRangeWriteMs = g_terrainBaseBreakdownFrame.fillRangeWriteMs;
        detailScanMs = g_terrainBaseBreakdownFrame.detailScanMs;
        detailAirSkipped = g_terrainBaseBreakdownFrame.detailAirSkipped;
        detailDepthCells = g_terrainBaseBreakdownFrame.detailDepthCells;
        detailSurfaceCells = g_terrainBaseBreakdownFrame.detailSurfaceCells;
        detailSurfaceNeighborChecks = g_terrainBaseBreakdownFrame.detailSurfaceNeighborChecks;
        detailWrites = g_terrainBaseBreakdownFrame.detailWrites;
    }

    void GetVoxelColumnStreamingPerfStats(size_t& resident,
                                          size_t& retained,
                                          size_t& evictable,
                                          int& started,
                                          int& loaded,
                                          int& completed,
                                          int& released,
                                          int& releasedBeforeComplete,
                                          int& releasedAfterComplete,
                                          int& activeRequeued,
                                          int& pendingFiltered,
                                          int& phase0,
                                          int& phase1,
                                          int& phase2,
                                          int& phase3) {
        resident = g_voxelStreamingPerfStats.columnResident;
        retained = g_voxelStreamingPerfStats.columnRetained;
        evictable = g_voxelStreamingPerfStats.columnEvictable;
        started = g_voxelStreamingPerfStats.columnStarted;
        loaded = g_voxelStreamingPerfStats.columnLoaded;
        completed = g_voxelStreamingPerfStats.columnCompleted;
        released = g_voxelStreamingPerfStats.columnReleased;
        releasedBeforeComplete = g_voxelStreamingPerfStats.columnReleasedBeforeComplete;
        releasedAfterComplete = g_voxelStreamingPerfStats.columnReleasedAfterComplete;
        activeRequeued = g_voxelStreamingPerfStats.columnActiveRequeued;
        pendingFiltered = g_voxelStreamingPerfStats.columnPendingFiltered;
        phase0 = g_voxelStreamingPerfStats.columnPhase0;
        phase1 = g_voxelStreamingPerfStats.columnPhase1;
        phase2 = g_voxelStreamingPerfStats.columnPhase2;
        phase3 = g_voxelStreamingPerfStats.columnPhase3;
    }

    void GetVoxelCaveFieldRuntimeStats(int& mode,
                                       size_t& binaryTileCount,
                                       size_t& perlinTileCount,
                                       uint64_t& totalTilesBuilt,
                                       uint64_t& totalCellsBuilt) {
        mode = g_caveField.mode;
        binaryTileCount = g_caveField.tiles.size();
        perlinTileCount = g_caveField.perlinTiles.size();
        totalTilesBuilt = g_caveFieldTotalTilesBuilt;
        totalCellsBuilt = g_caveFieldTotalCellsBuilt;
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
        pending = g_voxelColumnStreaming.pending.size()
            + g_voxelColumnStreaming.jobs.size()
            + g_voxelColumnStreaming.inProgress.size();
        desired = g_voxelColumnStreaming.desired.size();
        generated = g_voxelColumnStreaming.generated.size();
        jobs = g_voxelColumnStreaming.jobs.size();
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
        return g_voxelTerrainGenerated.count(key) > 0;
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
        g_terrainWorkerSetupFrameMs = 0.0f;
        g_terrainWorkerColumnFrameMs = 0.0f;
        g_terrainPublishFrameMs = 0.0f;
        g_terrainMaintenanceFrameMs = 0.0f;
        g_terrainPendingPopFrameMs = 0.0f;
        g_terrainSaveProbeFrameMs = 0.0f;
        g_terrainReleaseFrameMs = 0.0f;
        g_terrainStepFrameMs = 0.0f;
        g_terrainRequeueFrameMs = 0.0f;
        g_terrainPhase0FrameMs = 0.0f;
        g_terrainPhase1FrameMs = 0.0f;
        g_terrainPhase2FrameMs = 0.0f;
        g_terrainPhase3FrameMs = 0.0f;
        g_terrainBaseBreakdownFrame = {};
        auto resetVoxelStreamingState = [&]() {
            if (!baseSystem.voxelWorld) return;
            WorldSaveSystemLogic::FlushActiveWorld(baseSystem, false);
            baseSystem.voxelWorld->reset();
            g_voxelTerrainGenerated.clear();
            g_voxelColumnStreaming = VoxelColumnStreamingState{};
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
            g_caveField.perlinTiles.clear();
            g_caveFieldTotalTilesBuilt = 0;
            g_caveFieldTotalCellsBuilt = 0;
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
            g_voxelDimensionKey.clear();
            return;
        }
        if (baseSystem.voxelWorld) {
            baseSystem.voxelWorld->enabled = true;
        }
        if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return;
        if (baseSystem.registry) {
            const std::string terrainSchemaVersion = "terrain_schema_column_chunks_v23";
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
        const std::string dimensionKey = activeDimensionId(baseSystem);
        if (g_voxelLevelKey != levelKey || g_voxelDimensionKey != dimensionKey) {
            g_voxelLevelKey = levelKey;
            g_voxelDimensionKey = dimensionKey;
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
