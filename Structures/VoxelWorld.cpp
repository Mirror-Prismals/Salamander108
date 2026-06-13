#pragma once

#include "Structures/VoxelWorld.h"
#include <algorithm>

namespace {
    int floorDivInt(int value, int divisor) {
        if (divisor <= 0) return 0;
        if (value >= 0) return value / divisor;
        return -(((-value) + divisor - 1) / divisor);
    }

    glm::ivec3 floorDivVec(const glm::ivec3& v, int divisor) {
        return glm::ivec3(
            floorDivInt(v.x, divisor),
            floorDivInt(v.y, divisor),
            floorDivInt(v.z, divisor)
        );
    }

    int sectionSizeForTier(int baseSize, int /*ignoredTier*/) {
        return baseSize > 0 ? baseSize : 1;
    }

    VoxelColumnKey columnKeyForWorld(const glm::ivec3& worldPos, int size) {
        return VoxelColumnKey{glm::ivec2(
            floorDivInt(worldPos.x, size),
            floorDivInt(worldPos.z, size)
        )};
    }

    VoxelSectionKey sectionKeyForWorld(const glm::ivec3& worldPos, int size) {
        return VoxelSectionKey{glm::ivec3(
            floorDivInt(worldPos.x, size),
            floorDivInt(worldPos.y, size),
            floorDivInt(worldPos.z, size)
        )};
    }

    int voxelIndex(const glm::ivec3& local, int size) {
        return local.x + local.y * size + local.z * size * size;
    }

    constexpr uint8_t kAllSectionFacesMask = 0x3f;

    uint8_t sectionBoundaryFaceMask(int x, int y, int z, int size) {
        uint8_t mask = 0;
        if (x == size - 1) mask |= static_cast<uint8_t>(1u << 0);
        if (x == 0) mask |= static_cast<uint8_t>(1u << 1);
        if (y == size - 1) mask |= static_cast<uint8_t>(1u << 2);
        if (y == 0) mask |= static_cast<uint8_t>(1u << 3);
        if (z == size - 1) mask |= static_cast<uint8_t>(1u << 4);
        if (z == 0) mask |= static_cast<uint8_t>(1u << 5);
        return mask;
    }

    void updateSectionVisibility(VoxelSection& section) {
        section.visibilityFromFace.fill(0);
        section.openFaceMask = 0;

        const int size = section.size;
        if (size <= 0) return;
        const int count = size * size * size;
        if (static_cast<int>(section.ids.size()) < count) return;

        if (section.nonAirCount <= 0) {
            section.visibilityFromFace.fill(kAllSectionFacesMask);
            section.openFaceMask = kAllSectionFacesMask;
            return;
        }
        if (section.nonAirCount >= count) return;

        std::vector<uint8_t> visited(static_cast<size_t>(count), 0);
        std::vector<int> stack;
        stack.reserve(static_cast<size_t>(count - section.nonAirCount));

        auto pushAir = [&](int idx) {
            if (idx < 0 || idx >= count) return;
            const size_t slot = static_cast<size_t>(idx);
            if (visited[slot] != 0 || section.ids[slot] != 0u) return;
            visited[slot] = 1;
            stack.push_back(idx);
        };

        for (int startIdx = 0; startIdx < count; ++startIdx) {
            const size_t startSlot = static_cast<size_t>(startIdx);
            if (visited[startSlot] != 0 || section.ids[startSlot] != 0u) continue;

            uint8_t componentFaceMask = 0;
            pushAir(startIdx);
            while (!stack.empty()) {
                const int idx = stack.back();
                stack.pop_back();
                const int z = idx / (size * size);
                const int rem = idx - z * size * size;
                const int y = rem / size;
                const int x = rem - y * size;

                componentFaceMask |= sectionBoundaryFaceMask(x, y, z, size);

                if (x + 1 < size) pushAir(idx + 1);
                if (x - 1 >= 0) pushAir(idx - 1);
                if (y + 1 < size) pushAir(idx + size);
                if (y - 1 >= 0) pushAir(idx - size);
                if (z + 1 < size) pushAir(idx + size * size);
                if (z - 1 >= 0) pushAir(idx - size * size);
            }

            if (componentFaceMask == 0) continue;
            section.openFaceMask |= componentFaceMask;
            for (int face = 0; face < 6; ++face) {
                if ((componentFaceMask & static_cast<uint8_t>(1u << face)) == 0) continue;
                section.visibilityFromFace[static_cast<size_t>(face)] |= componentFaceMask;
            }
        }
    }

    int columnHeight(const VoxelColumn& column) {
        return std::max(0, column.maxYExclusive - column.minY);
    }

    int columnVoxelIndex(const VoxelColumn& column, const glm::ivec3& local) {
        const int height = columnHeight(column);
        return local.x + local.y * column.chunkSize + local.z * column.chunkSize * height;
    }

