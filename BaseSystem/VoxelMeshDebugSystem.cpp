#pragma once

#include "Host/PlatformInput.h"
#include <cmath>
#include <chrono>
#include <algorithm>
#include <iostream>
#include <limits>
#include <vector>

namespace RenderInitSystemLogic {
    int getRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback);
    bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback);
}
namespace VoxelMeshingSystemLogic {
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
                                         uint64_t& totalNeighborWaitWoken);
}
namespace VoxelMeshUploadSystemLogic {
    void GetVoxelMeshBuildPerfStats(uint64_t& buildCount,
                                    uint64_t& primaryBuiltCount,
                                    float& lastPrepareMs,
                                    float& lastPrimaryMs,
                                    float& lastFallbackMs,
                                    float& avgPrepareMs,
                                    float& avgPrimaryMs,
                                    float& avgFallbackMs,
                                    float& maxPrepareMs,
                                    float& maxPrimaryMs,
                                    float& maxFallbackMs,
                                    int& lastSectionSize,
                                    int& lastSectionY,
                                    int& lastNonAir,
                                    size_t& lastPrimaryFaces,
                                    size_t& lastFallbackFaces,
                                    size_t& lastFallbackCellsVisited,
                                    size_t& lastFallbackRenderableBlocks,
                                    size_t& lastOpaqueFaces,
                                    size_t& lastAlphaFaces,
                                    size_t& lastWaterSurfaceFaces,
                                    size_t& lastWaterBodyFaces);
    void GetVoxelMeshUploadPerfStats(float& lastUploadMs,
                                     size_t& lastUploadedSections,
                                     size_t& lastPendingBefore,
                                     size_t& lastPendingAfter,
                                     size_t& lastCandidates,
                                     size_t& lastUploadedClusters,
                                     size_t& lastUploadedFaces,
                                     size_t& lastUploadedBytes,
                                     size_t& lastUploadedBuffers,
                                     size_t& lastPackedTerrainFaces,
                                     size_t& lastPackedTerrainBytes,
                                     int& lastCapSections,
                                     float& lastBudgetMs,
                                     float& lastClusterSplitMs,
                                     float& lastBufferStageMs,
                                     float& lastPublishMs,
                                     bool& lastBootstrapActive,
                                     bool& lastHardSectionCapApplied,
                                     bool& lastBudgetOverrun,
                                     bool& lastClusterSplitEnabled,
                                     float& lastBudgetOverrunMs,
                                     uint64_t& totalUploadedSections,
                                     uint64_t& totalUploadedClusters);
}
namespace TerrainSystemLogic {
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
    void GetVoxelCaveFieldRuntimeStats(int& mode,
                                       size_t& binaryTileCount,
                                       size_t& perlinTileCount,
                                       uint64_t& totalTilesBuilt,
                                       uint64_t& totalCellsBuilt);
    void ConsumeTerrainCaveRampPerfStats(uint64_t& ramps,
                                         uint64_t& blockWrites,
                                         uint64_t& sections,
                                         uint64_t& pocketCandidates,
                                         uint64_t& validatorCalls,
                                         uint64_t& volumeDirs,
                                         uint64_t& supportDirs);
    void GetVoxelTerrainDetailedPerfStats(float& workerSetupMs,
                                          float& workerColumnMs,
                                          float& publishMs,
                                          float& maintenanceMs,
                                          float& pendingPopMs,
                                          float& saveProbeMs,
                                          float& releaseMs,
                                          float& stepMs,
                                          float& requeueMs);
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
                                               uint64_t& materializeCalls);
    void GetVoxelTerrainBaseCoarsePerfStats(float& fillDepthMs,
                                            float& fillRegularMs,
                                            float& fillRegularCaveScanMs,
                                            float& fillRangeWriteMs,
                                            float& detailScanMs,
                                            uint64_t& detailAirSkipped,
                                            uint64_t& detailDepthCells,
                                            uint64_t& detailSurfaceCells,
                                            uint64_t& detailSurfaceNeighborChecks,
                                            uint64_t& detailWrites);
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
                                          int& phase3);
}
namespace TreeGenerationSystemLogic {
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
                                 float& updateMs);
}

