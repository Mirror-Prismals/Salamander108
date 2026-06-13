#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VoxelMeshInitSystemLogic {
    int FloorDivInt(int value, int divisor);
    int SectionSizeForSection(const VoxelWorldContext& voxelWorld);
}
namespace RenderInitSystemLogic {
    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback);
    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback);
    float getRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback);
}
namespace VoxelMeshUploadSystemLogic {
    std::vector<VoxelMeshingPrototypeTraits> BuildVoxelMeshingPrototypeTraits(const BaseSystem& baseSystem,
                                                                              const std::vector<Entity>& prototypes);
    bool PrepareVoxelSectionMesh(const VoxelMeshingSnapshot& snapshot,
                                 const std::vector<VoxelMeshingPrototypeTraits>& prototypeTraits,
                                 PreparedVoxelSectionMesh& outMesh);
}

namespace VoxelMeshingSystemLogic {
    namespace {
        struct MeshingJob {
            VoxelMeshingSnapshot snapshot;
            std::shared_ptr<const std::vector<VoxelMeshingPrototypeTraits>> prototypeTraits;
            uint64_t epoch = 0;
        };

        struct MeshingResult {
            VoxelSectionKey key{};
            VoxelColumnKey columnKey{};
            bool columnMesh = false;
            PreparedVoxelSectionMesh mesh;
            uint64_t epoch = 0;
        };

        struct ColumnHaloWaiter {
            VoxelColumnKey key{};
            uint64_t dirtyTicket = 0;
        };

        struct MeshingRuntime {
            std::mutex mutex;
            std::condition_variable cv;
            std::vector<std::thread> workers;
            std::deque<MeshingJob> pendingJobs;
            std::deque<MeshingResult> completedJobs;
            std::unordered_map<VoxelSectionKey, uint64_t, VoxelSectionKeyHash> requestedTickets;
            std::unordered_map<VoxelColumnKey, uint64_t, VoxelColumnKeyHash> requestedColumnTickets;
            std::shared_ptr<const std::vector<VoxelMeshingPrototypeTraits>> prototypeTraits;
            size_t prototypeCount = 0;
            uint64_t epoch = 1;
            size_t activeJobs = 0;
            bool stopRequested = false;
            bool workersStarted = false;
        };

        struct MeshingPerfStats {
            size_t pendingJobs = 0;
            size_t activeJobs = 0;
            size_t requestedJobs = 0;
            size_t completedBuffered = 0;
            size_t dirtySections = 0;
            size_t candidates = 0;
            size_t missingSections = 0;
            size_t queued = 0;
            size_t snapshots = 0;
            size_t completedPopped = 0;
            size_t completedAccepted = 0;
            size_t completedDropped = 0;
            size_t outstandingBlocked = 0;
            size_t neighborWaitBlocked = 0;
            size_t neighborWaitWoken = 0;
            size_t neighborWaitResident = 0;
            size_t snapshotCap = 0;
            int schedulerPressure = 0;
            size_t preparedBacklog = 0;
            size_t uploadBacklog = 0;
            float frameSnapshotMs = 0.0f;
            float lastSnapshotMs = 0.0f;
            int lastSnapshotY = 0;
            int lastSnapshotNonAir = 0;
            uint64_t totalQueued = 0;
            uint64_t totalCompleted = 0;
            uint64_t totalDropped = 0;
            uint64_t totalSnapshots = 0;
            uint64_t totalOutstandingBlocked = 0;
            uint64_t totalNeighborWaitBlocked = 0;
            uint64_t totalNeighborWaitWoken = 0;
        };

        MeshingRuntime g_meshingRuntime;
        MeshingPerfStats g_meshingPerfStats;
        std::unordered_map<VoxelColumnKey, std::vector<ColumnHaloWaiter>, VoxelColumnKeyHash>
            g_columnHaloWaitersByNeighbor;
        std::unordered_map<VoxelColumnKey, uint64_t, VoxelColumnKeyHash> g_columnHaloWaitTickets;
        std::unordered_map<VoxelColumnKey, std::vector<VoxelColumnKey>, VoxelColumnKeyHash>
            g_columnHaloWaitBlockersByColumn;

        constexpr size_t kMeshingWorkerCount = 2;
        constexpr size_t kMaxOutstandingJobs = 4;
        constexpr size_t kMaxNewSnapshotsPerFrame = 2;

        int sectionVoxelIndex(int x, int y, int z, int size) {
            return x + y * size + z * size * size;
        }

        int columnHeight(const VoxelColumn& column) {
            return std::max(0, column.maxYExclusive - column.minY);
        }

        int columnVoxelIndex(const VoxelColumn& column, int localX, int worldY, int localZ) {
            const int height = columnHeight(column);
            return localX
                + (worldY - column.minY) * column.chunkSize
                + localZ * column.chunkSize * height;
        }

        int paddedSnapshotIndex(int x, int y, int z, const glm::ivec3& volumeSize) {
            const int paddedX = volumeSize.x + 2;
            const int paddedY = volumeSize.y + 2;
            return (x + 1)
                + (y + 1) * paddedX
                + (z + 1) * paddedX * paddedY;
        }

        float keyDist2ToCamera(const VoxelWorldContext& voxelWorld,
                               const VoxelSectionKey& key,
                               const glm::vec3& cameraPos) {
            int size = std::max(1, VoxelMeshInitSystemLogic::SectionSizeForSection(voxelWorld));
            const float span = static_cast<float>(size);
            const glm::vec3 center(
                (static_cast<float>(key.coord.x) + 0.5f) * span,
                (static_cast<float>(key.coord.y) + 0.5f) * span,
                (static_cast<float>(key.coord.z) + 0.5f) * span
            );
            const glm::vec3 delta = center - cameraPos;
            return glm::dot(delta, delta);
        }

        float columnDist2ToCamera(const VoxelWorldContext& voxelWorld,
                                  const VoxelColumnKey& key,
                                  const VoxelColumn& column,
                                  const glm::vec3& cameraPos) {
            int size = std::max(1, VoxelMeshInitSystemLogic::SectionSizeForSection(voxelWorld));
            const glm::vec3 center(
                (static_cast<float>(key.coord.x) + 0.5f) * static_cast<float>(size),
                0.5f * static_cast<float>(column.minY + column.maxYExclusive),
                (static_cast<float>(key.coord.y) + 0.5f) * static_cast<float>(size)
            );
            const glm::vec3 delta = center - cameraPos;
            return glm::dot(delta, delta);
        }

