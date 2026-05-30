#pragma once

namespace TerrainSystemLogic {

    namespace {
        struct TerrainDepthFeaturePassContext {
            BaseSystem& baseSystem;
            std::vector<Entity>& prototypes;
            VoxelWorldContext& voxelWorld;
            const glm::ivec3& sectionCoord;
            int size;
            bool outCompleted;
            bool isExpanseLevel;
            bool unifiedDepthsEnabled;
            int unifiedDepthsTopY;
            int unifiedDepthsMinY;
            bool waterfallEnabled;
            int waterfallMaxDrop;
            int waterfallCascadeBudget;
            int waterSurfaceY;
            bool chalkEnabled;
            const Entity* chalkProto;
            int chalkStickSpawnPercent;
            int chalkSeed;
            const Entity* chalkStickProtoX;
            const Entity* chalkStickProtoZ;
            const glm::vec3& chalkColor;
            bool depthLavaFloorEnabled;
            const std::array<const Entity*, 9>& depthLavaTileProtos;
            const glm::vec3& lavaColor;
            const Entity* obsidianProto;
            const Entity* waterProto;
            const Entity* waterSlopeProtoPosX;
            const Entity* waterSlopeProtoNegX;
            const Entity* waterSlopeProtoPosZ;
            const Entity* waterSlopeProtoNegZ;
            const Entity* waterSlopeCornerProtoPosXPosZ;
            const Entity* waterSlopeCornerProtoPosXNegZ;
            const Entity* waterSlopeCornerProtoNegXPosZ;
            const Entity* waterSlopeCornerProtoNegXNegZ;
            const Entity* depthStoneProto;
            const Entity* stoneProto;
            const Entity* depthPurpleDirtProto;
            const Entity* depthRustBeamProto;
            const std::array<const Entity*, 4>& depthBigLilypadProtosX;
            const std::array<const Entity*, 4>& depthBigLilypadProtosZ;
            const Entity* depthMossWallProtoPosX;
            const Entity* depthMossWallProtoNegX;
            const Entity* depthMossWallProtoPosZ;
            const Entity* depthMossWallProtoNegZ;
            const Entity* depthCrystalProto;
            const Entity* depthCrystalBlueProto;
            const Entity* depthCrystalBlueBigProto;
            const Entity* depthCrystalMagentaBigProto;
            int depthPurplePatchPercent;
            int depthRustBeamPercent;
            int depthRustBeamSeed;
            int depthMossPercent;
            int depthBigLilypadPercent;
            int depthRiverCrystalPercent;
            int depthRiverCrystalBankSearchDown;
            int depthRiverCrystalBankSearchRadius;
            uint32_t packedWaterColorUnknown;
        };

        bool ApplyTerrainDepthFeaturePasses(const TerrainDepthFeaturePassContext& ctx,
                                            const std::vector<glm::ivec3>& pendingChalkPlacements,
                                            bool& wroteAny);

        static std::unordered_map<VoxelSectionKey, std::vector<glm::ivec3>, VoxelSectionKeyHash>
            g_pendingDepthFeatureChalkPlacements;

        struct TerrainPrototypeLookupCache {
            const std::vector<Entity>* source = nullptr;
            const Entity* data = nullptr;
            size_t size = 0;
            std::unordered_map<std::string, const Entity*> byName;
            const Entity* fallbackBlock = nullptr;

            void sync(const std::vector<Entity>& prototypes) {
                const Entity* currentData = prototypes.empty() ? nullptr : prototypes.data();
                if (source == &prototypes && data == currentData && size == prototypes.size()) return;
                source = &prototypes;
                data = currentData;
                size = prototypes.size();
                byName.clear();
                fallbackBlock = nullptr;
            }
        };

        const Entity* findTerrainPrototypeCached(std::vector<Entity>& prototypes, const char* name) {
            static thread_local TerrainPrototypeLookupCache cache;
            cache.sync(prototypes);
            const std::string key(name ? name : "");
            auto it = cache.byName.find(key);
            if (it != cache.byName.end()) return it->second;
            const Entity* proto = HostLogic::findPrototype(key, prototypes);
            cache.byName.emplace(key, proto);
            return proto;
        }

        const Entity* findTerrainFallbackBlockCached(std::vector<Entity>& prototypes) {
            static thread_local TerrainPrototypeLookupCache cache;
            cache.sync(prototypes);
            if (!cache.fallbackBlock) {
                cache.fallbackBlock = findNonZeroBlockProto(prototypes);
            }
            return cache.fallbackBlock;
        }

