#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/glm.hpp>

struct VoxelSectionKey {
    glm::ivec3 coord{0};
    VoxelSectionKey() = default;
    explicit VoxelSectionKey(const glm::ivec3& c) : coord(c) {}
    bool operator==(const VoxelSectionKey& other) const noexcept {
        return coord == other.coord;
    }
};

struct VoxelSectionKeyHash {
    std::size_t operator()(const VoxelSectionKey& key) const noexcept {
        auto mix = [](uint64_t value) noexcept {
            value += 0x9e3779b97f4a7c15ull;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            return value ^ (value >> 31);
        };
        uint64_t h = mix(static_cast<uint32_t>(key.coord.x));
        h ^= mix(static_cast<uint32_t>(key.coord.y)) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        h ^= mix(static_cast<uint32_t>(key.coord.z)) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return static_cast<std::size_t>(h);
    }
};

struct VoxelColumnKey {
    glm::ivec2 coord{0};
    VoxelColumnKey() = default;
    explicit VoxelColumnKey(const glm::ivec2& c) : coord(c) {}
    bool operator==(const VoxelColumnKey& other) const noexcept {
        return coord == other.coord;
    }
};

struct VoxelColumnKeyHash {
    std::size_t operator()(const VoxelColumnKey& key) const noexcept {
        auto mix = [](uint64_t value) noexcept {
            value += 0x9e3779b97f4a7c15ull;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
            return value ^ (value >> 31);
        };
        uint64_t h = mix(static_cast<uint32_t>(key.coord.x));
        h ^= mix(static_cast<uint32_t>(key.coord.y)) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        return static_cast<std::size_t>(h);
    }
};

struct VoxelSection {
    int size = 0;
    glm::ivec3 coord{0};
    std::vector<uint32_t> ids;
    std::vector<uint32_t> colors;
    std::vector<uint8_t> skyLight;
    std::vector<uint8_t> blockLight;
    int nonAirCount = 0;
    uint32_t editVersion = 0;
    std::array<uint8_t, 6> visibilityFromFace{};
    uint8_t openFaceMask = 0;
    bool dirty = false;
};

struct VoxelColumn {
    VoxelColumnKey key{};
    int chunkSize = 16;
    int minY = -64;
    int maxYExclusive = 320;
    std::vector<uint32_t> ids;
    std::vector<uint32_t> colors;
    std::vector<uint8_t> skyLight;
    std::vector<uint8_t> blockLight;
    int nonAirCount = 0;
    int contentMinY = 0;
    int contentMaxY = -1;
    uint32_t editVersion = 0;
};

struct PendingVoxelColumnWrite {
    glm::ivec3 worldPos{0};
    uint32_t id = 0;
    uint32_t color = 0;
    bool onlyIfEmpty = false;
};

enum class VoxelChunkLifecycleStage : uint8_t {
    Unknown = 0,
    Desired,
    BaseQueued,
    BaseInProgress,
    BaseGenerated,
    FeatureQueued,
    FeatureInProgress,
    Ready
};

struct VoxelChunkLifecycleState {
    VoxelChunkLifecycleStage stage = VoxelChunkLifecycleStage::Unknown;
    bool desired = false;
    bool generated = false;
    bool hasSection = false;
    bool postFeaturesComplete = false;
    bool surfaceFoliageComplete = false;
    uint16_t featureDependencyReadyMask = 0;
    bool featureDependencyMaskInitialized = false;
    uint64_t touchFrame = 0;

    bool isRenderable() const noexcept {
        return hasSection && isFullyReady();
    }

    bool isFullyReady() const noexcept {
        return generated
            && postFeaturesComplete
            && (!hasSection || surfaceFoliageComplete);
    }
};

struct VoxelSectionBuffers {
    std::vector<uint32_t> ids;
    std::vector<uint32_t> colors;
    std::vector<uint8_t> skyLight;
    std::vector<uint8_t> blockLight;
};