    size_t columnVoxelCount(int size, int minY, int maxYExclusive) {
        const int height = std::max(0, maxYExclusive - minY);
        return static_cast<size_t>(size) * static_cast<size_t>(height) * static_cast<size_t>(size);
    }

    void initializeColumnStorage(VoxelWorldContext& world, VoxelColumn& column) {
        const size_t count = columnVoxelCount(column.chunkSize, column.minY, column.maxYExclusive);
        column.ids.assign(count, 0u);
        column.colors.assign(count, 0u);
        column.skyLight.clear();
        column.blockLight.clear();
        column.nonAirCount = 0;
        column.contentMinY = column.maxYExclusive;
        column.contentMaxY = column.minY - 1;
        (void)world;
    }

    void resetColumnContentBounds(VoxelColumn& column) {
        column.contentMinY = column.maxYExclusive;
        column.contentMaxY = column.minY - 1;
    }

    void includeColumnContentY(VoxelColumn& column, int worldY) {
        column.contentMinY = std::min(column.contentMinY, worldY);
        column.contentMaxY = std::max(column.contentMaxY, worldY);
    }

    bool columnProfileMatches(const VoxelColumn& column, int size, int minY, int maxYExclusive) {
        return column.chunkSize == size
            && column.minY == minY
            && column.maxYExclusive == maxYExclusive
            && column.ids.size() == columnVoxelCount(size, minY, maxYExclusive);
    }

    bool yInColumnProfile(const VoxelWorldContext& world, int y) {
        return y >= world.columnMinY && y < world.columnMaxYExclusive;
    }

    void releaseRenderSectionOnly(VoxelWorldContext& world, const VoxelSectionKey& key);

}

void VoxelWorldContext::reset() {
    columns.clear();
    sections.clear();
    dirtySections.clear();
    dirtyTickets.clear();
    chunkStates.clear();
    columnStates.clear();
    dirtyColumns.clear();
    dirtyColumnTickets.clear();
    pendingColumnWrites.clear();
    columnFeatureWritesActive = false;
    columnFeatureOwner = VoxelColumnKey{};
    nextDirtyTicket = 1;
}

VoxelChunkLifecycleState& VoxelWorldContext::ensureColumnState(const VoxelColumnKey& key) {
    return columnStates[key];
}

VoxelChunkLifecycleState* VoxelWorldContext::findColumnState(const VoxelColumnKey& key) {
    auto it = columnStates.find(key);
    if (it == columnStates.end()) return nullptr;
    return &it->second;
}

const VoxelChunkLifecycleState* VoxelWorldContext::findColumnState(const VoxelColumnKey& key) const {
    auto it = columnStates.find(key);
    if (it == columnStates.end()) return nullptr;
    return &it->second;
}

void VoxelWorldContext::eraseColumnState(const VoxelColumnKey& key) {
    columnStates.erase(key);
}

void VoxelWorldContext::setColumnStage(const VoxelColumnKey& key,
                                       VoxelChunkLifecycleStage stage,
                                       uint64_t touchFrame) {
    VoxelChunkLifecycleState& state = ensureColumnState(key);
    state.stage = stage;
    state.touchFrame = touchFrame;
}

VoxelChunkLifecycleState& VoxelWorldContext::ensureChunkState(const VoxelSectionKey& key) {
    return chunkStates[key];
}

VoxelChunkLifecycleState* VoxelWorldContext::findChunkState(const VoxelSectionKey& key) {
    auto it = chunkStates.find(key);
    if (it == chunkStates.end()) return nullptr;
    return &it->second;
}

const VoxelChunkLifecycleState* VoxelWorldContext::findChunkState(const VoxelSectionKey& key) const {
    auto it = chunkStates.find(key);
    if (it == chunkStates.end()) return nullptr;
    return &it->second;
}

void VoxelWorldContext::eraseChunkState(const VoxelSectionKey& key) {
    chunkStates.erase(key);
}

void VoxelWorldContext::setChunkStage(const VoxelSectionKey& key,
                                      VoxelChunkLifecycleStage stage,
                                      uint64_t touchFrame) {
    VoxelChunkLifecycleState& state = ensureChunkState(key);
    state.stage = stage;
    state.touchFrame = touchFrame;
}