        bool GenerateExpanseSectionWorker(BaseSystem& baseSystem,
                                          std::vector<Entity>& prototypes,
                                          WorldContext& worldCtx,
                                          const ExpanseConfig& cfg,
                                          const glm::ivec3& sectionCoord,
                                          int startColumn,
                                          int maxColumns,
                                          bool& inOutWroteAny,
                                          int& outNextColumn,
                                          bool& outCompleted,
                                          bool& outPostFeaturesCompleted,
                                          bool runPostFeatures,
                                          bool columnMode,
                                          int columnMinY,
                                          int columnMaxY) {
            if (!baseSystem.voxelWorld) {
                outNextColumn = startColumn;
                outCompleted = true;
                outPostFeaturesCompleted = true;
                return false;
            }
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            int size = sectionSizeForSection(voxelWorld);
            int scale = 1;
            auto readRegistryInt = static_cast<int (*)(const BaseSystem&, const std::string&, int)>(&getRegistryInt);
            auto readRegistryBool = static_cast<bool (*)(const BaseSystem&, const std::string&, bool)>(&getRegistryBool);
            auto readRegistryFloat = static_cast<float (*)(const BaseSystem&, const std::string&, float)>(&getRegistryFloat);
            auto readRegistryString =
                static_cast<std::string (*)(const BaseSystem&, const std::string&, const std::string&)>(&getRegistryString);
            std::unordered_map<std::string, int> registryIntCache;
            std::unordered_map<std::string, bool> registryBoolCache;
            std::unordered_map<std::string, float> registryFloatCache;
            std::unordered_map<std::string, std::string> registryStringCache;
            auto getRegistryInt = [&](const BaseSystem& system, const std::string& key, int fallback) -> int {
                const std::string cacheKey = key + "\n" + std::to_string(fallback);
                auto it = registryIntCache.find(cacheKey);
                if (it != registryIntCache.end()) return it->second;
                const int value = readRegistryInt(system, key, fallback);
                registryIntCache.emplace(cacheKey, value);
                return value;
            };
            auto getRegistryBool = [&](const BaseSystem& system, const std::string& key, bool fallback) -> bool {
                const std::string cacheKey = key + (fallback ? "\n1" : "\n0");
                auto it = registryBoolCache.find(cacheKey);
                if (it != registryBoolCache.end()) return it->second;
                const bool value = readRegistryBool(system, key, fallback);
                registryBoolCache.emplace(cacheKey, value);
                return value;
            };
            auto getRegistryFloat = [&](const BaseSystem& system, const std::string& key, float fallback) -> float {
                const std::string cacheKey = key + "\n" + std::to_string(fallback);
                auto it = registryFloatCache.find(cacheKey);
                if (it != registryFloatCache.end()) return it->second;
                const float value = readRegistryFloat(system, key, fallback);
                registryFloatCache.emplace(cacheKey, value);
                return value;
            };
            auto getRegistryString = [&](const BaseSystem& system,
                                         const std::string& key,
                                         const std::string& fallback) -> std::string {
                const std::string cacheKey = key + "\n" + fallback;
                auto it = registryStringCache.find(cacheKey);
                if (it != registryStringCache.end()) return it->second;
                const std::string value = readRegistryString(system, key, fallback);
                registryStringCache.emplace(cacheKey, value);
                return value;
            };
            const std::string currentLevel = getRegistryString(baseSystem, "level", "the_expanse");
            const std::string currentDimension = (baseSystem.worldSave && !baseSystem.worldSave->activeDimensionId.empty())
                ? baseSystem.worldSave->activeDimensionId
                : getRegistryString(baseSystem, "ActiveDimensionId", "overworld");
            const bool isOverworldDimension = (currentDimension == "overworld");
            const bool isDepthLevel = (currentLevel == "the_depths");
            const bool isExpanseLevel = (currentLevel == "the_expanse");
            outNextColumn = startColumn;
            outCompleted = false;
            outPostFeaturesCompleted = false;
            auto pickBlockProto = [&](std::initializer_list<const char*> names) -> const Entity* {
                for (const char* name : names) {
                    const Entity* proto = findTerrainPrototypeCached(prototypes, name);
                    if (proto && proto->prototypeID != 0) return proto;
                }
                return findTerrainFallbackBlockCached(prototypes);
            };
            std::array<const Entity*, 4> surfaceProtoConiferVariants = {
                pickBlockProto({"GrassBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"GrassBlockTexV002", "GrassBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"GrassBlockTexV003", "GrassBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"GrassBlockTexV004", "GrassBlockTex", "ScaffoldBlock"})
            };
            const Entity* surfaceProtoConifer = surfaceProtoConiferVariants[0];
            const Entity* surfaceProtoProceduralGrassBiome = pickBlockProto({"GrassBlockProceduralBiomeTex", "GrassBlockDebugBiomeTex", "DebugBlockTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* surfaceProtoMeadow = pickBlockProto({"GrassBlockMeadowTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* surfaceProtoJungle = pickBlockProto({"GrassBlockJungleTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* surfaceProtoBareWinter = pickBlockProto({"GrassBlockBareWinterTex", "GrassBlockTemperateTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* surfaceProtoSnow = pickBlockProto({"SnowBlockTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto = pickBlockProto({"SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto404 = pickBlockProto({"SandBlockDesertTex404", "SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto405 = pickBlockProto({"SandBlockDesertTex405", "SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto406 = pickBlockProto({"SandBlockDesertTex406", "SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto407 = pickBlockProto({"SandBlockDesertTex407", "SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandDesertProto408 = pickBlockProto({"SandBlockDesertTex408", "SandBlockDesertTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandBeachProto = pickBlockProto({"SandBlockBeachTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* sandSeabedProto = pickBlockProto({"SandBlockSeabedTex", "SandBlockTex", "ScaffoldBlock"});
            const Entity* soilProto = pickBlockProto({"DirtBlockTex", "GrassBlockTex", "ScaffoldBlock"});
            const Entity* stoneProto = pickBlockProto({"StoneBlockTex", "ScaffoldBlock"});
            const Entity* depthStoneProto = pickBlockProto({"DepthStoneBlockTex", "StoneBlockTex", "ScaffoldBlock"});
            // Portals are disabled; depths are now a contiguous underground band in the expanse.
            const Entity* voidPortalProto = nullptr;
            const Entity* rubyOreProto = findTerrainPrototypeCached(prototypes, "RubyOreTex");
            const Entity* silverOreProto = findTerrainPrototypeCached(prototypes, "SilverOreTex");
            const Entity* amethystOreProto = findTerrainPrototypeCached(prototypes, "AmethystOreTex");
            const Entity* flouriteOreProto = findTerrainPrototypeCached(prototypes, "FlouriteOreTex");
            const Entity* graniteProto = findTerrainPrototypeCached(prototypes, "GraniteBlockTex");
            const Entity* chalkProto = findTerrainPrototypeCached(prototypes, "ChalkBlockTex");
            const Entity* clayProto = findTerrainPrototypeCached(prototypes, "ClayBlockTex");
            const Entity* chalkStickProtoX = findTerrainPrototypeCached(prototypes, "StonePebbleChalkTexX");
            const Entity* chalkStickProtoZ = findTerrainPrototypeCached(prototypes, "StonePebbleChalkTexZ");
            const Entity* waterProto = findTerrainPrototypeCached(prototypes, "Water");
            const Entity* waterSlopeProtoPosX = findTerrainPrototypeCached(prototypes, "WaterSlopePosX");
            const Entity* waterSlopeProtoNegX = findTerrainPrototypeCached(prototypes, "WaterSlopeNegX");
            const Entity* waterSlopeProtoPosZ = findTerrainPrototypeCached(prototypes, "WaterSlopePosZ");
            const Entity* waterSlopeProtoNegZ = findTerrainPrototypeCached(prototypes, "WaterSlopeNegZ");
            const Entity* waterSlopeCornerProtoPosXPosZ = findTerrainPrototypeCached(prototypes, "WaterSlopeCornerPosXPosZ");
            const Entity* waterSlopeCornerProtoPosXNegZ = findTerrainPrototypeCached(prototypes, "WaterSlopeCornerPosXNegZ");
            const Entity* waterSlopeCornerProtoNegXPosZ = findTerrainPrototypeCached(prototypes, "WaterSlopeCornerNegXPosZ");
            const Entity* waterSlopeCornerProtoNegXNegZ = findTerrainPrototypeCached(prototypes, "WaterSlopeCornerNegXNegZ");
            const Entity* obsidianProto = pickBlockProto({"ObsidianBlockTex", "StoneBlockTex", "ScaffoldBlock"});
            const std::array<const Entity*, 9> depthLavaTileProtos = {
                pickBlockProto({"DepthLavaTileR01C01", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR01C02", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR01C03", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR02C01", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR02C02", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR02C03", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR03C01", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR03C02", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"}),
                pickBlockProto({"DepthLavaTileR03C03", "LavaBlockTex", "StoneBlockTex", "ScaffoldBlock"})
            };
            const std::array<const Entity*, 7> depthLodestoneOreProtos = {
                findTerrainPrototypeCached(prototypes, "DepthLodestoneOreV001"),
                findTerrainPrototypeCached(prototypes, "DepthLodestoneOreV002"),
                findTerrainPrototypeCached(prototypes, "DepthLodestoneOreV003"),
                findTerrainPrototypeCached(prototypes, "DepthLodestoneOreV004"),
                findTerrainPrototypeCached(prototypes, "DepthLodestoneOreV005"),
                findTerrainPrototypeCached(prototypes, "DepthLodestoneOreV006"),
                findTerrainPrototypeCached(prototypes, "DepthLodestoneOreV007")
            };
            const Entity* depthCopperSulfateOreProto = findTerrainPrototypeCached(prototypes, "DepthCopperSulfateOreTex");
            const Entity* depthPurpleDirtProto = findTerrainPrototypeCached(prototypes, "DepthPurpleDirtTex");
            const Entity* depthRustBeamProto = findTerrainPrototypeCached(prototypes, "DepthRustBeamTex");
            const std::array<const Entity*, 4> depthBigLilypadProtosX = {
                findTerrainPrototypeCached(prototypes, "GrassCoverBigLilypadR01C01TexX"),
                findTerrainPrototypeCached(prototypes, "GrassCoverBigLilypadR01C02TexX"),
                findTerrainPrototypeCached(prototypes, "GrassCoverBigLilypadR02C01TexX"),
                findTerrainPrototypeCached(prototypes, "GrassCoverBigLilypadR02C02TexX")
            };
            const std::array<const Entity*, 4> depthBigLilypadProtosZ = {
                findTerrainPrototypeCached(prototypes, "GrassCoverBigLilypadR01C01TexZ"),
                findTerrainPrototypeCached(prototypes, "GrassCoverBigLilypadR01C02TexZ"),
                findTerrainPrototypeCached(prototypes, "GrassCoverBigLilypadR02C01TexZ"),
                findTerrainPrototypeCached(prototypes, "GrassCoverBigLilypadR02C02TexZ")
            };
            const Entity* depthMossWallProtoPosX = findTerrainPrototypeCached(prototypes, "DepthMossWallTexPosX");
            const Entity* depthMossWallProtoNegX = findTerrainPrototypeCached(prototypes, "DepthMossWallTexNegX");
            const Entity* depthMossWallProtoPosZ = findTerrainPrototypeCached(prototypes, "DepthMossWallTexPosZ");
            const Entity* depthMossWallProtoNegZ = findTerrainPrototypeCached(prototypes, "DepthMossWallTexNegZ");
            const Entity* depthCrystalProto = findTerrainPrototypeCached(prototypes, "DepthCrystalClusterTex");
            const Entity* depthCrystalBlueProto = findTerrainPrototypeCached(prototypes, "DepthCrystalClusterBlueTex");
            const Entity* depthCrystalBlueBigProto = findTerrainPrototypeCached(prototypes, "DepthCrystalClusterBlueBigTex");
            const Entity* depthCrystalMagentaBigProto = findTerrainPrototypeCached(prototypes, "DepthCrystalClusterMagentaBigTex");
            if (!surfaceProtoConifer || !waterProto) {
                outNextColumn = startColumn;
                outCompleted = true;
                outPostFeaturesCompleted = true;
                return false;
            }
            for (auto& proto : surfaceProtoConiferVariants) {
                if (!proto) proto = surfaceProtoConifer;
            }
            if (!surfaceProtoProceduralGrassBiome) surfaceProtoProceduralGrassBiome = surfaceProtoConifer;
            if (!surfaceProtoMeadow) surfaceProtoMeadow = surfaceProtoConifer;
            if (!surfaceProtoJungle) surfaceProtoJungle = surfaceProtoConifer;
            if (!surfaceProtoBareWinter) surfaceProtoBareWinter = surfaceProtoConifer;
            if (!surfaceProtoSnow) surfaceProtoSnow = surfaceProtoConifer;
            if (!sandDesertProto) sandDesertProto = surfaceProtoConifer;
            std::array<const Entity*, 5> sandDesertVariantProtos = {
                sandDesertProto404,
                sandDesertProto405,
                sandDesertProto406,
                sandDesertProto407,
                sandDesertProto408
            };
            for (const Entity*& variant : sandDesertVariantProtos) {
                if (!variant) variant = sandDesertProto;
            }
            if (!sandBeachProto) sandBeachProto = sandDesertProto;
            if (!sandSeabedProto) sandSeabedProto = sandBeachProto;
            if (!soilProto) soilProto = surfaceProtoConifer;
            if (!stoneProto) stoneProto = surfaceProtoConifer;
            if (isDepthLevel && depthStoneProto) {
                stoneProto = depthStoneProto;
                soilProto = depthStoneProto;
            }

            int caveMinY = -70;
            if (isDepthLevel) {
                caveMinY = std::min(caveMinY, cfg.minY);
            }
            if (isExpanseLevel && isOverworldDimension && getRegistryBool(baseSystem, "UnifiedDepthsEnabled", true)) {
                caveMinY = std::min(
                    caveMinY,
                    getRegistryInt(baseSystem, "UnifiedDepthsMinY", -70)
                );
            }
            const int caveMaxY = std::max(
                192,
                std::max(
                    static_cast<int>(std::ceil(cfg.waterSurface + cfg.islandMaxHeight + (cfg.islandNoiseAmp * 2.0f))),
                    std::isfinite(cfg.maxSurfaceY) ? static_cast<int>(std::ceil(cfg.maxSurfaceY)) : 192
                )
            );
            ensureCaveField(baseSystem, cfg, caveMinY, caveMaxY);

            glm::vec3 grassColor = GetColorLocal(worldCtx, cfg.colorGrass, glm::vec3(0.2f, 0.8f, 0.2f));
            glm::vec3 sandColor = GetColorLocal(worldCtx, cfg.colorSand, glm::vec3(0.9f, 0.8f, 0.4f));
            glm::vec3 snowColor = GetColorLocal(worldCtx, cfg.colorSnow, glm::vec3(0.93f, 0.94f, 0.96f));
            glm::vec3 soilColor = GetColorLocal(worldCtx, cfg.colorSoil, glm::vec3(0.33f, 0.22f, 0.15f));
            glm::vec3 stoneColor = GetColorLocal(worldCtx, cfg.colorStone, glm::vec3(0.4f, 0.4f, 0.4f));
            glm::vec3 waterColor = GetColorLocal(worldCtx, cfg.colorWater, glm::vec3(0.05f, 0.2f, 0.5f));
            glm::vec3 lavaColor = GetColorLocal(worldCtx, cfg.colorLava, glm::vec3(0.96f, 0.33f, 0.08f));
            glm::vec3 seabedColor = GetColorLocal(worldCtx, cfg.colorSeabed, sandColor);
            const uint32_t packedWaterColorOcean = withWaterWaveClass(packColor(waterColor), kWaterWaveClassOcean);
            const uint32_t packedWaterColorLake = withWaterWaveClass(packColor(waterColor), kWaterWaveClassLake);
            const uint32_t packedWaterColorPond = withWaterWaveClass(packColor(waterColor), kWaterWaveClassPond);
            const uint32_t packedWaterColorRiver = withWaterWaveClass(packColor(waterColor), kWaterWaveClassRiver);
            const uint32_t packedWaterColorUnknown = withWaterWaveClass(packColor(waterColor), kWaterWaveClassUnknown);
            const glm::vec3 graniteColor = glm::vec3(1.0f, 1.0f, 1.0f);
            const glm::vec3 chalkColor = glm::vec3(0.94f, 0.95f, 0.92f);
            const glm::vec3 clayColor = glm::vec3(1.0f, 1.0f, 1.0f);
            const bool islandQuadrants = (cfg.islandRadius > 0.0f) && cfg.secondaryBiomeEnabled;
            const float volcanoCenterFactorX = std::clamp(cfg.jungleVolcanoCenterFactorX, 0.0f, 1.0f);
            const float volcanoCenterFactorZ = std::clamp(cfg.jungleVolcanoCenterFactorZ, 0.0f, 1.0f);
            const float volcanoCenterX = cfg.islandCenterX + cfg.islandRadius * volcanoCenterFactorX;
            const float volcanoCenterZ = cfg.islandCenterZ + cfg.islandRadius * volcanoCenterFactorZ;
            const float volcanoOuterRadius = std::max(8.0f, cfg.jungleVolcanoOuterRadius);
            const float volcanoCraterRadius = std::clamp(cfg.jungleVolcanoCraterRadius, 4.0f, volcanoOuterRadius * 0.95f);
            const float volcanoLavaRadius = volcanoCraterRadius;
            float volcanoPeakY = cfg.waterSurface + cfg.islandMaxHeight + cfg.jungleVolcanoHeight;
            if (std::isfinite(cfg.maxSurfaceY)) {
                volcanoPeakY = std::min(volcanoPeakY, cfg.maxSurfaceY);
            }
            const float volcanoCraterFloorY = volcanoPeakY - std::max(0.0f, cfg.jungleVolcanoCraterDepth);
            const int volcanoLavaSurfaceY = static_cast<int>(std::floor(
                volcanoCraterFloorY + std::max(2.0f, cfg.jungleVolcanoCraterDepth * 0.08f)
            ));
            const int volcanoLavaDepth = std::max(
                64,
                static_cast<int>(std::round(std::max(24.0f, cfg.jungleVolcanoCraterDepth * 1.05f)))
            );
            const int volcanoChamberDepth = std::max(
                48,
                static_cast<int>(std::round(std::max(20.0f, cfg.jungleVolcanoCraterDepth * 0.9f)))
            );
            const float volcanoChamberRadius = std::max(8.0f, volcanoCraterRadius * 0.74f);
            const std::array<const Entity*, 4> oreProtos = {rubyOreProto, silverOreProto, amethystOreProto, flouriteOreProto};
            const std::array<uint32_t, 4> oreColors = {
                packColor(glm::vec3(0.78f, 0.19f, 0.22f)), // ruby
                packColor(glm::vec3(0.72f, 0.74f, 0.78f)), // silver
                packColor(glm::vec3(0.67f, 0.44f, 0.82f)), // amethyst
                packColor(glm::vec3(0.38f, 0.78f, 0.62f))  // flourite / fluorite
            };
            const bool oreEnabled = getRegistryBool(baseSystem, "OreGenerationEnabled", true);
            const int oreSeed = getRegistryInt(baseSystem, "OreGenerationSeed", 4242);
            const int oreVeinCellSize = std::max(6, getRegistryInt(baseSystem, "OreVeinCellSize", 14));
            const float oreVeinRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "OreVeinRadiusMin", 2.0f));
            const float oreVeinRadiusMax = std::max(oreVeinRadiusMin, getRegistryFloat(baseSystem, "OreVeinRadiusMax", 4.5f));
            const float oreVeinChance = glm::clamp(getRegistryFloat(baseSystem, "OreBaseChance", 0.18f), 0.0f, 1.0f);
            const float oreSoilReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "OreSoilReplaceChance", 0.45f), 0.0f, 1.0f);
            const float oreStoneReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "OreStoneReplaceChance", 0.60f), 0.0f, 1.0f);
            const int oreMinDepthFromSurface = std::max(1, getRegistryInt(baseSystem, "OreMinDepthFromSurface", 4));
            const float oreCaveAdjacencyBoost = glm::clamp(getRegistryFloat(baseSystem, "OreCaveAdjacencyBoost", 0.35f), 0.0f, 1.0f);
            const bool chalkEnabled = getRegistryBool(baseSystem, "ChalkGenerationEnabled", true);
            const int chalkSeed = getRegistryInt(baseSystem, "ChalkGenerationSeed", 9185);
            const int chalkVeinCellSize = std::max(5, getRegistryInt(baseSystem, "ChalkVeinCellSize", 12));
            const float chalkVeinRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "ChalkVeinRadiusMin", 1.5f));
            const float chalkVeinRadiusMax = std::max(chalkVeinRadiusMin, getRegistryFloat(baseSystem, "ChalkVeinRadiusMax", 3.5f));
            const float chalkVeinChance = glm::clamp(getRegistryFloat(baseSystem, "ChalkVeinChance", 0.28f), 0.0f, 1.0f);
            const float chalkReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "ChalkReplaceChance", 0.80f), 0.0f, 1.0f);
            const int chalkStickSpawnPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "ChalkStickSpawnPercent", 18)));
            const bool clayEnabled = getRegistryBool(baseSystem, "ClayGenerationEnabled", true);
            const int claySeed = getRegistryInt(baseSystem, "ClayGenerationSeed", 12553);
            const int clayVeinCellSize = std::max(5, getRegistryInt(baseSystem, "ClayVeinCellSize", 12));
            const float clayVeinRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "ClayVeinRadiusMin", 1.5f));
            const float clayVeinRadiusMax = std::max(clayVeinRadiusMin, getRegistryFloat(baseSystem, "ClayVeinRadiusMax", 3.5f));
            const float clayVeinChance = glm::clamp(getRegistryFloat(baseSystem, "ClayVeinChance", 0.28f), 0.0f, 1.0f);
            const float clayReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "ClayReplaceChance", 0.80f), 0.0f, 1.0f);
            const bool graniteEnabled = getRegistryBool(baseSystem, "GraniteGenerationEnabled", true);
            const int graniteSeed = getRegistryInt(baseSystem, "GraniteGenerationSeed", 10273);
            const int graniteVeinCellSize = std::max(5, getRegistryInt(baseSystem, "GraniteVeinCellSize", 12));
            const float graniteVeinRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "GraniteVeinRadiusMin", 1.5f));
            const float graniteVeinRadiusMax = std::max(graniteVeinRadiusMin, getRegistryFloat(baseSystem, "GraniteVeinRadiusMax", 3.5f));
            const float graniteVeinChance = glm::clamp(getRegistryFloat(baseSystem, "GraniteVeinChance", 0.28f), 0.0f, 1.0f);
            const float graniteReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "GraniteReplaceChance", 0.80f), 0.0f, 1.0f);
            const bool surfaceRiverEnabled = getRegistryBool(baseSystem, "SurfaceRiverGenerationEnabled", true);
            const int surfaceRiverSeed = getRegistryInt(baseSystem, "SurfaceRiverSeed", 2701);
            const float surfaceRiverScale = std::max(32.0f, getRegistryFloat(baseSystem, "SurfaceRiverScale", 180.0f));
            const float surfaceRiverWarpScale = std::max(16.0f, getRegistryFloat(baseSystem, "SurfaceRiverWarpScale", 72.0f));
            const float surfaceRiverWarpStrength = std::max(0.0f, getRegistryFloat(baseSystem, "SurfaceRiverWarpStrength", 58.0f));
            const float surfaceRiverThresholdMin = glm::clamp(getRegistryFloat(baseSystem, "SurfaceRiverThresholdMin", 0.045f), 0.001f, 0.45f);
            const float surfaceRiverThresholdMax = glm::clamp(
                getRegistryFloat(baseSystem, "SurfaceRiverThresholdMax", 0.085f),
                surfaceRiverThresholdMin,
                0.75f
            );
            const int surfaceRiverDepthMin = std::max(1, getRegistryInt(baseSystem, "SurfaceRiverDepthMin", 3));
            const int surfaceRiverDepthMax = std::max(surfaceRiverDepthMin, getRegistryInt(baseSystem, "SurfaceRiverDepthMax", 9));
            const int surfaceRiverMinAboveSea = std::max(1, getRegistryInt(baseSystem, "SurfaceRiverMinAboveSea", 2));
            const int surfaceRiverChannelLowerMin = std::max(0, getRegistryInt(baseSystem, "SurfaceRiverChannelLowerMin", 3));
            const int surfaceRiverChannelLowerMax = std::max(
                surfaceRiverChannelLowerMin,
                getRegistryInt(baseSystem, "SurfaceRiverChannelLowerMax", 5)
            );
            const int surfaceRiverWaterlineExtraLower = std::max(
                0,
                getRegistryInt(baseSystem, "SurfaceRiverWaterlineExtraLower", 3)
            );
            const float surfaceRiverDepthMultiplier = std::max(
                1.0f,
                getRegistryFloat(baseSystem, "SurfaceRiverDepthMultiplier", 3.0f)
            );
            const bool waterfallEnabled = getRegistryBool(baseSystem, "WaterfallGenerationEnabled", true);
            const int waterfallMaxDrop = std::max(1, getRegistryInt(baseSystem, "WaterfallMaxDrop", 96));
            const int waterfallCascadeBudget = std::max(16, getRegistryInt(baseSystem, "WaterfallCascadeBudget", 2048));
            const bool surfaceLakeEnabled = (!isDepthLevel)
                && getRegistryBool(baseSystem, "SurfaceLakeGenerationEnabled", true);
            const int surfaceLakeSeed = getRegistryInt(baseSystem, "SurfaceLakeSeed", 9103);
            const int surfaceLakeCellSize = std::max(48, getRegistryInt(baseSystem, "SurfaceLakeCellSize", 360));
            const float surfaceLakeChance = glm::clamp(getRegistryFloat(baseSystem, "SurfaceLakeChance", 0.08f), 0.0f, 1.0f);
            const float surfaceLakeRadiusMin = std::max(6.0f, getRegistryFloat(baseSystem, "SurfaceLakeRadiusMin", 50.0f));
            const float surfaceLakeRadiusMax = std::max(surfaceLakeRadiusMin, getRegistryFloat(baseSystem, "SurfaceLakeRadiusMax", 150.0f));
            const int surfaceLakeDepthMin = std::max(2, getRegistryInt(baseSystem, "SurfaceLakeDepthMin", 24));
            const int surfaceLakeDepthMax = std::max(surfaceLakeDepthMin, getRegistryInt(baseSystem, "SurfaceLakeDepthMax", 34));
            const int surfaceLakeDepthExtra = std::max(0, getRegistryInt(baseSystem, "SurfaceLakeDepthExtra", 10));
            const int surfaceLakeMinAboveSea = std::max(1, getRegistryInt(baseSystem, "SurfaceLakeMinAboveSea", 4));
            const int surfaceLakeChannelLower = std::max(0, getRegistryInt(baseSystem, "SurfaceLakeChannelLower", 3));
            const bool surfacePondEnabled = (!isDepthLevel)
                && getRegistryBool(baseSystem, "SurfacePondGenerationEnabled", true);
            const int surfacePondSeed = getRegistryInt(baseSystem, "SurfacePondSeed", 1337);
            const int surfacePondCellSize = std::max(24, getRegistryInt(baseSystem, "SurfacePondCellSize", 40));
            const float surfacePondChance = glm::clamp(getRegistryFloat(baseSystem, "SurfacePondChance", 0.70f), 0.0f, 1.0f);
            const float surfacePondRadiusMin = std::max(2.0f, getRegistryFloat(baseSystem, "SurfacePondRadiusMin", 10.0f));
            const float surfacePondRadiusMax = std::max(surfacePondRadiusMin, getRegistryFloat(baseSystem, "SurfacePondRadiusMax", 18.0f));
            const int surfacePondDepthMin = std::max(1, getRegistryInt(baseSystem, "SurfacePondDepthMin", 3));
            const int surfacePondDepthMax = std::max(surfacePondDepthMin, getRegistryInt(baseSystem, "SurfacePondDepthMax", 7));
            const int surfacePondMinAboveSea = std::max(1, getRegistryInt(baseSystem, "SurfacePondMinAboveSea", 1));
            const int surfacePondChannelLower = std::max(0, getRegistryInt(baseSystem, "SurfacePondChannelLower", 3));
            struct PondCellInfo {
                bool valid = false;
                bool centerIsLand = false;
                int centerSurfaceY = 0;
                float centerX = 0.0f;
                float centerZ = 0.0f;
                float radius = 0.0f;
                int depth = 0;
            };
            std::unordered_map<uint64_t, PondCellInfo> pondCellCache;
            auto pondCellKey = [](int cellX, int cellZ) -> uint64_t {
                return (static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32)
                    | static_cast<uint64_t>(static_cast<uint32_t>(cellZ));
            };
            std::unordered_map<uint64_t, PondCellInfo> lakeCellCache;
            auto lakeCellInfoFor = [&](int cellX, int cellZ) -> const PondCellInfo& {
                const uint64_t key = pondCellKey(cellX, cellZ);
                auto found = lakeCellCache.find(key);
                if (found != lakeCellCache.end()) {
                    return found->second;
                }
                PondCellInfo info;
                if (surfaceLakeEnabled) {
                    const uint32_t seed = hash2DInt(cellX + surfaceLakeSeed * 61, cellZ - surfaceLakeSeed * 47);
                    const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                    if (chanceRoll <= surfaceLakeChance) {
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        info.centerX = (static_cast<float>(cellX) + offsetX) * static_cast<float>(surfaceLakeCellSize);
                        info.centerZ = (static_cast<float>(cellZ) + offsetZ) * static_cast<float>(surfaceLakeCellSize);
                        const uint32_t radiusSeed = hash2DInt(cellX * 139 + surfaceLakeSeed, cellZ * 191 - surfaceLakeSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        info.radius = surfaceLakeRadiusMin + (surfaceLakeRadiusMax - surfaceLakeRadiusMin) * radiusT;
                        const uint32_t depthSeed = hash2DInt(cellX * 331 + surfaceLakeSeed * 7, cellZ * 587 - surfaceLakeSeed * 11);
                        const float depthT = static_cast<float>((depthSeed >> 8u) & 0xffu) / 255.0f;
                        info.depth = surfaceLakeDepthMin
                            + static_cast<int>(std::round((surfaceLakeDepthMax - surfaceLakeDepthMin) * depthT));
                        float centerHeight = 0.0f;
                        info.centerIsLand = ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            info.centerX,
                            info.centerZ,
                            centerHeight
                        );
                        info.centerSurfaceY = static_cast<int>(std::floor(centerHeight));
                        info.valid = true;
                    }
                }
                auto inserted = lakeCellCache.emplace(key, info);
                return inserted.first->second;
            };
            auto pondCellInfoFor = [&](int cellX, int cellZ) -> const PondCellInfo& {
                const uint64_t key = pondCellKey(cellX, cellZ);
                auto found = pondCellCache.find(key);
                if (found != pondCellCache.end()) {
                    return found->second;
                }
                PondCellInfo info;
                if (surfacePondEnabled) {
                    const uint32_t seed = hash2DInt(cellX + surfacePondSeed * 37, cellZ - surfacePondSeed * 53);
                    const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                    if (chanceRoll <= surfacePondChance) {
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        info.centerX = (static_cast<float>(cellX) + offsetX) * static_cast<float>(surfacePondCellSize);
                        info.centerZ = (static_cast<float>(cellZ) + offsetZ) * static_cast<float>(surfacePondCellSize);
                        const uint32_t radiusSeed = hash2DInt(cellX * 131 + surfacePondSeed, cellZ * 173 - surfacePondSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        info.radius = surfacePondRadiusMin + (surfacePondRadiusMax - surfacePondRadiusMin) * radiusT;
                        const uint32_t depthSeed = hash2DInt(cellX * 313 + surfacePondSeed * 7, cellZ * 571 - surfacePondSeed * 11);
                        const float depthT = static_cast<float>((depthSeed >> 8u) & 0xffu) / 255.0f;
                        info.depth = surfacePondDepthMin
                            + static_cast<int>(std::round((surfacePondDepthMax - surfacePondDepthMin) * depthT));
                        float centerHeight = 0.0f;
                        info.centerIsLand = ExpanseBiomeSystemLogic::SampleTerrain(
                            worldCtx,
                            info.centerX,
                            info.centerZ,
                            centerHeight
                        );
                        info.centerSurfaceY = static_cast<int>(std::floor(centerHeight));
                        info.valid = true;
                    }
                }
                auto inserted = pondCellCache.emplace(key, info);
                return inserted.first->second;
            };
            auto oreVariantForColumn = [&](int worldXi, int worldZi) -> int {
                if (!oreEnabled) return -1;
                const int cellX = floorDivInt(worldXi, oreVeinCellSize);
                const int cellZ = floorDivInt(worldZi, oreVeinCellSize);
                int bestVariant = -1;
                float bestDist2 = std::numeric_limits<float>::max();
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + oreSeed * 17, vz - oreSeed * 23);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > oreVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(oreVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(oreVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 131 + oreSeed, vz * 197 - oreSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = oreVeinRadiusMin + (oreVeinRadiusMax - oreVeinRadiusMin) * radiusT;
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dist2 = dx * dx + dz * dz;
                        if (dist2 > radius * radius) continue;
                        if (dist2 < bestDist2) {
                            bestDist2 = dist2;
                            const uint32_t oreSeedValue = hash2DInt(vx * 313 + oreSeed * 7, vz * 571 - oreSeed * 11);
                            bestVariant = static_cast<int>((oreSeedValue >> 5u) & 0x3u);
                        }
                    }
                }
                return bestVariant;
            };
            auto chalkVeinForColumn = [&](int worldXi, int worldZi) -> bool {
                if (!chalkEnabled || !chalkProto) return false;
                const int cellX = floorDivInt(worldXi, chalkVeinCellSize);
                const int cellZ = floorDivInt(worldZi, chalkVeinCellSize);
                float bestDist2 = std::numeric_limits<float>::max();
                bool inVein = false;
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + chalkSeed * 29, vz - chalkSeed * 31);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > chalkVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(chalkVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(chalkVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 157 + chalkSeed, vz * 271 - chalkSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = chalkVeinRadiusMin + (chalkVeinRadiusMax - chalkVeinRadiusMin) * radiusT;
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dist2 = dx * dx + dz * dz;
                        if (dist2 > radius * radius) continue;
                        if (!inVein || dist2 < bestDist2) {
                            inVein = true;
                            bestDist2 = dist2;
                        }
                    }
                }
                return inVein;
            };
            auto graniteVeinForColumn = [&](int worldXi, int worldZi) -> bool {
                if (!graniteEnabled || !graniteProto) return false;
                const int cellX = floorDivInt(worldXi, graniteVeinCellSize);
                const int cellZ = floorDivInt(worldZi, graniteVeinCellSize);
                float bestDist2 = std::numeric_limits<float>::max();
                bool inVein = false;
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + graniteSeed * 37, vz - graniteSeed * 41);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > graniteVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(graniteVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(graniteVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 163 + graniteSeed, vz * 283 - graniteSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = graniteVeinRadiusMin + (graniteVeinRadiusMax - graniteVeinRadiusMin) * radiusT;
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dist2 = dx * dx + dz * dz;
                        if (dist2 > radius * radius) continue;
                        if (!inVein || dist2 < bestDist2) {
                            inVein = true;
                            bestDist2 = dist2;
                        }
                    }
                }
                return inVein;
            };
            auto clayVeinForColumn = [&](int worldXi, int worldZi) -> bool {
                if (!clayEnabled || !clayProto) return false;
                const int cellX = floorDivInt(worldXi, clayVeinCellSize);
                const int cellZ = floorDivInt(worldZi, clayVeinCellSize);
                float bestDist2 = std::numeric_limits<float>::max();
                bool inVein = false;
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + claySeed * 29, vz - claySeed * 31);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > clayVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(clayVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(clayVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 157 + claySeed, vz * 271 - claySeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = clayVeinRadiusMin + (clayVeinRadiusMax - clayVeinRadiusMin) * radiusT;
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dist2 = dx * dx + dz * dz;
                        if (dist2 > radius * radius) continue;
                        if (!inVein || dist2 < bestDist2) {
                            inVein = true;
                            bestDist2 = dist2;
                        }
                    }
                }
                return inVein;
            };
            auto caveCarvedAt = [&](float sampleX, float sampleY, float sampleZ, int surfaceY, bool allowCaves) -> bool {
                if (!allowCaves) return false;
                if (sampleY > static_cast<float>(surfaceY)) return false;
                float v1 = 0.0f;
                float v2 = 0.0f;
                if (!sampleCaveField(sampleX, sampleY, sampleZ, v1, v2)) return false;
                // Match cave carving profile used for land so ore can bias toward visible cave walls.
                float depth = static_cast<float>(surfaceY) - sampleY;
                if (depth <= 3.0f) return false;
                float t = std::clamp(depth / 24.0f, 0.0f, 1.0f);
                float thrA = 0.72f + (0.62f - 0.72f) * t;
                float thrB = 0.68f + (0.58f - 0.68f) * t;
                if (isDepthLevel) {
                    thrA -= 0.10f;
                    thrB -= 0.10f;
                }
                return (v1 > thrA) || (v2 > thrB);
            };

            int waterSurfaceY = static_cast<int>(std::floor(cfg.waterSurface));
            int waterFloorY = static_cast<int>(std::floor(cfg.waterFloor));
            const int waterFloorYLower = waterFloorY - 1;
            const int seabedPortalY = waterFloorY - 2;
            const int depthPortalY = cfg.minY + 1;
            const bool unifiedDepthsEnabled = isExpanseLevel
                && isOverworldDimension
                && getRegistryBool(baseSystem, "UnifiedDepthsEnabled", true);
            const int configuredUnifiedDepthsTopY = getRegistryInt(baseSystem, "UnifiedDepthsTopY", -20);
            const int configuredUnifiedDepthsMinY = getRegistryInt(baseSystem, "UnifiedDepthsMinY", -70);
            const int unifiedDepthsTopY = std::max(configuredUnifiedDepthsTopY, configuredUnifiedDepthsMinY);
            const int unifiedDepthsMinY = std::min(configuredUnifiedDepthsTopY, configuredUnifiedDepthsMinY);
            const int expanseDepthSplitY = isExpanseLevel ? (unifiedDepthsTopY + 1) : seabedPortalY;
            const int unifiedDepthRiverFloorGuard = std::max(
                1,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverFloorGuard", 2)
            );
            const int unifiedDepthRiverRoofGuard = std::max(
                1,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverRoofGuard", 2)
            );
            const int unifiedDepthRiverSeed = getRegistryInt(baseSystem, "UnifiedDepthsRiverSeed", 9117);
            const float unifiedDepthRiverScale = std::max(24.0f, getRegistryFloat(baseSystem, "UnifiedDepthsRiverScale", 128.0f));
            const float unifiedDepthRiverTerrainScale = std::max(
                24.0f,
                getRegistryFloat(baseSystem, "UnifiedDepthsRiverTerrainScale", 220.0f)
            );
            const float unifiedDepthRiverTerrainRelief = std::max(
                0.0f,
                getRegistryFloat(baseSystem, "UnifiedDepthsRiverTerrainRelief", 24.0f)
            );
            const float unifiedDepthRiverThresholdMin = glm::clamp(
                getRegistryFloat(baseSystem, "UnifiedDepthsRiverThresholdMin", 0.038f),
                0.004f,
                0.35f
            );
            const float unifiedDepthRiverThresholdMax = glm::clamp(
                getRegistryFloat(baseSystem, "UnifiedDepthsRiverThresholdMax", 0.085f),
                unifiedDepthRiverThresholdMin,
                0.55f
            );
            const int unifiedDepthRiverMinWaterY = unifiedDepthsMinY + unifiedDepthRiverFloorGuard + 2;
            const int unifiedDepthRiverMaxWaterY = unifiedDepthsTopY - unifiedDepthRiverRoofGuard - 1;
            const bool unifiedDepthRiverBandValid = (unifiedDepthRiverMinWaterY <= unifiedDepthRiverMaxWaterY);
            const int unifiedDepthRiverWaterY = unifiedDepthRiverBandValid
                ? std::clamp(
                    getRegistryInt(baseSystem, "UnifiedDepthsRiverWaterY", -45),
                    unifiedDepthRiverMinWaterY,
                    unifiedDepthRiverMaxWaterY
                )
                : unifiedDepthsMinY + 2;
            const int unifiedDepthRiverDepthMin = std::max(2, getRegistryInt(baseSystem, "UnifiedDepthsRiverDepthMin", 3));
            const int unifiedDepthRiverDepthMax = std::max(
                unifiedDepthRiverDepthMin,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverDepthMax", 9)
            );
            const float unifiedDepthRiverDepthMultiplier = std::max(
                1.0f,
                getRegistryFloat(baseSystem, "UnifiedDepthsRiverDepthMultiplier", 3.0f)
            );
            const int unifiedDepthRiverChannelLowerMin = std::max(
                0,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverChannelLowerMin", 2)
            );
            const int unifiedDepthRiverChannelLowerMax = std::max(
                unifiedDepthRiverChannelLowerMin,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverChannelLowerMax", 6)
            );
            const int unifiedDepthRiverSeatExtra = std::max(
                0,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverSeatExtra", 10)
            );
            const int unifiedDepthRiverWaterlineExtraLower = std::max(
                0,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverWaterlineExtraLower", 3)
            );
            const int unifiedDepthRiverCeilingCarveMin = std::max(
                0,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverCeilingCarveMin", 10)
            );
            const int unifiedDepthRiverCeilingCarveMax = std::max(
                unifiedDepthRiverCeilingCarveMin,
                getRegistryInt(baseSystem, "UnifiedDepthsRiverCeilingCarveMax", 15)
            );
            const bool depthLavaFloorEnabled = getRegistryBool(baseSystem, "DepthLavaFloorEnabled", true);
            const int depthLavaTopY = getRegistryInt(baseSystem, "DepthLavaTopY", -66);
            const int depthLavaBottomY = getRegistryInt(baseSystem, "DepthLavaBottomY", -69);
            const int depthLodestoneSeed = getRegistryInt(baseSystem, "DepthLodestoneSeed", 12037);
            const int depthLodestoneVeinCellSize = std::max(6, getRegistryInt(baseSystem, "DepthLodestoneVeinCellSize", 16));
            const float depthLodestoneVeinChance = glm::clamp(getRegistryFloat(baseSystem, "DepthLodestoneVeinChance", 0.23f), 0.0f, 1.0f);
            const float depthLodestoneReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "DepthLodestoneReplaceChance", 0.42f), 0.0f, 1.0f);
            const float depthLodestoneRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "DepthLodestoneRadiusMin", 2.0f));
            const float depthLodestoneRadiusMax = std::max(depthLodestoneRadiusMin, getRegistryFloat(baseSystem, "DepthLodestoneRadiusMax", 4.5f));
            const float depthLodestoneVerticalRadiusScale = glm::clamp(
                getRegistryFloat(baseSystem, "DepthLodestoneVerticalRadiusScale", 0.68f),
                0.2f,
                3.0f
            );
            const int depthCopperSeed = getRegistryInt(baseSystem, "DepthCopperSulfateSeed", 17011);
            const int depthCopperVeinCellSize = std::max(6, getRegistryInt(baseSystem, "DepthCopperSulfateVeinCellSize", 14));
            const float depthCopperVeinChance = glm::clamp(getRegistryFloat(baseSystem, "DepthCopperSulfateVeinChance", 0.17f), 0.0f, 1.0f);
            const float depthCopperReplaceChance = glm::clamp(getRegistryFloat(baseSystem, "DepthCopperSulfateReplaceChance", 0.34f), 0.0f, 1.0f);
            const float depthCopperRadiusMin = std::max(0.5f, getRegistryFloat(baseSystem, "DepthCopperSulfateRadiusMin", 1.8f));
            const float depthCopperRadiusMax = std::max(depthCopperRadiusMin, getRegistryFloat(baseSystem, "DepthCopperSulfateRadiusMax", 3.6f));
            const float depthCopperVerticalRadiusScale = glm::clamp(
                getRegistryFloat(baseSystem, "DepthCopperSulfateVerticalRadiusScale", 0.68f),
                0.2f,
                3.0f
            );
            const int depthOreCenterYPadding = std::max(
                0,
                getRegistryInt(baseSystem, "DepthOreCenterYPadding", 2)
            );
            const int depthPurplePatchPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "DepthPurplePatchPercent", 16)));
            const int depthRustBeamSeed = getRegistryInt(baseSystem, "DepthRustBeamSeed", 24611);
            const int depthRustBeamPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "DepthRustBeamPercent", 4)));
            const int depthMossPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "DepthMossPercent", 9)));
            const int depthBigLilypadPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "DepthBigLilypadPercent", 3)));
            const int depthRiverCrystalPercent = std::max(0, std::min(100, getRegistryInt(baseSystem, "DepthRiverCrystalPercent", 85)));
            const int depthRiverCrystalBankSearchDown = std::max(1, getRegistryInt(baseSystem, "DepthRiverCrystalBankSearchDown", 40));
            const int depthRiverCrystalBankSearchRadius = std::max(0, std::min(4, getRegistryInt(baseSystem, "DepthRiverCrystalBankSearchRadius", 2)));
            auto depthVeinCenterY = [&](uint32_t seedValue) -> int {
                const int minCenterY = unifiedDepthsMinY + depthOreCenterYPadding;
                const int maxCenterY = unifiedDepthsTopY - depthOreCenterYPadding;
                if (maxCenterY <= minCenterY) {
                    return (unifiedDepthsMinY + unifiedDepthsTopY) / 2;
                }
                const uint32_t span = static_cast<uint32_t>(maxCenterY - minCenterY + 1);
                return minCenterY + static_cast<int>(seedValue % span);
            };
            auto depthLodestoneVariantAt = [&](int worldXi, int worldYi, int worldZi) -> int {
                if (!unifiedDepthsEnabled) return -1;
                const int cellX = floorDivInt(worldXi, depthLodestoneVeinCellSize);
                const int cellZ = floorDivInt(worldZi, depthLodestoneVeinCellSize);
                int bestVariant = -1;
                float bestScore = std::numeric_limits<float>::max();
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + depthLodestoneSeed * 19, vz - depthLodestoneSeed * 23);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > depthLodestoneVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(depthLodestoneVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(depthLodestoneVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 179 + depthLodestoneSeed, vz * 211 - depthLodestoneSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = depthLodestoneRadiusMin + (depthLodestoneRadiusMax - depthLodestoneRadiusMin) * radiusT;
                        const float radiusY = std::max(1.0f, radius * depthLodestoneVerticalRadiusScale);
                        const uint32_t ySeed = hash2DInt(
                            vx * 241 + depthLodestoneSeed * 13,
                            vz * 337 - depthLodestoneSeed * 17
                        );
                        const int centerY = depthVeinCenterY(ySeed);
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dy = static_cast<float>(worldYi - centerY);
                        const float score = (dx * dx + dz * dz) / std::max(0.0001f, radius * radius)
                            + (dy * dy) / std::max(0.0001f, radiusY * radiusY);
                        if (score > 1.0f) continue;
                        if (score < bestScore) {
                            bestScore = score;
                            const uint32_t variantSeed = hash2DInt(vx * 313 + depthLodestoneSeed * 7, vz * 571 - depthLodestoneSeed * 11);
                            bestVariant = static_cast<int>(variantSeed % static_cast<uint32_t>(depthLodestoneOreProtos.size()));
                        }
                    }
                }
                return bestVariant;
            };
            auto depthCopperVeinAt = [&](int worldXi, int worldYi, int worldZi) -> bool {
                if (!unifiedDepthsEnabled) return false;
                const int cellX = floorDivInt(worldXi, depthCopperVeinCellSize);
                const int cellZ = floorDivInt(worldZi, depthCopperVeinCellSize);
                float bestScore = std::numeric_limits<float>::max();
                bool inVein = false;
                for (int oz = -1; oz <= 1; ++oz) {
                    for (int ox = -1; ox <= 1; ++ox) {
                        const int vx = cellX + ox;
                        const int vz = cellZ + oz;
                        const uint32_t seed = hash2DInt(vx + depthCopperSeed * 17, vz - depthCopperSeed * 29);
                        const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
                        if (chanceRoll > depthCopperVeinChance) continue;
                        const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                        const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                        const float centerX = (static_cast<float>(vx) + offsetX) * static_cast<float>(depthCopperVeinCellSize);
                        const float centerZ = (static_cast<float>(vz) + offsetZ) * static_cast<float>(depthCopperVeinCellSize);
                        const uint32_t radiusSeed = hash2DInt(vx * 137 + depthCopperSeed, vz * 263 - depthCopperSeed);
                        const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                        const float radius = depthCopperRadiusMin + (depthCopperRadiusMax - depthCopperRadiusMin) * radiusT;
                        const float radiusY = std::max(1.0f, radius * depthCopperVerticalRadiusScale);
                        const uint32_t ySeed = hash2DInt(
                            vx * 223 + depthCopperSeed * 19,
                            vz * 311 - depthCopperSeed * 23
                        );
                        const int centerY = depthVeinCenterY(ySeed);
                        const float dx = static_cast<float>(worldXi) - centerX;
                        const float dz = static_cast<float>(worldZi) - centerZ;
                        const float dy = static_cast<float>(worldYi - centerY);
                        const float score = (dx * dx + dz * dz) / std::max(0.0001f, radius * radius)
                            + (dy * dy) / std::max(0.0001f, radiusY * radiusY);
                        if (score > 1.0f) continue;
                        if (!inVein || score < bestScore) {
                            inVein = true;
                            bestScore = score;
                        }
                    }
                }
                return inVein;
            };
            int sectionMinY = columnMode ? columnMinY : (sectionCoord.y * size * scale);
            int sectionMaxY = columnMode ? columnMaxY : (sectionMinY + size * scale - 1);
            int minY = std::min(cfg.minY, waterFloorY);
            if (isExpanseLevel) {
                minY = std::min(minY, seabedPortalY);
                if (unifiedDepthsEnabled) {
                    minY = std::min(minY, unifiedDepthsMinY);
                }
            } else if (isDepthLevel) {
                minY = std::min(minY, depthPortalY);
            }
            if (islandQuadrants && cfg.jungleVolcanoEnabled) {
                const int volcanoChamberBottomY = volcanoLavaSurfaceY - volcanoLavaDepth - volcanoChamberDepth;
                minY = std::min(minY, volcanoChamberBottomY - 8);
            }
            if (isExpanseLevel && unifiedDepthsEnabled) {
                minY = std::max(minY, unifiedDepthsMinY);
            }
            int maxY = computeExpanseMaxY(baseSystem, worldCtx, cfg);
            const int totalColumns = size * size;
            const bool terrainDetailPass = !runPostFeatures && startColumn >= totalColumns;
            const bool terrainFillPass = !runPostFeatures && !terrainDetailPass;
            const int logicalStartColumn = terrainDetailPass ? (startColumn - totalColumns) : startColumn;
            if (sectionMaxY < minY || sectionMinY > maxY) {
                outNextColumn = runPostFeatures ? totalColumns : (totalColumns * 2);
                outCompleted = true;
                outPostFeaturesCompleted = true;
                return true;
            }

            int clampedStartColumn = std::max(0, std::min(logicalStartColumn, totalColumns));
            int clampedEndColumn = totalColumns;
            if (maxColumns > 0) {
                clampedEndColumn = std::min(totalColumns, clampedStartColumn + maxColumns);
            }
            bool wroteAny = false;
            VoxelColumn* directColumn = nullptr;
            const VoxelColumnKey directColumnKey{glm::ivec2(sectionCoord.x, sectionCoord.z)};
            auto ensureDirectColumn = [&]() -> VoxelColumn* {
                if (!directColumn) {
                    directColumn = voxelWorld.ensureColumnForWrite(directColumnKey);
                }
                return directColumn;
            };
            auto writeVoxel = [&](const glm::ivec3& worldCoord,
                                  uint32_t id,
                                  uint32_t color) -> bool {
                if (columnMode && !runPostFeatures
                    && floorDivInt(worldCoord.x, size) == sectionCoord.x
                    && floorDivInt(worldCoord.z, size) == sectionCoord.z) {
                    VoxelColumn* column = ensureDirectColumn();
                    if (!column) return false;
                    const int localX = worldCoord.x - sectionCoord.x * size;
                    const int localZ = worldCoord.z - sectionCoord.z * size;
                    return voxelWorld.writeColumnBlock(*column, localX, worldCoord.y, localZ, id, color);
                }
                voxelWorld.setBlock(worldCoord, id, color, false);
                return true;
            };
            auto writeVoxelRun = [&](int localX,
                                     int localZ,
                                     int minRunY,
                                     int maxRunY,
                                     uint32_t id,
                                     uint32_t color) -> int {
                if (minRunY > maxRunY) return 0;
                if (columnMode && !runPostFeatures) {
                    VoxelColumn* column = ensureDirectColumn();
                    if (!column) return 0;
                    return voxelWorld.writeColumnRun(*column, localX, localZ, minRunY, maxRunY, id, color);
                }
                int changed = 0;
                for (int y = minRunY; y <= maxRunY; ++y) {
                    voxelWorld.setBlock(glm::ivec3(sectionCoord.x * size + localX, y, sectionCoord.z * size + localZ),
                                        id,
                                        color,
                                        false);
                    changed += 1;
                }
                return changed;
            };
            struct TerrainSurfaceSample {
                bool isLand = false;
                float height = 0.0f;
            };
            std::unordered_map<uint64_t, TerrainSurfaceSample> terrainSampleCache;
            auto terrainSampleKey = [](int x, int z) -> uint64_t {
                return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32u)
                    | static_cast<uint32_t>(z);
            };
            auto sampleTerrainExact = [&](int x, int z) -> TerrainSurfaceSample {
                const uint64_t key = terrainSampleKey(x, z);
                auto it = terrainSampleCache.find(key);
                if (it != terrainSampleCache.end()) return it->second;
                TerrainSurfaceSample sample;
                sample.isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                    worldCtx,
                    static_cast<float>(x),
                    static_cast<float>(z),
                    sample.height
                );
                terrainSampleCache.emplace(key, sample);
                return sample;
            };
            const int terrainCoarseHeightStep = std::max(1, getRegistryInt(baseSystem, "TerrainCoarseHeightStep", 1));
            auto sampleTerrainColumn = [&](float worldX, float worldZ, float& outHeight) -> bool {
                const int sampleX = static_cast<int>(std::floor(worldX));
                const int sampleZ = static_cast<int>(std::floor(worldZ));
                if (terrainCoarseHeightStep <= 1) {
                    const TerrainSurfaceSample sample = sampleTerrainExact(sampleX, sampleZ);
                    outHeight = sample.height;
                    return sample.isLand;
                }

                const int baseX = floorDivInt(sampleX, terrainCoarseHeightStep) * terrainCoarseHeightStep;
                const int baseZ = floorDivInt(sampleZ, terrainCoarseHeightStep) * terrainCoarseHeightStep;
                const int nextX = baseX + terrainCoarseHeightStep;
                const int nextZ = baseZ + terrainCoarseHeightStep;
                const float tx = glm::clamp(
                    (worldX - static_cast<float>(baseX)) / static_cast<float>(terrainCoarseHeightStep),
                    0.0f,
                    1.0f
                );
                const float tz = glm::clamp(
                    (worldZ - static_cast<float>(baseZ)) / static_cast<float>(terrainCoarseHeightStep),
                    0.0f,
                    1.0f
                );
                const TerrainSurfaceSample s00 = sampleTerrainExact(baseX, baseZ);
                const TerrainSurfaceSample s10 = sampleTerrainExact(nextX, baseZ);
                const TerrainSurfaceSample s01 = sampleTerrainExact(baseX, nextZ);
                const TerrainSurfaceSample s11 = sampleTerrainExact(nextX, nextZ);
                const float hx0 = glm::mix(s00.height, s10.height, tx);
                const float hx1 = glm::mix(s01.height, s11.height, tx);
                outHeight = glm::mix(hx0, hx1, tz);
                return outHeight > cfg.waterSurface
                    || s00.isLand
                    || s10.isLand
                    || s01.isLand
                    || s11.isLand;
            };
            std::vector<glm::ivec3> pendingChalkPlacements;
            pendingChalkPlacements.reserve(static_cast<size_t>(totalColumns));
            for (int column = clampedStartColumn; column < clampedEndColumn; ++column) {
                int z = column / size;
                int x = column - z * size;
                    float worldX = static_cast<float>((sectionCoord.x * size + x) * scale);
                    float worldZ = static_cast<float>((sectionCoord.z * size + z) * scale);
                    const int worldXi = static_cast<int>(std::floor(worldX));
                    const int worldZi = static_cast<int>(std::floor(worldZ));
                    float height = 0.0f;
                    bool isLand = sampleTerrainColumn(worldX, worldZ, height);
                    int surfaceY = static_cast<int>(std::floor(height));
                    if (isDepthLevel) {
                        // Depth dimension is intentionally all cave-terrain land columns;
                        // do not generate expanse-style seabed/ocean bands here.
                        isLand = true;
                    }
                    bool isBeach = isLand && (surfaceY <= waterSurfaceY + static_cast<int>(cfg.beachHeight));
                    const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(worldCtx, worldX, worldZ);
                    const uint32_t coniferVariantSeed = hash2DInt(worldXi + 12011, worldZi - 9029);
                    const size_t coniferVariantIndex = static_cast<size_t>(
                        coniferVariantSeed % static_cast<uint32_t>(surfaceProtoConiferVariants.size())
                    );
                    const Entity* biomeSurfaceProto = surfaceProtoConiferVariants[coniferVariantIndex];
                    glm::vec3 biomeSurfaceColor = grassColor;
                    if (biomeID == 1) {
                        biomeSurfaceProto = surfaceProtoMeadow;
                        biomeSurfaceColor = glm::mix(grassColor, glm::vec3(0.24f, 0.86f, 0.22f), 0.45f);
                    } else if (biomeID == 2) {
                        biomeSurfaceProto = sandDesertProto;
                        biomeSurfaceColor = sandColor;
                    } else if (biomeID == 3) {
                        if (cfg.islandRadius > 0.0f) {
                            biomeSurfaceProto = surfaceProtoJungle;
                            biomeSurfaceColor = glm::mix(grassColor, glm::vec3(0.13f, 0.56f, 0.20f), 0.35f);
                        } else {
                            biomeSurfaceProto = surfaceProtoSnow;
                            biomeSurfaceColor = snowColor;
                        }
                    } else if (biomeID == 4) {
                        biomeSurfaceProto = surfaceProtoBareWinter;
                        biomeSurfaceColor = glm::mix(glm::vec3(0.43f, 0.40f, 0.34f), grassColor, 0.15f);
                    } else if (biomeID == 5) {
                        biomeSurfaceProto = surfaceProtoProceduralGrassBiome;
                        biomeSurfaceColor = glm::vec3(1.0f);
                    }
                    float volcanoDist = std::numeric_limits<float>::max();
                    bool volcanoArea = false;
                    bool craterArea = false;
                    if (islandQuadrants && cfg.jungleVolcanoEnabled && biomeID == 3) {
                        const float volcanoDx = worldX - volcanoCenterX;
                        const float volcanoDz = worldZ - volcanoCenterZ;
                        volcanoDist = std::sqrt(volcanoDx * volcanoDx + volcanoDz * volcanoDz);
                        volcanoArea = (volcanoDist <= volcanoOuterRadius);
                        craterArea = (volcanoDist <= volcanoCraterRadius);
                    }
                    if (volcanoArea) {
                        // Volcano footprint uses rock instead of grass/soil surfaces.
                        biomeSurfaceProto = stoneProto;
                        biomeSurfaceColor = glm::mix(stoneColor, glm::vec3(0.12f, 0.10f, 0.08f), 0.35f);
                        isBeach = false;
                    }
                    const Entity* topSurfaceProto = isBeach ? sandBeachProto : biomeSurfaceProto;
                    if (biomeID == 2 && !isBeach) {
                        topSurfaceProto = sandDesertProto;
                    }
                    if (isDepthLevel) {
                        isBeach = false;
                        biomeSurfaceProto = stoneProto;
                        biomeSurfaceColor = stoneColor;
                        topSurfaceProto = stoneProto;
                    }
                    bool lavaColumnActive = false;
                    bool chamberCarveColumn = false;
                    int lavaFillMinY = 0;
                    int lavaSurfaceY = volcanoLavaSurfaceY;
                    int lavaBottomY = lavaSurfaceY - volcanoLavaDepth + 1;
                    int chamberBottomY = lavaBottomY - volcanoChamberDepth;
                    if (craterArea) {
                        lavaFillMinY = lavaBottomY;
                        lavaColumnActive = true;
                        chamberCarveColumn = (volcanoDist <= volcanoChamberRadius);
                    }
                    bool inIsland = false;
                    if (cfg.islandRadius > 0.0f) {
                        float dx = worldX - cfg.islandCenterX;
                        float dz = worldZ - cfg.islandCenterZ;
                        float dist = std::sqrt(dx * dx + dz * dz);
                        inIsland = dist < cfg.islandRadius;
                    }
                    bool lakeColumn = false;
                    int lakeWaterY = surfaceY;
                    if (surfaceLakeEnabled
                        && isLand
                        && !isBeach
                        && !volcanoArea
                        && surfaceY >= (waterSurfaceY + surfaceLakeMinAboveSea)) {
                        const int lakeCellX = floorDivInt(worldXi, surfaceLakeCellSize);
                        const int lakeCellZ = floorDivInt(worldZi, surfaceLakeCellSize);
                        float bestWeight = 0.0f;
                        const PondCellInfo* bestLake = nullptr;
                        for (int oz = -1; oz <= 1; ++oz) {
                            for (int ox = -1; ox <= 1; ++ox) {
                                const PondCellInfo& lake = lakeCellInfoFor(lakeCellX + ox, lakeCellZ + oz);
                                if (!lake.valid || !lake.centerIsLand) continue;
                                if (lake.centerSurfaceY < (waterSurfaceY + surfaceLakeMinAboveSea)) continue;
                                const float dx = worldX - lake.centerX;
                                const float dz = worldZ - lake.centerZ;
                                const float dist2 = dx * dx + dz * dz;
                                const float radius2 = lake.radius * lake.radius;
                                if (dist2 > radius2) continue;
                                const float dist = std::sqrt(dist2);
                                const float weight = 1.0f - (dist / lake.radius);
                                if (!bestLake || weight > bestWeight) {
                                    bestWeight = weight;
                                    bestLake = &lake;
                                }
                            }
                        }
                        if (bestLake) {
                            const int centerSurfaceY = bestLake->centerSurfaceY;
                            const int lakeDepthAllowance = bestLake->depth + 6;
                            if (std::abs(surfaceY - centerSurfaceY) <= lakeDepthAllowance) {
                                const float innerT = std::clamp((bestWeight - 0.06f) / 0.94f, 0.0f, 1.0f);
                                if (innerT > 0.0f) {
                                    lakeWaterY = centerSurfaceY - 1 - surfaceLakeChannelLower;
                                    if (lakeWaterY < surfaceY) {
                                        const int lakeDepthHere = std::max(
                                            2,
                                            static_cast<int>(std::round(static_cast<float>(bestLake->depth + surfaceLakeDepthExtra) * innerT * innerT))
                                        );
                                        const int lakeFloorY = lakeWaterY - lakeDepthHere;
                                        if (lakeFloorY < surfaceY && lakeWaterY > waterSurfaceY) {
                                            surfaceY = lakeFloorY;
                                            lakeColumn = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    bool pondColumn = false;
                    int pondWaterY = surfaceY;
                    if (surfacePondEnabled
                        && !lakeColumn
                        && isLand
                        && !volcanoArea
                        && surfaceY >= (waterSurfaceY + surfacePondMinAboveSea)) {
                        const int pondCellX = floorDivInt(worldXi, surfacePondCellSize);
                        const int pondCellZ = floorDivInt(worldZi, surfacePondCellSize);
                        float bestWeight = 0.0f;
                        const PondCellInfo* bestPond = nullptr;
                        for (int oz = -1; oz <= 1; ++oz) {
                            for (int ox = -1; ox <= 1; ++ox) {
                                const PondCellInfo& pond = pondCellInfoFor(pondCellX + ox, pondCellZ + oz);
                                if (!pond.valid || !pond.centerIsLand) continue;
                                if (pond.centerSurfaceY < (waterSurfaceY + surfacePondMinAboveSea)) continue;
                                const float dx = worldX - pond.centerX;
                                const float dz = worldZ - pond.centerZ;
                                const float dist2 = dx * dx + dz * dz;
                                const float radius2 = pond.radius * pond.radius;
                                if (dist2 > radius2) continue;
                                const float dist = std::sqrt(dist2);
                                const float weight = 1.0f - (dist / pond.radius);
                                if (!bestPond || weight > bestWeight) {
                                    bestWeight = weight;
                                    bestPond = &pond;
                                }
                            }
                        }
                        if (bestPond) {
                            const int centerSurfaceY = bestPond->centerSurfaceY;
                            const int pondDepthAllowance = bestPond->depth + 3;
                            if (std::abs(surfaceY - centerSurfaceY) <= pondDepthAllowance) {
                                const float innerT = std::clamp((bestWeight - 0.12f) / 0.88f, 0.0f, 1.0f);
                                if (innerT > 0.0f) {
                                    pondWaterY = centerSurfaceY - 1 - surfacePondChannelLower;
                                    if (pondWaterY < surfaceY) {
                                        const int pondDepthHere = std::max(
                                            1,
                                            static_cast<int>(std::round(static_cast<float>(bestPond->depth) * innerT * innerT))
                                        );
                                        const int pondFloorY = pondWaterY - pondDepthHere;
                                        if (pondFloorY < surfaceY && pondWaterY > waterSurfaceY) {
                                            surfaceY = pondFloorY;
                                            pondColumn = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    const bool basinColumn = lakeColumn || pondColumn;
                    const int basinWaterY = lakeColumn ? lakeWaterY : pondWaterY;
                    bool riverColumn = false;
                    int riverWaterY = surfaceY;
                    if (surfaceRiverEnabled
                        && !basinColumn
                        && isLand
                        && !volcanoArea
                        && surfaceY >= (waterSurfaceY + surfaceRiverMinAboveSea)) {
                        const float warpSampleX = worldX / surfaceRiverWarpScale;
                        const float warpSampleZ = worldZ / surfaceRiverWarpScale;
                        const float warpNoiseX =
                            fbmValueNoise2D(surfaceRiverSeed * 13 + 7, warpSampleX, warpSampleZ, 3) * 2.0f - 1.0f;
                        const float warpNoiseZ =
                            fbmValueNoise2D(surfaceRiverSeed * 17 - 5, warpSampleX + 19.4f, warpSampleZ - 11.2f, 3) * 2.0f - 1.0f;
                        const float riverSampleX = (worldX + warpNoiseX * surfaceRiverWarpStrength) / surfaceRiverScale;
                        const float riverSampleZ = (worldZ + warpNoiseZ * surfaceRiverWarpStrength) / surfaceRiverScale;
                        const float primary = fbmValueNoise2D(surfaceRiverSeed, riverSampleX, riverSampleZ, 4);
                        const float ridge = std::abs(primary * 2.0f - 1.0f);
                        const float widthNoise = fbmValueNoise2D(
                            surfaceRiverSeed * 23 + 101,
                            riverSampleX * 0.65f + 3.7f,
                            riverSampleZ * 0.65f - 2.1f,
                            2
                        );
                        const float ridgeThreshold = surfaceRiverThresholdMin
                            + (surfaceRiverThresholdMax - surfaceRiverThresholdMin) * widthNoise;
                        if (ridge < ridgeThreshold) {
                            const float innerT = std::clamp(1.0f - (ridge / ridgeThreshold), 0.0f, 1.0f);
                            const float depthNoise = fbmValueNoise2D(
                                surfaceRiverSeed * 31 + 29,
                                riverSampleX * 0.8f - 4.2f,
                                riverSampleZ * 0.8f + 5.3f,
                                2
                            );
                            const int channelLower = surfaceRiverChannelLowerMin
                                + static_cast<int>(std::round(
                                    static_cast<float>(surfaceRiverChannelLowerMax - surfaceRiverChannelLowerMin) * depthNoise
                                ));
                            const int baseRiverWaterY = surfaceY - channelLower;
                            if (baseRiverWaterY > waterSurfaceY) {
                                const int depthBase = surfaceRiverDepthMin
                                    + static_cast<int>(std::round((surfaceRiverDepthMax - surfaceRiverDepthMin) * depthNoise));
                                const int depthBaseBoosted = std::max(
                                    1,
                                    static_cast<int>(std::round(static_cast<float>(depthBase) * surfaceRiverDepthMultiplier))
                                );
                                const int depthHere = std::max(
                                    2,
                                    static_cast<int>(std::round(static_cast<float>(depthBaseBoosted) * innerT * innerT))
                                );
                                const int baseRiverFloorY = baseRiverWaterY - depthHere;
                                riverWaterY = std::max(
                                    baseRiverFloorY + 2,
                                    baseRiverWaterY - surfaceRiverWaterlineExtraLower
                                );
                                if (riverWaterY > waterSurfaceY && baseRiverFloorY < surfaceY) {
                                    surfaceY = baseRiverFloorY;
                                    riverColumn = true;
                                }
                            }
                        }
                    }
                    const bool waterFeatureColumn = basinColumn || riverColumn;
                    const int waterFeatureWaterY = basinColumn ? basinWaterY : riverWaterY;
                    const uint32_t packedFeatureWaterColor = riverColumn
                        ? packedWaterColorRiver
                        : (lakeColumn ? packedWaterColorLake
                                      : (pondColumn ? packedWaterColorPond : packedWaterColorUnknown));
                    bool unifiedDepthRiverColumn = false;
                    int unifiedDepthRiverBottomY = unifiedDepthRiverWaterY - unifiedDepthRiverDepthMin;
                    int unifiedDepthRiverTopY = unifiedDepthRiverWaterY;
                    int unifiedDepthRiverCeilingCarveTopY = unifiedDepthRiverWaterY;
                    if (unifiedDepthsEnabled && unifiedDepthRiverBandValid) {
                        const float riverSampleX = worldX / unifiedDepthRiverScale;
                        const float riverSampleZ = worldZ / unifiedDepthRiverScale;
                        const float primary = fbmValueNoise2D(unifiedDepthRiverSeed, riverSampleX, riverSampleZ, 4);
                        const float ridge = std::abs(primary * 2.0f - 1.0f);
                        const float widthNoise = fbmValueNoise2D(
                            unifiedDepthRiverSeed * 19 + 31,
                            riverSampleX * 0.77f + 3.1f,
                            riverSampleZ * 0.77f - 2.4f,
                            2
                        );
                        const float ridgeThreshold = unifiedDepthRiverThresholdMin
                            + (unifiedDepthRiverThresholdMax - unifiedDepthRiverThresholdMin) * widthNoise;
                        if (ridge < ridgeThreshold) {
                            const float innerT = std::clamp(1.0f - (ridge / ridgeThreshold), 0.0f, 1.0f);
                            const float terrainNoise = fbmValueNoise2D(
                                unifiedDepthRiverSeed * 41 + 73,
                                worldX / unifiedDepthRiverTerrainScale,
                                worldZ / unifiedDepthRiverTerrainScale,
                                3
                            );
                            const int riverRoofY = unifiedDepthsTopY - unifiedDepthRiverRoofGuard;
                            const int riverFloorY = unifiedDepthsMinY + unifiedDepthRiverFloorGuard;
                            const int terrainBandMinY = riverFloorY + 3;
                            const int terrainBandMaxY = riverRoofY - 1;
                            const float terrainSigned = terrainNoise * 2.0f - 1.0f;
                            const int terrainCenterY = std::clamp(
                                unifiedDepthRiverWaterY + unifiedDepthRiverSeatExtra + unifiedDepthRiverChannelLowerMin,
                                terrainBandMinY,
                                terrainBandMaxY
                            );
                            const int reliefOffset = static_cast<int>(std::round(unifiedDepthRiverTerrainRelief * terrainSigned));
                            const int localDepthTerrainY = std::clamp(
                                terrainCenterY + reliefOffset,
                                terrainBandMinY,
                                terrainBandMaxY
                            );
                            const float depthNoise = fbmValueNoise2D(
                                unifiedDepthRiverSeed * 29 + 17,
                                riverSampleX * 0.9f - 7.5f,
                                riverSampleZ * 0.9f + 4.7f,
                                2
                            );
                            const int channelLowerBase = unifiedDepthRiverChannelLowerMin
                                + static_cast<int>(std::round(
                                    static_cast<float>(unifiedDepthRiverChannelLowerMax - unifiedDepthRiverChannelLowerMin) * depthNoise
                                ));
                            const int channelLower = channelLowerBase + unifiedDepthRiverSeatExtra;
                            const int baseRiverWaterY = localDepthTerrainY - channelLower;
                            if (baseRiverWaterY < (riverFloorY + 1)) {
                                continue;
                            }
                            const int depthBase = unifiedDepthRiverDepthMin
                                + static_cast<int>(std::round(
                                    static_cast<float>(unifiedDepthRiverDepthMax - unifiedDepthRiverDepthMin) * depthNoise
                                ));
                            const int depthBaseBoosted = std::max(
                                1,
                                static_cast<int>(std::round(static_cast<float>(depthBase) * unifiedDepthRiverDepthMultiplier))
                            );
                            const int depthHere = std::max(
                                2,
                                static_cast<int>(std::round(static_cast<float>(depthBaseBoosted) * innerT * innerT))
                            );
                            const int baseRiverFloorY = baseRiverWaterY - depthHere;
                            const int riverWaterY = std::max(
                                baseRiverFloorY + 2,
                                baseRiverWaterY - unifiedDepthRiverWaterlineExtraLower
                            );
                            const int clampedWaterY = std::clamp(riverWaterY, riverFloorY + 1, riverRoofY - 1);
                            const int maxDepthHere = std::max(1, clampedWaterY - riverFloorY);
                            const int clampedDepthHere = std::clamp(depthHere, 1, maxDepthHere);
                            unifiedDepthRiverTopY = clampedWaterY;
                            unifiedDepthRiverBottomY = clampedWaterY - clampedDepthHere;
                            unifiedDepthRiverColumn = (unifiedDepthRiverBottomY <= unifiedDepthRiverTopY);
                            if (unifiedDepthRiverColumn && unifiedDepthRiverCeilingCarveMax > 0) {
                                const int carveSpan = unifiedDepthRiverCeilingCarveMax - unifiedDepthRiverCeilingCarveMin;
                                int carveUp = unifiedDepthRiverCeilingCarveMin;
                                if (carveSpan > 0) {
                                    const uint32_t carveSeed = hash3DInt(
                                        worldXi + unifiedDepthRiverSeed * 131,
                                        unifiedDepthRiverTopY + unifiedDepthRiverSeed * 193,
                                        worldZi - unifiedDepthRiverSeed * 157
                                    );
                                    carveUp += static_cast<int>(carveSeed % static_cast<uint32_t>(carveSpan + 1));
                                }
                                unifiedDepthRiverCeilingCarveTopY = std::min(
                                    unifiedDepthsTopY - 1,
                                    unifiedDepthRiverTopY + carveUp
                                );
                            }
                        }
                    }
                    const bool waterFeatureColumnForPortal = isLand && waterFeatureColumn && (waterFeatureWaterY > surfaceY);
                    const bool portalColumnExpanse = isExpanseLevel && (!isLand || waterFeatureColumnForPortal);
                    int oreVariant = -1;
                    if (terrainDetailPass && oreEnabled && isLand && !isBeach) {
                        oreVariant = oreVariantForColumn(worldXi, worldZi);
                    }
                    const int depthLavaBandTop = std::max(depthLavaTopY, depthLavaBottomY);
                    const int depthLavaBandBottom = std::min(depthLavaTopY, depthLavaBottomY);
                    auto depthBandCarveAtY = [&](int sampleY) {
                        float dv1 = 0.0f;
                        float dv2 = 0.0f;
                        if (!sampleCaveField(worldX, static_cast<float>(sampleY), worldZ, dv1, dv2)) {
                            return false;
                        }
                        const float bandT = std::clamp(
                            static_cast<float>(sampleY - unifiedDepthsMinY)
                                / static_cast<float>(std::max(1, unifiedDepthsTopY - unifiedDepthsMinY)),
                            0.0f,
                            1.0f
                        );
                        const float thrA = 0.57f + (0.64f - 0.57f) * bandT;
                        const float thrB = 0.53f + (0.61f - 0.53f) * bandT;
                        return (dv1 > thrA) || (dv2 > thrB);
                    };
                    int depthLavaColumnTopY = std::numeric_limits<int>::min();
                    if (isExpanseLevel
                        && unifiedDepthsEnabled
                        && depthLavaFloorEnabled
                        && depthLavaBandBottom <= depthLavaBandTop) {
                        const int depthLavaSeedScanTop = std::min(unifiedDepthsTopY - 1, depthLavaBandTop + 6);
                        for (int sampleY = depthLavaBandBottom; sampleY <= depthLavaSeedScanTop; ++sampleY) {
                            if (sampleY <= unifiedDepthsMinY || sampleY >= unifiedDepthsTopY) continue;
                            if (depthBandCarveAtY(sampleY)) {
                                depthLavaColumnTopY = std::max(depthLavaColumnTopY, sampleY);
                            }
                        }
                        if (depthLavaColumnTopY > unifiedDepthsMinY) {
                            // Guarantee a meaningful lava depth once seeded.
                            depthLavaColumnTopY = std::max(depthLavaColumnTopY, depthLavaBandTop);
                        }
                    }
                    const bool chalkColumnCandidate = terrainDetailPass
                        && chalkEnabled
                        && (chalkProto != nullptr)
                        && isLand
                        && chalkVeinForColumn(worldXi, worldZi);
                    const bool clayColumnCandidate = terrainDetailPass
                        && clayEnabled
                        && (clayProto != nullptr)
                        && isLand
                        && clayVeinForColumn(worldXi, worldZi);
                    const bool graniteColumnCandidate = terrainDetailPass
                        && graniteEnabled
                        && (graniteProto != nullptr)
                        && isLand
                        && (biomeID != 2)
                        && graniteVeinForColumn(worldXi, worldZi);
                    bool chalkReplaceRollPass = false;
                    if (chalkColumnCandidate) {
                        const uint32_t chalkSeedValue = hash3DInt(
                            worldXi + chalkSeed * 97,
                            surfaceY + chalkSeed * 131,
                            worldZi - chalkSeed * 151
                        );
                        const float chalkRoll = static_cast<float>((chalkSeedValue >> 8u) & 0xffu) / 255.0f;
                        chalkReplaceRollPass = chalkRoll <= chalkReplaceChance;
                    }
                    bool clayReplaceRollPass = false;
                    if (clayColumnCandidate) {
                        const uint32_t claySeedValue = hash3DInt(
                            worldXi + claySeed * 97,
                            surfaceY + claySeed * 131,
                            worldZi - claySeed * 151
                        );
                        const float clayRoll = static_cast<float>((claySeedValue >> 8u) & 0xffu) / 255.0f;
                        clayReplaceRollPass = clayRoll <= clayReplaceChance;
                    }
                    auto isDepthLavaTileId = [&](uint32_t id) {
                        if (id == 0u) return false;
                        for (const Entity* proto : depthLavaTileProtos) {
                            if (proto && proto->prototypeID > 0 && id == static_cast<uint32_t>(proto->prototypeID)) {
                                return true;
                            }
                        }
                        return false;
                    };
                    auto isOpenOrWaterNeighbor = [&](const glm::ivec3& cell) {
                        static const std::array<glm::ivec3, 6> offsets = {{
                            glm::ivec3(1, 0, 0),
                            glm::ivec3(-1, 0, 0),
                            glm::ivec3(0, 1, 0),
                            glm::ivec3(0, -1, 0),
                            glm::ivec3(0, 0, 1),
                            glm::ivec3(0, 0, -1)
                        }};
                        for (const glm::ivec3& offset : offsets) {
                            const uint32_t neighborId = voxelWorld.getBlockWorld(cell + offset);
                            if (neighborId == 0u || neighborId == static_cast<uint32_t>(waterProto->prototypeID)) {
                                return true;
                            }
                        }
                        return false;
                    };
                    const int verticalCellCount = columnMode
                        ? std::max(0, sectionMaxY - sectionMinY + 1)
                        : size;
                    for (int y = 0; y < verticalCellCount; ++y) {
                        int worldY = columnMode
                            ? (sectionMinY + y)
                            : ((sectionCoord.y * size + y) * scale);
                        glm::ivec3 worldCoord(sectionCoord.x * size + x,
                                            worldY,
                                            sectionCoord.z * size + z);
                        int cellMinY = worldY;
                        int cellMaxY = worldY + scale - 1;
                        auto rangeContains = [&](int y) {
                            return y >= cellMinY && y <= cellMaxY;
                        };
                        auto rangeOverlaps = [&](int minY, int maxY) {
                            return cellMaxY >= minY && cellMinY <= maxY;
                        };

                        if (terrainDetailPass) {
                            const uint32_t currentId = voxelWorld.getBlockWorld(worldCoord);
                            if (currentId == 0u) continue;

                            if (isExpanseLevel && worldY < expanseDepthSplitY) {
                                if (!unifiedDepthsEnabled || worldY < unifiedDepthsMinY) {
                                    continue;
                                }
                                if (currentId == static_cast<uint32_t>(waterProto->prototypeID)
                                    || isDepthLavaTileId(currentId)) {
                                    continue;
                                }
                                const uint32_t baseDepthStoneId = static_cast<uint32_t>(
                                    (depthStoneProto ? depthStoneProto : stoneProto)->prototypeID
                                );
                                if (currentId != baseDepthStoneId
                                    && currentId != static_cast<uint32_t>(stoneProto->prototypeID)) {
                                    continue;
                                }

                                const int depthLodestoneVariant = depthLodestoneVariantAt(worldXi, worldY, worldZi);
                                const bool depthCopperColumn = depthCopperVeinAt(worldXi, worldY, worldZi);
                                uint32_t placeId = currentId;
                                uint32_t placeColor = packColor(stoneColor);
                                if (depthLodestoneVariant >= 0
                                    && depthLodestoneVariant < static_cast<int>(depthLodestoneOreProtos.size())
                                    && depthLodestoneOreProtos[static_cast<size_t>(depthLodestoneVariant)] != nullptr) {
                                    const uint32_t oreCellSeed = hash3DInt(
                                        worldXi + depthLodestoneSeed * 97,
                                        worldY + depthLodestoneSeed * 131,
                                        worldZi - depthLodestoneSeed * 151
                                    );
                                    const float oreCellRoll = static_cast<float>((oreCellSeed >> 8u) & 0xffu) / 255.0f;
                                    if (oreCellRoll <= depthLodestoneReplaceChance) {
                                        placeId = static_cast<uint32_t>(
                                            depthLodestoneOreProtos[static_cast<size_t>(depthLodestoneVariant)]->prototypeID
                                        );
                                        placeColor = packColor(glm::vec3(0.33f, 0.35f, 0.36f));
                                    }
                                } else if (depthCopperColumn && depthCopperSulfateOreProto) {
                                    const uint32_t oreCellSeed = hash3DInt(
                                        worldXi + depthCopperSeed * 97,
                                        worldY + depthCopperSeed * 131,
                                        worldZi - depthCopperSeed * 151
                                    );
                                    const float oreCellRoll = static_cast<float>((oreCellSeed >> 8u) & 0xffu) / 255.0f;
                                    if (oreCellRoll <= depthCopperReplaceChance) {
                                        placeId = static_cast<uint32_t>(depthCopperSulfateOreProto->prototypeID);
                                        placeColor = packColor(glm::vec3(0.15f, 0.62f, 0.86f));
                                    }
                                }
                                if (placeId != currentId && writeVoxel(worldCoord, placeId, placeColor)) {
                                    wroteAny = true;
                                }
                                continue;
                            }

                            if (!isLand) continue;

                            if (rangeContains(surfaceY)) {
                                if (waterFeatureColumn
                                    && waterFeatureWaterY > surfaceY
                                    && riverColumn
                                    && clayColumnCandidate
                                    && clayReplaceRollPass
                                    && clayProto
                                    && currentId == static_cast<uint32_t>(soilProto->prototypeID)) {
                                    if (writeVoxel(
                                        worldCoord,
                                        static_cast<uint32_t>(clayProto->prototypeID),
                                        packColor(clayColor)
                                    )) {
                                        wroteAny = true;
                                    }
                                } else if (chalkColumnCandidate && chalkReplaceRollPass && chalkProto) {
                                    pendingChalkPlacements.emplace_back(
                                        sectionCoord.x * size + x,
                                        surfaceY,
                                        sectionCoord.z * size + z
                                    );
                                }
                                continue;
                            }

                            if (worldY < surfaceY) {
                                const int soilMin = surfaceY - cfg.soilDepth;
                                const int stoneMin = surfaceY - cfg.soilDepth - cfg.stoneDepth;
                                const bool inSoilLayer = rangeOverlaps(soilMin, surfaceY - 1);
                                const bool inStoneLayer = rangeOverlaps(cfg.minY, stoneMin) || (worldY < stoneMin);
                                if (!(inSoilLayer || inStoneLayer)) continue;

                                const uint32_t baseSoilId = static_cast<uint32_t>(soilProto->prototypeID);
                                const uint32_t baseStoneId = static_cast<uint32_t>(stoneProto->prototypeID);
                                if (currentId != baseSoilId && currentId != baseStoneId) continue;

                                bool caveAdjacent = false;
                                if (inIsland) {
                                    caveAdjacent = isOpenOrWaterNeighbor(worldCoord);
                                }

                                bool placeOre = false;
                                if (oreVariant >= 0
                                    && oreVariant < static_cast<int>(oreProtos.size())
                                    && oreProtos[static_cast<size_t>(oreVariant)] != nullptr) {
                                    const int depthBelowSurface = surfaceY - worldY;
                                    float oreChance = inStoneLayer ? oreStoneReplaceChance : oreSoilReplaceChance;
                                    if (caveAdjacent) {
                                        oreChance = std::min(1.0f, oreChance + oreCaveAdjacencyBoost);
                                    }
                                    if (depthBelowSurface >= oreMinDepthFromSurface) {
                                        const uint32_t oreCellSeed = hash3DInt(
                                            worldXi + oreSeed * 97,
                                            worldY + oreSeed * 131,
                                            worldZi - oreSeed * 151
                                        );
                                        const float oreCellRoll = static_cast<float>((oreCellSeed >> 8u) & 0xffu) / 255.0f;
                                        placeOre = (oreCellRoll <= oreChance);
                                    }
                                }

                                bool placeGranite = false;
                                if (!placeOre
                                    && inStoneLayer
                                    && caveAdjacent
                                    && (worldY <= waterSurfaceY)
                                    && graniteColumnCandidate
                                    && graniteProto) {
                                    const uint32_t graniteSeedValue = hash3DInt(
                                        worldXi + graniteSeed * 103,
                                        worldY + graniteSeed * 127,
                                        worldZi - graniteSeed * 149
                                    );
                                    const float graniteRoll = static_cast<float>((graniteSeedValue >> 8u) & 0xffu) / 255.0f;
                                    placeGranite = graniteRoll <= graniteReplaceChance;
                                }

                                if (placeOre) {
                                    if (writeVoxel(
                                        worldCoord,
                                        oreProtos[static_cast<size_t>(oreVariant)]->prototypeID,
                                        oreColors[static_cast<size_t>(oreVariant)]
                                    )) {
                                        wroteAny = true;
                                    }
                                } else if (placeGranite
                                           && currentId != static_cast<uint32_t>(graniteProto->prototypeID)) {
                                    if (writeVoxel(
                                        worldCoord,
                                        graniteProto->prototypeID,
                                        packColor(graniteColor)
                                    )) {
                                        wroteAny = true;
                                    }
                                }
                            }
                            continue;
                        }

                        bool carve = false;
                        if (inIsland && worldY <= (isLand ? surfaceY : waterFloorY)) {
                            float v1 = 0.0f;
                            float v2 = 0.0f;
                            if (!sampleCaveField(worldX, static_cast<float>(worldY), worldZ, v1, v2)) {
                                v1 = 0.0f;
                                v2 = 0.0f;
                            }
                            if (isLand) {
                                // Taper caves near the surface to reduce openings.
                                float depth = static_cast<float>(surfaceY - worldY);
                                if (depth > 3.0f) {
                                    float t = std::clamp(depth / 24.0f, 0.0f, 1.0f);
                                    float thrA = 0.72f + (0.62f - 0.72f) * t;
                                    float thrB = 0.68f + (0.58f - 0.68f) * t;
                                    if (isDepthLevel) {
                                        thrA -= 0.10f;
                                        thrB -= 0.10f;
                                    }
                                    if (v1 > thrA || v2 > thrB) carve = true;
                                }
                            } else {
                                if (v1 > 0.62f || v2 > 0.58f) carve = true;
                            }
                        }

                        if (voidPortalProto && voidPortalProto->prototypeID > 0) {
                            const bool isPortalLayer =
                                (portalColumnExpanse && rangeContains(seabedPortalY))
                                || (isDepthLevel && rangeContains(depthPortalY));
                            if (isPortalLayer) {
                                if (writeVoxel(worldCoord,
                                    static_cast<uint32_t>(voidPortalProto->prototypeID),
                                    packColor(glm::vec3(1.0f))
                                )) {
                                    wroteAny = true;
                                }
                                continue;
                            }
                        }

                        if (isExpanseLevel && worldY < expanseDepthSplitY) {
                            // Strict split:
                            // - unified depths band (UnifiedDepthsTopY..UnifiedDepthsMinY): generate depth stone/caves/rivers
                            // - below UnifiedDepthsMinY: empty (never regular expanse cave stone)
                            if (unifiedDepthsEnabled && worldY >= unifiedDepthsMinY) {
                                if (worldY == unifiedDepthsTopY || worldY == unifiedDepthsMinY) {
                                    const uint32_t depthStoneId = static_cast<uint32_t>((depthStoneProto ? depthStoneProto : stoneProto)->prototypeID);
                                    if (writeVoxel(worldCoord, depthStoneId, packColor(stoneColor))) {
                                        wroteAny = true;
                                    }
                                    continue;
                                }
                                const bool inDepthLavaBand = depthLavaFloorEnabled
                                    && (worldY >= depthLavaBandBottom)
                                    && (worldY <= depthLavaBandTop);
                                if (unifiedDepthRiverColumn && rangeOverlaps(unifiedDepthRiverBottomY, unifiedDepthRiverTopY)) {
                                    if (writeVoxel(worldCoord, waterProto->prototypeID, packedWaterColorRiver)) {
                                        wroteAny = true;
                                    }
                                    continue;
                                }
                                if (unifiedDepthRiverColumn
                                    && unifiedDepthRiverCeilingCarveTopY > unifiedDepthRiverTopY
                                    && rangeOverlaps(unifiedDepthRiverTopY + 1, unifiedDepthRiverCeilingCarveTopY)) {
                                    // Upward erosion for visibility: clear the channel ceiling while preserving roof cap.
                                    continue;
                                }
                                const bool extendDepthLavaToFloor = depthLavaFloorEnabled
                                    && (depthLavaColumnTopY > unifiedDepthsMinY)
                                    && (worldY > unifiedDepthsMinY)
                                    && (worldY <= depthLavaColumnTopY);
                                if (extendDepthLavaToFloor) {
                                    const int tx = positiveMod(worldXi, 3);
                                    const int tz = positiveMod(worldZi, 3);
                                    const int tileIdx = tz * 3 + tx;
                                    const Entity* lavaTileProto = depthLavaTileProtos[static_cast<size_t>(tileIdx)];
                                    if (lavaTileProto && lavaTileProto->prototypeID > 0) {
                                        if (writeVoxel(worldCoord, static_cast<uint32_t>(lavaTileProto->prototypeID), packColor(lavaColor))) {
                                            wroteAny = true;
                                        }
                                    }
                                    continue;
                                }
                                const bool depthCarve = depthBandCarveAtY(worldY);
                                if (depthCarve) {
                                    if (inDepthLavaBand) {
                                        const int tx = positiveMod(worldXi, 3);
                                        const int tz = positiveMod(worldZi, 3);
                                        const int tileIdx = tz * 3 + tx;
                                        const Entity* lavaTileProto = depthLavaTileProtos[static_cast<size_t>(tileIdx)];
                                        if (lavaTileProto && lavaTileProto->prototypeID > 0) {
                                            if (writeVoxel(worldCoord, static_cast<uint32_t>(lavaTileProto->prototypeID), packColor(lavaColor))) {
                                                wroteAny = true;
                                            }
                                        }
                                    }
                                    continue;
                                }
                                if (writeVoxel(
                                    worldCoord,
                                    static_cast<uint32_t>((depthStoneProto ? depthStoneProto : stoneProto)->prototypeID),
                                    packColor(stoneColor)
                                )) {
                                    wroteAny = true;
                                }
                                continue;
                            }
                            // Below configured depths band: keep empty.
                            continue;
                        }

                        if (!isLand) {
                            if (rangeContains(waterFloorY)) {
                                if (writeVoxelRun(x, z, worldY, worldY, sandSeabedProto->prototypeID, packColor(seabedColor)) > 0) {
                                    wroteAny = true;
                                }
                                continue;
                            }
                            if (worldY < waterFloorY) {
                                if (writeVoxelRun(x, z, worldY, worldY, stoneProto->prototypeID, packColor(stoneColor)) > 0) {
                                    wroteAny = true;
                                }
                                continue;
                            }
                            if (waterSurfaceY > waterFloorY) {
                                int waterMin = waterFloorY + 1;
                                int waterMax = waterSurfaceY;
                                if (rangeOverlaps(waterMin, waterMax)) {
                                    if (writeVoxelRun(x, z, worldY, worldY, waterProto->prototypeID, packedWaterColorOcean) > 0) {
                                        wroteAny = true;
                                    }
                                }
                            }
                            continue;
                        }

                        if (carve) {
                            if (worldY <= waterSurfaceY) {
                                if (writeVoxel(worldCoord, waterProto->prototypeID, packedWaterColorOcean)) {
                                    wroteAny = true;
                                }
                            }
                            continue;
                        }

                        if (waterFeatureColumn && waterFeatureWaterY > surfaceY) {
                            const int featureWaterMinY = surfaceY + 1;
                            if (rangeOverlaps(featureWaterMinY, waterFeatureWaterY)) {
                                if (writeVoxel(worldCoord, waterProto->prototypeID, packedFeatureWaterColor)) {
                                    wroteAny = true;
                                }
                                continue;
                            }
                        }

                        if (lavaColumnActive) {
                            // Keep crater interior open above lava so the bowl reaches the edge.
                            if (worldY > lavaSurfaceY && worldY <= surfaceY) {
                                continue;
                            }
                            if (rangeOverlaps(lavaFillMinY, lavaSurfaceY)) {
                                if (writeVoxel(worldCoord, waterProto->prototypeID, packColor(lavaColor))) {
                                    wroteAny = true;
                                }
                                continue;
                            }
                            // Carve a magma chamber under the lake to push lava/cavity deep underground.
                            if (chamberCarveColumn && rangeOverlaps(chamberBottomY, lavaFillMinY - 1)) {
                                continue;
                            }
                        }

                        if (rangeContains(surfaceY)) {
                            if (waterFeatureColumn && waterFeatureWaterY > surfaceY) {
                                if (writeVoxel(worldCoord,
                                    static_cast<uint32_t>(soilProto->prototypeID),
                                    packColor(soilColor)
                                )) {
                                    wroteAny = true;
                                }
                                continue;
                            }

                            glm::vec3 topColor = isBeach ? sandColor : biomeSurfaceColor;
                            if (writeVoxel(worldCoord,
                                topSurfaceProto->prototypeID,
                                packColor(topColor)
                            )) {
                                wroteAny = true;
                            }
                            continue;
                        }

                        if (worldY < surfaceY) {
                            int soilMin = surfaceY - cfg.soilDepth;
                            int stoneMin = surfaceY - cfg.soilDepth - cfg.stoneDepth;
                            if (rangeContains(waterFloorY)) {
                                if (writeVoxel(worldCoord, sandSeabedProto->prototypeID, packColor(seabedColor))) {
                                    wroteAny = true;
                                }
                                continue;
                            }
                            bool inSoilLayer = rangeOverlaps(soilMin, surfaceY - 1);
                            bool inStoneLayer = rangeOverlaps(cfg.minY, stoneMin) || (worldY < stoneMin);
                            if (inSoilLayer || inStoneLayer) {
                                if (inSoilLayer) {
                                    if (writeVoxel(worldCoord, soilProto->prototypeID, packColor(soilColor))) {
                                        wroteAny = true;
                                    }
                                } else {
                                    if (writeVoxel(worldCoord, stoneProto->prototypeID, packColor(stoneColor))) {
                                        wroteAny = true;
                                    }
                                }
                                continue;
                            }
                        }
                    }
            }
            if (runPostFeatures) {
                outNextColumn = totalColumns;
                outCompleted = true;
            } else if (terrainFillPass) {
                outNextColumn = (clampedEndColumn >= totalColumns) ? totalColumns : clampedEndColumn;
                outCompleted = false;
            } else {
                outNextColumn = (clampedEndColumn >= totalColumns)
                    ? (totalColumns * 2)
                    : (totalColumns + clampedEndColumn);
                outCompleted = (clampedEndColumn >= totalColumns);
            }
            const VoxelSectionKey sectionKey{sectionCoord};
            auto cachedChalkIt = g_pendingDepthFeatureChalkPlacements.find(sectionKey);
            if (runPostFeatures) {
                const std::vector<glm::ivec3>* chalkPlacementsForFeatures = &pendingChalkPlacements;
                if (pendingChalkPlacements.empty() && cachedChalkIt != g_pendingDepthFeatureChalkPlacements.end()) {
                    chalkPlacementsForFeatures = &cachedChalkIt->second;
                }

                TerrainDepthFeaturePassContext depthFeatureCtx{
                    baseSystem,
                    prototypes,
                    voxelWorld,
                    sectionCoord,
                    size,
                    outCompleted,
                    isExpanseLevel,
                    unifiedDepthsEnabled,
                    unifiedDepthsTopY,
                    unifiedDepthsMinY,
                    waterfallEnabled,
                    waterfallMaxDrop,
                    waterfallCascadeBudget,
                    waterSurfaceY,
                    chalkEnabled,
                    chalkProto,
                    chalkStickSpawnPercent,
                    chalkSeed,
                    chalkStickProtoX,
                    chalkStickProtoZ,
                    chalkColor,
                    depthLavaFloorEnabled,
                    depthLavaTileProtos,
                    lavaColor,
                    obsidianProto,
                    waterProto,
                    waterSlopeProtoPosX,
                    waterSlopeProtoNegX,
                    waterSlopeProtoPosZ,
                    waterSlopeProtoNegZ,
                    waterSlopeCornerProtoPosXPosZ,
                    waterSlopeCornerProtoPosXNegZ,
                    waterSlopeCornerProtoNegXPosZ,
                    waterSlopeCornerProtoNegXNegZ,
                    depthStoneProto,
                    stoneProto,
                    depthPurpleDirtProto,
                    depthRustBeamProto,
                    depthBigLilypadProtosX,
                    depthBigLilypadProtosZ,
                    depthMossWallProtoPosX,
                    depthMossWallProtoNegX,
                    depthMossWallProtoPosZ,
                    depthMossWallProtoNegZ,
                    depthCrystalProto,
                    depthCrystalBlueProto,
                    depthCrystalBlueBigProto,
                    depthCrystalMagentaBigProto,
                    depthPurplePatchPercent,
                    depthRustBeamPercent,
                    depthRustBeamSeed,
                    depthMossPercent,
                    depthBigLilypadPercent,
                    depthRiverCrystalPercent,
                    depthRiverCrystalBankSearchDown,
                    depthRiverCrystalBankSearchRadius,
                    packedWaterColorUnknown
                };
                const bool postFeaturesCompleted = ApplyTerrainDepthFeaturePasses(
                    depthFeatureCtx,
                    *chalkPlacementsForFeatures,
                    wroteAny
                );
                outPostFeaturesCompleted = postFeaturesCompleted;
                if (postFeaturesCompleted) {
                    g_pendingDepthFeatureChalkPlacements.erase(sectionKey);
                }
            } else {
                bool hasDeferredFeatures = false;
                if (outCompleted) {
                    if (columnMode) {
                        const int minSectionY = floorDivInt(sectionMinY, size);
                        const int maxSectionY = floorDivInt(sectionMaxY, size);
                        for (int sy = minSectionY; sy <= maxSectionY; ++sy) {
                            g_pendingDepthFeatureChalkPlacements.erase(
                                VoxelSectionKey{glm::ivec3(sectionCoord.x, sy, sectionCoord.z)}
                            );
                        }
                        for (const glm::ivec3& placement : pendingChalkPlacements) {
                            const int placementSectionY = floorDivInt(placement.y, size);
                            g_pendingDepthFeatureChalkPlacements[
                                VoxelSectionKey{glm::ivec3(sectionCoord.x, placementSectionY, sectionCoord.z)}
                            ].push_back(placement);
                        }
                    } else if (!pendingChalkPlacements.empty()) {
                        g_pendingDepthFeatureChalkPlacements[sectionKey] = pendingChalkPlacements;
                    } else {
                        g_pendingDepthFeatureChalkPlacements.erase(sectionKey);
                    }
                    const bool hasDepthDecor = isExpanseLevel && unifiedDepthsEnabled;
                    const bool hasWaterfall = waterfallEnabled;
                    const bool hasChalk = chalkEnabled
                        && chalkProto
                        && chalkProto->prototypeID > 0
                        && !pendingChalkPlacements.empty();
                    const bool hasDepthLava = isExpanseLevel
                        && unifiedDepthsEnabled
                        && depthLavaFloorEnabled
                        && !depthLavaTileProtos.empty();
                    const bool hasObsidian = obsidianProto
                        && obsidianProto->prototypeID > 0
                        && waterProto
                        && waterProto->prototypeID > 0;
                    hasDeferredFeatures = hasDepthDecor || hasWaterfall || hasChalk || hasDepthLava || hasObsidian;
                }
                outPostFeaturesCompleted = !hasDeferredFeatures;
            }

            const bool hadAnyWrites = (inOutWroteAny || wroteAny);
            inOutWroteAny = hadAnyWrites;
            // Keep terrain writes hidden from meshing until the terrain phases are fully done.
            const bool publishSectionNow = runPostFeatures ? wroteAny : (outCompleted && hadAnyWrites);
            if (publishSectionNow) {
                auto markSectionDirty = [&](const glm::ivec3& coord, bool bumpVersion) {
                    VoxelSectionKey dirtyKey{coord};
                    if (bumpVersion || voxelWorld.sections.find(dirtyKey) != voxelWorld.sections.end()) {
                        voxelWorld.materializeSectionFromColumn(dirtyKey);
                    }
                    auto it = voxelWorld.sections.find(dirtyKey);
                    if (it == voxelWorld.sections.end()) return;
                    // Only bump version for the section whose voxel data changed.
                    // Neighbor sections should remesh, but not invalidate in-flight meshes repeatedly.
                    if (bumpVersion) it->second.editVersion += 1;
                    it->second.dirty = true;
                    voxelWorld.markSectionDirty(dirtyKey);
                };

                if (columnMode) {
                    const int minSectionY = floorDivInt(sectionMinY, size);
                    const int maxSectionY = floorDivInt(sectionMaxY, size);
                    for (int sy = minSectionY; sy <= maxSectionY; ++sy) {
                        const glm::ivec3 coord(sectionCoord.x, sy, sectionCoord.z);
                        markSectionDirty(coord, true);
                        markSectionDirty(coord + glm::ivec3(1, 0, 0), false);
                        markSectionDirty(coord + glm::ivec3(-1, 0, 0), false);
                        markSectionDirty(coord + glm::ivec3(0, 0, 1), false);
                        markSectionDirty(coord + glm::ivec3(0, 0, -1), false);
                    }
                } else {
                    markSectionDirty(sectionCoord, true);
                    markSectionDirty(sectionCoord + glm::ivec3(1, 0, 0), false);
                    markSectionDirty(sectionCoord + glm::ivec3(-1, 0, 0), false);
                    markSectionDirty(sectionCoord + glm::ivec3(0, 1, 0), false);
                    markSectionDirty(sectionCoord + glm::ivec3(0, -1, 0), false);
                    markSectionDirty(sectionCoord + glm::ivec3(0, 0, 1), false);
                    markSectionDirty(sectionCoord + glm::ivec3(0, 0, -1), false);
                }
            }
            return outCompleted;
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
                                        bool& outPostFeaturesCompleted) {
            return GenerateExpanseSectionWorker(
                baseSystem,
                prototypes,
                worldCtx,
                cfg,
                sectionCoord,
                startColumn,
                maxColumns,
                inOutWroteAny,
                outNextColumn,
                outCompleted,
                outPostFeaturesCompleted,
                false,
                false,
                0,
                0
            );
        }

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
                                       bool& outPostFeaturesCompleted) {
            const glm::ivec3 columnCoord(columnKey.coord.x, 0, columnKey.coord.y);
            return GenerateExpanseSectionWorker(
                baseSystem,
                prototypes,
                worldCtx,
                cfg,
                columnCoord,
                startColumn,
                maxColumns,
                inOutWroteAny,
                outNextColumn,
                outCompleted,
                outPostFeaturesCompleted,
                false,
                true,
                minY,
                maxY
            );
        }

        bool RunExpanseSectionDecorators(BaseSystem& baseSystem,
                                         std::vector<Entity>& prototypes,
                                         WorldContext& worldCtx,
                                         const ExpanseConfig& cfg,
                                         const glm::ivec3& sectionCoord,
                                         bool& inOutWroteAny,
                                         bool& outPostFeaturesCompleted) {
            if (!baseSystem.voxelWorld) {
                outPostFeaturesCompleted = true;
                return false;
            }
            const int size = sectionSizeForSection(*baseSystem.voxelWorld);
            const int totalColumns = size * size;
            int nextColumn = totalColumns;
            bool outCompleted = false;
            return GenerateExpanseSectionWorker(
                baseSystem,
                prototypes,
                worldCtx,
                cfg,
                sectionCoord,
                nextColumn,
                0,
                inOutWroteAny,
                nextColumn,
                outCompleted,
                outPostFeaturesCompleted,
                true,
                false,
                0,
                0
            );
        }
    }

}