        void collectTouchedSections(const VoxelWorldContext& voxelWorld,
                                    const glm::ivec3& worldCell,
                                    std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash>& out) {
            const int sectionSize = std::max(1, VoxelMeshInitSystemLogic::SectionSizeForSection(voxelWorld));
            const glm::ivec3 baseCoord(
                VoxelMeshInitSystemLogic::FloorDivInt(worldCell.x, sectionSize),
                VoxelMeshInitSystemLogic::FloorDivInt(worldCell.y, sectionSize),
                VoxelMeshInitSystemLogic::FloorDivInt(worldCell.z, sectionSize)
            );
            const glm::ivec3 local = worldCell - baseCoord * sectionSize;

            auto add = [&](const glm::ivec3& coord) { out.insert(VoxelSectionKey{coord}); };
            add(baseCoord);
            if (local.x == 0) add(baseCoord + glm::ivec3(-1, 0, 0));
            if (local.x == sectionSize - 1) add(baseCoord + glm::ivec3(1, 0, 0));
            if (local.y == 0) add(baseCoord + glm::ivec3(0, -1, 0));
            if (local.y == sectionSize - 1) add(baseCoord + glm::ivec3(0, 1, 0));
            if (local.z == 0) add(baseCoord + glm::ivec3(0, 0, -1));
            if (local.z == sectionSize - 1) add(baseCoord + glm::ivec3(0, 0, 1));
        }

        bool isRenderableSection(const VoxelWorldContext& voxelWorld, const VoxelSectionKey& key) {
            const VoxelChunkLifecycleState* state = voxelWorld.findChunkState(key);
            if (!state) return true;
            return state->isRenderable();
        }

        bool containsColumnKey(const std::vector<VoxelColumnKey>& keys, const VoxelColumnKey& key) {
            return std::find(keys.begin(), keys.end(), key) != keys.end();
        }

        void collectColumnMeshingBlockingNeighbors(const VoxelWorldContext& voxelWorld,
                                                   const VoxelColumnKey& key,
                                                   std::vector<VoxelColumnKey>& outBlockers) {
            outBlockers.clear();
            const glm::ivec2 offsets[4] = {
                glm::ivec2(1, 0),
                glm::ivec2(-1, 0),
                glm::ivec2(0, 1),
                glm::ivec2(0, -1)
            };
            for (const glm::ivec2& offset : offsets) {
                const VoxelColumnKey neighborKey{key.coord + offset};
                const VoxelChunkLifecycleState* neighborState = voxelWorld.findColumnState(neighborKey);
                if (!neighborState || !neighborState->desired) continue;
                if (!neighborState->isFullyReady()) {
                    outBlockers.push_back(neighborKey);
                }
            }
        }

        bool columnMeshingNeighborsReady(const VoxelWorldContext& voxelWorld, const VoxelColumnKey& key) {
            std::vector<VoxelColumnKey> blockers;
            collectColumnMeshingBlockingNeighbors(voxelWorld, key, blockers);
            return blockers.empty();
        }

        void clearColumnHaloWait(const VoxelColumnKey& key) {
            g_columnHaloWaitTickets.erase(key);
            g_columnHaloWaitBlockersByColumn.erase(key);
        }

        void removeColumnHaloWaitBlocker(const VoxelColumnKey& key, const VoxelColumnKey& blocker) {
            auto blockersIt = g_columnHaloWaitBlockersByColumn.find(key);
            if (blockersIt == g_columnHaloWaitBlockersByColumn.end()) return;
            std::vector<VoxelColumnKey>& blockers = blockersIt->second;
            blockers.erase(
                std::remove(blockers.begin(), blockers.end(), blocker),
                blockers.end()
            );
        }

        void registerColumnHaloWait(VoxelWorldContext& voxelWorld,
                                    const VoxelColumnKey& key,
                                    uint64_t dirtyTicket,
                                    const std::vector<VoxelColumnKey>& blockers) {
            if (dirtyTicket == 0 || blockers.empty()) return;
            if (voxelWorld.getColumnDirtyTicket(key) != dirtyTicket) return;

            auto ticketIt = g_columnHaloWaitTickets.find(key);
            if (ticketIt == g_columnHaloWaitTickets.end() || ticketIt->second != dirtyTicket) {
                g_columnHaloWaitTickets[key] = dirtyTicket;
                g_columnHaloWaitBlockersByColumn[key].clear();
            }

            std::vector<VoxelColumnKey>& registeredBlockers = g_columnHaloWaitBlockersByColumn[key];
            for (const VoxelColumnKey& blocker : blockers) {
                if (containsColumnKey(registeredBlockers, blocker)) continue;
                registeredBlockers.push_back(blocker);
                g_columnHaloWaitersByNeighbor[blocker].push_back(ColumnHaloWaiter{key, dirtyTicket});
            }
            voxelWorld.dirtyColumns.erase(key);
        }

        void pruneInvalidColumnHaloWaits(VoxelWorldContext& voxelWorld) {
            for (auto it = g_columnHaloWaitTickets.begin(); it != g_columnHaloWaitTickets.end();) {
                const VoxelColumnKey key = it->first;
                const uint64_t ticket = it->second;
                const VoxelChunkLifecycleState* state = voxelWorld.findColumnState(key);
                const auto columnIt = voxelWorld.columns.find(key);
                const bool invalid =
                    voxelWorld.getColumnDirtyTicket(key) != ticket
                    || !state
                    || !state->desired
                    || columnIt == voxelWorld.columns.end()
                    || columnIt->second.nonAirCount <= 0;
                if (!invalid) {
                    ++it;
                    continue;
                }
                if (voxelWorld.getColumnDirtyTicket(key) == ticket) {
                    voxelWorld.clearColumnDirty(key);
                }
                g_columnHaloWaitBlockersByColumn.erase(key);
                it = g_columnHaloWaitTickets.erase(it);
            }
        }

        size_t wakeReadyColumnHaloWaiters(VoxelWorldContext& voxelWorld) {
            pruneInvalidColumnHaloWaits(voxelWorld);

            std::vector<VoxelColumnKey> readyNeighbors;
            readyNeighbors.reserve(g_columnHaloWaitersByNeighbor.size());
            for (const auto& [neighborKey, waiters] : g_columnHaloWaitersByNeighbor) {
                (void)waiters;
                const VoxelChunkLifecycleState* neighborState = voxelWorld.findColumnState(neighborKey);
                if (!neighborState || !neighborState->desired || neighborState->isFullyReady()) {
                    readyNeighbors.push_back(neighborKey);
                }
            }

            size_t woken = 0;
            std::vector<VoxelColumnKey> blockers;
            for (const VoxelColumnKey& neighborKey : readyNeighbors) {
                auto waitersIt = g_columnHaloWaitersByNeighbor.find(neighborKey);
                if (waitersIt == g_columnHaloWaitersByNeighbor.end()) continue;
                std::vector<ColumnHaloWaiter> waiters = std::move(waitersIt->second);
                g_columnHaloWaitersByNeighbor.erase(waitersIt);

                for (const ColumnHaloWaiter& waiter : waiters) {
                    removeColumnHaloWaitBlocker(waiter.key, neighborKey);
                    auto ticketIt = g_columnHaloWaitTickets.find(waiter.key);
                    if (ticketIt == g_columnHaloWaitTickets.end()
                        || ticketIt->second != waiter.dirtyTicket
                        || voxelWorld.getColumnDirtyTicket(waiter.key) != waiter.dirtyTicket) {
                        continue;
                    }

                    const VoxelChunkLifecycleState* state = voxelWorld.findColumnState(waiter.key);
                    const auto columnIt = voxelWorld.columns.find(waiter.key);
                    if (!state
                        || !state->desired
                        || columnIt == voxelWorld.columns.end()
                        || columnIt->second.nonAirCount <= 0) {
                        voxelWorld.clearColumnDirty(waiter.key);
                        clearColumnHaloWait(waiter.key);
                        continue;
                    }

                    collectColumnMeshingBlockingNeighbors(voxelWorld, waiter.key, blockers);
                    if (!blockers.empty()) {
                        registerColumnHaloWait(voxelWorld, waiter.key, waiter.dirtyTicket, blockers);
                        continue;
                    }

                    clearColumnHaloWait(waiter.key);
                    voxelWorld.dirtyColumns.insert(waiter.key);
                    ++woken;
                }
            }
            return woken;
        }