namespace {
    VoxelSectionBuffers acquireBuffers(VoxelWorldContext& world, int size) {
        VoxelSectionBuffers buffers;
        auto it = world.bufferPools.find(size);
        if (it != world.bufferPools.end() && !it->second.empty()) {
            buffers = std::move(it->second.back());
            it->second.pop_back();
        }
        const size_t count = static_cast<size_t>(size * size * size);
        if (buffers.ids.size() != count) {
            buffers.ids.assign(count, 0);
        } else {
            std::fill(buffers.ids.begin(), buffers.ids.end(), 0);
        }
        if (buffers.colors.size() != count) {
            buffers.colors.assign(count, 0);
        } else {
            std::fill(buffers.colors.begin(), buffers.colors.end(), 0);
        }
        const uint8_t defaultSky = world.defaultSkyLightLevel;
        if (buffers.skyLight.size() != count) {
            buffers.skyLight.assign(count, defaultSky);
        } else {
            std::fill(buffers.skyLight.begin(), buffers.skyLight.end(), defaultSky);
        }
        if (buffers.blockLight.size() != count) {
            buffers.blockLight.assign(count, static_cast<uint8_t>(0));
        } else {
            std::fill(buffers.blockLight.begin(), buffers.blockLight.end(), static_cast<uint8_t>(0));
        }
        return buffers;
    }

    void releaseBuffers(VoxelWorldContext& world, int size, VoxelSectionBuffers&& buffers) {
        world.bufferPools[size].push_back(std::move(buffers));
    }

    void releaseRenderSectionOnly(VoxelWorldContext& world, const VoxelSectionKey& key) {
        auto it = world.sections.find(key);
        if (it == world.sections.end()) return;
        VoxelSectionBuffers buffers;
        buffers.ids = std::move(it->second.ids);
        buffers.colors = std::move(it->second.colors);
        buffers.skyLight = std::move(it->second.skyLight);
        buffers.blockLight = std::move(it->second.blockLight);
        releaseBuffers(world, it->second.size, std::move(buffers));
        world.sections.erase(it);
        world.clearSectionDirty(key);
        auto chunkIt = world.chunkStates.find(key);
        if (chunkIt != world.chunkStates.end()) {
            chunkIt->second.hasSection = false;
        }
    }
}

uint32_t VoxelWorldContext::getBlockWorld(const glm::ivec3& worldPos) const {
    int size = sectionSizeForTier(sectionSize, 0);
    if (!yInColumnProfile(*this, worldPos.y)) return 0;
    const VoxelColumnKey columnKey = columnKeyForWorld(worldPos, size);
    auto columnIt = columns.find(columnKey);
    if (columnIt == columns.end()) return 0;
    const VoxelColumn& column = columnIt->second;
    if (!columnProfileMatches(column, size, column.minY, column.maxYExclusive)) return 0;
    const glm::ivec3 local(
        worldPos.x - columnKey.coord.x * size,
        worldPos.y - column.minY,
        worldPos.z - columnKey.coord.y * size
    );
    int idx = columnVoxelIndex(column, local);
    if (idx < 0 || idx >= static_cast<int>(column.ids.size())) return 0;
    return column.ids[static_cast<size_t>(idx)];
}

uint32_t VoxelWorldContext::getColorWorld(const glm::ivec3& worldPos) const {
    int size = sectionSizeForTier(sectionSize, 0);
    if (!yInColumnProfile(*this, worldPos.y)) return 0;
    const VoxelColumnKey columnKey = columnKeyForWorld(worldPos, size);
    auto columnIt = columns.find(columnKey);
    if (columnIt == columns.end()) return 0;
    const VoxelColumn& column = columnIt->second;
    if (!columnProfileMatches(column, size, column.minY, column.maxYExclusive)) return 0;
    const glm::ivec3 local(
        worldPos.x - columnKey.coord.x * size,
        worldPos.y - column.minY,
        worldPos.z - columnKey.coord.y * size
    );
    int idx = columnVoxelIndex(column, local);
    if (idx < 0 || idx >= static_cast<int>(column.colors.size())) return 0;
    return column.colors[static_cast<size_t>(idx)];
}

uint8_t VoxelWorldContext::getSkyLightWorld(const glm::ivec3& worldPos) const {
    int size = sectionSizeForTier(sectionSize, 0);
    if (!yInColumnProfile(*this, worldPos.y)) return defaultSkyLightLevel;
    const VoxelColumnKey columnKey = columnKeyForWorld(worldPos, size);
    auto columnIt = columns.find(columnKey);
    if (columnIt == columns.end()) return defaultSkyLightLevel;
    const VoxelColumn& column = columnIt->second;
    if (!columnProfileMatches(column, size, column.minY, column.maxYExclusive)) return defaultSkyLightLevel;
    const glm::ivec3 local(
        worldPos.x - columnKey.coord.x * size,
        worldPos.y - column.minY,
        worldPos.z - columnKey.coord.y * size
    );
    int idx = columnVoxelIndex(column, local);
    if (idx < 0 || idx >= static_cast<int>(column.skyLight.size())) return defaultSkyLightLevel;
    return column.skyLight[static_cast<size_t>(idx)];
}