struct VoxelWorldContext {
    int sectionSize = 16;
    int columnMinY = -64;
    int columnMaxYExclusive = 320;
    bool enabled = false;
    uint8_t defaultSkyLightLevel = static_cast<uint8_t>(15);
    std::unordered_map<VoxelColumnKey, VoxelColumn, VoxelColumnKeyHash> columns;
    std::unordered_map<VoxelSectionKey, VoxelSection, VoxelSectionKeyHash> sections;
    std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> dirtySections;
    std::unordered_map<VoxelSectionKey, uint64_t, VoxelSectionKeyHash> dirtyTickets;
    std::unordered_map<VoxelSectionKey, VoxelChunkLifecycleState, VoxelSectionKeyHash> chunkStates;
    std::unordered_map<VoxelColumnKey, VoxelChunkLifecycleState, VoxelColumnKeyHash> columnStates;
    std::unordered_set<VoxelColumnKey, VoxelColumnKeyHash> dirtyColumns;
    std::unordered_map<VoxelColumnKey, uint64_t, VoxelColumnKeyHash> dirtyColumnTickets;
    std::unordered_map<VoxelColumnKey, std::vector<PendingVoxelColumnWrite>, VoxelColumnKeyHash> pendingColumnWrites;
    std::unordered_map<int, std::vector<VoxelSectionBuffers>> bufferPools;
    bool columnFeatureWritesActive = false;
    VoxelColumnKey columnFeatureOwner{};
    uint64_t nextDirtyTicket = 1;

    void reset();
    VoxelChunkLifecycleState& ensureColumnState(const VoxelColumnKey& key);
    VoxelChunkLifecycleState* findColumnState(const VoxelColumnKey& key);
    const VoxelChunkLifecycleState* findColumnState(const VoxelColumnKey& key) const;
    void eraseColumnState(const VoxelColumnKey& key);
    void setColumnStage(const VoxelColumnKey& key,
                        VoxelChunkLifecycleStage stage,
                        uint64_t touchFrame = 0);
    VoxelChunkLifecycleState& ensureChunkState(const VoxelSectionKey& key);
    VoxelChunkLifecycleState* findChunkState(const VoxelSectionKey& key);
    const VoxelChunkLifecycleState* findChunkState(const VoxelSectionKey& key) const;
    void eraseChunkState(const VoxelSectionKey& key);
    void setChunkStage(const VoxelSectionKey& key,
                       VoxelChunkLifecycleStage stage,
                       uint64_t touchFrame = 0);
    uint32_t getBlockWorld(const glm::ivec3& worldPos) const;
    uint32_t getColorWorld(const glm::ivec3& worldPos) const;
    uint8_t getSkyLightWorld(const glm::ivec3& worldPos) const;
    uint8_t getBlockLightWorld(const glm::ivec3& worldPos) const;
    void setBlockWorld(const glm::ivec3& worldPos, uint32_t id, uint32_t color);
    void setBlock(const glm::ivec3& worldPos, uint32_t id, uint32_t color, bool markDirty = true);
    bool setBlockIfEmpty(const glm::ivec3& worldPos, uint32_t id, uint32_t color, bool markDirty = true);
    VoxelColumnKey columnKeyForWorldCell(const glm::ivec3& worldPos) const;
    void beginColumnFeatureWrites(const VoxelColumnKey& ownerKey);
    void endColumnFeatureWrites();
    bool isColumnReadyForFeatureWrite(const VoxelColumnKey& key) const;
    void enqueuePendingColumnWrite(const VoxelColumnKey& targetKey,
                                   const glm::ivec3& worldPos,
                                   uint32_t id,
                                   uint32_t color,
                                   bool onlyIfEmpty);
    size_t applyPendingColumnWrites(const VoxelColumnKey& key, bool markDirty = false);
    VoxelColumn* ensureColumnForWrite(const VoxelColumnKey& key);
    bool writeColumnBlock(VoxelColumn& column, int localX, int worldY, int localZ, uint32_t id, uint32_t color);
    int writeColumnRun(VoxelColumn& column, int localX, int localZ, int minY, int maxY, uint32_t id, uint32_t color);
    bool materializeSectionFromColumn(const VoxelSectionKey& key);
    uint64_t markSectionDirty(const VoxelSectionKey& key);
    void clearSectionDirty(const VoxelSectionKey& key);
    uint64_t getSectionDirtyTicket(const VoxelSectionKey& key) const;
    uint64_t markColumnDirty(const VoxelColumnKey& key);
    void clearColumnDirty(const VoxelColumnKey& key);
    uint64_t getColumnDirtyTicket(const VoxelColumnKey& key) const;
    void releaseSection(const VoxelSectionKey& key);
    void releaseColumn(const VoxelColumnKey& key);
};