        std::shared_ptr<const std::vector<VoxelMeshingPrototypeTraits>>
        ensurePrototypeTraits(const BaseSystem& baseSystem, const std::vector<Entity>& prototypes) {
            {
                std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
                if (g_meshingRuntime.prototypeTraits
                    && g_meshingRuntime.prototypeCount == prototypes.size()) {
                    return g_meshingRuntime.prototypeTraits;
                }
            }

            auto rebuilt = std::make_shared<const std::vector<VoxelMeshingPrototypeTraits>>(
                VoxelMeshUploadSystemLogic::BuildVoxelMeshingPrototypeTraits(baseSystem, prototypes)
            );

            std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
            if (!g_meshingRuntime.prototypeTraits
                || g_meshingRuntime.prototypeCount != prototypes.size()) {
                g_meshingRuntime.prototypeTraits = rebuilt;
                g_meshingRuntime.prototypeCount = prototypes.size();
            }
            return g_meshingRuntime.prototypeTraits;
        }

        bool captureMeshingSnapshot(const BaseSystem& baseSystem,
                                    const VoxelWorldContext& voxelWorld,
                                    const VoxelSectionKey& key,
                                    const VoxelSection& section,
                                    uint64_t dirtyTicket,
                                    VoxelMeshingSnapshot& outSnapshot) {
            if (section.size <= 0) return false;

            const int sectionSize = section.size;
            const glm::ivec3 volumeSize(sectionSize);
            const size_t paddedCount =
                static_cast<size_t>(volumeSize.x + 2)
                * static_cast<size_t>(volumeSize.y + 2)
                * static_cast<size_t>(volumeSize.z + 2);
            outSnapshot = {};
            outSnapshot.sectionKey = key;
            outSnapshot.sectionBase = section.coord * sectionSize;
            outSnapshot.sectionSize = sectionSize;
            outSnapshot.volumeSize = volumeSize;
            outSnapshot.nonAirCount = section.nonAirCount;
            outSnapshot.dirtyTicket = dirtyTicket;
            outSnapshot.voxelLightingEnabled =
                ::RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelLightingEnabled", true);
            outSnapshot.voxelLightingStrength = glm::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingStrength", 1.0f),
                0.0f,
                1.0f
            );
            outSnapshot.voxelLightingMinBrightness = glm::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingMinBrightness", 0.08f),
                0.0f,
                1.0f
            );
            outSnapshot.voxelLightingGamma = glm::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingGamma", 1.35f),
                0.25f,
                4.0f
            );
            outSnapshot.paddedIds.assign(paddedCount, 0u);
            outSnapshot.paddedColors.assign(paddedCount, 0u);
            outSnapshot.paddedCombinedLight.assign(paddedCount, static_cast<uint8_t>(0));

            if (section.nonAirCount <= 0) {
                return true;
            }

            for (int z = 0; z < sectionSize; ++z) {
                for (int y = 0; y < sectionSize; ++y) {
                    const int srcIndex = sectionVoxelIndex(0, y, z, sectionSize);
                    const int dstIndex = paddedSnapshotIndex(0, y, z, volumeSize);
                    const size_t bytes = static_cast<size_t>(sectionSize) * sizeof(uint32_t);
                    std::memcpy(outSnapshot.paddedIds.data() + static_cast<size_t>(dstIndex),
                                section.ids.data() + static_cast<size_t>(srcIndex),
                                bytes);
                    std::memcpy(outSnapshot.paddedColors.data() + static_cast<size_t>(dstIndex),
                                section.colors.data() + static_cast<size_t>(srcIndex),
                                bytes);
                    if (!outSnapshot.voxelLightingEnabled) continue;
                    for (int x = 0; x < sectionSize; ++x) {
                        const int sectionIndex = srcIndex + x;
                        uint8_t sky = voxelWorld.defaultSkyLightLevel;
                        if (static_cast<size_t>(sectionIndex) < section.skyLight.size()) {
                            sky = section.skyLight[static_cast<size_t>(sectionIndex)];
                        }
                        uint8_t block = static_cast<uint8_t>(0);
                        if (static_cast<size_t>(sectionIndex) < section.blockLight.size()) {
                            block = section.blockLight[static_cast<size_t>(sectionIndex)];
                        }
                        outSnapshot.paddedCombinedLight[static_cast<size_t>(dstIndex + x)] =
                            static_cast<uint8_t>(std::max<int>(sky, block));
                    }
                }
            }

            for (int z = -1; z <= sectionSize; ++z) {
                for (int y = -1; y <= sectionSize; ++y) {
                    for (int x = -1; x <= sectionSize; ++x) {
                        const bool border = (x < 0 || x >= sectionSize
                                          || y < 0 || y >= sectionSize
                                          || z < 0 || z >= sectionSize);
                        if (!border) continue;
                        const glm::ivec3 worldCell = outSnapshot.sectionBase + glm::ivec3(x, y, z);
                        const int dstIndex = paddedSnapshotIndex(x, y, z, volumeSize);
                        outSnapshot.paddedIds[static_cast<size_t>(dstIndex)] = voxelWorld.getBlockWorld(worldCell);
                        outSnapshot.paddedColors[static_cast<size_t>(dstIndex)] = voxelWorld.getColorWorld(worldCell);
                        if (outSnapshot.voxelLightingEnabled) {
                            outSnapshot.paddedCombinedLight[static_cast<size_t>(dstIndex)] =
                                static_cast<uint8_t>(std::max<int>(
                                    voxelWorld.getSkyLightWorld(worldCell),
                                    voxelWorld.getBlockLightWorld(worldCell)
                                ));
                        }
                    }
                }
            }

            return true;
        }

        bool captureColumnMeshingSnapshot(const BaseSystem& baseSystem,
                                          const VoxelWorldContext& voxelWorld,
                                          const VoxelColumnKey& key,
                                          const VoxelColumn& column,
                                          uint64_t dirtyTicket,
                                          VoxelMeshingSnapshot& outSnapshot) {
            const int size = column.chunkSize;
            const int fullHeight = columnHeight(column);
            if (size <= 0 || fullHeight <= 0) return false;

            int minNonAirY = column.contentMinY;
            int maxNonAirY = column.contentMaxY;
            if (column.nonAirCount > 0
                && (minNonAirY > maxNonAirY
                    || minNonAirY < column.minY
                    || maxNonAirY >= column.maxYExclusive)) {
                minNonAirY = column.maxYExclusive;
                maxNonAirY = column.minY - 1;
                for (int z = 0; z < size; ++z) {
                    for (int y = column.minY; y < column.maxYExclusive; ++y) {
                        const int rowIdx = columnVoxelIndex(column, 0, y, z);
                        if (rowIdx < 0 || rowIdx + size > static_cast<int>(column.ids.size())) continue;
                        bool rowHasNonAir = false;
                        for (int x = 0; x < size; ++x) {
                            if (column.ids[static_cast<size_t>(rowIdx + x)] == 0u) continue;
                            rowHasNonAir = true;
                            break;
                        }
                        if (!rowHasNonAir) continue;
                        minNonAirY = std::min(minNonAirY, y);
                        maxNonAirY = std::max(maxNonAirY, y);
                    }
                }
            }

            if (minNonAirY > maxNonAirY) {
                outSnapshot = {};
                outSnapshot.sectionKey = VoxelSectionKey{glm::ivec3(key.coord.x, 0, key.coord.y)};
                outSnapshot.columnKey = key;
                outSnapshot.columnMesh = true;
                outSnapshot.sectionBase = glm::ivec3(key.coord.x * size, column.minY, key.coord.y * size);
                outSnapshot.sectionSize = size;
                outSnapshot.volumeSize = glm::ivec3(size, 0, size);
                outSnapshot.nonAirCount = 0;
                outSnapshot.dirtyTicket = dirtyTicket;
                return true;
            }

            const int height = maxNonAirY - minNonAirY + 1;
            const glm::ivec3 volumeSize(size, height, size);
            const size_t paddedCount =
                static_cast<size_t>(volumeSize.x + 2)
                * static_cast<size_t>(volumeSize.y + 2)
                * static_cast<size_t>(volumeSize.z + 2);
            outSnapshot = {};
            outSnapshot.sectionKey = VoxelSectionKey{glm::ivec3(key.coord.x, 0, key.coord.y)};
            outSnapshot.columnKey = key;
            outSnapshot.columnMesh = true;
            outSnapshot.sectionBase = glm::ivec3(
                key.coord.x * size,
                minNonAirY,
                key.coord.y * size
            );
            outSnapshot.sectionSize = size;
            outSnapshot.volumeSize = volumeSize;
            outSnapshot.nonAirCount = column.nonAirCount;
            outSnapshot.dirtyTicket = dirtyTicket;
            outSnapshot.voxelLightingEnabled =
                ::RenderInitSystemLogic::getRegistryBool(baseSystem, "VoxelLightingEnabled", true);
            outSnapshot.voxelLightingStrength = glm::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingStrength", 1.0f),
                0.0f,
                1.0f
            );
            outSnapshot.voxelLightingMinBrightness = glm::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingMinBrightness", 0.08f),
                0.0f,
                1.0f
            );
            outSnapshot.voxelLightingGamma = glm::clamp(
                ::RenderInitSystemLogic::getRegistryFloat(baseSystem, "VoxelLightingGamma", 1.35f),
                0.25f,
                4.0f
            );
            outSnapshot.paddedIds.assign(paddedCount, 0u);
            outSnapshot.paddedColors.assign(paddedCount, 0u);
            outSnapshot.paddedCombinedLight.assign(paddedCount, static_cast<uint8_t>(0));

            if (column.nonAirCount <= 0) {
                return true;
            }

            for (int z = 0; z < size; ++z) {
                for (int y = 0; y < height; ++y) {
                    const int worldY = minNonAirY + y;
                    const int columnRowIdx = columnVoxelIndex(column, 0, worldY, z);
                    const int dstIndex = paddedSnapshotIndex(0, y, z, volumeSize);
                    if (columnRowIdx < 0
                        || dstIndex < 0
                        || columnRowIdx + size > static_cast<int>(column.ids.size())
                        || dstIndex + size > static_cast<int>(outSnapshot.paddedIds.size())) {
                        continue;
                    }
                    const size_t bytes = static_cast<size_t>(size) * sizeof(uint32_t);
                    std::memcpy(outSnapshot.paddedIds.data() + static_cast<size_t>(dstIndex),
                                column.ids.data() + static_cast<size_t>(columnRowIdx),
                                bytes);
                    std::memcpy(outSnapshot.paddedColors.data() + static_cast<size_t>(dstIndex),
                                column.colors.data() + static_cast<size_t>(columnRowIdx),
                                bytes);
                    if (!outSnapshot.voxelLightingEnabled) continue;
                    for (int x = 0; x < size; ++x) {
                        const int columnIndex = columnRowIdx + x;
                        uint8_t sky = voxelWorld.defaultSkyLightLevel;
                        if (static_cast<size_t>(columnIndex) < column.skyLight.size()) {
                            sky = column.skyLight[static_cast<size_t>(columnIndex)];
                        }
                        uint8_t block = static_cast<uint8_t>(0);
                        if (static_cast<size_t>(columnIndex) < column.blockLight.size()) {
                            block = column.blockLight[static_cast<size_t>(columnIndex)];
                        }
                        outSnapshot.paddedCombinedLight[static_cast<size_t>(dstIndex + x)] =
                            static_cast<uint8_t>(std::max<int>(sky, block));
                    }
                }
            }

            for (int z = -1; z <= size; ++z) {
                for (int y = -1; y <= height; ++y) {
                    for (int x = -1; x <= size; ++x) {
                        const bool border = (x < 0 || x >= size
                                          || y < 0 || y >= height
                                          || z < 0 || z >= size);
                        if (!border) continue;
                        const glm::ivec3 worldCell = outSnapshot.sectionBase + glm::ivec3(x, y, z);
                        const int dstIndex = paddedSnapshotIndex(x, y, z, volumeSize);
                        outSnapshot.paddedIds[static_cast<size_t>(dstIndex)] = voxelWorld.getBlockWorld(worldCell);
                        outSnapshot.paddedColors[static_cast<size_t>(dstIndex)] = voxelWorld.getColorWorld(worldCell);
                        if (outSnapshot.voxelLightingEnabled) {
                            outSnapshot.paddedCombinedLight[static_cast<size_t>(dstIndex)] =
                                static_cast<uint8_t>(std::max<int>(
                                    voxelWorld.getSkyLightWorld(worldCell),
                                    voxelWorld.getBlockLightWorld(worldCell)
                                ));
                        }
                    }
                }
            }

            return true;
        }

        void workerMain() {
            for (;;) {
                MeshingJob job;
                {
                    std::unique_lock<std::mutex> lock(g_meshingRuntime.mutex);
                    g_meshingRuntime.cv.wait(lock, [] {
                        return g_meshingRuntime.stopRequested || !g_meshingRuntime.pendingJobs.empty();
                    });
                    if (g_meshingRuntime.stopRequested && g_meshingRuntime.pendingJobs.empty()) {
                        break;
                    }
                    job = std::move(g_meshingRuntime.pendingJobs.front());
                    g_meshingRuntime.pendingJobs.pop_front();
                    const bool ticketValid = job.snapshot.columnMesh
                        ? [&]() {
                            const auto requestedIt = g_meshingRuntime.requestedColumnTickets.find(job.snapshot.columnKey);
                            return requestedIt != g_meshingRuntime.requestedColumnTickets.end()
                                && requestedIt->second == job.snapshot.dirtyTicket;
                        }()
                        : [&]() {
                            const auto requestedIt = g_meshingRuntime.requestedTickets.find(job.snapshot.sectionKey);
                            return requestedIt != g_meshingRuntime.requestedTickets.end()
                                && requestedIt->second == job.snapshot.dirtyTicket;
                        }();
                    if (job.epoch != g_meshingRuntime.epoch || !ticketValid) {
                        continue;
                    }
                    g_meshingRuntime.activeJobs += 1;
                }

                PreparedVoxelSectionMesh preparedMesh;
                const bool built = job.prototypeTraits
                    && VoxelMeshUploadSystemLogic::PrepareVoxelSectionMesh(
                        job.snapshot,
                        *job.prototypeTraits,
                        preparedMesh
                    );

                {
                    std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
                    if (g_meshingRuntime.activeJobs > 0) {
                        g_meshingRuntime.activeJobs -= 1;
                    }
                    if (!built || job.epoch != g_meshingRuntime.epoch) {
                        continue;
                    }
                    const bool ticketValid = job.snapshot.columnMesh
                        ? [&]() {
                            const auto requestedIt = g_meshingRuntime.requestedColumnTickets.find(job.snapshot.columnKey);
                            return requestedIt != g_meshingRuntime.requestedColumnTickets.end()
                                && requestedIt->second == job.snapshot.dirtyTicket;
                        }()
                        : [&]() {
                            const auto requestedIt = g_meshingRuntime.requestedTickets.find(job.snapshot.sectionKey);
                            return requestedIt != g_meshingRuntime.requestedTickets.end()
                                && requestedIt->second == job.snapshot.dirtyTicket;
                        }();
                    if (!ticketValid) {
                        continue;
                    }
                    MeshingResult result;
                    result.key = job.snapshot.sectionKey;
                    result.columnKey = job.snapshot.columnKey;
                    result.columnMesh = job.snapshot.columnMesh;
                    result.mesh = std::move(preparedMesh);
                    result.epoch = job.epoch;
                    g_meshingRuntime.completedJobs.push_back(std::move(result));
                }
            }
        }

        void ensureWorkersStarted() {
            std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
            if (g_meshingRuntime.workersStarted) return;
            g_meshingRuntime.stopRequested = false;
            g_meshingRuntime.workers.reserve(kMeshingWorkerCount);
            for (size_t i = 0; i < kMeshingWorkerCount; ++i) {
                g_meshingRuntime.workers.emplace_back(workerMain);
            }
            g_meshingRuntime.workersStarted = true;
        }
    }

    void GetVoxelMeshingRuntimePerfStats(size_t& pendingJobs,
                                         size_t& activeJobs,
                                         size_t& requestedJobs,
                                         size_t& completedBuffered,
                                         size_t& dirtySections,
                                         size_t& candidates,
                                         size_t& missingSections,
                                         size_t& queued,
                                         size_t& snapshots,
                                         size_t& completedPopped,
                                         size_t& completedAccepted,
                                         size_t& completedDropped,
                                         size_t& outstandingBlocked,
                                         size_t& neighborWaitBlocked,
                                         size_t& neighborWaitWoken,
                                         size_t& neighborWaitResident,
                                         size_t& snapshotCap,
                                         int& schedulerPressure,
                                         size_t& preparedBacklog,
                                         size_t& uploadBacklog,
                                         float& frameSnapshotMs,
                                         float& lastSnapshotMs,
                                         int& lastSnapshotY,
                                         int& lastSnapshotNonAir,
                                         uint64_t& totalQueued,
                                         uint64_t& totalCompleted,
                                         uint64_t& totalDropped,
                                         uint64_t& totalSnapshots,
                                         uint64_t& totalOutstandingBlocked,
                                         uint64_t& totalNeighborWaitBlocked,
                                         uint64_t& totalNeighborWaitWoken) {
        const MeshingPerfStats stats = g_meshingPerfStats;
        pendingJobs = stats.pendingJobs;
        activeJobs = stats.activeJobs;
        requestedJobs = stats.requestedJobs;
        completedBuffered = stats.completedBuffered;
        dirtySections = stats.dirtySections;
        candidates = stats.candidates;
        missingSections = stats.missingSections;
        queued = stats.queued;
        snapshots = stats.snapshots;
        completedPopped = stats.completedPopped;
        completedAccepted = stats.completedAccepted;
        completedDropped = stats.completedDropped;
        outstandingBlocked = stats.outstandingBlocked;
        neighborWaitBlocked = stats.neighborWaitBlocked;
        neighborWaitWoken = stats.neighborWaitWoken;
        neighborWaitResident = stats.neighborWaitResident;
        snapshotCap = stats.snapshotCap;
        schedulerPressure = stats.schedulerPressure;
        preparedBacklog = stats.preparedBacklog;
        uploadBacklog = stats.uploadBacklog;
        frameSnapshotMs = stats.frameSnapshotMs;
        lastSnapshotMs = stats.lastSnapshotMs;
        lastSnapshotY = stats.lastSnapshotY;
        lastSnapshotNonAir = stats.lastSnapshotNonAir;
        totalQueued = stats.totalQueued;
        totalCompleted = stats.totalCompleted;
        totalDropped = stats.totalDropped;
        totalSnapshots = stats.totalSnapshots;
        totalOutstandingBlocked = stats.totalOutstandingBlocked;
        totalNeighborWaitBlocked = stats.totalNeighborWaitBlocked;
        totalNeighborWaitWoken = stats.totalNeighborWaitWoken;

        std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
        pendingJobs = g_meshingRuntime.pendingJobs.size();
        activeJobs = g_meshingRuntime.activeJobs;
        requestedJobs = g_meshingRuntime.requestedTickets.size()
            + g_meshingRuntime.requestedColumnTickets.size();
        completedBuffered = g_meshingRuntime.completedJobs.size();
    }

    void ResetMeshingRuntime() {
        std::vector<std::thread> workers;
        {
            std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
            g_meshingRuntime.stopRequested = true;
            g_meshingRuntime.epoch += 1;
            g_meshingRuntime.pendingJobs.clear();
            g_meshingRuntime.completedJobs.clear();
            g_meshingRuntime.requestedTickets.clear();
            g_meshingRuntime.requestedColumnTickets.clear();
            g_meshingRuntime.prototypeTraits.reset();
            g_meshingRuntime.prototypeCount = 0;
            workers = std::move(g_meshingRuntime.workers);
            g_meshingRuntime.workersStarted = false;
        }
        g_meshingRuntime.cv.notify_all();

        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
        g_meshingRuntime.stopRequested = false;
        g_meshingRuntime.activeJobs = 0;
        g_meshingPerfStats = {};
        g_columnHaloWaitersByNeighbor.clear();
        g_columnHaloWaitTickets.clear();
        g_columnHaloWaitBlockersByColumn.clear();
    }

    void RequestPriorityVoxelRemesh(BaseSystem& baseSystem,
                                    std::vector<Entity>&,
                                    const glm::ivec3& worldCell) {
        if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return;
        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;

        std::unordered_set<VoxelSectionKey, VoxelSectionKeyHash> touched;
        touched.reserve(7);
        collectTouchedSections(voxelWorld, worldCell, touched);

        for (const VoxelSectionKey& key : touched) {
            voxelWorld.markSectionDirty(key);
        }
    }

    void UpdateVoxelMeshing(BaseSystem& baseSystem,
                            std::vector<Entity>& prototypes,
                            float,
                            PlatformWindowHandle) {
        if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled || !baseSystem.voxelRender) return;
        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
        VoxelRenderContext& voxelRender = *baseSystem.voxelRender;

        ensureWorkersStarted();
        std::shared_ptr<const std::vector<VoxelMeshingPrototypeTraits>> prototypeTraits =
            ensurePrototypeTraits(baseSystem, prototypes);
        if (!prototypeTraits) return;

        std::vector<MeshingResult> completed;
        uint64_t runtimeEpoch = 0;
        {
            std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
            runtimeEpoch = g_meshingRuntime.epoch;
            completed.reserve(g_meshingRuntime.completedJobs.size());
            while (!g_meshingRuntime.completedJobs.empty()) {
                completed.push_back(std::move(g_meshingRuntime.completedJobs.front()));
                g_meshingRuntime.completedJobs.pop_front();
            }
        }
        const size_t completedPoppedThisFrame = completed.size();
        size_t completedAcceptedThisFrame = 0;
        size_t completedDroppedThisFrame = 0;

        for (MeshingResult& result : completed) {
            if (result.epoch != runtimeEpoch) continue;

            if (result.columnMesh) {
                {
                    std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
                    auto requestedIt = g_meshingRuntime.requestedColumnTickets.find(result.columnKey);
                    if (requestedIt != g_meshingRuntime.requestedColumnTickets.end()
                        && requestedIt->second == result.mesh.dirtyTicket) {
                        g_meshingRuntime.requestedColumnTickets.erase(requestedIt);
                    }
                }

                auto columnIt = voxelWorld.columns.find(result.columnKey);
                if (columnIt == voxelWorld.columns.end() || columnIt->second.nonAirCount <= 0) {
                    voxelRender.preparedColumnMeshes.erase(result.columnKey);
                    voxelRender.wireframeColumnMeshes.erase(result.columnKey);
                    voxelRender.columnRenderBuffersDirty.erase(result.columnKey);
                    voxelWorld.clearColumnDirty(result.columnKey);
                    ++completedDroppedThisFrame;
                    continue;
                }

                const uint64_t currentTicket = voxelWorld.getColumnDirtyTicket(result.columnKey);
                if (currentTicket == 0 || currentTicket != result.mesh.dirtyTicket) {
                    voxelRender.preparedColumnMeshes.erase(result.columnKey);
                    voxelRender.wireframeColumnMeshes.erase(result.columnKey);
                    ++completedDroppedThisFrame;
                    continue;
                }

                voxelRender.preparedColumnMeshes[result.columnKey] = std::move(result.mesh);
                voxelRender.columnRenderBuffersDirty.insert(result.columnKey);
                ++completedAcceptedThisFrame;
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
                auto requestedIt = g_meshingRuntime.requestedTickets.find(result.key);
                if (requestedIt != g_meshingRuntime.requestedTickets.end()
                    && requestedIt->second == result.mesh.dirtyTicket) {
                    g_meshingRuntime.requestedTickets.erase(requestedIt);
                }
            }

            auto sectionIt = voxelWorld.sections.find(result.key);
            if (sectionIt == voxelWorld.sections.end() || sectionIt->second.nonAirCount <= 0) {
                voxelRender.preparedMeshes.erase(result.key);
                voxelRender.wireframeMeshes.erase(result.key);
                voxelRender.renderBuffersDirty.erase(result.key);
                voxelWorld.clearSectionDirty(result.key);
                ++completedDroppedThisFrame;
                continue;
            }

            const uint64_t currentTicket = voxelWorld.getSectionDirtyTicket(result.key);
            if (currentTicket == 0 || currentTicket != result.mesh.dirtyTicket) {
                voxelRender.preparedMeshes.erase(result.key);
                voxelRender.wireframeMeshes.erase(result.key);
                ++completedDroppedThisFrame;
                continue;
            }
            if (!isRenderableSection(voxelWorld, result.key)) {
                ++completedDroppedThisFrame;
                continue;
            }

            voxelRender.preparedMeshes[result.key] = std::move(result.mesh);
            voxelRender.renderBuffersDirty.insert(result.key);
            ++completedAcceptedThisFrame;
        }

        const size_t neighborWaitWokenThisFrame = wakeReadyColumnHaloWaiters(voxelWorld);

        struct Candidate {
            VoxelSectionKey key;
            uint64_t dirtyTicket = 0;
            float dist2 = 0.0f;
        };
        struct ColumnCandidate {
            VoxelColumnKey key;
            uint64_t dirtyTicket = 0;
            float dist2 = 0.0f;
        };

        const glm::vec3 cameraPos = baseSystem.player
            ? baseSystem.player->cameraPosition
            : glm::vec3(0.0f);

        std::vector<VoxelSectionKey> missingSections;
        std::vector<Candidate> candidates;
        candidates.reserve(voxelWorld.dirtySections.size());
        for (const VoxelSectionKey& key : voxelWorld.dirtySections) {
            auto sectionIt = voxelWorld.sections.find(key);
            if (sectionIt == voxelWorld.sections.end() || sectionIt->second.nonAirCount <= 0) {
                missingSections.push_back(key);
                continue;
            }
            if (!isRenderableSection(voxelWorld, key)) continue;

            const uint64_t dirtyTicket = voxelWorld.getSectionDirtyTicket(key);
            if (dirtyTicket == 0) continue;

            auto preparedIt = voxelRender.preparedMeshes.find(key);
            if (preparedIt != voxelRender.preparedMeshes.end()) {
                if (preparedIt->second.dirtyTicket == dirtyTicket) {
                    if (voxelRender.renderBuffersDirty.count(key) == 0) {
                        voxelRender.renderBuffersDirty.insert(key);
                    }
                    continue;
                }
                voxelRender.preparedMeshes.erase(preparedIt);
            }

            candidates.push_back({
                key,
                dirtyTicket,
                keyDist2ToCamera(voxelWorld, key, cameraPos)
            });
        }

        for (const VoxelSectionKey& key : missingSections) {
            voxelRender.preparedMeshes.erase(key);
            voxelRender.wireframeMeshes.erase(key);
            voxelRender.renderBuffersDirty.erase(key);
            voxelWorld.clearSectionDirty(key);
        }

        std::vector<VoxelColumnKey> missingColumns;
        std::vector<ColumnCandidate> columnCandidates;
        columnCandidates.reserve(voxelWorld.dirtyColumns.size());
        for (const VoxelColumnKey& key : voxelWorld.dirtyColumns) {
            auto columnIt = voxelWorld.columns.find(key);
            if (columnIt == voxelWorld.columns.end() || columnIt->second.nonAirCount <= 0) {
                missingColumns.push_back(key);
                continue;
            }

            const uint64_t dirtyTicket = voxelWorld.getColumnDirtyTicket(key);
            if (dirtyTicket == 0) continue;
            auto waitTicketIt = g_columnHaloWaitTickets.find(key);
            if (waitTicketIt != g_columnHaloWaitTickets.end()
                && waitTicketIt->second != dirtyTicket) {
                clearColumnHaloWait(key);
            }

            auto preparedIt = voxelRender.preparedColumnMeshes.find(key);
            if (preparedIt != voxelRender.preparedColumnMeshes.end()) {
                if (preparedIt->second.dirtyTicket == dirtyTicket) {
                    if (voxelRender.columnRenderBuffersDirty.count(key) == 0) {
                        voxelRender.columnRenderBuffersDirty.insert(key);
                    }
                    continue;
                }
                voxelRender.preparedColumnMeshes.erase(preparedIt);
            }

            columnCandidates.push_back({
                key,
                dirtyTicket,
                columnDist2ToCamera(voxelWorld, key, columnIt->second, cameraPos)
            });
        }

        for (const VoxelColumnKey& key : missingColumns) {
            voxelRender.preparedColumnMeshes.erase(key);
            voxelRender.wireframeColumnMeshes.erase(key);
            voxelRender.columnRenderBuffersDirty.erase(key);
            voxelWorld.clearColumnDirty(key);
        }

        std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
            if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
            if (a.key.coord.y != b.key.coord.y) return a.key.coord.y < b.key.coord.y;
            return a.key.coord.z < b.key.coord.z;
        });
        std::sort(columnCandidates.begin(), columnCandidates.end(), [](const ColumnCandidate& a, const ColumnCandidate& b) {
            if (a.dist2 != b.dist2) return a.dist2 < b.dist2;
            if (a.key.coord.x != b.key.coord.x) return a.key.coord.x < b.key.coord.x;
            return a.key.coord.y < b.key.coord.y;
        });

        int snapshotCapThisFrame = std::max(
            0,
            ::RenderInitSystemLogic::getRegistryInt(
                baseSystem,
                "voxelMeshingMaxNewSnapshotsPerFrame",
                static_cast<int>(kMaxNewSnapshotsPerFrame)
            )
        );
        const bool meshingBackpressureEnabled = ::RenderInitSystemLogic::getRegistryBool(
            baseSystem,
            "voxelMeshingBackpressureEnabled",
            true
        );
        const size_t preparedBacklog =
            voxelRender.preparedMeshes.size() + voxelRender.preparedColumnMeshes.size();
        const size_t uploadBacklog =
            voxelRender.renderBuffersDirty.size() + voxelRender.columnRenderBuffersDirty.size();
        const size_t preparedSoft = static_cast<size_t>(std::max(
            1,
            ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelMeshingPreparedBacklogSoft", 2)
        ));
        const size_t preparedHard = static_cast<size_t>(std::max(
            static_cast<int>(preparedSoft),
            ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelMeshingPreparedBacklogHard", 8)
        ));
        const size_t uploadSoft = static_cast<size_t>(std::max(
            1,
            ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelMeshingUploadBacklogSoft", 2)
        ));
        const size_t uploadHard = static_cast<size_t>(std::max(
            static_cast<int>(uploadSoft),
            ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelMeshingUploadBacklogHard", 8)
        ));
        int meshingPressure = 0;
        if (meshingBackpressureEnabled) {
            const bool hardPressure = preparedBacklog >= preparedHard || uploadBacklog >= uploadHard;
            const bool softPressure = preparedBacklog >= preparedSoft || uploadBacklog >= uploadSoft;
            meshingPressure = hardPressure ? 2 : (softPressure ? 1 : 0);
            if (meshingPressure >= 2) {
                snapshotCapThisFrame = 0;
            } else if (meshingPressure == 1) {
                snapshotCapThisFrame = std::min(snapshotCapThisFrame, 1);
            }
        }

        size_t enqueuedThisFrame = 0;
        size_t snapshotsThisFrame = 0;
        size_t outstandingBlockedThisFrame = 0;
        size_t neighborWaitBlockedThisFrame = 0;
        float frameSnapshotMs = 0.0f;
        float lastSnapshotMs = 0.0f;
        int lastSnapshotY = 0;
        int lastSnapshotNonAir = 0;
        for (const Candidate& candidate : candidates) {
            if (enqueuedThisFrame >= static_cast<size_t>(snapshotCapThisFrame)) break;

            auto sectionIt = voxelWorld.sections.find(candidate.key);
            if (sectionIt == voxelWorld.sections.end() || sectionIt->second.nonAirCount <= 0) {
                voxelRender.preparedMeshes.erase(candidate.key);
                voxelRender.wireframeMeshes.erase(candidate.key);
                voxelRender.renderBuffersDirty.erase(candidate.key);
                voxelWorld.clearSectionDirty(candidate.key);
                continue;
            }
            if (!isRenderableSection(voxelWorld, candidate.key)) continue;
            if (voxelWorld.getSectionDirtyTicket(candidate.key) != candidate.dirtyTicket) continue;
            const VoxelColumnKey sectionColumnKey{
                glm::ivec2(candidate.key.coord.x, candidate.key.coord.z)
            };
            std::vector<VoxelColumnKey> blockingNeighbors;
            collectColumnMeshingBlockingNeighbors(voxelWorld, sectionColumnKey, blockingNeighbors);
            if (!blockingNeighbors.empty()) {
                ++neighborWaitBlockedThisFrame;
                continue;
            }

            VoxelMeshingSnapshot snapshot;
            const auto snapshotStart = std::chrono::steady_clock::now();
            const bool capturedSnapshot = captureMeshingSnapshot(
                baseSystem,
                voxelWorld,
                candidate.key,
                sectionIt->second,
                candidate.dirtyTicket,
                snapshot
            );
            const float snapshotMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - snapshotStart
            ).count();
            if (!capturedSnapshot) {
                continue;
            }
            frameSnapshotMs += snapshotMs;
            lastSnapshotMs = snapshotMs;
            lastSnapshotY = candidate.key.coord.y;
            lastSnapshotNonAir = sectionIt->second.nonAirCount;
            ++snapshotsThisFrame;

            bool queued = false;
            {
                std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
                const auto requestedIt = g_meshingRuntime.requestedTickets.find(candidate.key);
                if (requestedIt != g_meshingRuntime.requestedTickets.end()
                    && requestedIt->second == candidate.dirtyTicket) {
                    continue;
                }
                const size_t outstandingJobs = g_meshingRuntime.pendingJobs.size() + g_meshingRuntime.activeJobs;
                if (outstandingJobs >= kMaxOutstandingJobs) {
                    ++outstandingBlockedThisFrame;
                    break;
                }
                MeshingJob job;
                job.snapshot = std::move(snapshot);
                job.prototypeTraits = prototypeTraits;
                job.epoch = g_meshingRuntime.epoch;
                g_meshingRuntime.requestedTickets[candidate.key] = candidate.dirtyTicket;
                g_meshingRuntime.pendingJobs.push_back(std::move(job));
                queued = true;
            }
            if (queued) {
                g_meshingRuntime.cv.notify_one();
                enqueuedThisFrame += 1;
            }
        }

        for (const ColumnCandidate& candidate : columnCandidates) {
            if (enqueuedThisFrame >= static_cast<size_t>(snapshotCapThisFrame)) break;

            auto columnIt = voxelWorld.columns.find(candidate.key);
            if (columnIt == voxelWorld.columns.end() || columnIt->second.nonAirCount <= 0) {
                voxelRender.preparedColumnMeshes.erase(candidate.key);
                voxelRender.wireframeColumnMeshes.erase(candidate.key);
                voxelRender.columnRenderBuffersDirty.erase(candidate.key);
                voxelWorld.clearColumnDirty(candidate.key);
                continue;
            }
            if (voxelWorld.getColumnDirtyTicket(candidate.key) != candidate.dirtyTicket) continue;
            std::vector<VoxelColumnKey> blockingNeighbors;
            collectColumnMeshingBlockingNeighbors(voxelWorld, candidate.key, blockingNeighbors);
            if (!blockingNeighbors.empty()) {
                registerColumnHaloWait(
                    voxelWorld,
                    candidate.key,
                    candidate.dirtyTicket,
                    blockingNeighbors
                );
                ++neighborWaitBlockedThisFrame;
                continue;
            }

            VoxelMeshingSnapshot snapshot;
            const auto snapshotStart = std::chrono::steady_clock::now();
            const bool capturedSnapshot = captureColumnMeshingSnapshot(
                baseSystem,
                voxelWorld,
                candidate.key,
                columnIt->second,
                candidate.dirtyTicket,
                snapshot
            );
            const float snapshotMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - snapshotStart
            ).count();
            if (!capturedSnapshot) {
                continue;
            }
            frameSnapshotMs += snapshotMs;
            lastSnapshotMs = snapshotMs;
            lastSnapshotY = snapshot.sectionBase.y;
            lastSnapshotNonAir = columnIt->second.nonAirCount;
            ++snapshotsThisFrame;

            bool queued = false;
            {
                std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
                const auto requestedIt = g_meshingRuntime.requestedColumnTickets.find(candidate.key);
                if (requestedIt != g_meshingRuntime.requestedColumnTickets.end()
                    && requestedIt->second == candidate.dirtyTicket) {
                    continue;
                }
                const size_t outstandingJobs = g_meshingRuntime.pendingJobs.size() + g_meshingRuntime.activeJobs;
                if (outstandingJobs >= kMaxOutstandingJobs) {
                    ++outstandingBlockedThisFrame;
                    break;
                }
                MeshingJob job;
                job.snapshot = std::move(snapshot);
                job.prototypeTraits = prototypeTraits;
                job.epoch = g_meshingRuntime.epoch;
                g_meshingRuntime.requestedColumnTickets[candidate.key] = candidate.dirtyTicket;
                g_meshingRuntime.pendingJobs.push_back(std::move(job));
                queued = true;
            }
            if (queued) {
                g_meshingRuntime.cv.notify_one();
                enqueuedThisFrame += 1;
            }
        }

        MeshingPerfStats stats{};
        stats.dirtySections = voxelWorld.dirtySections.size() + voxelWorld.dirtyColumns.size();
        stats.candidates = candidates.size() + columnCandidates.size();
        stats.missingSections = missingSections.size() + missingColumns.size();
        stats.queued = enqueuedThisFrame;
        stats.snapshots = snapshotsThisFrame;
        stats.completedPopped = completedPoppedThisFrame;
        stats.completedAccepted = completedAcceptedThisFrame;
        stats.completedDropped = completedDroppedThisFrame;
        stats.outstandingBlocked = outstandingBlockedThisFrame;
        stats.neighborWaitBlocked = neighborWaitBlockedThisFrame;
        stats.neighborWaitWoken = neighborWaitWokenThisFrame;
        stats.neighborWaitResident = g_columnHaloWaitTickets.size();
        stats.snapshotCap = static_cast<size_t>(snapshotCapThisFrame);
        stats.schedulerPressure = meshingPressure;
        stats.preparedBacklog = preparedBacklog;
        stats.uploadBacklog = uploadBacklog;
        stats.frameSnapshotMs = frameSnapshotMs;
        stats.lastSnapshotMs = lastSnapshotMs;
        stats.lastSnapshotY = lastSnapshotY;
        stats.lastSnapshotNonAir = lastSnapshotNonAir;
        stats.totalQueued = g_meshingPerfStats.totalQueued + static_cast<uint64_t>(enqueuedThisFrame);
        stats.totalCompleted = g_meshingPerfStats.totalCompleted + static_cast<uint64_t>(completedAcceptedThisFrame);
        stats.totalDropped = g_meshingPerfStats.totalDropped + static_cast<uint64_t>(completedDroppedThisFrame);
        stats.totalSnapshots = g_meshingPerfStats.totalSnapshots + static_cast<uint64_t>(snapshotsThisFrame);
        stats.totalOutstandingBlocked = g_meshingPerfStats.totalOutstandingBlocked
            + static_cast<uint64_t>(outstandingBlockedThisFrame);
        stats.totalNeighborWaitBlocked = g_meshingPerfStats.totalNeighborWaitBlocked
            + static_cast<uint64_t>(neighborWaitBlockedThisFrame);
        stats.totalNeighborWaitWoken = g_meshingPerfStats.totalNeighborWaitWoken
            + static_cast<uint64_t>(neighborWaitWokenThisFrame);
        {
            std::lock_guard<std::mutex> lock(g_meshingRuntime.mutex);
            stats.pendingJobs = g_meshingRuntime.pendingJobs.size();
            stats.activeJobs = g_meshingRuntime.activeJobs;
            stats.requestedJobs = g_meshingRuntime.requestedTickets.size()
                + g_meshingRuntime.requestedColumnTickets.size();
            stats.completedBuffered = g_meshingRuntime.completedJobs.size();
        }
        g_meshingPerfStats = stats;
    }
}