uint8_t VoxelWorldContext::getBlockLightWorld(const glm::ivec3& worldPos) const {
    int size = sectionSizeForTier(sectionSize, 0);
    if (!yInColumnProfile(*this, worldPos.y)) return static_cast<uint8_t>(0);
    const VoxelColumnKey columnKey = columnKeyForWorld(worldPos, size);
    auto columnIt = columns.find(columnKey);
    if (columnIt == columns.end()) return static_cast<uint8_t>(0);
    const VoxelColumn& column = columnIt->second;
    if (!columnProfileMatches(column, size, column.minY, column.maxYExclusive)) return static_cast<uint8_t>(0);
    const glm::ivec3 local(
        worldPos.x - columnKey.coord.x * size,
        worldPos.y - column.minY,
        worldPos.z - columnKey.coord.y * size
    );
    int idx = columnVoxelIndex(column, local);
    if (idx < 0 || idx >= static_cast<int>(column.blockLight.size())) return static_cast<uint8_t>(0);
    return column.blockLight[static_cast<size_t>(idx)];
}

void VoxelWorldContext::setBlockWorld(const glm::ivec3& worldPos, uint32_t id, uint32_t color) {
    setBlock(worldPos, id, color, true);
}

VoxelColumn* VoxelWorldContext::ensureColumnForWrite(const VoxelColumnKey& key) {
    const int size = sectionSizeForTier(sectionSize, 0);
    auto existingColumnIt = columns.find(key);
    if (existingColumnIt != columns.end()
        && !columnProfileMatches(existingColumnIt->second, size, columnMinY, columnMaxYExclusive)) {
        releaseColumn(key);
        existingColumnIt = columns.end();
    }

    if (existingColumnIt == columns.end()) {
        VoxelColumn column;
        column.key = key;
        column.chunkSize = size;
        column.minY = columnMinY;
        column.maxYExclusive = columnMaxYExclusive;
        initializeColumnStorage(*this, column);
        auto [insertedColumnIt, _] = columns.emplace(key, std::move(column));
        existingColumnIt = insertedColumnIt;
        ensureColumnState(key).hasSection = true;
    }
    return &existingColumnIt->second;
}

bool VoxelWorldContext::writeColumnBlock(VoxelColumn& column,
                                         int localX,
                                         int worldY,
                                         int localZ,
                                         uint32_t id,
                                         uint32_t color) {
    if (localX < 0 || localX >= column.chunkSize) return false;
    if (localZ < 0 || localZ >= column.chunkSize) return false;
    if (worldY < column.minY || worldY >= column.maxYExclusive) return false;
    const int height = columnHeight(column);
    const int idx = localX
        + (worldY - column.minY) * column.chunkSize
        + localZ * column.chunkSize * height;
    if (idx < 0 || idx >= static_cast<int>(column.ids.size())) return false;

    const size_t slot = static_cast<size_t>(idx);
    const uint32_t oldId = column.ids[slot];
    const uint32_t oldColor = column.colors[slot];
    const uint32_t storedColor = (id == 0u) ? 0u : color;
    if (oldId == id && oldColor == storedColor) return false;

    column.ids[slot] = id;
    column.colors[slot] = storedColor;
    if (oldId == 0u && id != 0u) {
        column.nonAirCount += 1;
        includeColumnContentY(column, worldY);
    } else if (oldId != 0u && id == 0u) {
        column.nonAirCount = std::max(0, column.nonAirCount - 1);
        if (column.nonAirCount <= 0) {
            resetColumnContentBounds(column);
        }
    } else if (id != 0u) {
        includeColumnContentY(column, worldY);
    }
    return true;
}

int VoxelWorldContext::writeColumnRun(VoxelColumn& column,
                                      int localX,
                                      int localZ,
                                      int minY,
                                      int maxY,
                                      uint32_t id,
                                      uint32_t color) {
    if (localX < 0 || localX >= column.chunkSize) return 0;
    if (localZ < 0 || localZ >= column.chunkSize) return 0;
    int writeMinY = std::max(minY, column.minY);
    int writeMaxY = std::min(maxY, column.maxYExclusive - 1);
    if (writeMinY > writeMaxY) return 0;

    const uint32_t storedColor = (id == 0u) ? 0u : color;
    int changed = 0;
    const int height = columnHeight(column);
    int idx = localX
        + (writeMinY - column.minY) * column.chunkSize
        + localZ * column.chunkSize * height;
    for (int y = writeMinY; y <= writeMaxY; ++y) {
        if (idx < 0 || idx >= static_cast<int>(column.ids.size())) {
            idx += column.chunkSize;
            continue;
        }
        const size_t slot = static_cast<size_t>(idx);
        const uint32_t oldId = column.ids[slot];
        const uint32_t oldColor = column.colors[slot];
        if (oldId == id && oldColor == storedColor) {
            idx += column.chunkSize;
            continue;
        }

        column.ids[slot] = id;
        column.colors[slot] = storedColor;
        if (oldId == 0u && id != 0u) {
            column.nonAirCount += 1;
            includeColumnContentY(column, y);
        } else if (oldId != 0u && id == 0u) {
            column.nonAirCount = std::max(0, column.nonAirCount - 1);
            if (column.nonAirCount <= 0) {
                resetColumnContentBounds(column);
            }
        } else if (id != 0u) {
            includeColumnContentY(column, y);
        }
        changed += 1;
        idx += column.chunkSize;
    }
    return changed;
}

