        void writeCaveSlopeToSection(const std::vector<Entity>& prototypes,
                                     const WorldContext& worldCtx,
                                     VoxelWorldContext& voxelWorld,
                                     int sectionTier,
                                     const glm::ivec3& sectionCoord,
                                     int sectionSize,
                                     int sectionScale,
                                     int slopeProtoPosX,
                                     int slopeProtoNegX,
                                     int slopeProtoPosZ,
                                     int slopeProtoNegZ,
                                     const FoliageSpec& spec,
                                     bool& modified) {
            if (!spec.enabled || !spec.caveSlopeEnabled) return;
            const bool hasAnySlopePrototype =
                (slopeProtoPosX >= 0)
                || (slopeProtoNegX >= 0)
                || (slopeProtoPosZ >= 0)
                || (slopeProtoNegZ >= 0);
            if (!hasAnySlopePrototype) return;

            const int slopeChance = std::max(0, std::min(100, spec.caveSlopeSpawnPercent));
            if (slopeChance <= 0) return;
            const int minDepthFromSurface = std::max(1, std::min(256, spec.caveSlopeMinDepthFromSurface));
            const int minDepthFromSurfaceTier = std::max(
                1,
                static_cast<int>(std::ceil(static_cast<float>(minDepthFromSurface) / static_cast<float>(sectionScale)))
            );

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;
            auto inSection = [&](const glm::ivec3& cell) {
                return cell.x >= minX && cell.x <= maxX
                    && cell.y >= minY && cell.y <= maxY
                    && cell.z >= minZ && cell.z <= maxZ;
            };

            auto isWallStonePrototypeID = [&](uint32_t id) {
                if (id == 0u || id >= prototypes.size()) return false;
                return isWallStonePrototypeName(prototypes[id].name);
            };
            auto isSolidSupportCell = [&](const glm::ivec3& cell) {
                const uint32_t id = getBlockAt(voxelWorld, cell);
                if (id == 0u || id >= prototypes.size()) return false;
                const Entity& proto = prototypes[id];
                if (!proto.isBlock || !proto.isSolid) return false;
                return proto.name != "Water";
            };
            auto isOpenPocketCell = [&](const glm::ivec3& cell) {
                const uint32_t id = getBlockAt(voxelWorld, cell);
                if (id == 0u) return true;
                if (id >= prototypes.size()) return false;
                return isWallStonePrototypeName(prototypes[id].name);
            };

            const int kNoSurface = std::numeric_limits<int>::min();
            std::vector<int> surfaceHeights(static_cast<size_t>(sectionSize * sectionSize), kNoSurface);
            auto surfaceIdx = [&](int tierX, int tierZ) {
                return (tierZ - minZ) * sectionSize + (tierX - minX);
            };

            for (int tierZ = minZ; tierZ <= maxZ; ++tierZ) {
                for (int tierX = minX; tierX <= maxX; ++tierX) {
                    const int worldX = tierX * sectionScale;
                    const int worldZ = tierZ * sectionScale;
                    float terrainHeight = 0.0f;
                    if (!ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ),
                            terrainHeight)) {
                        continue;
                    }
                    surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))] =
                        floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                }
            }
            for (int tierZ = minZ; tierZ <= maxZ; ++tierZ) {
                for (int tierX = minX; tierX <= maxX; ++tierX) {
                    const int worldX = tierX * sectionScale;
                    const int worldZ = tierZ * sectionScale;
                    const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))];
                    if (surfaceY == kNoSurface) continue;
                    const int maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceTier);
                    if (maxPlaceY < minY) continue;

                    for (int tierY = minY; tierY <= maxPlaceY; ++tierY) {
                        const int worldY = tierY * sectionScale;
                        const glm::ivec3 cell(tierX, tierY, tierZ);
                        if (!isOpenPocketCell(cell)) continue;

                        const uint32_t seed = hash3D(worldX, worldY, worldZ);
                        if (static_cast<int>(seed % 100u) >= slopeChance) continue;

                        struct RampCandidate {
                            CaveSlopeDir dir = CaveSlopeDir::None;
                            uint32_t solidID = 0u;
                        };
                        std::array<RampCandidate, 4> candidateDirs{};
                        int candidateDirCount = 0;
                        auto addCandidateDir = [&](CaveSlopeDir dir, uint32_t solidID) {
                            if (dir == CaveSlopeDir::None || candidateDirCount >= static_cast<int>(candidateDirs.size())) return;
                            candidateDirs[static_cast<size_t>(candidateDirCount)] = RampCandidate{dir, solidID};
                            candidateDirCount += 1;
                        };

                        const std::array<CaveSlopeDir, 4> dirs = {
                            CaveSlopeDir::PosX,
                            CaveSlopeDir::NegX,
                            CaveSlopeDir::PosZ,
                            CaveSlopeDir::NegZ
                        };
                        for (CaveSlopeDir dir : dirs) {
                            const glm::ivec3 lowDir = caveSlopeLowDirection(dir);
                            const glm::ivec3 perpDir = caveSlopePerpDirection(dir);
                            if (lowDir == glm::ivec3(0) || perpDir == glm::ivec3(0)) continue;

                            bool volumeOpen = true;
                            for (int depth = 0; depth < 3 && volumeOpen; ++depth) {
                                for (int yStep = 0; yStep < 3 && volumeOpen; ++yStep) {
                                    for (int lateral = -1; lateral <= 1; ++lateral) {
                                        const glm::ivec3 volumeCell =
                                            cell - lowDir * depth + perpDir * lateral + glm::ivec3(0, yStep, 0);
                                        if (!inSection(volumeCell) || !isOpenPocketCell(volumeCell)) {
                                            volumeOpen = false;
                                            break;
                                        }
                                    }
                                }
                            }
                            if (!volumeOpen) continue;

                            bool hasBackWall = true;
                            uint32_t solidID = 0u;
                            for (int yStep = 0; yStep < 3 && hasBackWall; ++yStep) {
                                for (int lateral = -1; lateral <= 1; ++lateral) {
                                    const glm::ivec3 wallCell =
                                        cell - lowDir * 3 + perpDir * lateral + glm::ivec3(0, yStep, 0);
                                    if (!inSection(wallCell) || !isSolidSupportCell(wallCell)) {
                                        hasBackWall = false;
                                        break;
                                    }
                                    if (solidID == 0u) {
                                        solidID = getBlockAt(voxelWorld, wallCell);
                                    }
                                }
                            }
                            if (!hasBackWall || solidID == 0u) continue;

                            addCandidateDir(dir, solidID);
                        }
                        if (candidateDirCount <= 0) continue;
                        const RampCandidate candidate = candidateDirs[static_cast<size_t>(
                            (seed >> 8u) % static_cast<uint32_t>(candidateDirCount)
                        )];
                        const CaveSlopeDir slopeDir = candidate.dir;
                        if (slopeDir == CaveSlopeDir::None) continue;
                        const int slopeID = caveSlopePrototypeForDir(slopeDir, slopeProtoPosX, slopeProtoNegX, slopeProtoPosZ, slopeProtoNegZ);
                        if (slopeID < 0) continue;
                        const uint32_t solidStoneID = candidate.solidID;
                        if (solidStoneID == 0u || solidStoneID >= prototypes.size()) continue;

                        const glm::ivec3 lowDir = caveSlopeLowDirection(slopeDir);
                        const glm::ivec3 perpDir = caveSlopePerpDirection(slopeDir);
                        auto canReplaceWithSlope = [&](const glm::ivec3& target) {
                            return inSection(target) && isOpenPocketCell(target);
                        };
                        struct PriorCellState {
                            glm::ivec3 pos{0};
                            uint32_t id = 0u;
                            uint32_t color = 0u;
                        };
                        std::vector<PriorCellState> placedSlopePriorStates;
                        std::unordered_set<glm::ivec3, IVec3Hash> placedSlopeCells;
                        auto writeRampCell = [&](const glm::ivec3& target, uint32_t id, uint32_t color) -> bool {
                            if (!canReplaceWithSlope(target)) return false;
                            if (placedSlopeCells.insert(target).second) {
                                placedSlopePriorStates.push_back(
                                    PriorCellState{
                                        target,
                                        getBlockAt(voxelWorld, target),
                                        getColorAt(voxelWorld, target)
                                    }
                                );
                            }
                            voxelWorld.setBlock(target,
                                id,
                                color,
                                false);
                            modified = true;
                            return true;
                        };

                        bool rampAccepted = true;
                        for (int depth = 0; depth < 3 && rampAccepted; ++depth) {
                            for (int yStep = 0; yStep < 3 && rampAccepted; ++yStep) {
                                for (int lateral = -1; lateral <= 1; ++lateral) {
                                    const glm::ivec3 target =
                                        cell - lowDir * depth + perpDir * lateral + glm::ivec3(0, yStep, 0);
                                    const uint32_t color = caveStoneColorForCell(
                                        target.x * sectionScale,
                                        target.y * sectionScale,
                                        target.z * sectionScale
                                    );
                                    if (yStep == depth) {
                                        if (!writeRampCell(target, static_cast<uint32_t>(slopeID), color)) {
                                            rampAccepted = false;
                                            break;
                                        }
                                    } else if (yStep < depth) {
                                        if (!writeRampCell(target, solidStoneID, color)) {
                                            rampAccepted = false;
                                            break;
                                        }
                                    } else {
                                        const uint32_t existingID = getBlockAt(voxelWorld, target);
                                        if (isWallStonePrototypeID(existingID)) {
                                            if (!writeRampCell(target, 0u, 0u)) {
                                                rampAccepted = false;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }

                        if (!rampAccepted) {
                            for (const auto& prior : placedSlopePriorStates) {
                                voxelWorld.setBlock(prior.pos, prior.id, prior.color, false);
                            }
                            modified = true;
                        }
                    }
                }
            }
        }

        void writeCaveStoneToSection(const std::vector<Entity>& prototypes,
                                     const WorldContext& worldCtx,
                                     VoxelWorldContext& voxelWorld,
                                     int sectionTier,
                                     const glm::ivec3& sectionCoord,
                                     int sectionSize,
                                     int sectionScale,
                                     int stonePrototypeIDX,
                                     int stonePrototypeIDZ,
                                     int waterPrototypeID,
                                     const FoliageSpec& spec,
                                     bool& modified) {
            if (!spec.enabled || !spec.caveStoneEnabled) return;
            const bool hasAnyStonePrototype = (stonePrototypeIDX >= 0 || stonePrototypeIDZ >= 0);
            if (!hasAnyStonePrototype) return;
            const int stoneChance = std::max(0, std::min(100, spec.caveStoneSpawnPercent));
            if (stoneChance <= 0) return;

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;
            const int stoneNearWaterRadiusTier = std::max(
                1,
                (std::max(1, spec.pebblePatchNearWaterRadius) + sectionScale - 1) / sectionScale
            );
            const int stoneNearWaterVerticalRangeTier = std::max(
                0,
                (std::max(0, spec.pebblePatchNearWaterVerticalRange) + sectionScale - 1) / sectionScale
            );
            auto isNearSurfaceWater = [&](const glm::ivec3& centerCell) {
                if (waterPrototypeID < 0) return false;
                const int radiusSq = stoneNearWaterRadiusTier * stoneNearWaterRadiusTier;
                for (int dz = -stoneNearWaterRadiusTier; dz <= stoneNearWaterRadiusTier; ++dz) {
                    for (int dx = -stoneNearWaterRadiusTier; dx <= stoneNearWaterRadiusTier; ++dx) {
                        if ((dx * dx + dz * dz) > radiusSq) continue;
                        for (int dy = -stoneNearWaterVerticalRangeTier; dy <= stoneNearWaterVerticalRangeTier; ++dy) {
                            const glm::ivec3 probe(centerCell.x + dx, centerCell.y + dy, centerCell.z + dz);
                            if (getBlockAt(voxelWorld, probe) != static_cast<uint32_t>(waterPrototypeID)) {
                                continue;
                            }
                            if (getBlockAt(voxelWorld, probe + glm::ivec3(0, 1, 0)) == 0u) {
                                return true;
                            }
                        }
                    }
                }
                return false;
            };

            const int kNoSurface = std::numeric_limits<int>::min();
            std::vector<int> surfaceHeights(static_cast<size_t>(sectionSize * sectionSize), kNoSurface);
            auto surfaceIdx = [&](int tierX, int tierZ) {
                return (tierZ - minZ) * sectionSize + (tierX - minX);
            };

            for (int tierZ = minZ; tierZ <= maxZ; ++tierZ) {
                for (int tierX = minX; tierX <= maxX; ++tierX) {
                    const int worldX = tierX * sectionScale;
                    const int worldZ = tierZ * sectionScale;
                    float terrainHeight = 0.0f;
                    if (!ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ),
                            terrainHeight)) {
                        continue;
                    }
                    surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))] =
                        floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                }
            }

            for (int tierZ = minZ; tierZ <= maxZ; ++tierZ) {
                for (int tierX = minX; tierX <= maxX; ++tierX) {
                    const int worldX = tierX * sectionScale;
                    const int worldZ = tierZ * sectionScale;
                    const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(
                        worldCtx,
                        static_cast<float>(worldX),
                        static_cast<float>(worldZ)
                    );
                    if (biomeID == 2) continue;
                    const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))];
                    if (surfaceY == kNoSurface) continue;
                    const int tierY = surfaceY + 1;
                    if (tierY < minY || tierY > maxY) continue;
                    const int worldY = tierY * sectionScale;

                    const uint32_t seed = hash3D(worldX, worldY, worldZ);
                    if (static_cast<int>(seed % 100u) >= stoneChance) continue;

                    const glm::ivec3 placeCell(tierX, tierY, tierZ);
                    if (!isNearSurfaceWater(placeCell)) continue;
                    const uint32_t existingID = getBlockAt(voxelWorld, placeCell);
                    if (existingID != 0u) {
                        bool replaceGrass = false;
                        if (existingID < prototypes.size()) {
                            const std::string& existingName = prototypes[existingID].name;
                            replaceGrass = isGrassFoliagePrototypeName(existingName);
                        }
                        if (!replaceGrass) continue;
                    }

                    const glm::ivec3 aboveCell(tierX, tierY + 1, tierZ);
                    if (getBlockAt(voxelWorld, aboveCell) != 0u) continue;

                    const glm::ivec3 groundCell(tierX, tierY - 1, tierZ);
                    const uint32_t groundID = getBlockAt(voxelWorld, groundCell);
                    if (!isFoliageGroundPrototypeID(prototypes, groundID, waterPrototypeID)) continue;

                    int stoneID = (((seed >> 10u) & 1u) == 0u) ? stonePrototypeIDX : stonePrototypeIDZ;
                    if (stoneID < 0) stoneID = (stonePrototypeIDX >= 0) ? stonePrototypeIDX : stonePrototypeIDZ;
                    if (stoneID < 0) continue;

                    voxelWorld.setBlock(placeCell,
                        static_cast<uint32_t>(stoneID),
                        caveStoneColorForCell(worldX, worldY, worldZ),
                        false
                    );
                    modified = true;
                }
            }
        }

        void writeCavePotsToSection(const std::vector<Entity>& prototypes,
                                    const WorldContext& worldCtx,
                                    VoxelWorldContext& voxelWorld,
                                    int sectionTier,
                                    const glm::ivec3& sectionCoord,
                                    int sectionSize,
                                    int sectionScale,
                                    int potPrototypeIDX,
                                    int potPrototypeIDZ,
                                    const FoliageSpec& spec,
                                    bool& modified) {
            if (!spec.enabled || !spec.cavePotEnabled) return;
            const bool hasAnyPotPrototype = (potPrototypeIDX >= 0 || potPrototypeIDZ >= 0);
            if (!hasAnyPotPrototype) return;
            const int potChance = std::max(0, std::min(100, spec.cavePotSpawnPercent));
            if (potChance <= 0) return;
            const int minDepthFromSurface = std::max(1, std::min(256, spec.cavePotMinDepthFromSurface));
            const int minDepthFromSurfaceTier = std::max(
                1,
                static_cast<int>(std::ceil(static_cast<float>(minDepthFromSurface) / static_cast<float>(sectionScale)))
            );
            const int pileMaxCount = std::max(1, std::min(3, spec.cavePotPileMaxCount));

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;
            auto inSection = [&](const glm::ivec3& cell) {
                return cell.x >= minX && cell.x <= maxX
                    && cell.y >= minY && cell.y <= maxY
                    && cell.z >= minZ && cell.z <= maxZ;
            };

            const int kNoSurface = std::numeric_limits<int>::min();
            std::vector<int> surfaceHeights(static_cast<size_t>(sectionSize * sectionSize), kNoSurface);
            auto surfaceIdx = [&](int tierX, int tierZ) {
                return (tierZ - minZ) * sectionSize + (tierX - minX);
            };

            for (int tierZ = minZ; tierZ <= maxZ; ++tierZ) {
                for (int tierX = minX; tierX <= maxX; ++tierX) {
                    const int worldX = tierX * sectionScale;
                    const int worldZ = tierZ * sectionScale;
                    float terrainHeight = 0.0f;
                    if (!ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ),
                            terrainHeight)) {
                        continue;
                    }
                    surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))] =
                        floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                }
            }

            std::unordered_set<glm::ivec3, IVec3Hash> placedCells;
            auto canPlacePotAt = [&](const glm::ivec3& cell) -> bool {
                if (!inSection(cell)) return false;
                const int worldY = cell.y * sectionScale;
                if (worldY >= 60) return false;
                if (placedCells.count(cell) > 0) return false;
                if (getBlockAt(voxelWorld, cell) != 0u) return false;
                if (getBlockAt(voxelWorld, cell + glm::ivec3(0, 1, 0)) != 0u) return false;

                const glm::ivec3 supportCell = cell + glm::ivec3(0, -1, 0);
                const uint32_t supportID = getBlockAt(voxelWorld, supportCell);
                if (supportID == 0u || supportID >= prototypes.size()) return false;
                const Entity& supportProto = prototypes[supportID];
                if (!supportProto.isBlock || !supportProto.isSolid) return false;
                if (supportProto.name == "Water") return false;
                if (caveSlopeDirFromName(supportProto.name) != CaveSlopeDir::None) return false;
                if (supportProto.name.rfind("WaterSlope", 0) == 0) return false;

                int openSides = 0;
                const std::array<glm::ivec3, 4> sideOffsets = {
                    glm::ivec3(1, 0, 0),
                    glm::ivec3(-1, 0, 0),
                    glm::ivec3(0, 0, 1),
                    glm::ivec3(0, 0, -1)
                };
                for (const glm::ivec3& side : sideOffsets) {
                    const glm::ivec3 sideCell = cell + side;
                    if (!inSection(sideCell)) continue;
                    if (getBlockAt(voxelWorld, sideCell) == 0u) {
                        openSides += 1;
                    }
                }
                return openSides > 0;
            };

            const std::array<glm::ivec3, 9> pileOffsets = {
                glm::ivec3(0, 0, 0),
                glm::ivec3(1, 0, 0),
                glm::ivec3(-1, 0, 0),
                glm::ivec3(0, 0, 1),
                glm::ivec3(0, 0, -1),
                glm::ivec3(1, 0, 1),
                glm::ivec3(-1, 0, 1),
                glm::ivec3(1, 0, -1),
                glm::ivec3(-1, 0, -1)
            };

            std::vector<glm::ivec3> candidates;
            auto gatherCandidates = [&](bool enforceMinDepth) {
                candidates.clear();
                candidates.reserve(static_cast<size_t>(sectionSize * sectionSize));
                for (int tierZ = minZ; tierZ <= maxZ; ++tierZ) {
                    for (int tierX = minX; tierX <= maxX; ++tierX) {
                        const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))];
                        if (surfaceY == kNoSurface) continue;
                        int maxPlaceY = maxY;
                        if (enforceMinDepth) {
                            maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceTier);
                        }
                        if (maxPlaceY < minY) continue;
                        for (int tierY = minY; tierY <= maxPlaceY; ++tierY) {
                            const glm::ivec3 cell(tierX, tierY, tierZ);
                            if (canPlacePotAt(cell)) {
                                candidates.push_back(cell);
                            }
                        }
                    }
                }
            };
            gatherCandidates(true);
            // Fallback: if strict depth filtering finds nothing, allow shallower cave pockets.
            if (candidates.empty()) {
                gatherCandidates(false);
            }
            if (candidates.empty()) return;

            const uint32_t sectionSeed = hash3D(
                sectionCoord.x * 911 + sectionTier * 37,
                sectionCoord.y * 613 - sectionTier * 53,
                sectionCoord.z * 353 + 19
            );
            // Rare-by-section roll so cave pots do not explode in dense cave volumes.
            if (static_cast<int>(sectionSeed % 100u) >= potChance) return;

            const size_t startIndex = static_cast<size_t>((sectionSeed >> 8u) % static_cast<uint32_t>(candidates.size()));
            for (size_t attempt = 0; attempt < candidates.size(); ++attempt) {
                const glm::ivec3 baseCell = candidates[(startIndex + attempt) % candidates.size()];
                if (!canPlacePotAt(baseCell)) continue;

                const int baseWorldX = baseCell.x * sectionScale;
                const int baseWorldY = baseCell.y * sectionScale;
                const int baseWorldZ = baseCell.z * sectionScale;
                const uint32_t seed = hash3D(baseWorldX + 121, baseWorldY - 877, baseWorldZ + 43);
                const int pileCount = 1 + static_cast<int>((seed >> 8u) % static_cast<uint32_t>(pileMaxCount));
                const int offsetStart = static_cast<int>((seed >> 14u) % static_cast<uint32_t>(pileOffsets.size()));

                int placed = 0;
                for (size_t i = 0; i < pileOffsets.size() && placed < pileCount; ++i) {
                    const size_t offsetIndex = (static_cast<size_t>(offsetStart) + i) % pileOffsets.size();
                    const glm::ivec3 target = baseCell + pileOffsets[offsetIndex];
                    if (!canPlacePotAt(target)) continue;

                    const int targetWorldX = target.x * sectionScale;
                    const int targetWorldY = target.y * sectionScale;
                    const int targetWorldZ = target.z * sectionScale;
                    const uint32_t pickSeed = hash3D(targetWorldX + 9, targetWorldY + 17, targetWorldZ + 23);
                    int potID = (((pickSeed >> 9u) & 1u) == 0u) ? potPrototypeIDX : potPrototypeIDZ;
                    if (potID < 0) potID = (potPrototypeIDX >= 0) ? potPrototypeIDX : potPrototypeIDZ;
                    if (potID < 0) continue;

                    voxelWorld.setBlock(target,
                        static_cast<uint32_t>(potID),
                        packColor(glm::vec3(1.0f)),
                        false
                    );
                    placedCells.insert(target);
                    modified = true;
                    placed += 1;
                }

                // At most one pile per section by design to keep cave pots very rare.
                if (placed > 0) break;
            }
        }

        void writeCaveWallStoneToSection(const std::vector<Entity>& prototypes,
                                         const WorldContext& worldCtx,
                                         VoxelWorldContext& voxelWorld,
                                         int sectionTier,
                                         const glm::ivec3& sectionCoord,
                                         int sectionSize,
                                         int sectionScale,
                                         int wallStonePrototypePosX,
                                         int wallStonePrototypeNegX,
                                         int wallStonePrototypePosZ,
                                         int wallStonePrototypeNegZ,
                                         const FoliageSpec& spec,
                                         bool& modified) {
            if (!spec.enabled || !spec.caveWallStoneEnabled) return;
            const bool hasAnyWallStonePrototype =
                (wallStonePrototypePosX >= 0)
                || (wallStonePrototypeNegX >= 0)
                || (wallStonePrototypePosZ >= 0)
                || (wallStonePrototypeNegZ >= 0);
            if (!hasAnyWallStonePrototype) return;
            const int stoneChance = std::max(0, std::min(100, spec.caveWallStoneSpawnPercent));
            if (stoneChance <= 0) return;
            const int minDepthFromSurface = std::max(1, std::min(256, spec.caveWallStoneMinDepthFromSurface));
            const int minDepthFromSurfaceTier = std::max(
                1,
                static_cast<int>(std::ceil(static_cast<float>(minDepthFromSurface) / static_cast<float>(sectionScale)))
            );

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;

            const int kNoSurface = std::numeric_limits<int>::min();
            std::vector<int> surfaceHeights(static_cast<size_t>(sectionSize * sectionSize), kNoSurface);
            auto surfaceIdx = [&](int tierX, int tierZ) {
                return (tierZ - minZ) * sectionSize + (tierX - minX);
            };

            for (int tierZ = minZ; tierZ <= maxZ; ++tierZ) {
                for (int tierX = minX; tierX <= maxX; ++tierX) {
                    const int worldX = tierX * sectionScale;
                    const int worldZ = tierZ * sectionScale;
                    float terrainHeight = 0.0f;
                    if (!ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ),
                            terrainHeight)) {
                        continue;
                    }
                    surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))] =
                        floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                }
            }

            const uint32_t sectionSeed = hash3D(
                sectionCoord.x * 911 + sectionTier * 37,
                sectionCoord.y * 613 - sectionTier * 53,
                sectionCoord.z * 353 + 97
            );
            const int sectionVeinChance = std::max(1, std::min(100, stoneChance * 2));
            if (static_cast<int>(sectionSeed % 100u) >= sectionVeinChance) return;

            const int seedChance = std::max(1, stoneChance / 8);
            const int growChance = std::max(20, std::min(90, stoneChance * 5));
            const int growRounds = 2 + static_cast<int>((sectionSeed >> 8u) % 3u);
            const int maxPlacements = std::max(4, std::min(64, stoneChance * 2));
            int placedCount = 0;
            std::unordered_set<glm::ivec3, IVec3Hash> placedCells;

            auto hasPlacedNeighbor = [&](const glm::ivec3& cell) {
                static const std::array<glm::ivec3, 6> kNeighbors = {
                    glm::ivec3(1, 0, 0),
                    glm::ivec3(-1, 0, 0),
                    glm::ivec3(0, 1, 0),
                    glm::ivec3(0, -1, 0),
                    glm::ivec3(0, 0, 1),
                    glm::ivec3(0, 0, -1)
                };
                for (const glm::ivec3& n : kNeighbors) {
                    if (placedCells.count(cell + n) > 0) return true;
                }
                return false;
            };

            auto tryPlaceWallStone = [&](const glm::ivec3& placeCell, uint32_t seed, bool requireNeighbor) {
                if (placedCount >= maxPlacements) return false;
                if (getBlockAt(voxelWorld, placeCell) != 0u) return false;
                if (placedCells.count(placeCell) > 0) return false;
                if (requireNeighbor && !hasPlacedNeighbor(placeCell)) return false;

                std::array<int, 4> candidates = {-1, -1, -1, -1};
                int candidateCount = 0;
                auto trySupport = [&](const glm::ivec3& offset, int wallStonePrototypeID) {
                    if (wallStonePrototypeID < 0) return;
                    const glm::ivec3 supportCell = placeCell + offset;
                    const uint32_t supportID = getBlockAt(voxelWorld, supportCell);
                    if (supportID == 0u || supportID >= prototypes.size()) return;
                    const Entity& supportProto = prototypes[supportID];
                    if (!supportProto.isBlock || !supportProto.isSolid) return;
                    if (!isStoneSurfacePrototypeName(supportProto.name)) return;
                    candidates[static_cast<size_t>(candidateCount)] = wallStonePrototypeID;
                    candidateCount += 1;
                };

                // Support comes from the neighboring wall block; selected prototype encodes mount side.
                trySupport(glm::ivec3(1, 0, 0), wallStonePrototypePosX);
                trySupport(glm::ivec3(-1, 0, 0), wallStonePrototypeNegX);
                trySupport(glm::ivec3(0, 0, 1), wallStonePrototypePosZ);
                trySupport(glm::ivec3(0, 0, -1), wallStonePrototypeNegZ);
                if (candidateCount <= 0) return false;

                int pick = static_cast<int>((seed >> 9u) % static_cast<uint32_t>(candidateCount));
                pick = std::max(0, std::min(candidateCount - 1, pick));
                const int stoneID = candidates[static_cast<size_t>(pick)];
                if (stoneID < 0) return false;

                const int worldX = placeCell.x * sectionScale;
                const int worldY = placeCell.y * sectionScale;
                const int worldZ = placeCell.z * sectionScale;
                voxelWorld.setBlock(placeCell,
                    static_cast<uint32_t>(stoneID),
                    caveStoneColorForCell(worldX, worldY, worldZ),
                    false
                );
                placedCells.insert(placeCell);
                placedCount += 1;
                modified = true;
                return true;
            };

            for (int tierZ = minZ; tierZ <= maxZ && placedCount < maxPlacements; ++tierZ) {
                for (int tierX = minX; tierX <= maxX && placedCount < maxPlacements; ++tierX) {
                    const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))];
                    if (surfaceY == kNoSurface) continue;
                    const int maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceTier);
                    if (maxPlaceY < minY) continue;
                    for (int tierY = minY; tierY <= maxPlaceY && placedCount < maxPlacements; ++tierY) {
                        const int worldX = tierX * sectionScale;
                        const int worldY = tierY * sectionScale;
                        const int worldZ = tierZ * sectionScale;
                        const uint32_t seed = hash3D(worldX + 13, worldY - 47, worldZ + 71);
                        if (static_cast<int>(seed % 100u) >= seedChance) continue;
                        (void)tryPlaceWallStone(glm::ivec3(tierX, tierY, tierZ), seed, false);
                    }
                }
            }

            for (int round = 0; round < growRounds && placedCount < maxPlacements; ++round) {
                bool grew = false;
                for (int tierZ = minZ; tierZ <= maxZ && placedCount < maxPlacements; ++tierZ) {
                    for (int tierX = minX; tierX <= maxX && placedCount < maxPlacements; ++tierX) {
                        const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))];
                        if (surfaceY == kNoSurface) continue;
                        const int maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceTier);
                        if (maxPlaceY < minY) continue;
                        for (int tierY = minY; tierY <= maxPlaceY && placedCount < maxPlacements; ++tierY) {
                            const int worldX = tierX * sectionScale;
                            const int worldY = tierY * sectionScale;
                            const int worldZ = tierZ * sectionScale;
                            const uint32_t seed = hash3D(
                                worldX + 101 * (round + 1),
                                worldY - 53 * (round + 1),
                                worldZ + 211 * (round + 1)
                            );
                            if (static_cast<int>(seed % 100u) >= growChance) continue;
                            if (tryPlaceWallStone(glm::ivec3(tierX, tierY, tierZ), seed, true)) {
                                grew = true;
                            }
                        }
                    }
                }
                if (!grew) break;
            }
        }

        void writeCaveCeilingStoneToSection(const std::vector<Entity>& prototypes,
                                            const WorldContext& worldCtx,
                                            VoxelWorldContext& voxelWorld,
                                            int sectionTier,
                                            const glm::ivec3& sectionCoord,
                                            int sectionSize,
                                            int sectionScale,
                                            int ceilingStonePrototypePosX,
                                            int ceilingStonePrototypeNegX,
                                            int ceilingStonePrototypePosZ,
                                            int ceilingStonePrototypeNegZ,
                                            const FoliageSpec& spec,
                                            bool& modified) {
            if (!spec.enabled || !spec.caveCeilingStoneEnabled) return;
            const bool hasAnyCeilingStonePrototype =
                (ceilingStonePrototypePosX >= 0)
                || (ceilingStonePrototypeNegX >= 0)
                || (ceilingStonePrototypePosZ >= 0)
                || (ceilingStonePrototypeNegZ >= 0);
            if (!hasAnyCeilingStonePrototype) return;
            const int stoneChance = std::max(0, std::min(100, spec.caveCeilingStoneSpawnPercent));
            if (stoneChance <= 0) return;
            const int minDepthFromSurface = std::max(1, std::min(256, spec.caveCeilingStoneMinDepthFromSurface));
            const int minDepthFromSurfaceTier = std::max(
                1,
                static_cast<int>(std::ceil(static_cast<float>(minDepthFromSurface) / static_cast<float>(sectionScale)))
            );

            const int minX = sectionCoord.x * sectionSize;
            const int minY = sectionCoord.y * sectionSize;
            const int minZ = sectionCoord.z * sectionSize;
            const int maxX = minX + sectionSize - 1;
            const int maxY = minY + sectionSize - 1;
            const int maxZ = minZ + sectionSize - 1;

            const int kNoSurface = std::numeric_limits<int>::min();
            std::vector<int> surfaceHeights(static_cast<size_t>(sectionSize * sectionSize), kNoSurface);
            auto surfaceIdx = [&](int tierX, int tierZ) {
                return (tierZ - minZ) * sectionSize + (tierX - minX);
            };

            for (int tierZ = minZ; tierZ <= maxZ; ++tierZ) {
                for (int tierX = minX; tierX <= maxX; ++tierX) {
                    const int worldX = tierX * sectionScale;
                    const int worldZ = tierZ * sectionScale;
                    float terrainHeight = 0.0f;
                    if (!ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            static_cast<float>(worldX),
                            static_cast<float>(worldZ),
                            terrainHeight)) {
                        continue;
                    }
                    surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))] =
                        floorDivInt(static_cast<int>(std::floor(terrainHeight)), sectionScale);
                }
            }

            const uint32_t sectionSeed = hash3D(
                sectionCoord.x * 911 + sectionTier * 37,
                sectionCoord.y * 613 - sectionTier * 53,
                sectionCoord.z * 353 + 131
            );
            const int sectionVeinChance = std::max(1, std::min(100, stoneChance * 2));
            if (static_cast<int>(sectionSeed % 100u) >= sectionVeinChance) return;

            const int seedChance = std::max(1, stoneChance / 8);
            const int growChance = std::max(20, std::min(90, stoneChance * 5));
            const int growRounds = 2 + static_cast<int>((sectionSeed >> 8u) % 3u);
            const int maxPlacements = std::max(4, std::min(64, stoneChance * 2));
            int placedCount = 0;
            std::unordered_set<glm::ivec3, IVec3Hash> placedCells;

            auto hasPlacedNeighbor = [&](const glm::ivec3& cell) {
                static const std::array<glm::ivec3, 6> kNeighbors = {
                    glm::ivec3(1, 0, 0),
                    glm::ivec3(-1, 0, 0),
                    glm::ivec3(0, 1, 0),
                    glm::ivec3(0, -1, 0),
                    glm::ivec3(0, 0, 1),
                    glm::ivec3(0, 0, -1)
                };
                for (const glm::ivec3& n : kNeighbors) {
                    if (placedCells.count(cell + n) > 0) return true;
                }
                return false;
            };

            auto tryPlaceCeilingStone = [&](const glm::ivec3& placeCell, uint32_t seed, bool requireNeighbor) {
                if (placedCount >= maxPlacements) return false;
                if (getBlockAt(voxelWorld, placeCell) != 0u) return false;
                if (placedCells.count(placeCell) > 0) return false;
                if (requireNeighbor && !hasPlacedNeighbor(placeCell)) return false;

                const glm::ivec3 belowCell = placeCell + glm::ivec3(0, -1, 0);
                if (getBlockAt(voxelWorld, belowCell) != 0u) return false;

                const glm::ivec3 supportCell = placeCell + glm::ivec3(0, 1, 0);
                const uint32_t supportID = getBlockAt(voxelWorld, supportCell);
                if (supportID == 0u || supportID >= prototypes.size()) return false;
                const Entity& supportProto = prototypes[supportID];
                if (!supportProto.isBlock || !supportProto.isSolid) return false;
                if (!isStoneSurfacePrototypeName(supportProto.name)) return false;

                std::array<int, 4> candidates = {
                    ceilingStonePrototypePosX,
                    ceilingStonePrototypeNegX,
                    ceilingStonePrototypePosZ,
                    ceilingStonePrototypeNegZ
                };
                int candidateCount = 0;
                for (int i = 0; i < 4; ++i) {
                    if (candidates[static_cast<size_t>(i)] >= 0) {
                        candidates[static_cast<size_t>(candidateCount)] = candidates[static_cast<size_t>(i)];
                        candidateCount += 1;
                    }
                }
                if (candidateCount <= 0) return false;

                int pick = static_cast<int>((seed >> 10u) % static_cast<uint32_t>(candidateCount));
                pick = std::max(0, std::min(candidateCount - 1, pick));
                const int stoneID = candidates[static_cast<size_t>(pick)];
                if (stoneID < 0) return false;

                const int worldX = placeCell.x * sectionScale;
                const int worldY = placeCell.y * sectionScale;
                const int worldZ = placeCell.z * sectionScale;
                voxelWorld.setBlock(placeCell,
                    static_cast<uint32_t>(stoneID),
                    caveStoneColorForCell(worldX, worldY, worldZ),
                    false
                );
                placedCells.insert(placeCell);
                placedCount += 1;
                modified = true;
                return true;
            };

            for (int tierZ = minZ; tierZ <= maxZ && placedCount < maxPlacements; ++tierZ) {
                for (int tierX = minX; tierX <= maxX && placedCount < maxPlacements; ++tierX) {
                    const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))];
                    if (surfaceY == kNoSurface) continue;
                    const int maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceTier);
                    if (maxPlaceY < minY) continue;
                    for (int tierY = minY; tierY <= maxPlaceY && placedCount < maxPlacements; ++tierY) {
                        const int worldX = tierX * sectionScale;
                        const int worldY = tierY * sectionScale;
                        const int worldZ = tierZ * sectionScale;
                        const uint32_t seed = hash3D(worldX + 17, worldY - 61, worldZ + 89);
                        if (static_cast<int>(seed % 100u) >= seedChance) continue;
                        (void)tryPlaceCeilingStone(glm::ivec3(tierX, tierY, tierZ), seed, false);
                    }
                }
            }

            for (int round = 0; round < growRounds && placedCount < maxPlacements; ++round) {
                bool grew = false;
                for (int tierZ = minZ; tierZ <= maxZ && placedCount < maxPlacements; ++tierZ) {
                    for (int tierX = minX; tierX <= maxX && placedCount < maxPlacements; ++tierX) {
                        const int surfaceY = surfaceHeights[static_cast<size_t>(surfaceIdx(tierX, tierZ))];
                        if (surfaceY == kNoSurface) continue;
                        const int maxPlaceY = std::min(maxY, surfaceY - minDepthFromSurfaceTier);
                        if (maxPlaceY < minY) continue;
                        for (int tierY = minY; tierY <= maxPlaceY && placedCount < maxPlacements; ++tierY) {
                            const int worldX = tierX * sectionScale;
                            const int worldY = tierY * sectionScale;
                            const int worldZ = tierZ * sectionScale;
                            const uint32_t seed = hash3D(
                                worldX + 131 * (round + 1),
                                worldY - 73 * (round + 1),
                                worldZ + 227 * (round + 1)
                            );
                            if (static_cast<int>(seed % 100u) >= growChance) continue;
                            if (tryPlaceCeilingStone(glm::ivec3(tierX, tierY, tierZ), seed, true)) {
                                grew = true;
                            }
                        }
                    }
                }
                if (!grew) break;
            }
        }