namespace VoxelMeshDebugSystemLogic {
    void UpdateVoxelMeshDebug(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle) {
        if (!baseSystem.voxelWorld) return;
        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;

        static auto lastPerfLog = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
            const bool voxelPerfLogEnabled = ::RenderInitSystemLogic::getRegistryBool(
                baseSystem, "voxelPerfLogEnabled", true
            );
        const int voxelPerfLogIntervalMs = std::max(
            250,
            ::RenderInitSystemLogic::getRegistryInt(baseSystem, "voxelPerfLogIntervalMs", 1000)
        );
        if (voxelPerfLogEnabled && now - lastPerfLog >= std::chrono::milliseconds(voxelPerfLogIntervalMs)) {
            size_t voxelRenderDirtyCount = baseSystem.voxelRender
                ? baseSystem.voxelRender->renderBuffersDirty.size()
                    + baseSystem.voxelRender->columnRenderBuffersDirty.size()
                : 0;
            size_t terrainPending = 0, terrainDesired = 0, terrainGenerated = 0, terrainJobs = 0;
            int terrainStepped = 0;
            int terrainBuilt = 0, terrainConsumed = 0, terrainSkipped = 0, terrainFiltered = 0;
            int terrainRescueSurface = 0, terrainRescueMissing = 0, terrainCapDrop = 0, terrainReprio = 0;
            float terrainPrepMs = 0.0f;
            float terrainMs = 0.0f;
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
            int terrainCaveFieldMode = 0;
            size_t terrainBinaryCaveTiles = 0;
            size_t terrainPerlinCaveTiles = 0;
            uint64_t terrainCaveFieldTotalTilesBuilt = 0;
            uint64_t terrainCaveFieldTotalCellsBuilt = 0;
            uint64_t terrainCaveRampCount = 0;
            uint64_t terrainCaveRampBlockWrites = 0;
            uint64_t terrainCaveRampSections = 0;
            uint64_t terrainCaveRampPocketCandidates = 0;
            uint64_t terrainCaveRampValidatorCalls = 0;
            uint64_t terrainCaveRampVolumeDirs = 0;
            uint64_t terrainCaveRampSupportDirs = 0;
            float terrainWorkerSetupMs = 0.0f;
            float terrainWorkerColumnMs = 0.0f;
            float terrainPublishMs = 0.0f;
            float terrainMaintenanceMs = 0.0f;
            float terrainPendingPopMs = 0.0f;
            float terrainSaveProbeMs = 0.0f;
            float terrainReleaseMs = 0.0f;
            float terrainStepMs = 0.0f;
            float terrainRequeueMs = 0.0f;
            float terrainBaseSetupBreakdownMs = 0.0f;
            float terrainBaseCaveFieldPrepMs = 0.0f;
            float terrainBaseSurfaceBiomeMs = 0.0f;
            float terrainBaseHydrologyMs = 0.0f;
            float terrainBaseCaveMs = 0.0f;
            float terrainBaseOreDecorMs = 0.0f;
            float terrainBaseMemoryMs = 0.0f;
            float terrainBaseBlockWriteMs = 0.0f;
            float terrainBaseCaveRampMs = 0.0f;
            float terrainBaseBookkeepingMs = 0.0f;
            float terrainBaseMaterializeMs = 0.0f;
            float terrainBaseFillPassMs = 0.0f;
            float terrainBaseDetailPassMs = 0.0f;
            float terrainBasePostFeaturePassMs = 0.0f;
            float terrainBaseFillDepthMs = 0.0f;
            float terrainBaseFillRegularMs = 0.0f;
            float terrainBaseFillRegularCaveScanMs = 0.0f;
            float terrainBaseFillRangeWriteMs = 0.0f;
            float terrainBaseDetailScanMs = 0.0f;
            uint64_t terrainBaseWorkerCalls = 0;
            uint64_t terrainBaseFillPassCalls = 0;
            uint64_t terrainBaseDetailPassCalls = 0;
            uint64_t terrainBasePostFeatureCalls = 0;
            uint64_t terrainBaseColumnsVisited = 0;
            uint64_t terrainBaseVerticalCellsVisited = 0;
            uint64_t terrainBaseTerrainSampleCalls = 0;
            uint64_t terrainBaseTerrainSampleMisses = 0;
            uint64_t terrainBaseBiomeResolveCalls = 0;
            uint64_t terrainBaseCaveSampleCalls = 0;
            uint64_t terrainBaseHydrologyColumns = 0;
            uint64_t terrainBaseOreDecorColumnCalls = 0;
            uint64_t terrainBaseOreDecorCellCalls = 0;
            uint64_t terrainBaseDirectColumnEnsureCalls = 0;
            uint64_t terrainBaseDirectColumnCreateCalls = 0;
            uint64_t terrainBaseWriteVoxelCalls = 0;
            uint64_t terrainBaseWriteRunCalls = 0;
            uint64_t terrainBaseWriteCellsChanged = 0;
            uint64_t terrainBaseCaveRampScanCalls = 0;
            uint64_t terrainBaseMaterializeCalls = 0;
            uint64_t terrainBaseDetailAirSkipped = 0;
            uint64_t terrainBaseDetailDepthCells = 0;
            uint64_t terrainBaseDetailSurfaceCells = 0;
            uint64_t terrainBaseDetailSurfaceNeighborChecks = 0;
            uint64_t terrainBaseDetailWrites = 0;
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
                terrainMs,
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
            TerrainSystemLogic::GetVoxelCaveFieldRuntimeStats(
                terrainCaveFieldMode,
                terrainBinaryCaveTiles,
                terrainPerlinCaveTiles,
                terrainCaveFieldTotalTilesBuilt,
                terrainCaveFieldTotalCellsBuilt
            );
            TerrainSystemLogic::ConsumeTerrainCaveRampPerfStats(
                terrainCaveRampCount,
                terrainCaveRampBlockWrites,
                terrainCaveRampSections,
                terrainCaveRampPocketCandidates,
                terrainCaveRampValidatorCalls,
                terrainCaveRampVolumeDirs,
                terrainCaveRampSupportDirs
            );
            TerrainSystemLogic::GetVoxelTerrainDetailedPerfStats(
                terrainWorkerSetupMs,
                terrainWorkerColumnMs,
                terrainPublishMs,
                terrainMaintenanceMs,
                terrainPendingPopMs,
                terrainSaveProbeMs,
                terrainReleaseMs,
                terrainStepMs,
                terrainRequeueMs
            );
            TerrainSystemLogic::GetVoxelTerrainBaseBreakdownPerfStats(
                terrainBaseSetupBreakdownMs,
                terrainBaseCaveFieldPrepMs,
                terrainBaseSurfaceBiomeMs,
                terrainBaseHydrologyMs,
                terrainBaseCaveMs,
                terrainBaseOreDecorMs,
                terrainBaseMemoryMs,
                terrainBaseBlockWriteMs,
                terrainBaseCaveRampMs,
                terrainBaseBookkeepingMs,
                terrainBaseMaterializeMs,
                terrainBaseFillPassMs,
                terrainBaseDetailPassMs,
                terrainBasePostFeaturePassMs,
                terrainBaseWorkerCalls,
                terrainBaseFillPassCalls,
                terrainBaseDetailPassCalls,
                terrainBasePostFeatureCalls,
                terrainBaseColumnsVisited,
                terrainBaseVerticalCellsVisited,
                terrainBaseTerrainSampleCalls,
                terrainBaseTerrainSampleMisses,
                terrainBaseBiomeResolveCalls,
                terrainBaseCaveSampleCalls,
                terrainBaseHydrologyColumns,
                terrainBaseOreDecorColumnCalls,
                terrainBaseOreDecorCellCalls,
                terrainBaseDirectColumnEnsureCalls,
                terrainBaseDirectColumnCreateCalls,
                terrainBaseWriteVoxelCalls,
                terrainBaseWriteRunCalls,
                terrainBaseWriteCellsChanged,
                terrainBaseCaveRampScanCalls,
                terrainBaseMaterializeCalls
            );
            TerrainSystemLogic::GetVoxelTerrainBaseCoarsePerfStats(
                terrainBaseFillDepthMs,
                terrainBaseFillRegularMs,
                terrainBaseFillRegularCaveScanMs,
                terrainBaseFillRangeWriteMs,
                terrainBaseDetailScanMs,
                terrainBaseDetailAirSkipped,
                terrainBaseDetailDepthCells,
                terrainBaseDetailSurfaceCells,
                terrainBaseDetailSurfaceNeighborChecks,
                terrainBaseDetailWrites
            );
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
            TerrainSystemLogic::GetVoxelColumnStreamingPerfStats(
                columnResident,
                columnRetained,
                columnEvictable,
                columnStarted,
                columnLoaded,
                columnCompleted,
                columnReleased,
                columnReleasedBeforeComplete,
                columnReleasedAfterComplete,
                columnActiveRequeued,
                columnPendingFiltered,
                columnPhase0,
                columnPhase1,
                columnPhase2,
                columnPhase3
            );
            size_t treePending = 0, treePendingDeps = 0, treeVisited = 0;
            int treeSelected = 0, treeProcessed = 0, treeDeferred = 0, treeBackfillAppended = 0;
            int treeScanned = 0, treeCandidates = 0, treePlaced = 0, treeSkipRange = 0, treeSkipNonLand = 0;
            int treeSkipGround = 0, treeBlockDeps = 0, treeBlockColumn = 0, treeBlockSpacing = 0;
            bool treeBackfillRan = false;
            float treeMs = 0.0f;
            TreeGenerationSystemLogic::GetTreeFoliagePerfStats(
                treePending,
                treePendingDeps,
                treeVisited,
                treeSelected,
                treeProcessed,
                treeDeferred,
                treeBackfillAppended,
                treeScanned,
                treeCandidates,
                treePlaced,
                treeSkipRange,
                treeSkipNonLand,
                treeSkipGround,
                treeBlockDeps,
                treeBlockColumn,
                treeBlockSpacing,
                treeBackfillRan,
                treeMs
            );
            struct VerticalStats {
                size_t count = 0;
                size_t belowFloor = 0;
                size_t belowFloorMinus32 = 0;
                int minSectionY = std::numeric_limits<int>::max();
                int maxSectionY = std::numeric_limits<int>::min();
            };
            const int sectionSize = std::max(1, voxelWorld.sectionSize);
            const int waterFloorY = (baseSystem.world && baseSystem.world->expanse.loaded)
                ? static_cast<int>(std::floor(baseSystem.world->expanse.waterFloor))
                : -96;
            const int deepCutoffY = waterFloorY - 32;
            auto addVertical = [&](VerticalStats& stats, const VoxelSectionKey& key, size_t weight = 1) {
                if (weight == 0) return;
                const int sectionBaseY = key.coord.y * sectionSize;
                stats.count += weight;
                if (sectionBaseY <= waterFloorY) {
                    stats.belowFloor += weight;
                }
                if (sectionBaseY <= deepCutoffY) {
                    stats.belowFloorMinus32 += weight;
                }
                stats.minSectionY = std::min(stats.minSectionY, key.coord.y);
                stats.maxSectionY = std::max(stats.maxSectionY, key.coord.y);
            };
            auto printVertical = [](const char* label, const VerticalStats& stats) {
                const int minY = stats.count > 0 ? stats.minSectionY : 0;
                const int maxY = stats.count > 0 ? stats.maxSectionY : 0;
                std::cout << " " << label << "Count=" << stats.count
                          << " " << label << "Range=" << minY << ".." << maxY
                          << " " << label << "BelowFloor=" << stats.belowFloor
                          << " " << label << "BelowFloorMinus32=" << stats.belowFloorMinus32;
            };
            VerticalStats desiredYStats;
            VerticalStats generatedYStats;
            VerticalStats sectionYStats;
            VerticalStats renderableYStats;
            VerticalStats meshYStats;
            VerticalStats clusterYStats;
            for (const auto& [key, section] : voxelWorld.sections) {
                (void)section;
                addVertical(sectionYStats, key);
            }
            if (baseSystem.voxelRender) {
                for (const auto& [key, buffers] : baseSystem.voxelRender->renderBuffers) {
                    (void)buffers;
                    addVertical(meshYStats, key);
                }
                for (const auto& [key, clusters] : baseSystem.voxelRender->renderClusters) {
                    addVertical(clusterYStats, key, clusters.size());
                }
                for (const auto& [key, buffers] : baseSystem.voxelRender->columnRenderBuffers) {
                    (void)buffers;
                    addVertical(meshYStats, VoxelSectionKey{glm::ivec3(key.coord.x, 0, key.coord.y)});
                }
                for (const auto& [key, clusters] : baseSystem.voxelRender->columnRenderClusters) {
                    addVertical(clusterYStats, VoxelSectionKey{glm::ivec3(key.coord.x, 0, key.coord.y)}, clusters.size());
                }
            }

            size_t meshPendingJobs = 0, meshActiveJobs = 0, meshRequestedJobs = 0, meshCompletedBuffered = 0;
            size_t meshDirtySections = 0, meshCandidates = 0, meshMissingSections = 0, meshQueued = 0;
            size_t meshSnapshots = 0, meshCompletedPopped = 0, meshCompletedAccepted = 0;
            size_t meshCompletedDropped = 0, meshOutstandingBlocked = 0, meshNeighborWaitBlocked = 0;
            size_t meshNeighborWaitWoken = 0, meshNeighborWaitResident = 0;
            size_t meshSnapshotCap = 0, meshPreparedBacklog = 0, meshUploadBacklog = 0;
            int meshSchedulerPressure = 0;
            float meshFrameSnapshotMs = 0.0f, meshLastSnapshotMs = 0.0f;
            int meshLastSnapshotY = 0, meshLastSnapshotNonAir = 0;
            uint64_t meshTotalQueued = 0, meshTotalCompleted = 0, meshTotalDropped = 0;
            uint64_t meshTotalSnapshots = 0, meshTotalOutstandingBlocked = 0, meshTotalNeighborWaitBlocked = 0;
            uint64_t meshTotalNeighborWaitWoken = 0;
            VoxelMeshingSystemLogic::GetVoxelMeshingRuntimePerfStats(
                meshPendingJobs,
                meshActiveJobs,
                meshRequestedJobs,
                meshCompletedBuffered,
                meshDirtySections,
                meshCandidates,
                meshMissingSections,
                meshQueued,
                meshSnapshots,
                meshCompletedPopped,
                meshCompletedAccepted,
                meshCompletedDropped,
                meshOutstandingBlocked,
                meshNeighborWaitBlocked,
                meshNeighborWaitWoken,
                meshNeighborWaitResident,
                meshSnapshotCap,
                meshSchedulerPressure,
                meshPreparedBacklog,
                meshUploadBacklog,
                meshFrameSnapshotMs,
                meshLastSnapshotMs,
                meshLastSnapshotY,
                meshLastSnapshotNonAir,
                meshTotalQueued,
                meshTotalCompleted,
                meshTotalDropped,
                meshTotalSnapshots,
                meshTotalOutstandingBlocked,
                meshTotalNeighborWaitBlocked,
                meshTotalNeighborWaitWoken
            );

            uint64_t meshBuildCount = 0, meshPrimaryBuiltCount = 0;
            float meshLastPrepareMs = 0.0f, meshLastPrimaryMs = 0.0f, meshLastFallbackMs = 0.0f;
            float meshAvgPrepareMs = 0.0f, meshAvgPrimaryMs = 0.0f, meshAvgFallbackMs = 0.0f;
            float meshMaxPrepareMs = 0.0f, meshMaxPrimaryMs = 0.0f, meshMaxFallbackMs = 0.0f;
            int meshLastBuildSectionSize = 0, meshLastBuildY = 0, meshLastBuildNonAir = 0;
            size_t meshLastPrimaryFaces = 0, meshLastFallbackFaces = 0;
            size_t meshLastFallbackCellsVisited = 0, meshLastFallbackRenderableBlocks = 0;
            size_t meshLastOpaqueFaces = 0, meshLastAlphaFaces = 0;
            size_t meshLastWaterSurfaceFaces = 0, meshLastWaterBodyFaces = 0;
            VoxelMeshUploadSystemLogic::GetVoxelMeshBuildPerfStats(
                meshBuildCount,
                meshPrimaryBuiltCount,
                meshLastPrepareMs,
                meshLastPrimaryMs,
                meshLastFallbackMs,
                meshAvgPrepareMs,
                meshAvgPrimaryMs,
                meshAvgFallbackMs,
                meshMaxPrepareMs,
                meshMaxPrimaryMs,
                meshMaxFallbackMs,
                meshLastBuildSectionSize,
                meshLastBuildY,
                meshLastBuildNonAir,
                meshLastPrimaryFaces,
                meshLastFallbackFaces,
                meshLastFallbackCellsVisited,
                meshLastFallbackRenderableBlocks,
                meshLastOpaqueFaces,
                meshLastAlphaFaces,
                meshLastWaterSurfaceFaces,
                meshLastWaterBodyFaces
            );

            float meshUploadLastMs = 0.0f, meshUploadBudgetMs = 0.0f;
            float meshUploadSplitMs = 0.0f, meshUploadStageMs = 0.0f, meshUploadPublishMs = 0.0f;
            size_t meshUploadLastSections = 0, meshUploadPendingBefore = 0, meshUploadPendingAfter = 0;
            size_t meshUploadCandidates = 0, meshUploadClusters = 0;
            size_t meshUploadFaces = 0, meshUploadBytes = 0, meshUploadBuffers = 0;
            size_t meshUploadPackedFaces = 0, meshUploadPackedBytes = 0;
            int meshUploadCapSections = 0;
            bool meshUploadBootstrapActive = false;
            bool meshUploadHardCapApplied = false;
            bool meshUploadBudgetOverrun = false;
            bool meshUploadClusterSplitEnabled = true;
            float meshUploadBudgetOverrunMs = 0.0f;
            uint64_t meshUploadTotalSections = 0, meshUploadTotalClusters = 0;
            VoxelMeshUploadSystemLogic::GetVoxelMeshUploadPerfStats(
                meshUploadLastMs,
                meshUploadLastSections,
                meshUploadPendingBefore,
                meshUploadPendingAfter,
                meshUploadCandidates,
                meshUploadClusters,
                meshUploadFaces,
                meshUploadBytes,
                meshUploadBuffers,
                meshUploadPackedFaces,
                meshUploadPackedBytes,
                meshUploadCapSections,
                meshUploadBudgetMs,
                meshUploadSplitMs,
                meshUploadStageMs,
                meshUploadPublishMs,
                meshUploadBootstrapActive,
                meshUploadHardCapApplied,
                meshUploadBudgetOverrun,
                meshUploadClusterSplitEnabled,
                meshUploadBudgetOverrunMs,
                meshUploadTotalSections,
                meshUploadTotalClusters
            );
            size_t lifeDesired = 0;
            size_t lifeBaseQueued = 0;
            size_t lifeBaseInProgress = 0;
            size_t lifeBaseGenerated = 0;
            size_t lifeFeatureQueued = 0;
            size_t lifeFeatureInProgress = 0;
            size_t lifeSurfacePending = 0;
            size_t lifeReady = 0;
            size_t lifeRenderable = 0;
            size_t lifeReadyNoMesh = 0;
            for (const auto& [key, state] : voxelWorld.chunkStates) {
                if (state.desired) {
                    addVertical(desiredYStats, key);
                }
                if (state.generated) {
                    addVertical(generatedYStats, key);
                }
                if (state.isRenderable()) {
                    addVertical(renderableYStats, key);
                }
                if (!state.desired) continue;
                lifeDesired += 1;
                switch (state.stage) {
                    case VoxelChunkLifecycleStage::BaseQueued:
                        lifeBaseQueued += 1;
                        break;
                    case VoxelChunkLifecycleStage::BaseInProgress:
                        lifeBaseInProgress += 1;
                        break;
                    case VoxelChunkLifecycleStage::BaseGenerated:
                        lifeBaseGenerated += 1;
                        break;
                    case VoxelChunkLifecycleStage::FeatureQueued:
                        lifeFeatureQueued += 1;
                        break;
                    case VoxelChunkLifecycleStage::FeatureInProgress:
                        lifeFeatureInProgress += 1;
                        break;
                    case VoxelChunkLifecycleStage::Ready:
                        lifeReady += 1;
                        break;
                    case VoxelChunkLifecycleStage::Desired:
                    case VoxelChunkLifecycleStage::Unknown:
                        break;
                }
                if (state.generated && state.postFeaturesComplete && state.hasSection && !state.surfaceFoliageComplete) {
                    lifeSurfacePending += 1;
                }
                if (state.isRenderable()) {
                    lifeRenderable += 1;
                    if (!baseSystem.voxelRender || baseSystem.voxelRender->renderBuffers.count(key) == 0) {
                        lifeReadyNoMesh += 1;
                    }
                }
            }
            std::cout << "[VoxelPerf] dirty=" << (voxelWorld.dirtySections.size() + voxelWorld.dirtyColumns.size())
                      << " voxelRenderDirty=" << voxelRenderDirtyCount
                      << " terrainPending=" << terrainPending
                      << " terrainDesired=" << terrainDesired
                      << " terrainGenerated=" << terrainGenerated
                      << " terrainJobs=" << terrainJobs
                      << " terrainStepped=" << terrainStepped
                      << " terrainBuilt=" << terrainBuilt
                      << " terrainConsumed=" << terrainConsumed
                      << " terrainSkipped=" << terrainSkipped
                      << " terrainFiltered=" << terrainFiltered
                      << " terrainRescue=" << terrainRescueSurface << "/" << terrainRescueMissing
                      << " terrainCapDrop=" << terrainCapDrop
                      << " terrainReprio=" << terrainReprio
                      << " terrainPrepMs=" << terrainPrepMs
                      << " terrainMs=" << terrainMs
                      << " terrainDesiredMs=" << terrainDesiredMs
                      << " terrainBaseMs=" << terrainBaseMs
                      << " terrainFeatureMs=" << terrainFeatureMs
                      << " terrainSurfaceMs=" << terrainSurfaceMs
                      << " terrainCaveFieldMs=" << terrainCaveFieldMs
                      << " terrainCaveFieldCells=" << terrainCaveFieldCellsBuilt
                      << " terrainCaveSamples=" << terrainCaveSamples
                      << " terrainCaveFieldMode=" << (terrainCaveFieldMode == 1 ? "perlin" : "binary")
                      << " terrainCaveFieldCache=" << terrainBinaryCaveTiles
                      << "/" << terrainPerlinCaveTiles
                      << " terrainCaveFieldBuiltTotal=" << terrainCaveFieldTotalTilesBuilt
                      << "/" << terrainCaveFieldTotalCellsBuilt
                      << " terrainCaveRamps=" << terrainCaveRampCount << "/" << terrainCaveRampBlockWrites
                      << " terrainCaveRampCandidates="
                      << terrainCaveRampSections
                      << "/" << terrainCaveRampPocketCandidates
                      << "/" << terrainCaveRampValidatorCalls
                      << "/" << terrainCaveRampVolumeDirs
                      << "/" << terrainCaveRampSupportDirs
                      << " terrainWorkerSetupMs=" << terrainWorkerSetupMs
                      << " terrainWorkerColumnMs=" << terrainWorkerColumnMs
                      << " terrainPublishMs=" << terrainPublishMs
                      << " terrainBaseHotTimers=0"
                      << " terrainBaseBreakdownMs=setup:" << terrainBaseSetupBreakdownMs
                      << ",caveField:" << terrainBaseCaveFieldPrepMs
                      << ",surfaceBiome:" << terrainBaseSurfaceBiomeMs
                      << ",hydro:" << terrainBaseHydrologyMs
                      << ",cave:" << terrainBaseCaveMs
                      << ",oreDecor:" << terrainBaseOreDecorMs
                      << ",memory:" << terrainBaseMemoryMs
                      << ",writes:" << terrainBaseBlockWriteMs
                      << ",ramps:" << terrainBaseCaveRampMs
                      << ",book:" << terrainBaseBookkeepingMs
                      << ",materialize:" << terrainBaseMaterializeMs
                      << " terrainBasePassMs=fill:" << terrainBaseFillPassMs
                      << ",detail:" << terrainBaseDetailPassMs
                      << ",post:" << terrainBasePostFeaturePassMs
                      << " terrainBaseCoarseMs=fillDepth:" << terrainBaseFillDepthMs
                      << ",fillRegular:" << terrainBaseFillRegularMs
                      << ",fillCaveScan:" << terrainBaseFillRegularCaveScanMs
                      << ",fillWrites:" << terrainBaseFillRangeWriteMs
                      << ",detailScan:" << terrainBaseDetailScanMs
                      << " terrainBaseBreakdownCalls=worker:" << terrainBaseWorkerCalls
                      << ",fill:" << terrainBaseFillPassCalls
                      << ",detail:" << terrainBaseDetailPassCalls
                      << ",post:" << terrainBasePostFeatureCalls
                      << ",columns:" << terrainBaseColumnsVisited
                      << ",cells:" << terrainBaseVerticalCellsVisited
                      << ",terrainSamples:" << terrainBaseTerrainSampleCalls
                      << "/" << terrainBaseTerrainSampleMisses
                      << ",biome:" << terrainBaseBiomeResolveCalls
                      << ",caveSamples:" << terrainBaseCaveSampleCalls
                      << ",hydroColumns:" << terrainBaseHydrologyColumns
                      << ",oreColumns:" << terrainBaseOreDecorColumnCalls
                      << ",oreCells:" << terrainBaseOreDecorCellCalls
                      << ",ensure:" << terrainBaseDirectColumnEnsureCalls
                      << "/" << terrainBaseDirectColumnCreateCalls
                      << ",writes:" << terrainBaseWriteVoxelCalls
                      << "/" << terrainBaseWriteRunCalls
                      << "/" << terrainBaseWriteCellsChanged
                      << ",ramps:" << terrainBaseCaveRampScanCalls
                      << ",materialize:" << terrainBaseMaterializeCalls
                      << " terrainBaseDetailCounts=air:" << terrainBaseDetailAirSkipped
                      << ",depth:" << terrainBaseDetailDepthCells
                      << ",surface:" << terrainBaseDetailSurfaceCells
                      << ",neighbors:" << terrainBaseDetailSurfaceNeighborChecks
                      << ",writes:" << terrainBaseDetailWrites
                      << " terrainLoopMs=maint:" << terrainMaintenanceMs
                      << ",pop:" << terrainPendingPopMs
                      << ",save:" << terrainSaveProbeMs
                      << ",release:" << terrainReleaseMs
                      << ",step:" << terrainStepMs
                      << ",requeue:" << terrainRequeueMs
                      << " terrainSched=pressure:" << terrainSchedulerPressure
                      << ",bud:" << terrainDesiredBudget
                      << "/" << terrainBaseBudget
                      << "/" << terrainFeatureBudget
                      << "/" << terrainSurfaceBudget
                      << ",ms:" << terrainBaseBudgetMs
                      << "/" << terrainFeatureBudgetMs
                      << "/" << terrainSurfaceBudgetMs
                      << ",down:" << terrainDownstreamDirty
                      << "/" << terrainDownstreamPrepared
                      << "/" << terrainDownstreamUpload
                      << " columnResident=" << columnResident
                      << " columnRetained=" << columnRetained
                      << " columnEvictable=" << columnEvictable
                      << " columnStarted=" << columnStarted
                      << " columnLoaded=" << columnLoaded
                      << " columnCompleted=" << columnCompleted
                      << " columnReleased=" << columnReleased
                      << " columnReleasedBefore=" << columnReleasedBeforeComplete
                      << " columnReleasedAfter=" << columnReleasedAfterComplete
                      << " columnRequeued=" << columnActiveRequeued
                      << " columnPendingFiltered=" << columnPendingFiltered
                      << " columnPhases=" << columnPhase0
                      << "/" << columnPhase1
                      << "/" << columnPhase2
                      << "/" << columnPhase3
                      << " treePending=" << treePending
                      << " treePendingDeps=" << treePendingDeps
                      << " treeVisited=" << treeVisited
                      << " treeSelected=" << treeSelected
                      << " treeProcessed=" << treeProcessed
                      << " treeDeferred=" << treeDeferred
                      << " treeBackfill=" << (treeBackfillRan ? treeBackfillAppended : 0)
                      << " treeScanned=" << treeScanned
                      << " treeCandidates=" << treeCandidates
                      << " treePlaced=" << treePlaced
                      << " treeSkipRange=" << treeSkipRange
                      << " treeSkipNonLand=" << treeSkipNonLand
                      << " treeSkipGround=" << treeSkipGround
                      << " treeBlockDeps=" << treeBlockDeps
                      << " treeBlockColumn=" << treeBlockColumn
                      << " treeBlockSpacing=" << treeBlockSpacing
                      << " treeMs=" << treeMs
                      << " sections=" << voxelWorld.sections.size()
                      << " columns=" << voxelWorld.columns.size()
                      << " renderMeshes=" << (baseSystem.voxelRender
                          ? baseSystem.voxelRender->renderBuffers.size()
                              + baseSystem.voxelRender->columnRenderBuffers.size()
                          : 0)
                      << " wireframeNearFaces=" << (baseSystem.voxelRender ? baseSystem.voxelRender->wireframeOverlayNearFaces : 0)
                      << " wireframeFarFaces=" << (baseSystem.voxelRender ? baseSystem.voxelRender->wireframeOverlayFarFaces : 0)
                      << " wireframeSegments=" << (baseSystem.voxelRender ? baseSystem.voxelRender->wireframeOverlayLineSegments : 0)
	                      << " lifeDesired=" << lifeDesired
	                      << " lifeBaseQ=" << lifeBaseQueued
	                      << " lifeBaseIP=" << lifeBaseInProgress
                      << " lifeBaseGen=" << lifeBaseGenerated
                      << " lifeFeatQ=" << lifeFeatureQueued
                      << " lifeFeatIP=" << lifeFeatureInProgress
	                      << " lifeSurfPend=" << lifeSurfacePending
                      << " lifeReady=" << lifeReady
                      << " lifeRenderable=" << lifeRenderable
                      << " lifeReadyNoMesh=" << lifeReadyNoMesh;
            std::cout << " yFloor=" << waterFloorY
                      << " yDeepCutoff=" << deepCutoffY;
            printVertical("desiredY", desiredYStats);
            printVertical("generatedY", generatedYStats);
            printVertical("sectionY", sectionYStats);
            printVertical("renderableY", renderableYStats);
            printVertical("meshY", meshYStats);
            printVertical("clusterY", clusterYStats);
            std::cout << " meshJobs=" << meshPendingJobs << "/" << meshActiveJobs
                      << "/" << meshRequestedJobs << "/" << meshCompletedBuffered
                      << " meshFrame=dirty:" << meshDirtySections
                      << ",cand:" << meshCandidates
                      << ",missing:" << meshMissingSections
                      << ",queued:" << meshQueued
	                      << ",snap:" << meshSnapshots
                          << ",cap:" << meshSnapshotCap
                          << ",pressure:" << meshSchedulerPressure
                          << ",backlog:" << meshPreparedBacklog
                          << "/" << meshUploadBacklog
	                      << ",popped:" << meshCompletedPopped
                      << ",accepted:" << meshCompletedAccepted
                      << ",dropped:" << meshCompletedDropped
                      << ",blocked:" << meshOutstandingBlocked
                      << ",neighborWait:" << meshNeighborWaitBlocked
                      << ",neighborWake:" << meshNeighborWaitWoken
                      << ",neighborResident:" << meshNeighborWaitResident
                      << " meshSnapMs=" << meshFrameSnapshotMs << "/" << meshLastSnapshotMs
                      << " meshLastSnapY=" << meshLastSnapshotY
                      << " meshLastSnapNonAir=" << meshLastSnapshotNonAir
                      << " meshTotals=q:" << meshTotalQueued
                      << ",done:" << meshTotalCompleted
                      << ",drop:" << meshTotalDropped
                      << ",snap:" << meshTotalSnapshots
                      << ",blocked:" << meshTotalOutstandingBlocked
                      << ",neighborWait:" << meshTotalNeighborWaitBlocked
                      << ",neighborWake:" << meshTotalNeighborWaitWoken
	                      << " meshBuilds=" << meshBuildCount << "/" << meshPrimaryBuiltCount
	                      << " meshBuildMs=" << meshLastPrepareMs << "/" << meshAvgPrepareMs
                      << "/" << meshMaxPrepareMs
                      << " meshPrimaryMs=" << meshLastPrimaryMs << "/" << meshAvgPrimaryMs
                      << "/" << meshMaxPrimaryMs
                      << " meshFallbackMs=" << meshLastFallbackMs << "/" << meshAvgFallbackMs
                      << "/" << meshMaxFallbackMs
                      << " meshLastBuild=size:" << meshLastBuildSectionSize
                      << ",y:" << meshLastBuildY
                      << ",nonAir:" << meshLastBuildNonAir
                      << " meshFaces=primary:" << meshLastPrimaryFaces
                      << ",fallback:" << meshLastFallbackFaces
                      << ",opaque:" << meshLastOpaqueFaces
                      << ",alpha:" << meshLastAlphaFaces
                      << ",waterSurface:" << meshLastWaterSurfaceFaces
                      << ",waterBody:" << meshLastWaterBodyFaces
                      << " meshFallbackScan=" << meshLastFallbackCellsVisited
                      << "/" << meshLastFallbackRenderableBlocks
                      << " meshUpload=uploaded:" << meshUploadLastSections
                      << ",pending:" << meshUploadPendingBefore << "->" << meshUploadPendingAfter
                      << ",cand:" << meshUploadCandidates
                      << ",clusters:" << meshUploadClusters
                      << ",ms:" << meshUploadLastMs
	                      << ",cap:" << meshUploadCapSections
	                      << ",budget:" << meshUploadBudgetMs
	                      << ",bootstrap:" << (meshUploadBootstrapActive ? 1 : 0)
	                          << ",hardCap:" << (meshUploadHardCapApplied ? 1 : 0)
	                          << ",overrun:" << (meshUploadBudgetOverrun ? meshUploadBudgetOverrunMs : 0.0f)
                              << ",clusterSplit:" << (meshUploadClusterSplitEnabled ? 1 : 0)
                              << ",detailMs:" << meshUploadSplitMs
                              << "/" << meshUploadStageMs
                              << "/" << meshUploadPublishMs
                              << ",detailWork:" << meshUploadFaces
                              << "/" << meshUploadBuffers
                              << "/" << meshUploadBytes
                              << ",detailPacked:" << meshUploadPackedFaces
                              << "/" << meshUploadPackedBytes
		                      << ",total:" << meshUploadTotalSections
	                      << "/" << meshUploadTotalClusters;
	            if (baseSystem.farTerrain) {
                const FarTerrainClipmapContext& far = *baseSystem.farTerrain;
                std::cout << " farEnabled=" << far.enabled
                          << " farInitialized=" << far.initialized
                          << " farRebuilds=" << far.rebuildCount
                          << " farLastFull=" << far.lastFullRebuild
                          << " farLastHandoff=" << far.lastHandoffRefresh
                          << " farFaces=" << far.visibleFaceCount
                          << " farHandoffFaces=" << far.handoffVisibleFaceCount
                          << " farBodyFaces=" << far.bodyVisibleFaceCount
                          << " farHandoffClusters=" << far.handoffRenderClusters.size()
                          << " farBodyClusters=" << far.bodyRenderClusters.size()
                          << " farCells=" << far.lastLandCellCount
                          << " farHandoffCells=" << far.lastHandoffCellCount
                          << " farBodyCells=" << far.lastBodyCellCount
                          << " farSuppressed=" << far.lastSuppressedCellCount
                          << " farBaseCell=" << far.baseCellSize
                          << " farNear=" << far.nearRadiusBlocks
                          << " farMax=" << far.maxRadiusBlocks
                          << " farRings=" << far.ringCount
                          << " farVisibleRings=" << far.visibleRingCount
                          << " farLastParamsChanged=" << far.lastParamsChanged
                          << " farLastAnchorChanged=" << far.lastAnchorChanged
                          << " farLastCoverageChanged=" << far.lastCoverageChanged
                          << " farLastPeriodicDue=" << far.lastPeriodicRefreshDue
                          << " farLastViewBucketChanged=" << far.lastViewBucketChanged
                          << " farLastAnchorDelta=" << far.lastAnchorDeltaX << "," << far.lastAnchorDeltaZ
                          << " farUpdateMs=" << far.lastUpdateMs
                          << " farSetupMs=" << far.lastSetupMs
                          << " farBodyBuildMs=" << far.lastBodyBuildMs
                          << " farHandoffMs=" << far.lastHandoffBuildMs
                          << " farUploadMs=" << far.lastUploadMs
                          << " farCellResolveMs=" << far.lastCellResolveMs
                          << " farTopGreedyMs=" << far.lastTopGreedyMs
                          << " farTopMergeMs=" << far.lastTopMergeMs
                          << " farVerticalMs=" << far.lastVerticalBuildMs
                          << " farClusterPrepMs=" << far.lastClusterPrepMs
                          << " farClusterUploadMs=" << far.lastClusterUploadMs
                          << " farUploadOnlyCount=" << far.uploadOnlyCount
                          << " farHandoffRefreshCount=" << far.handoffRefreshCount
                          << " farHandoffRefreshByCoverage=" << far.handoffRefreshByCoverageCount
                          << " farHandoffRefreshByPeriodic=" << far.handoffRefreshByPeriodicCount
                          << " farRebuildByParams=" << far.fullRebuildByParamsCount
                          << " farRebuildByAnchor=" << far.fullRebuildByAnchorCount
                          << " farRebuildByCoverage=" << far.fullRebuildByCoverageCount
                          << " farRebuildByView=" << far.fullRebuildByViewCount
                          << " farSectorRingTests=" << far.sectorRingTests
                          << " farSectorRingRejected=" << far.sectorRingRejected
                          << " farSectorCellTests=" << far.sectorCellTests
                          << " farSectorCellRejected=" << far.sectorCellRejected
                          << " farFrustumRingTests=" << far.frustumRingTests
                          << " farFrustumRingRejected=" << far.frustumRingRejected
                          << " farFrustumCellTests=" << far.frustumCellTests
                          << " farFrustumCellRejected=" << far.frustumCellRejected
                          << " farLodCells=";
                for (size_t i = 0; i < far.lodCellCounts.size(); ++i) {
                    if (i > 0) std::cout << ",";
                    std::cout << far.lodCellCounts[i];
                }
                std::cout << " farLodFaces=";
                for (size_t i = 0; i < far.lodFaceCounts.size(); ++i) {
                    if (i > 0) std::cout << ",";
                    std::cout << far.lodFaceCounts[i];
                }
                std::cout << " farLodTriangles=";
                for (size_t i = 0; i < far.lodTriangleCounts.size(); ++i) {
                    if (i > 0) std::cout << ",";
                    std::cout << far.lodTriangleCounts[i];
                }
            }
            if (baseSystem.occlusionCulling) {
                const OcclusionCullingContext& occlusion = *baseSystem.occlusionCulling;
                std::cout << " occTested=" << occlusion.testedSectionCount
                          << " occVisible=" << occlusion.visibleSectionCount
                          << " occOccluded=" << occlusion.occludedSectionCount
                          << " occNearKept=" << occlusion.nearKeptSectionCount
                          << " occFrustumRejected=" << occlusion.frustumRejectedSectionCount
                          << " occFrozen=" << (occlusion.debugFrozen ? 1 : 0)
                          << " occHzbValid=" << (occlusion.hzbValid ? 1 : 0)
                          << " occHzbOccluders=" << occlusion.hzbOccluderClusterCount
                          << " occHzbCaptureMs=" << occlusion.hzbCaptureMs
                          << " occHzbReadbackMs=" << occlusion.hzbReadbackMs
                          << " occHzbBuildMs=" << occlusion.hzbBuildMs
                          << " occHzbQueryMs=" << occlusion.hzbQueryMs
                          << " farOccTested=" << occlusion.farTestedCount
                          << " farOccOccluded=" << occlusion.farOccludedCount;
            }
            std::cout << std::endl;
            lastPerfLog = now;
        }

        if (::RenderInitSystemLogic::getRegistryBool(baseSystem, "DebugVoxelMesh", false)) {
            static auto lastDebugLog = std::chrono::steady_clock::now();
            auto nowDbg = std::chrono::steady_clock::now();
            if (nowDbg - lastDebugLog >= std::chrono::seconds(1)) {
                size_t worldDirty = baseSystem.voxelWorld ? baseSystem.voxelWorld->dirtySections.size() : 0;
                size_t renderDirty = baseSystem.voxelRender ? baseSystem.voxelRender->renderBuffersDirty.size() : 0;
                std::cout << "[DebugVoxelMesh] worldDirty=" << worldDirty
                          << " renderDirty=" << renderDirty
                          << std::endl;
                lastDebugLog = nowDbg;
            }
        }
    }
}