void VoxelWorldContext::setBlock(const glm::ivec3& worldPos, uint32_t id, uint32_t color, bool markDirty) {
    int size = sectionSizeForTier(sectionSize, 0);
    if (!yInColumnProfile(*this, worldPos.y)) return;

    const VoxelColumnKey columnKey = columnKeyForWorld(worldPos, size);
    bool columnFeatureCrossWrite = false;
    if (columnFeatureWritesActive && !(columnKey == columnFeatureOwner)) {
        if (!isColumnReadyForFeatureWrite(columnKey)) {
            if (id != 0u) {
                enqueuePendingColumnWrite(columnKey, worldPos, id, color, false);
            }
            return;
        }
        columnFeatureCrossWrite = true;
        markDirty = true;
    }

    const VoxelSectionKey sectionKey = sectionKeyForWorld(worldPos, size);
    const int sectionY = sectionKey.coord.y;
    auto existingColumnIt = columns.find(columnKey);
    if (existingColumnIt != columns.end()
        && !columnProfileMatches(existingColumnIt->second, size, columnMinY, columnMaxYExclusive)) {
        releaseColumn(columnKey);
    }

    const glm::ivec3 local(
        worldPos.x - columnKey.coord.x * size,
        worldPos.y - columnMinY,
        worldPos.z - columnKey.coord.y * size
    );
    const glm::ivec3 sectionLocal(
        worldPos.x - sectionKey.coord.x * size,
        worldPos.y - sectionY * size,
        worldPos.z - sectionKey.coord.z * size
    );
    const int sectionIdx = voxelIndex(sectionLocal, size);
    if (sectionIdx < 0 || sectionIdx >= size * size * size) return;

    auto columnIt = columns.find(columnKey);
    if (columnIt == columns.end()) {
        if (id == 0) return;
        VoxelColumn column;
        column.key = columnKey;
        column.chunkSize = size;
        column.minY = columnMinY;
        column.maxYExclusive = columnMaxYExclusive;
        initializeColumnStorage(*this, column);
        auto [insertedColumnIt, _] = columns.emplace(columnKey, std::move(column));
        columnIt = insertedColumnIt;
        ensureColumnState(columnKey).hasSection = true;
    }

    VoxelColumn& column = columnIt->second;
    const int idx = columnVoxelIndex(column, local);
    if (idx < 0 || idx >= static_cast<int>(column.ids.size())) return;
    uint32_t oldId = column.ids[static_cast<size_t>(idx)];
    uint32_t oldColor = column.colors[static_cast<size_t>(idx)];
    if (oldId == id && oldColor == color) return;
    column.ids[static_cast<size_t>(idx)] = id;
    column.colors[static_cast<size_t>(idx)] = (id == 0) ? 0 : color;
    if (oldId == 0 && id != 0) {
        column.nonAirCount += 1;
        includeColumnContentY(column, worldPos.y);
    }
    if (oldId != 0 && id == 0) {
        column.nonAirCount -= 1;
        if (column.nonAirCount <= 0) {
            resetColumnContentBounds(column);
        }
    } else if (id != 0) {
        includeColumnContentY(column, worldPos.y);
    }

    auto sectionIt = sections.find(sectionKey);
    if (sectionIt == sections.end()) {
        if (markDirty && !columnFeatureCrossWrite) {
            materializeSectionFromColumn(sectionKey);
            sectionIt = sections.find(sectionKey);
        }
    }
    if (sectionIt != sections.end() && !columnFeatureCrossWrite) {
        VoxelSection& section = sectionIt->second;
        const uint32_t oldSectionId = section.ids[static_cast<size_t>(sectionIdx)];
        section.ids[static_cast<size_t>(sectionIdx)] = id;
        section.colors[static_cast<size_t>(sectionIdx)] = (id == 0) ? 0 : color;
        if (oldSectionId == 0 && id != 0) section.nonAirCount += 1;
        if (oldSectionId != 0 && id == 0) section.nonAirCount -= 1;
        if (section.nonAirCount <= 0) {
            releaseRenderSectionOnly(*this, sectionKey);
        } else {
            updateSectionVisibility(section);
            if (markDirty) {
                section.editVersion += 1;
                section.dirty = true;
                markSectionDirty(sectionKey);
            }
        }
    }

    if (markDirty) {
        column.editVersion += 1;
        markColumnDirty(columnKey);
        auto markExistingNeighborColumnDirty = [&](const VoxelColumnKey& neighborKey) {
            auto neighborIt = columns.find(neighborKey);
            if (neighborIt == columns.end() || neighborIt->second.nonAirCount <= 0) return;
            markColumnDirty(neighborKey);
        };
        if (local.x == 0) {
            markExistingNeighborColumnDirty(VoxelColumnKey{columnKey.coord + glm::ivec2(-1, 0)});
        } else if (local.x == size - 1) {
            markExistingNeighborColumnDirty(VoxelColumnKey{columnKey.coord + glm::ivec2(1, 0)});
        }
        if (local.z == 0) {
            markExistingNeighborColumnDirty(VoxelColumnKey{columnKey.coord + glm::ivec2(0, -1)});
        } else if (local.z == size - 1) {
            markExistingNeighborColumnDirty(VoxelColumnKey{columnKey.coord + glm::ivec2(0, 1)});
        }
    }

    if (column.nonAirCount <= 0) {
        columns.erase(columnKey);
        auto columnStateIt = columnStates.find(columnKey);
        if (columnStateIt != columnStates.end()) {
            columnStateIt->second.hasSection = false;
        }
    }
}

bool VoxelWorldContext::setBlockIfEmpty(const glm::ivec3& worldPos,
                                        uint32_t id,
                                        uint32_t color,
                                        bool markDirty) {
    int size = sectionSizeForTier(sectionSize, 0);
    if (!yInColumnProfile(*this, worldPos.y)) return false;

    const VoxelColumnKey columnKey = columnKeyForWorld(worldPos, size);
    if (columnFeatureWritesActive && !(columnKey == columnFeatureOwner)) {
        if (!isColumnReadyForFeatureWrite(columnKey)) {
            if (id != 0u) {
                enqueuePendingColumnWrite(columnKey, worldPos, id, color, true);
                return true;
            }
            return false;
        }
        markDirty = true;
    }

    if (getBlockWorld(worldPos) != 0u) return false;
    setBlock(worldPos, id, color, markDirty);
    return true;
}

VoxelColumnKey VoxelWorldContext::columnKeyForWorldCell(const glm::ivec3& worldPos) const {
    return columnKeyForWorld(worldPos, sectionSizeForTier(sectionSize, 0));
}

void VoxelWorldContext::beginColumnFeatureWrites(const VoxelColumnKey& ownerKey) {
    columnFeatureWritesActive = true;
    columnFeatureOwner = ownerKey;
}

void VoxelWorldContext::endColumnFeatureWrites() {
    columnFeatureWritesActive = false;
    columnFeatureOwner = VoxelColumnKey{};
}

bool VoxelWorldContext::isColumnReadyForFeatureWrite(const VoxelColumnKey& key) const {
    const auto stateIt = columnStates.find(key);
    if (stateIt == columnStates.end()) return false;
    const VoxelChunkLifecycleState& state = stateIt->second;
    return state.generated && state.postFeaturesComplete && columns.count(key) > 0;
}

void VoxelWorldContext::enqueuePendingColumnWrite(const VoxelColumnKey& targetKey,
                                                 const glm::ivec3& worldPos,
                                                 uint32_t id,
                                                 uint32_t color,
                                                 bool onlyIfEmpty) {
    if (id == 0u) return;
    pendingColumnWrites[targetKey].push_back(PendingVoxelColumnWrite{
        worldPos,
        id,
        color,
        onlyIfEmpty
    });
}

size_t VoxelWorldContext::applyPendingColumnWrites(const VoxelColumnKey& key, bool markDirty) {
    auto pendingIt = pendingColumnWrites.find(key);
    if (pendingIt == pendingColumnWrites.end()) return 0u;

    std::vector<PendingVoxelColumnWrite> writes;
    writes.swap(pendingIt->second);
    pendingColumnWrites.erase(pendingIt);

    const bool previousActive = columnFeatureWritesActive;
    const VoxelColumnKey previousOwner = columnFeatureOwner;
    columnFeatureWritesActive = false;

    size_t applied = 0u;
    for (const PendingVoxelColumnWrite& write : writes) {
        if (write.onlyIfEmpty && getBlockWorld(write.worldPos) != 0u) {
            continue;
        }
        const uint32_t before = getBlockWorld(write.worldPos);
        const uint32_t beforeColor = getColorWorld(write.worldPos);
        setBlock(write.worldPos, write.id, write.color, markDirty);
        if (before != write.id || beforeColor != write.color) {
            applied += 1u;
        }
    }

    columnFeatureWritesActive = previousActive;
    columnFeatureOwner = previousOwner;
    return applied;
}

bool VoxelWorldContext::materializeSectionFromColumn(const VoxelSectionKey& key) {
    const int size = sectionSizeForTier(sectionSize, 0);
    const VoxelColumnKey columnKey{glm::ivec2(key.coord.x, key.coord.z)};
    auto columnIt = columns.find(columnKey);
    if (columnIt == columns.end()) {
        releaseRenderSectionOnly(*this, key);
        return false;
    }

    VoxelColumn& column = columnIt->second;
    if (!columnProfileMatches(column, size, columnMinY, columnMaxYExclusive)) {
        releaseColumn(columnKey);
        return false;
    }

    const int sectionMinY = key.coord.y * size;
    const int sectionMaxYExclusive = sectionMinY + size;
    const int copyMinY = std::max(sectionMinY, column.minY);
    const int copyMaxYExclusive = std::min(sectionMaxYExclusive, column.maxYExclusive);
    if (copyMinY >= copyMaxYExclusive) {
        releaseRenderSectionOnly(*this, key);
        return false;
    }

    auto sectionIt = sections.find(key);
    if (sectionIt == sections.end()) {
        VoxelSection section;
        section.size = size;
        section.coord = key.coord;
        VoxelSectionBuffers buffers = acquireBuffers(*this, size);
        section.ids = std::move(buffers.ids);
        section.colors = std::move(buffers.colors);
        section.skyLight = std::move(buffers.skyLight);
        section.blockLight = std::move(buffers.blockLight);
        auto [insertedIt, _] = sections.emplace(key, std::move(section));
        sectionIt = insertedIt;
    } else {
        VoxelSection& section = sectionIt->second;
        const size_t count = static_cast<size_t>(size) * static_cast<size_t>(size) * static_cast<size_t>(size);
        section.size = size;
        section.coord = key.coord;
        section.ids.assign(count, 0u);
        section.colors.assign(count, 0u);
        section.skyLight.assign(count, defaultSkyLightLevel);
        section.blockLight.assign(count, static_cast<uint8_t>(0));
        section.nonAirCount = 0;
    }

    VoxelSection& section = sectionIt->second;
    const int columnHeightValue = columnHeight(column);
    for (int z = 0; z < size; ++z) {
        for (int y = copyMinY; y < copyMaxYExclusive; ++y) {
            const int sectionLocalY = y - sectionMinY;
            const int columnRowIdx = (y - column.minY) * column.chunkSize
                + z * column.chunkSize * columnHeightValue;
            const int sectionRowIdx = sectionLocalY * size
                + z * size * size;
            if (columnRowIdx < 0
                || sectionRowIdx < 0
                || columnRowIdx + size > static_cast<int>(column.ids.size())
                || sectionRowIdx + size > static_cast<int>(section.ids.size())) {
                continue;
            }
            for (int x = 0; x < size; ++x) {
                const int columnIdx = columnRowIdx + x;
                const int sectionIdx = sectionRowIdx + x;
                const uint32_t id = column.ids[static_cast<size_t>(columnIdx)];
                section.ids[static_cast<size_t>(sectionIdx)] = id;
                section.colors[static_cast<size_t>(sectionIdx)] =
                    id == 0u ? 0u : column.colors[static_cast<size_t>(columnIdx)];
                if (static_cast<size_t>(columnIdx) < column.skyLight.size()
                    && static_cast<size_t>(sectionIdx) < section.skyLight.size()) {
                    section.skyLight[static_cast<size_t>(sectionIdx)] =
                        column.skyLight[static_cast<size_t>(columnIdx)];
                }
                if (static_cast<size_t>(columnIdx) < column.blockLight.size()
                    && static_cast<size_t>(sectionIdx) < section.blockLight.size()) {
                    section.blockLight[static_cast<size_t>(sectionIdx)] =
                        column.blockLight[static_cast<size_t>(columnIdx)];
                }
                if (id != 0u) section.nonAirCount += 1;
            }
        }
    }

    if (section.nonAirCount <= 0) {
        releaseRenderSectionOnly(*this, key);
        return false;
    }

    updateSectionVisibility(section);
    ensureChunkState(key).hasSection = true;
    return true;
}

uint64_t VoxelWorldContext::markSectionDirty(const VoxelSectionKey& key) {
    dirtySections.insert(key);
    uint64_t ticket = nextDirtyTicket++;
    if (ticket == 0) {
        ticket = nextDirtyTicket++;
    }
    dirtyTickets[key] = ticket;
    return ticket;
}

void VoxelWorldContext::clearSectionDirty(const VoxelSectionKey& key) {
    dirtySections.erase(key);
    dirtyTickets.erase(key);
}

uint64_t VoxelWorldContext::getSectionDirtyTicket(const VoxelSectionKey& key) const {
    auto it = dirtyTickets.find(key);
    if (it == dirtyTickets.end()) return 0;
    return it->second;
}

uint64_t VoxelWorldContext::markColumnDirty(const VoxelColumnKey& key) {
    dirtyColumns.insert(key);
    uint64_t ticket = nextDirtyTicket++;
    if (ticket == 0) {
        ticket = nextDirtyTicket++;
    }
    dirtyColumnTickets[key] = ticket;
    return ticket;
}

void VoxelWorldContext::clearColumnDirty(const VoxelColumnKey& key) {
    dirtyColumns.erase(key);
    dirtyColumnTickets.erase(key);
}

uint64_t VoxelWorldContext::getColumnDirtyTicket(const VoxelColumnKey& key) const {
    auto it = dirtyColumnTickets.find(key);
    if (it == dirtyColumnTickets.end()) return 0;
    return it->second;
}

void VoxelWorldContext::releaseSection(const VoxelSectionKey& key) {
    releaseRenderSectionOnly(*this, key);

    const int size = sectionSizeForTier(sectionSize, 0);
    const VoxelColumnKey columnKey{glm::ivec2(key.coord.x, key.coord.z)};
    auto columnIt = columns.find(columnKey);
    if (columnIt != columns.end()) {
        VoxelColumn& column = columnIt->second;
        bool changedColumn = false;
        const int sectionMinY = key.coord.y * size;
        const int sectionMaxYExclusive = sectionMinY + size;
        const int clearMinY = std::max(sectionMinY, column.minY);
        const int clearMaxYExclusive = std::min(sectionMaxYExclusive, column.maxYExclusive);
        for (int z = 0; z < size; ++z) {
            for (int y = clearMinY; y < clearMaxYExclusive; ++y) {
                for (int x = 0; x < size; ++x) {
                    const glm::ivec3 local(x, y - column.minY, z);
                    const int idx = columnVoxelIndex(column, local);
                    if (idx < 0 || idx >= static_cast<int>(column.ids.size())) continue;
                    uint32_t& oldId = column.ids[static_cast<size_t>(idx)];
                    if (oldId != 0u) {
                        oldId = 0u;
                        column.colors[static_cast<size_t>(idx)] = 0u;
                        column.nonAirCount = std::max(0, column.nonAirCount - 1);
                        if (column.nonAirCount <= 0) {
                            resetColumnContentBounds(column);
                        }
                        changedColumn = true;
                    }
                }
            }
        }
        if (changedColumn) {
            column.editVersion += 1;
            markColumnDirty(columnKey);
        }
        if (column.nonAirCount <= 0) {
            columns.erase(columnIt);
            auto columnStateIt = columnStates.find(columnKey);
            if (columnStateIt != columnStates.end()) {
                columnStateIt->second.hasSection = false;
            }
        }
    }
    (void)size;
}

void VoxelWorldContext::releaseColumn(const VoxelColumnKey& key) {
    const int size = sectionSizeForTier(sectionSize, 0);
    auto columnIt = columns.find(key);
    if (columnIt != columns.end()) {
        std::vector<VoxelSectionKey> slabKeys;
        const int minSectionY = floorDivInt(columnIt->second.minY, size);
        const int maxSectionY = floorDivInt(columnIt->second.maxYExclusive - 1, size);
        slabKeys.reserve(static_cast<size_t>(std::max(0, maxSectionY - minSectionY + 1)));
        for (int sectionY = minSectionY; sectionY <= maxSectionY; ++sectionY) {
            const VoxelSectionKey slabKey{glm::ivec3(key.coord.x, sectionY, key.coord.y)};
            if (sections.count(slabKey) > 0 || chunkStates.count(slabKey) > 0) {
                slabKeys.push_back(slabKey);
            }
        }
        for (const VoxelSectionKey& slabKey : slabKeys) {
            releaseRenderSectionOnly(*this, slabKey);
            chunkStates.erase(slabKey);
        }
        columns.erase(columnIt);
    } else {
        std::vector<VoxelSectionKey> renderKeys;
        for (const auto& [sectionKey, _] : sections) {
            if (sectionKey.coord.x == key.coord.x && sectionKey.coord.z == key.coord.y) {
                renderKeys.push_back(sectionKey);
            }
        }
        for (const VoxelSectionKey& sectionKey : renderKeys) {
            releaseRenderSectionOnly(*this, sectionKey);
            chunkStates.erase(sectionKey);
        }
    }
    columnStates.erase(key);
    clearColumnDirty(key);
    (void)size;
}
