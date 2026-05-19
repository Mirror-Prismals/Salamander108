#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace {
    struct FarTerrainBuildPerfStats {
        double cellResolveMs = 0.0;
        double topGreedyMs = 0.0;
        double topMergeMs = 0.0;
        double verticalBuildMs = 0.0;
        double clusterPrepMs = 0.0;
        double clusterUploadMs = 0.0;
    };

    constexpr int kFarTerrainWaterWaveClassPond = 1;
    constexpr int kFarTerrainWaterWaveClassLake = 2;
    constexpr int kFarTerrainWaterWaveClassRiver = 3;
    constexpr int kFarTerrainWaterWaveClassOcean = 4;
    constexpr float kFarTerrainPineBillboardAlpha = -34.0f;
    constexpr float kFarTerrainBareBillboardAlpha = -35.0f;
    constexpr float kFarTerrainJungleBillboardAlpha = -36.0f;
    constexpr float kFarTerrainGrassBillboardAlpha = -37.0f;
    constexpr int kFarTerrainTreeBillboardTileEncodeBase = 65536;
    constexpr int kFarTerrainTreeBillboardTileStride = 1024;

    bool farTerrainGetRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (!std::holds_alternative<bool>(it->second)) return fallback;
        return std::get<bool>(it->second);
    }

    int farTerrainGetRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
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

    float farTerrainGetRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
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

    std::string farTerrainGetRegistryString(const BaseSystem& baseSystem,
                                            const std::string& key,
                                            const std::string& fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (!std::holds_alternative<std::string>(it->second)) return fallback;
        return std::get<std::string>(it->second);
    }

    struct FarTerrainProxyVoxelConfig {
        bool enabled = false;
        bool terrainVoxelsEnabled = false;
        int startRing = 0;
        int minCellSize = 1;
        int columnHeight = 1;
        int fullCoverMaxCellSize = 4;
        float terrainScale = 0.04f;
        int maxProjectedFootprint = 128;
        int seamPadding = 1;
        float terrainHeightScale = 0.35f;
        bool includeWater = true;
        bool treeVoxelsEnabled = false;
        float treeScale = 0.18f;
        int treeMaxHeight = 7;
    };

    FarTerrainProxyVoxelConfig farTerrainReadProxyVoxelConfig(const BaseSystem& baseSystem) {
        FarTerrainProxyVoxelConfig cfg{};
        cfg.enabled = farTerrainGetRegistryBool(baseSystem, "FarTerrainProxyVoxelLodEnabled", true);
        cfg.terrainVoxelsEnabled = farTerrainGetRegistryBool(baseSystem, "FarTerrainProxyVoxelTerrainEnabled", true);
        cfg.startRing = std::max(0, farTerrainGetRegistryInt(baseSystem, "FarTerrainProxyVoxelStartRing", cfg.startRing));
        cfg.minCellSize = std::max(1, farTerrainGetRegistryInt(baseSystem, "FarTerrainProxyVoxelMinCellSize", cfg.minCellSize));
        cfg.columnHeight = std::max(1, farTerrainGetRegistryInt(baseSystem, "FarTerrainProxyVoxelColumnHeight", cfg.columnHeight));
        cfg.fullCoverMaxCellSize = std::max(1, farTerrainGetRegistryInt(baseSystem, "FarTerrainProxyVoxelFullCoverMaxCellSize", cfg.fullCoverMaxCellSize));
        cfg.terrainScale = glm::clamp(
            farTerrainGetRegistryFloat(baseSystem, "FarTerrainProxyVoxelTerrainScale", cfg.terrainScale),
            0.01f,
            1.0f
        );
        cfg.maxProjectedFootprint = std::max(1, farTerrainGetRegistryInt(baseSystem, "FarTerrainProxyVoxelMaxProjectedFootprint", cfg.maxProjectedFootprint));
        cfg.seamPadding = std::max(0, farTerrainGetRegistryInt(baseSystem, "FarTerrainProxyVoxelSeamPadding", cfg.seamPadding));
        cfg.terrainHeightScale = glm::clamp(
            farTerrainGetRegistryFloat(baseSystem, "FarTerrainProxyVoxelTerrainHeightScale", cfg.terrainHeightScale),
            0.05f,
            1.0f
        );
        cfg.includeWater = farTerrainGetRegistryBool(baseSystem, "FarTerrainProxyVoxelIncludeWater", cfg.includeWater);
        cfg.treeVoxelsEnabled = farTerrainGetRegistryBool(baseSystem, "FarTerrainProxyVoxelTreesEnabled", cfg.treeVoxelsEnabled);
        cfg.treeScale = glm::clamp(
            farTerrainGetRegistryFloat(baseSystem, "FarTerrainProxyVoxelTreeScale", cfg.treeScale),
            0.04f,
            1.0f
        );
        cfg.treeMaxHeight = std::max(2, farTerrainGetRegistryInt(baseSystem, "FarTerrainProxyVoxelTreeMaxHeight", cfg.treeMaxHeight));
        return cfg;
    }

    uint64_t farTerrainProxyVoxelConfigSignature(const FarTerrainProxyVoxelConfig& cfg) {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](int value) {
            h ^= static_cast<uint64_t>(static_cast<uint32_t>(value));
            h *= 1099511628211ull;
        };
        mix(cfg.enabled ? 1 : 0);
        mix(cfg.terrainVoxelsEnabled ? 1 : 0);
        mix(cfg.startRing);
        mix(cfg.minCellSize);
        mix(cfg.columnHeight);
        mix(cfg.fullCoverMaxCellSize);
        mix(static_cast<int>(std::round(cfg.terrainScale * 1000.0f)));
        mix(cfg.maxProjectedFootprint);
        mix(cfg.seamPadding);
        mix(static_cast<int>(std::round(cfg.terrainHeightScale * 1000.0f)));
        mix(cfg.includeWater ? 1 : 0);
        mix(cfg.treeVoxelsEnabled ? 1 : 0);
        mix(static_cast<int>(std::round(cfg.treeScale * 1000.0f)));
        mix(cfg.treeMaxHeight);
        return h;
    }

    bool farTerrainCellUsesProxyVoxelLod(const FarTerrainCachedCell& cell,
                                         const FarTerrainProxyVoxelConfig& cfg) {
        if (!cfg.enabled) return false;
        if (cell.lodRing < cfg.startRing) return false;
        if (cell.size < cfg.minCellSize) return false;
        if (!cfg.includeWater && cell.hasWaterSurface) return false;
        return true;
    }

    bool farTerrainCellUsesProxyTerrainLod(const FarTerrainCachedCell& cell,
                                           const FarTerrainProxyVoxelConfig& cfg) {
        return cfg.terrainVoxelsEnabled && farTerrainCellUsesProxyVoxelLod(cell, cfg);
    }

    uint32_t farTerrainHash2DInt(int x, int z) {
        uint32_t ux = static_cast<uint32_t>(x) * 73856093u;
        uint32_t uz = static_cast<uint32_t>(z) * 19349663u;
        uint32_t h = ux ^ uz;
        h ^= (h >> 13);
        h *= 1274126177u;
        h ^= (h >> 16);
        return h;
    }

    uint64_t farTerrainHash3DInt(int x, int y, int z) {
        uint64_t h = 1469598103934665603ull;
        h ^= static_cast<uint64_t>(static_cast<uint32_t>(x));
        h *= 1099511628211ull;
        h ^= static_cast<uint64_t>(static_cast<uint32_t>(y));
        h *= 1099511628211ull;
        h ^= static_cast<uint64_t>(static_cast<uint32_t>(z));
        h *= 1099511628211ull;
        h ^= h >> 32u;
        h *= 0xd6e8feb86659fd93ull;
        h ^= h >> 32u;
        return h;
    }

    float farTerrainHashToUnitFloat01(uint32_t h) {
        return static_cast<float>(h & 0x00ffffffu) / 16777215.0f;
    }

    int farTerrainFloorDiv(int value, int divisor) {
        if (divisor <= 0) return 0;
        if (value >= 0) return value / divisor;
        return -(((-value) + divisor - 1) / divisor);
    }

    float farTerrainSmoothstep01(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float farTerrainValueNoise2D(int seed, float x, float z) {
        const int ix = static_cast<int>(std::floor(x));
        const int iz = static_cast<int>(std::floor(z));
        const float fx = x - static_cast<float>(ix);
        const float fz = z - static_cast<float>(iz);
        const float u = farTerrainSmoothstep01(fx);
        const float v = farTerrainSmoothstep01(fz);
        auto sample = [&](int gx, int gz) -> float {
            return farTerrainHashToUnitFloat01(farTerrainHash2DInt(gx + seed * 37, gz - seed * 53));
        };
        const float n00 = sample(ix, iz);
        const float n10 = sample(ix + 1, iz);
        const float n01 = sample(ix, iz + 1);
        const float n11 = sample(ix + 1, iz + 1);
        const float nx0 = n00 + (n10 - n00) * u;
        const float nx1 = n01 + (n11 - n01) * u;
        return nx0 + (nx1 - nx0) * v;
    }

    float farTerrainFbmValueNoise2D(int seed,
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
            sum += farTerrainValueNoise2D(seed + i * 911, x * frequency, z * frequency) * amplitude;
            norm += amplitude;
            amplitude *= gain;
            frequency *= lacunarity;
        }
        if (norm <= 0.0f) return 0.0f;
        return sum / norm;
    }

    int farTerrainConservativeMaxY(const BaseSystem& baseSystem, const WorldContext& world) {
        const ExpanseConfig& cfg = world.expanse;
        int maxY = static_cast<int>(std::ceil(cfg.baseElevation + cfg.mountainElevation));
        if (cfg.islandRadius > 0.0f) {
            maxY = static_cast<int>(std::ceil(
                cfg.waterSurface + cfg.islandMaxHeight + (cfg.islandNoiseAmp * 2.0f)
            ));
        }
        maxY = std::max(maxY, static_cast<int>(std::ceil(cfg.waterSurface)));
        if (world.leyLines.enabled && world.leyLines.loaded) {
            float upliftBudget = std::max(0.0f, world.leyLines.upliftMax);
            if (world.leyLines.mountainLayerEnabled && world.leyLines.mountainLayerStrength > 0.0f) {
                upliftBudget += upliftBudget * world.leyLines.mountainLayerStrength;
            }
            maxY += static_cast<int>(std::ceil(upliftBudget));
        }
        maxY += std::max(0, farTerrainGetRegistryInt(baseSystem, "ExpanseVerticalHeadroom", 48));
        maxY = std::max(maxY, farTerrainGetRegistryInt(baseSystem, "ExpanseAbsoluteMaxY", 320));
        return maxY;
    }

    int farTerrainAngleBucket(float degrees, float bucketDegrees, float offset) {
        const float safeBucket = std::max(1.0f, bucketDegrees);
        return static_cast<int>(std::floor((degrees + offset) / safeBucket));
    }

    float farTerrainHorizontalHalfFovDegrees(const BaseSystem& baseSystem) {
        if (!baseSystem.player) return 60.0f;
        const float m00 = baseSystem.player->projectionMatrix[0][0];
        if (std::abs(m00) <= 1e-5f) return 60.0f;
        return glm::degrees(std::atan(1.0f / m00));
    }

    glm::vec2 farTerrainCameraForwardXZ(const BaseSystem& baseSystem) {
        if (!baseSystem.player) return glm::vec2(0.0f, -1.0f);
        const float yawRadians = glm::radians(FrustumCullingSystemLogic::GetCullingCameraYaw(baseSystem));
        glm::vec2 forward(std::cos(yawRadians), std::sin(yawRadians));
        const float len2 = glm::dot(forward, forward);
        if (len2 <= 1e-6f) return glm::vec2(0.0f, -1.0f);
        return forward / std::sqrt(len2);
    }

    bool farTerrainHorizontalSectorVisible(const BaseSystem& baseSystem,
                                           int minX,
                                           int minZ,
                                           int maxX,
                                           int maxZ) {
        if (!baseSystem.player) return true;
        const glm::vec3 cullingCameraPos = FrustumCullingSystemLogic::GetCullingCameraPosition(baseSystem);
        const glm::vec2 cameraXZ(cullingCameraPos.x, cullingCameraPos.z);
        const glm::vec2 forwardXZ = farTerrainCameraForwardXZ(baseSystem);
        const glm::vec2 center(
            0.5f * static_cast<float>(minX + maxX),
            0.5f * static_cast<float>(minZ + maxZ)
        );
        const glm::vec2 toCenter = center - cameraXZ;
        const float distanceSquared = glm::dot(toCenter, toCenter);
        if (distanceSquared <= 1e-4f) return true;

        const float halfWidth = 0.5f * static_cast<float>(maxX - minX);
        const float halfDepth = 0.5f * static_cast<float>(maxZ - minZ);
        const float boundRadius = std::sqrt(halfWidth * halfWidth + halfDepth * halfDepth);
        const float distance = std::sqrt(distanceSquared);
        if (distance <= boundRadius) return true;

        const glm::vec2 direction = toCenter / distance;
        const float cosine = glm::clamp(glm::dot(direction, forwardXZ), -1.0f, 1.0f);
        const float centerAngleDegrees = glm::degrees(std::acos(cosine));
        const float angularRadiusDegrees = glm::degrees(std::asin(glm::clamp(boundRadius / distance, 0.0f, 1.0f)));
        const float paddingDegrees = 25.0f;
        const float visibleHalfAngleDegrees = farTerrainHorizontalHalfFovDegrees(baseSystem) + paddingDegrees;
        return centerAngleDegrees <= visibleHalfAngleDegrees + angularRadiusDegrees;
    }

    bool farTerrainConservativeAabbVisible(const BaseSystem& baseSystem,
                                           int minX,
                                           int minY,
                                           int minZ,
                                           int maxX,
                                           int maxY,
                                           int maxZ) {
        return FrustumCullingSystemLogic::ShouldRenderWorldAabb(
            baseSystem,
            glm::vec3(static_cast<float>(minX), static_cast<float>(minY), static_cast<float>(minZ)),
            glm::vec3(static_cast<float>(maxX), static_cast<float>(maxY), static_cast<float>(maxZ))
        );
    }

    glm::vec3 farTerrainColorByName(const WorldContext& world, const std::string& name, const glm::vec3& fallback) {
        auto it = world.colorLibrary.find(name);
        if (it != world.colorLibrary.end()) return it->second;
        return fallback;
    }

    struct FarTerrainMaterial {
        const char* prototypeName = "GrassBlockTex";
        glm::vec3 fallbackColor = glm::vec3(0.35f, 0.62f, 0.22f);
    };

    FarTerrainMaterial farTerrainMaterialForBiome(const WorldContext& world, int biome) {
        const ExpanseConfig& cfg = world.expanse;
        if (biome == 2) {
            return {
                "SandBlockDesertTex",
                farTerrainColorByName(world, cfg.colorSand, glm::vec3(0.72f, 0.62f, 0.38f))
            };
        }
        if (biome == 5) {
            return {
                "GrassBlockProceduralBiomeTex",
                glm::vec3(1.0f)
            };
        }
        if (biome == 1) {
            return {
                "GrassBlockMeadowTex",
                farTerrainColorByName(world, cfg.colorGrass, glm::vec3(0.30f, 0.64f, 0.24f))
            };
        }
        if (biome == 3 && cfg.islandRadius > 0.0f) {
            return {
                "GrassBlockJungleTex",
                farTerrainColorByName(world, cfg.colorGrass, glm::vec3(0.20f, 0.56f, 0.20f))
            };
        }
        if (biome == 4 || (biome == 3 && cfg.islandRadius <= 0.0f)) {
            return {
                "GrassBlockBareWinterTex",
                farTerrainColorByName(world, cfg.colorSnow, glm::vec3(0.74f, 0.78f, 0.76f))
            };
        }
        return {
            "GrassBlockTex",
            farTerrainColorByName(world, cfg.colorGrass, glm::vec3(0.35f, 0.62f, 0.22f))
        };
    }

    FarTerrainMaterial farTerrainMaterialForSample(const WorldContext& world, float x, float z) {
        return farTerrainMaterialForBiome(world, ExpanseBiomeSystemLogic::ResolveBiome(world, x, z));
    }

    FarTerrainMaterial farTerrainSideMaterialForBiome(const WorldContext& world, int biome) {
        const ExpanseConfig& cfg = world.expanse;
        if (biome == 2) {
            return {
                "SandBlockTex",
                farTerrainColorByName(world, cfg.colorSand, glm::vec3(0.72f, 0.62f, 0.38f))
            };
        }
        return {
            "DirtBlockTex",
            farTerrainColorByName(world, cfg.colorSoil, glm::vec3(0.35f, 0.24f, 0.14f))
        };
    }

    FarTerrainMaterial farTerrainSideMaterialForSample(const WorldContext& world, float x, float z) {
        return farTerrainSideMaterialForBiome(world, ExpanseBiomeSystemLogic::ResolveBiome(world, x, z));
    }

    FarTerrainMaterial farTerrainDeepMaterialForSample(const WorldContext& world) {
        const ExpanseConfig& cfg = world.expanse;
        return {
            "StoneBlockTex",
            farTerrainColorByName(world, cfg.colorStone, glm::vec3(0.28f, 0.24f, 0.20f))
        };
    }

    FarTerrainMaterial farTerrainSeabedMaterialForSample(const WorldContext& world) {
        const ExpanseConfig& cfg = world.expanse;
        return {
            "SandBlockTex",
            farTerrainColorByName(world, cfg.colorSeabed, glm::vec3(0.66f, 0.57f, 0.34f))
        };
    }

    bool farTerrainIsWithinJungleVolcano(const ExpanseConfig& cfg,
                                         int biomeID,
                                         int worldX,
                                         int worldZ) {
        if (!cfg.loaded
            || !cfg.jungleVolcanoEnabled
            || !cfg.secondaryBiomeEnabled
            || cfg.islandRadius <= 0.0f) {
            return false;
        }
        if (biomeID != 3) return false;
        const float centerFactorX = std::clamp(cfg.jungleVolcanoCenterFactorX, 0.0f, 1.0f);
        const float centerFactorZ = std::clamp(cfg.jungleVolcanoCenterFactorZ, 0.0f, 1.0f);
        const float volcanoCenterX = cfg.islandCenterX + cfg.islandRadius * centerFactorX;
        const float volcanoCenterZ = cfg.islandCenterZ + cfg.islandRadius * centerFactorZ;
        const float volcanoRadius = std::max(8.0f, cfg.jungleVolcanoOuterRadius);
        const float dx = static_cast<float>(worldX) - volcanoCenterX;
        const float dz = static_cast<float>(worldZ) - volcanoCenterZ;
        return (dx * dx + dz * dz) <= (volcanoRadius * volcanoRadius);
    }

    int farTerrainTreeBillboardFaceTypeForCamera(const BaseSystem& baseSystem) {
        const glm::vec2 forward = farTerrainCameraForwardXZ(baseSystem);
        if (std::abs(forward.x) > std::abs(forward.y)) {
            return forward.x >= 0.0f ? 1 : 0;
        }
        return forward.y >= 0.0f ? 5 : 4;
    }

    int farTerrainEncodeTreeBillboardTiles(int leafTile, int trunkTile) {
        leafTile = std::clamp(leafTile, 0, kFarTerrainTreeBillboardTileStride - 1);
        trunkTile = std::clamp(trunkTile, 0, kFarTerrainTreeBillboardTileStride - 1);
        return kFarTerrainTreeBillboardTileEncodeBase
            + leafTile * kFarTerrainTreeBillboardTileStride
            + trunkTile;
    }

    struct FarTerrainHydrologyCell {
        bool valid = false;
        bool centerIsLand = false;
        int centerSurfaceY = 0;
        float centerX = 0.0f;
        float centerZ = 0.0f;
        float radius = 0.0f;
        int depth = 0;
    };

    struct FarTerrainHydrologySampler {
        bool isDepthLevel = false;
        int waterSurfaceY = 0;
        bool lakeEnabled = false;
        int lakeSeed = 9103;
        int lakeCellSize = 360;
        float lakeChance = 0.08f;
        float lakeRadiusMin = 50.0f;
        float lakeRadiusMax = 150.0f;
        int lakeDepthMin = 24;
        int lakeDepthMax = 34;
        int lakeDepthExtra = 10;
        int lakeMinAboveSea = 4;
        int lakeChannelLower = 3;
        bool pondEnabled = false;
        int pondSeed = 1337;
        int pondCellSize = 40;
        float pondChance = 0.70f;
        float pondRadiusMin = 10.0f;
        float pondRadiusMax = 18.0f;
        int pondDepthMin = 3;
        int pondDepthMax = 7;
        int pondMinAboveSea = 1;
        int pondChannelLower = 3;
        bool riverEnabled = false;
        int riverSeed = 2701;
        float riverScale = 180.0f;
        float riverWarpScale = 72.0f;
        float riverWarpStrength = 58.0f;
        float riverThresholdMin = 0.045f;
        float riverThresholdMax = 0.085f;
        int riverDepthMin = 3;
        int riverDepthMax = 9;
        int riverMinAboveSea = 2;
        int riverChannelLowerMin = 3;
        int riverChannelLowerMax = 5;
        int riverWaterlineExtraLower = 3;
        float riverDepthMultiplier = 3.0f;
        std::unordered_map<uint64_t, FarTerrainHydrologyCell> lakeCache;
        std::unordered_map<uint64_t, FarTerrainHydrologyCell> pondCache;
    };

    struct FarTerrainHydrologySample {
        int terrainSurfaceY = 0;
        bool hasWater = false;
        int waterSurfaceY = 0;
        int waterFloorY = 0;
        int waterWaveClass = 0;
    };

    uint64_t farTerrainHydrologyCellKey(int cellX, int cellZ) {
        return (static_cast<uint64_t>(static_cast<uint32_t>(cellX)) << 32u)
            | static_cast<uint64_t>(static_cast<uint32_t>(cellZ));
    }

    FarTerrainHydrologySampler farTerrainCreateHydrologySampler(const BaseSystem& baseSystem,
                                                                const WorldContext& world) {
        FarTerrainHydrologySampler sampler{};
        const ExpanseConfig& cfg = world.expanse;
        sampler.waterSurfaceY = static_cast<int>(std::floor(cfg.waterSurface));
        sampler.isDepthLevel = farTerrainGetRegistryString(baseSystem, "level", "the_expanse") == "the_depths";
        sampler.lakeEnabled = !sampler.isDepthLevel && farTerrainGetRegistryBool(baseSystem, "SurfaceLakeGenerationEnabled", true);
        sampler.lakeSeed = farTerrainGetRegistryInt(baseSystem, "SurfaceLakeSeed", 9103);
        sampler.lakeCellSize = std::max(48, farTerrainGetRegistryInt(baseSystem, "SurfaceLakeCellSize", 360));
        sampler.lakeChance = glm::clamp(farTerrainGetRegistryFloat(baseSystem, "SurfaceLakeChance", 0.08f), 0.0f, 1.0f);
        sampler.lakeRadiusMin = std::max(6.0f, farTerrainGetRegistryFloat(baseSystem, "SurfaceLakeRadiusMin", 50.0f));
        sampler.lakeRadiusMax = std::max(sampler.lakeRadiusMin, farTerrainGetRegistryFloat(baseSystem, "SurfaceLakeRadiusMax", 150.0f));
        sampler.lakeDepthMin = std::max(2, farTerrainGetRegistryInt(baseSystem, "SurfaceLakeDepthMin", 24));
        sampler.lakeDepthMax = std::max(sampler.lakeDepthMin, farTerrainGetRegistryInt(baseSystem, "SurfaceLakeDepthMax", 34));
        sampler.lakeDepthExtra = std::max(0, farTerrainGetRegistryInt(baseSystem, "SurfaceLakeDepthExtra", 10));
        sampler.lakeMinAboveSea = std::max(1, farTerrainGetRegistryInt(baseSystem, "SurfaceLakeMinAboveSea", 4));
        sampler.lakeChannelLower = std::max(0, farTerrainGetRegistryInt(baseSystem, "SurfaceLakeChannelLower", 3));
        sampler.pondEnabled = !sampler.isDepthLevel && farTerrainGetRegistryBool(baseSystem, "SurfacePondGenerationEnabled", true);
        sampler.pondSeed = farTerrainGetRegistryInt(baseSystem, "SurfacePondSeed", 1337);
        sampler.pondCellSize = std::max(24, farTerrainGetRegistryInt(baseSystem, "SurfacePondCellSize", 40));
        sampler.pondChance = glm::clamp(farTerrainGetRegistryFloat(baseSystem, "SurfacePondChance", 0.70f), 0.0f, 1.0f);
        sampler.pondRadiusMin = std::max(2.0f, farTerrainGetRegistryFloat(baseSystem, "SurfacePondRadiusMin", 10.0f));
        sampler.pondRadiusMax = std::max(sampler.pondRadiusMin, farTerrainGetRegistryFloat(baseSystem, "SurfacePondRadiusMax", 18.0f));
        sampler.pondDepthMin = std::max(1, farTerrainGetRegistryInt(baseSystem, "SurfacePondDepthMin", 3));
        sampler.pondDepthMax = std::max(sampler.pondDepthMin, farTerrainGetRegistryInt(baseSystem, "SurfacePondDepthMax", 7));
        sampler.pondMinAboveSea = std::max(1, farTerrainGetRegistryInt(baseSystem, "SurfacePondMinAboveSea", 1));
        sampler.pondChannelLower = std::max(0, farTerrainGetRegistryInt(baseSystem, "SurfacePondChannelLower", 3));
        sampler.riverEnabled = !sampler.isDepthLevel && farTerrainGetRegistryBool(baseSystem, "SurfaceRiverGenerationEnabled", true);
        sampler.riverSeed = farTerrainGetRegistryInt(baseSystem, "SurfaceRiverSeed", 2701);
        sampler.riverScale = std::max(32.0f, farTerrainGetRegistryFloat(baseSystem, "SurfaceRiverScale", 180.0f));
        sampler.riverWarpScale = std::max(16.0f, farTerrainGetRegistryFloat(baseSystem, "SurfaceRiverWarpScale", 72.0f));
        sampler.riverWarpStrength = std::max(0.0f, farTerrainGetRegistryFloat(baseSystem, "SurfaceRiverWarpStrength", 58.0f));
        sampler.riverThresholdMin = glm::clamp(farTerrainGetRegistryFloat(baseSystem, "SurfaceRiverThresholdMin", 0.045f), 0.001f, 0.45f);
        sampler.riverThresholdMax = glm::clamp(
            farTerrainGetRegistryFloat(baseSystem, "SurfaceRiverThresholdMax", 0.085f),
            sampler.riverThresholdMin,
            0.75f
        );
        sampler.riverDepthMin = std::max(1, farTerrainGetRegistryInt(baseSystem, "SurfaceRiverDepthMin", 3));
        sampler.riverDepthMax = std::max(sampler.riverDepthMin, farTerrainGetRegistryInt(baseSystem, "SurfaceRiverDepthMax", 9));
        sampler.riverMinAboveSea = std::max(1, farTerrainGetRegistryInt(baseSystem, "SurfaceRiverMinAboveSea", 2));
        sampler.riverChannelLowerMin = std::max(0, farTerrainGetRegistryInt(baseSystem, "SurfaceRiverChannelLowerMin", 3));
        sampler.riverChannelLowerMax = std::max(
            sampler.riverChannelLowerMin,
            farTerrainGetRegistryInt(baseSystem, "SurfaceRiverChannelLowerMax", 5)
        );
        sampler.riverWaterlineExtraLower = std::max(0, farTerrainGetRegistryInt(baseSystem, "SurfaceRiverWaterlineExtraLower", 3));
        sampler.riverDepthMultiplier = std::max(1.0f, farTerrainGetRegistryFloat(baseSystem, "SurfaceRiverDepthMultiplier", 3.0f));
        return sampler;
    }

    FarTerrainHydrologyCell& farTerrainLakeCellInfo(FarTerrainHydrologySampler& sampler,
                                                    const WorldContext& world,
                                                    int cellX,
                                                    int cellZ) {
        const uint64_t key = farTerrainHydrologyCellKey(cellX, cellZ);
        auto found = sampler.lakeCache.find(key);
        if (found != sampler.lakeCache.end()) return found->second;

        FarTerrainHydrologyCell info{};
        if (sampler.lakeEnabled) {
            const uint32_t seed = farTerrainHash2DInt(cellX + sampler.lakeSeed * 61, cellZ - sampler.lakeSeed * 47);
            const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
            if (chanceRoll <= sampler.lakeChance) {
                const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                info.centerX = (static_cast<float>(cellX) + offsetX) * static_cast<float>(sampler.lakeCellSize);
                info.centerZ = (static_cast<float>(cellZ) + offsetZ) * static_cast<float>(sampler.lakeCellSize);
                const uint32_t radiusSeed = farTerrainHash2DInt(cellX * 139 + sampler.lakeSeed, cellZ * 191 - sampler.lakeSeed);
                const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                info.radius = sampler.lakeRadiusMin + (sampler.lakeRadiusMax - sampler.lakeRadiusMin) * radiusT;
                const uint32_t depthSeed = farTerrainHash2DInt(cellX * 331 + sampler.lakeSeed * 7, cellZ * 587 - sampler.lakeSeed * 11);
                const float depthT = static_cast<float>((depthSeed >> 8u) & 0xffu) / 255.0f;
                info.depth = sampler.lakeDepthMin
                    + static_cast<int>(std::round((sampler.lakeDepthMax - sampler.lakeDepthMin) * depthT));
                float centerHeight = 0.0f;
                info.centerIsLand = ExpanseBiomeSystemLogic::SampleTerrain(world, info.centerX, info.centerZ, centerHeight);
                info.centerSurfaceY = static_cast<int>(std::floor(centerHeight));
                info.valid = true;
            }
        }
        return sampler.lakeCache.emplace(key, info).first->second;
    }

    FarTerrainHydrologyCell& farTerrainPondCellInfo(FarTerrainHydrologySampler& sampler,
                                                    const WorldContext& world,
                                                    int cellX,
                                                    int cellZ) {
        const uint64_t key = farTerrainHydrologyCellKey(cellX, cellZ);
        auto found = sampler.pondCache.find(key);
        if (found != sampler.pondCache.end()) return found->second;

        FarTerrainHydrologyCell info{};
        if (sampler.pondEnabled) {
            const uint32_t seed = farTerrainHash2DInt(cellX + sampler.pondSeed * 37, cellZ - sampler.pondSeed * 53);
            const float chanceRoll = static_cast<float>((seed >> 24u) & 0xffu) / 255.0f;
            if (chanceRoll <= sampler.pondChance) {
                const float offsetX = static_cast<float>(seed & 0xffu) / 255.0f;
                const float offsetZ = static_cast<float>((seed >> 8u) & 0xffu) / 255.0f;
                info.centerX = (static_cast<float>(cellX) + offsetX) * static_cast<float>(sampler.pondCellSize);
                info.centerZ = (static_cast<float>(cellZ) + offsetZ) * static_cast<float>(sampler.pondCellSize);
                const uint32_t radiusSeed = farTerrainHash2DInt(cellX * 131 + sampler.pondSeed, cellZ * 173 - sampler.pondSeed);
                const float radiusT = static_cast<float>(radiusSeed & 0xffu) / 255.0f;
                info.radius = sampler.pondRadiusMin + (sampler.pondRadiusMax - sampler.pondRadiusMin) * radiusT;
                const uint32_t depthSeed = farTerrainHash2DInt(cellX * 313 + sampler.pondSeed * 7, cellZ * 571 - sampler.pondSeed * 11);
                const float depthT = static_cast<float>((depthSeed >> 8u) & 0xffu) / 255.0f;
                info.depth = sampler.pondDepthMin
                    + static_cast<int>(std::round((sampler.pondDepthMax - sampler.pondDepthMin) * depthT));
                float centerHeight = 0.0f;
                info.centerIsLand = ExpanseBiomeSystemLogic::SampleTerrain(world, info.centerX, info.centerZ, centerHeight);
                info.centerSurfaceY = static_cast<int>(std::floor(centerHeight));
                info.valid = true;
            }
        }
        return sampler.pondCache.emplace(key, info).first->second;
    }

    FarTerrainHydrologySample farTerrainHydrologySample(FarTerrainHydrologySampler& sampler,
                                                        const WorldContext& world,
                                                        float worldX,
                                                        float worldZ,
                                                        bool isLand,
                                                        int rawSurfaceY) {
        FarTerrainHydrologySample result{};
        result.terrainSurfaceY = rawSurfaceY;
        result.waterSurfaceY = rawSurfaceY;
        result.waterFloorY = rawSurfaceY;
        if (!isLand || sampler.isDepthLevel) return result;

        int surfaceY = rawSurfaceY;
        bool hasWater = false;
        int waterSurfaceY = rawSurfaceY;
        int waterFloorY = rawSurfaceY;
        int waterWaveClass = 0;
        const int worldXi = static_cast<int>(std::floor(worldX));
        const int worldZi = static_cast<int>(std::floor(worldZ));
        const ExpanseConfig& cfg = world.expanse;
        const bool isBeach = surfaceY <= sampler.waterSurfaceY + static_cast<int>(cfg.beachHeight);

        bool lakeColumn = false;
        int lakeWaterY = surfaceY;
        if (sampler.lakeEnabled
            && !isBeach
            && surfaceY >= (sampler.waterSurfaceY + sampler.lakeMinAboveSea)) {
            const int lakeCellX = farTerrainFloorDiv(worldXi, sampler.lakeCellSize);
            const int lakeCellZ = farTerrainFloorDiv(worldZi, sampler.lakeCellSize);
            float bestWeight = 0.0f;
            const FarTerrainHydrologyCell* bestLake = nullptr;
            for (int oz = -1; oz <= 1; ++oz) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const FarTerrainHydrologyCell& lake = farTerrainLakeCellInfo(sampler, world, lakeCellX + ox, lakeCellZ + oz);
                    if (!lake.valid || !lake.centerIsLand) continue;
                    if (lake.centerSurfaceY < (sampler.waterSurfaceY + sampler.lakeMinAboveSea)) continue;
                    const float dx = worldX - lake.centerX;
                    const float dz = worldZ - lake.centerZ;
                    const float dist2 = dx * dx + dz * dz;
                    const float radius2 = lake.radius * lake.radius;
                    if (dist2 > radius2) continue;
                    const float weight = 1.0f - (std::sqrt(dist2) / lake.radius);
                    if (!bestLake || weight > bestWeight) {
                        bestWeight = weight;
                        bestLake = &lake;
                    }
                }
            }
            if (bestLake) {
                const int lakeDepthAllowance = bestLake->depth + 6;
                if (std::abs(surfaceY - bestLake->centerSurfaceY) <= lakeDepthAllowance) {
                    const float innerT = std::clamp((bestWeight - 0.06f) / 0.94f, 0.0f, 1.0f);
                    if (innerT > 0.0f) {
                        lakeWaterY = bestLake->centerSurfaceY - 1 - sampler.lakeChannelLower;
                        if (lakeWaterY < surfaceY) {
                            const int lakeDepthHere = std::max(
                                2,
                                static_cast<int>(std::round(static_cast<float>(bestLake->depth + sampler.lakeDepthExtra) * innerT * innerT))
                            );
                            const int lakeFloorY = lakeWaterY - lakeDepthHere;
                            if (lakeFloorY < surfaceY && lakeWaterY > sampler.waterSurfaceY) {
                                surfaceY = lakeFloorY;
                                lakeColumn = true;
                                hasWater = true;
                                waterSurfaceY = lakeWaterY;
                                waterFloorY = lakeFloorY;
                                waterWaveClass = kFarTerrainWaterWaveClassLake;
                            }
                        }
                    }
                }
            }
        }

        bool pondColumn = false;
        int pondWaterY = surfaceY;
        if (sampler.pondEnabled
            && !lakeColumn
            && surfaceY >= (sampler.waterSurfaceY + sampler.pondMinAboveSea)) {
            const int pondCellX = farTerrainFloorDiv(worldXi, sampler.pondCellSize);
            const int pondCellZ = farTerrainFloorDiv(worldZi, sampler.pondCellSize);
            float bestWeight = 0.0f;
            const FarTerrainHydrologyCell* bestPond = nullptr;
            for (int oz = -1; oz <= 1; ++oz) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const FarTerrainHydrologyCell& pond = farTerrainPondCellInfo(sampler, world, pondCellX + ox, pondCellZ + oz);
                    if (!pond.valid || !pond.centerIsLand) continue;
                    if (pond.centerSurfaceY < (sampler.waterSurfaceY + sampler.pondMinAboveSea)) continue;
                    const float dx = worldX - pond.centerX;
                    const float dz = worldZ - pond.centerZ;
                    const float dist2 = dx * dx + dz * dz;
                    const float radius2 = pond.radius * pond.radius;
                    if (dist2 > radius2) continue;
                    const float weight = 1.0f - (std::sqrt(dist2) / pond.radius);
                    if (!bestPond || weight > bestWeight) {
                        bestWeight = weight;
                        bestPond = &pond;
                    }
                }
            }
            if (bestPond) {
                const int pondDepthAllowance = bestPond->depth + 3;
                if (std::abs(surfaceY - bestPond->centerSurfaceY) <= pondDepthAllowance) {
                    const float innerT = std::clamp((bestWeight - 0.12f) / 0.88f, 0.0f, 1.0f);
                    if (innerT > 0.0f) {
                        pondWaterY = bestPond->centerSurfaceY - 1 - sampler.pondChannelLower;
                        if (pondWaterY < surfaceY) {
                            const int pondDepthHere = std::max(
                                1,
                                static_cast<int>(std::round(static_cast<float>(bestPond->depth) * innerT * innerT))
                            );
                            const int pondFloorY = pondWaterY - pondDepthHere;
                            if (pondFloorY < surfaceY && pondWaterY > sampler.waterSurfaceY) {
                                surfaceY = pondFloorY;
                                pondColumn = true;
                                hasWater = true;
                                waterSurfaceY = pondWaterY;
                                waterFloorY = pondFloorY;
                                waterWaveClass = kFarTerrainWaterWaveClassPond;
                            }
                        }
                    }
                }
            }
        }

        const bool basinColumn = lakeColumn || pondColumn;
        if (sampler.riverEnabled
            && !basinColumn
            && surfaceY >= (sampler.waterSurfaceY + sampler.riverMinAboveSea)) {
            const float warpSampleX = worldX / sampler.riverWarpScale;
            const float warpSampleZ = worldZ / sampler.riverWarpScale;
            const float warpNoiseX = farTerrainFbmValueNoise2D(sampler.riverSeed * 13 + 7, warpSampleX, warpSampleZ, 3) * 2.0f - 1.0f;
            const float warpNoiseZ = farTerrainFbmValueNoise2D(sampler.riverSeed * 17 - 5, warpSampleX + 19.4f, warpSampleZ - 11.2f, 3) * 2.0f - 1.0f;
            const float riverSampleX = (worldX + warpNoiseX * sampler.riverWarpStrength) / sampler.riverScale;
            const float riverSampleZ = (worldZ + warpNoiseZ * sampler.riverWarpStrength) / sampler.riverScale;
            const float primary = farTerrainFbmValueNoise2D(sampler.riverSeed, riverSampleX, riverSampleZ, 4);
            const float ridge = std::abs(primary * 2.0f - 1.0f);
            const float widthNoise = farTerrainFbmValueNoise2D(
                sampler.riverSeed * 23 + 101,
                riverSampleX * 0.65f + 3.7f,
                riverSampleZ * 0.65f - 2.1f,
                2
            );
            const float ridgeThreshold = sampler.riverThresholdMin
                + (sampler.riverThresholdMax - sampler.riverThresholdMin) * widthNoise;
            if (ridge < ridgeThreshold) {
                const float innerT = std::clamp(1.0f - (ridge / ridgeThreshold), 0.0f, 1.0f);
                const float depthNoise = farTerrainFbmValueNoise2D(
                    sampler.riverSeed * 31 + 29,
                    riverSampleX * 0.8f - 4.2f,
                    riverSampleZ * 0.8f + 5.3f,
                    2
                );
                const int channelLower = sampler.riverChannelLowerMin
                    + static_cast<int>(std::round(
                        static_cast<float>(sampler.riverChannelLowerMax - sampler.riverChannelLowerMin) * depthNoise
                    ));
                const int baseRiverWaterY = surfaceY - channelLower;
                if (baseRiverWaterY > sampler.waterSurfaceY) {
                    const int depthBase = sampler.riverDepthMin
                        + static_cast<int>(std::round((sampler.riverDepthMax - sampler.riverDepthMin) * depthNoise));
                    const int depthBaseBoosted = std::max(
                        1,
                        static_cast<int>(std::round(static_cast<float>(depthBase) * sampler.riverDepthMultiplier))
                    );
                    const int depthHere = std::max(
                        2,
                        static_cast<int>(std::round(static_cast<float>(depthBaseBoosted) * innerT * innerT))
                    );
                    const int baseRiverFloorY = baseRiverWaterY - depthHere;
                    const int riverWaterY = std::max(
                        baseRiverFloorY + 2,
                        baseRiverWaterY - sampler.riverWaterlineExtraLower
                    );
                    if (riverWaterY > sampler.waterSurfaceY && baseRiverFloorY < surfaceY) {
                        surfaceY = baseRiverFloorY;
                        hasWater = true;
                        waterSurfaceY = riverWaterY;
                        waterFloorY = baseRiverFloorY;
                        waterWaveClass = kFarTerrainWaterWaveClassRiver;
                    }
                }
            }
        }

        result.terrainSurfaceY = surfaceY;
        result.hasWater = hasWater;
        result.waterSurfaceY = waterSurfaceY;
        result.waterFloorY = waterFloorY;
        result.waterWaveClass = waterWaveClass;
        return result;
    }

    const Entity* farTerrainFindPrototypeByName(const char* name, const std::vector<Entity>& prototypes) {
        if (!name || name[0] == '\0') return nullptr;
        for (const Entity& proto : prototypes) {
            if (proto.name == name) return &proto;
        }
        return nullptr;
    }

    int farTerrainMaterialTile(const WorldContext& world,
                               const std::vector<Entity>& prototypes,
                               const char* prototypeName,
                               int faceType) {
        const Entity* proto = farTerrainFindPrototypeByName(prototypeName, prototypes);
        if (!proto) proto = farTerrainFindPrototypeByName("GrassBlockTex", prototypes);
        if (!proto) proto = farTerrainFindPrototypeByName("Block", prototypes);
        if (!proto) return -1;
        return RenderInitSystemLogic::FaceTileIndexFor(&world, *proto, faceType);
    }

    struct FarTerrainMaterialTileCacheKey {
        const char* prototypeName = nullptr;
        int faceType = 0;

        bool operator==(const FarTerrainMaterialTileCacheKey& other) const noexcept {
            return prototypeName == other.prototypeName && faceType == other.faceType;
        }
    };

    struct FarTerrainMaterialTileCacheKeyHash {
        size_t operator()(const FarTerrainMaterialTileCacheKey& key) const noexcept {
            size_t h = std::hash<const void*>()(static_cast<const void*>(key.prototypeName));
            h ^= std::hash<int>()(key.faceType) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            return h;
        }
    };

    struct FarTerrainMaterialTileCache {
        const WorldContext& world;
        const std::vector<Entity>& prototypes;
        std::unordered_map<FarTerrainMaterialTileCacheKey, int, FarTerrainMaterialTileCacheKeyHash> tileIndices;

        FarTerrainMaterialTileCache(const WorldContext& worldRef, const std::vector<Entity>& prototypesRef)
            : world(worldRef), prototypes(prototypesRef) {
            tileIndices.reserve(32);
        }

        int get(const char* prototypeName, int faceType) {
            const char* safeName = prototypeName ? prototypeName : "";
            const FarTerrainMaterialTileCacheKey key{safeName, faceType};
            auto found = tileIndices.find(key);
            if (found != tileIndices.end()) return found->second;
            const int tileIndex = farTerrainMaterialTile(world, prototypes, safeName, faceType);
            tileIndices.emplace(key, tileIndex);
            return tileIndex;
        }
    };

    void farTerrainClearFaceSet(std::array<std::vector<FaceInstanceRenderData>, 6>& faces, size_t& visibleFaceCount) {
        for (auto& batch : faces) batch.clear();
        visibleFaceCount = 0;
    }

    void farTerrainClearWaterFaceSet(std::array<std::vector<WaterFaceInstanceRenderData>, 6>& faces) {
        for (auto& batch : faces) batch.clear();
    }

    void farTerrainSyncVisibleFaceCount(FarTerrainClipmapContext& ctx) {
        ctx.visibleFaceCount = ctx.handoffVisibleFaceCount + ctx.bodyVisibleFaceCount;
    }

    void farTerrainClearAllFaces(FarTerrainClipmapContext& ctx) {
        farTerrainClearFaceSet(ctx.handoffFaces, ctx.handoffVisibleFaceCount);
        farTerrainClearFaceSet(ctx.bodyFaces, ctx.bodyVisibleFaceCount);
        farTerrainClearWaterFaceSet(ctx.handoffWaterSurfaceFaces);
        farTerrainClearWaterFaceSet(ctx.bodyWaterSurfaceFaces);
        farTerrainSyncVisibleFaceCount(ctx);
        ctx.lastLandCellCount = 0;
        ctx.lastHandoffCellCount = 0;
        ctx.lastBodyCellCount = 0;
        ctx.lastSuppressedCellCount = 0;
        ctx.lastFullRebuild = false;
        ctx.lastHandoffRefresh = false;
        ctx.boundsDebugEnabled = false;
        ctx.visibleRingCount = 0;
        ctx.lodCellCounts.fill(0);
        ctx.lodFaceCounts.fill(0);
        ctx.lodTriangleCounts.fill(0);
    }

    int farTerrainCellTopYForBounds(const FarTerrainCachedCell& cell) {
        return cell.hasWaterSurface ? std::max(cell.topY, cell.waterSurfaceY) : cell.topY;
    }

    void farTerrainDestroyRenderBuffer(BaseSystem& baseSystem,
                                       ChunkRenderBuffers& renderBuffers,
                                       bool& renderBuffersValid,
                                       bool& renderBuffersDirty) {
        if (baseSystem.renderBackend && renderBuffersValid) {
            RenderInitSystemLogic::DestroyChunkRenderBuffers(renderBuffers, *baseSystem.renderBackend);
        }
        renderBuffers = ChunkRenderBuffers{};
        renderBuffersValid = false;
        renderBuffersDirty = false;
    }

    void farTerrainDestroyRenderBuffers(BaseSystem& baseSystem, FarTerrainClipmapContext& ctx) {
        auto destroyRenderClusters = [&](std::vector<VoxelRenderCluster>& clusters) {
            if (baseSystem.renderBackend) {
                for (VoxelRenderCluster& cluster : clusters) {
                    RenderInitSystemLogic::DestroyChunkRenderBuffers(cluster.buffers, *baseSystem.renderBackend);
                }
            }
            clusters.clear();
        };
        destroyRenderClusters(ctx.handoffRenderClusters);
        destroyRenderClusters(ctx.bodyRenderClusters);
        farTerrainDestroyRenderBuffer(
            baseSystem,
            ctx.handoffRenderBuffers,
            ctx.handoffRenderBuffersValid,
            ctx.handoffRenderBuffersDirty
        );
        farTerrainDestroyRenderBuffer(
            baseSystem,
            ctx.bodyRenderBuffers,
            ctx.bodyRenderBuffersValid,
            ctx.bodyRenderBuffersDirty
        );
    }

    glm::vec3 farTerrainFaceHalfExtents(int faceType, const glm::vec2& scale) {
        switch (faceType) {
            case 0:
            case 1:
                return glm::vec3(0.5f, scale.y * 0.5f, scale.x * 0.5f);
            case 2:
            case 3:
                return glm::vec3(scale.x * 0.5f, 0.5f, scale.y * 0.5f);
            case 4:
            case 5:
                return glm::vec3(scale.x * 0.5f, scale.y * 0.5f, 0.5f);
            default:
                return glm::vec3(0.5f);
        }
    }

    struct FarTerrainRenderClusterCoord {
        int x = 0;
        int y = 0;
        int z = 0;

        bool operator==(const FarTerrainRenderClusterCoord& other) const noexcept {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    struct FarTerrainRenderClusterCoordHash {
        size_t operator()(const FarTerrainRenderClusterCoord& coord) const noexcept {
            size_t h = std::hash<int>()(coord.x);
            h ^= std::hash<int>()(coord.y) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            h ^= std::hash<int>()(coord.z) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            return h;
        }
    };

    struct FarTerrainClusterPreparedMesh {
        PreparedVoxelSectionMesh mesh;
        glm::vec3 minBounds = glm::vec3(std::numeric_limits<float>::max());
        glm::vec3 maxBounds = glm::vec3(-std::numeric_limits<float>::max());
        bool hasBounds = false;
    };

    void farTerrainExpandClusterBounds(FarTerrainClusterPreparedMesh& cluster,
                                       const glm::vec3& minBounds,
                                       const glm::vec3& maxBounds) {
        if (!cluster.hasBounds) {
            cluster.minBounds = minBounds;
            cluster.maxBounds = maxBounds;
            cluster.hasBounds = true;
            return;
        }
        cluster.minBounds = glm::min(cluster.minBounds, minBounds);
        cluster.maxBounds = glm::max(cluster.maxBounds, maxBounds);
    }

    FarTerrainRenderClusterCoord farTerrainClusterCoordForPosition(const glm::vec3& position, int clusterSizeBlocks) {
        const float invCluster = 1.0f / static_cast<float>(std::max(1, clusterSizeBlocks));
        return {
            static_cast<int>(std::floor(position.x * invCluster)),
            static_cast<int>(std::floor(position.y * invCluster)),
            static_cast<int>(std::floor(position.z * invCluster))
        };
    }

    void farTerrainUploadRenderClusterSet(BaseSystem& baseSystem,
                                          const std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                          const std::array<std::vector<WaterFaceInstanceRenderData>, 6>& waterSurfaceFaces,
                                          int clusterSizeBlocks,
                                          std::vector<VoxelRenderCluster>& renderClusters,
                                          ChunkRenderBuffers& legacyRenderBuffers,
                                          bool& renderBuffersValid,
                                          bool& renderBuffersDirty,
                                          FarTerrainBuildPerfStats* perfStats = nullptr) {
        if (!renderBuffersDirty || !baseSystem.renderer || !baseSystem.renderBackend) return;

        if (renderBuffersValid) {
            RenderInitSystemLogic::DestroyChunkRenderBuffers(legacyRenderBuffers, *baseSystem.renderBackend);
            legacyRenderBuffers = ChunkRenderBuffers{};
            renderBuffersValid = false;
        }
        for (VoxelRenderCluster& cluster : renderClusters) {
            RenderInitSystemLogic::DestroyChunkRenderBuffers(cluster.buffers, *baseSystem.renderBackend);
        }
        renderClusters.clear();

        std::unordered_map<
            FarTerrainRenderClusterCoord,
            FarTerrainClusterPreparedMesh,
            FarTerrainRenderClusterCoordHash
        > clusterMeshes;
        clusterMeshes.reserve(64);
        const auto prepStart = std::chrono::steady_clock::now();

        auto getCluster = [&](const FarTerrainRenderClusterCoord& coord) -> FarTerrainClusterPreparedMesh& {
            FarTerrainClusterPreparedMesh& cluster = clusterMeshes[coord];
            cluster.mesh.usesTexturedFaceBuffers = true;
            return cluster;
        };

        for (int faceType = 0; faceType < 6; ++faceType) {
            for (const FaceInstanceRenderData& face : faces[static_cast<size_t>(faceType)]) {
                const FarTerrainRenderClusterCoord coord =
                    farTerrainClusterCoordForPosition(face.position, clusterSizeBlocks);
                FarTerrainClusterPreparedMesh& cluster = getCluster(coord);
                cluster.mesh.opaqueFaces[static_cast<size_t>(faceType)].push_back(face);
                const glm::vec3 halfExtents = farTerrainFaceHalfExtents(faceType, face.scale);
                farTerrainExpandClusterBounds(cluster, face.position - halfExtents, face.position + halfExtents);
            }
            for (const WaterFaceInstanceRenderData& face : waterSurfaceFaces[static_cast<size_t>(faceType)]) {
                const FarTerrainRenderClusterCoord coord =
                    farTerrainClusterCoordForPosition(face.position, clusterSizeBlocks);
                FarTerrainClusterPreparedMesh& cluster = getCluster(coord);
                cluster.mesh.waterSurfaceFaces[static_cast<size_t>(faceType)].push_back(face);
                const glm::vec3 halfExtents = farTerrainFaceHalfExtents(faceType, face.scale);
                farTerrainExpandClusterBounds(cluster, face.position - halfExtents, face.position + halfExtents);
            }
        }
        if (perfStats) {
            perfStats->clusterPrepMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - prepStart
            ).count();
        }

        renderClusters.reserve(clusterMeshes.size());
        const auto uploadStart = std::chrono::steady_clock::now();
        for (auto& [coord, clusterMesh] : clusterMeshes) {
            (void)coord;
            if (!clusterMesh.hasBounds) continue;
            VoxelRenderCluster cluster{};
            cluster.minBounds = clusterMesh.minBounds;
            cluster.maxBounds = clusterMesh.maxBounds;
            if (!VoxelMeshUploadSystemLogic::UploadPreparedVoxelSectionMesh(
                    clusterMesh.mesh,
                    cluster.buffers,
                    *baseSystem.renderer,
                    *baseSystem.renderBackend)) {
                RenderInitSystemLogic::DestroyChunkRenderBuffers(cluster.buffers, *baseSystem.renderBackend);
                continue;
            }
            renderClusters.push_back(std::move(cluster));
        }
        if (perfStats) {
            perfStats->clusterUploadMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - uploadStart
            ).count();
        }

        renderBuffersValid = !renderClusters.empty();
        renderBuffersDirty = false;
    }

    void farTerrainUploadRenderBuffers(BaseSystem& baseSystem,
                                       FarTerrainClipmapContext& ctx,
                                       FarTerrainBuildPerfStats* perfStats = nullptr) {
        const int handoffClusterSizeBlocks = std::max(
            64,
            farTerrainGetRegistryInt(baseSystem, "FarTerrainHandoffClusterSizeBlocks", 512)
        );
        const int bodyClusterSizeBlocks = std::max(
            128,
            farTerrainGetRegistryInt(baseSystem, "FarTerrainBodyClusterSizeBlocks", 2048)
        );
        farTerrainUploadRenderClusterSet(
            baseSystem,
            ctx.handoffFaces,
            ctx.handoffWaterSurfaceFaces,
            handoffClusterSizeBlocks,
            ctx.handoffRenderClusters,
            ctx.handoffRenderBuffers,
            ctx.handoffRenderBuffersValid,
            ctx.handoffRenderBuffersDirty,
            perfStats
        );
        farTerrainUploadRenderClusterSet(
            baseSystem,
            ctx.bodyFaces,
            ctx.bodyWaterSurfaceFaces,
            bodyClusterSizeBlocks,
            ctx.bodyRenderClusters,
            ctx.bodyRenderBuffers,
            ctx.bodyRenderBuffersValid,
            ctx.bodyRenderBuffersDirty,
            perfStats
        );
    }

    int farTerrainFloorToMultiple(float value, int step) {
        const float scaled = value / static_cast<float>(std::max(1, step));
        return static_cast<int>(std::floor(scaled)) * std::max(1, step);
    }

    int farTerrainFloorDivInt(int value, int divisor) {
        if (divisor <= 0) return 0;
        if (value >= 0) return value / divisor;
        return -(((-value) + divisor - 1) / divisor);
    }

    uint64_t farTerrainCellKey(int x, int z) {
        const uint32_t ux = static_cast<uint32_t>(x);
        const uint32_t uz = static_cast<uint32_t>(z);
        return (static_cast<uint64_t>(ux) << 32u) | static_cast<uint64_t>(uz);
    }

    uint64_t farTerrainHashMix(uint64_t hash, int value) {
        hash ^= static_cast<uint64_t>(static_cast<uint32_t>(value)) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        return hash;
    }

    uint64_t farTerrainCellResolveCacheKey(int x, int z, int size) {
        uint64_t hash = 1469598103934665603ull;
        hash = farTerrainHashMix(hash, x);
        hash = farTerrainHashMix(hash, z);
        hash = farTerrainHashMix(hash, size);
        return hash;
    }

    const FarTerrainCachedCell* farTerrainFindCellAt(const std::vector<FarTerrainCachedCell>& cells,
                                                     const std::unordered_map<uint64_t, size_t>& cellIndex,
                                                     int x,
                                                     int z) {
        auto it = cellIndex.find(farTerrainCellKey(x, z));
        if (it == cellIndex.end()) return nullptr;
        return &cells[it->second];
    }

    bool farTerrainOccludesTopCorner(const FarTerrainCachedCell* neighbor, int topY) {
        return neighbor && neighbor->topY >= topY;
    }

    float farTerrainCornerAo(bool sideA, bool sideB, bool corner) {
        if (sideA && sideB) return 0.56f;
        float ao = 1.0f;
        if (sideA) ao -= 0.16f;
        if (sideB) ao -= 0.16f;
        if (corner) ao -= 0.10f;
        return glm::clamp(ao, 0.58f, 1.0f);
    }

    glm::vec4 farTerrainTopAo(const FarTerrainCachedCell& cell,
                              const std::vector<FarTerrainCachedCell>& cells,
                              const std::unordered_map<uint64_t, size_t>& cellIndex) {
        const int s = cell.size;
        const FarTerrainCachedCell* west = farTerrainFindCellAt(cells, cellIndex, cell.x - s, cell.z);
        const FarTerrainCachedCell* east = farTerrainFindCellAt(cells, cellIndex, cell.x + s, cell.z);
        const FarTerrainCachedCell* north = farTerrainFindCellAt(cells, cellIndex, cell.x, cell.z - s);
        const FarTerrainCachedCell* south = farTerrainFindCellAt(cells, cellIndex, cell.x, cell.z + s);
        const FarTerrainCachedCell* northWest = farTerrainFindCellAt(cells, cellIndex, cell.x - s, cell.z - s);
        const FarTerrainCachedCell* northEast = farTerrainFindCellAt(cells, cellIndex, cell.x + s, cell.z - s);
        const FarTerrainCachedCell* southWest = farTerrainFindCellAt(cells, cellIndex, cell.x - s, cell.z + s);
        const FarTerrainCachedCell* southEast = farTerrainFindCellAt(cells, cellIndex, cell.x + s, cell.z + s);

        const bool w = farTerrainOccludesTopCorner(west, cell.topY);
        const bool e = farTerrainOccludesTopCorner(east, cell.topY);
        const bool n = farTerrainOccludesTopCorner(north, cell.topY);
        const bool s0 = farTerrainOccludesTopCorner(south, cell.topY);
        return glm::vec4(
            farTerrainCornerAo(w, n, farTerrainOccludesTopCorner(northWest, cell.topY)),
            farTerrainCornerAo(e, n, farTerrainOccludesTopCorner(northEast, cell.topY)),
            farTerrainCornerAo(e, s0, farTerrainOccludesTopCorner(southEast, cell.topY)),
            farTerrainCornerAo(w, s0, farTerrainOccludesTopCorner(southWest, cell.topY))
        );
    }

    glm::vec4 farTerrainVerticalAo(int heightSpan, bool upperContact) {
        const float topAo = upperContact ? 0.78f : 0.92f;
        const float bottomAo = heightSpan > 6 ? 0.64f : 0.74f;
        return glm::vec4(topAo, topAo, bottomAo, bottomAo);
    }

    glm::vec3 farTerrainLitColor(const FarTerrainCachedCell& cell, int faceType, bool textured) {
        float light = 1.0f;
        if (faceType == 0) {
            light = 0.72f;
        } else if (faceType == 1) {
            light = 0.58f;
        } else if (faceType == 4) {
            light = 0.66f;
        } else if (faceType == 5) {
            light = 0.62f;
        }
        if (textured) {
            return glm::vec3(light);
        }
        return cell.fallbackColor * light;
    }

    glm::vec2 farTerrainTileUvScale(const glm::vec2& faceScale);

    void farTerrainAppendProxyBoxFaces(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                       size_t& visibleFaceCount,
                                       const glm::ivec3& minBlock,
                                       const glm::ivec3& sizeBlocks,
                                       const std::array<int, 6>& tileIndices,
                                       const std::array<glm::vec3, 6>& colors,
                                       const glm::vec4& ao = glm::vec4(1.0f)) {
        if (sizeBlocks.x <= 0 || sizeBlocks.y <= 0 || sizeBlocks.z <= 0) return;

        const glm::vec3 center(
            static_cast<float>(minBlock.x) + 0.5f * static_cast<float>(sizeBlocks.x - 1),
            static_cast<float>(minBlock.y) + 0.5f * static_cast<float>(sizeBlocks.y - 1),
            static_cast<float>(minBlock.z) + 0.5f * static_cast<float>(sizeBlocks.z - 1)
        );

        for (int faceType = 0; faceType < 6; ++faceType) {
            FaceInstanceRenderData face{};
            face.tileIndex = tileIndices[static_cast<size_t>(faceType)];
            face.color = colors[static_cast<size_t>(faceType)];
            face.alpha = 1.0f;
            face.ao = ao;

            if (faceType == 0) {
                face.position = glm::vec3(
                    static_cast<float>(minBlock.x + sizeBlocks.x - 1) + 0.5f,
                    center.y,
                    center.z
                );
                face.scale = glm::vec2(static_cast<float>(sizeBlocks.z), static_cast<float>(sizeBlocks.y));
            } else if (faceType == 1) {
                face.position = glm::vec3(
                    static_cast<float>(minBlock.x) - 0.5f,
                    center.y,
                    center.z
                );
                face.scale = glm::vec2(static_cast<float>(sizeBlocks.z), static_cast<float>(sizeBlocks.y));
            } else if (faceType == 2) {
                face.position = glm::vec3(
                    center.x,
                    static_cast<float>(minBlock.y + sizeBlocks.y - 1) + 0.5f,
                    center.z
                );
                face.scale = glm::vec2(static_cast<float>(sizeBlocks.x), static_cast<float>(sizeBlocks.z));
            } else if (faceType == 3) {
                face.position = glm::vec3(
                    center.x,
                    static_cast<float>(minBlock.y) - 0.5f,
                    center.z
                );
                face.scale = glm::vec2(static_cast<float>(sizeBlocks.x), static_cast<float>(sizeBlocks.z));
            } else if (faceType == 4) {
                face.position = glm::vec3(
                    center.x,
                    center.y,
                    static_cast<float>(minBlock.z + sizeBlocks.z - 1) + 0.5f
                );
                face.scale = glm::vec2(static_cast<float>(sizeBlocks.x), static_cast<float>(sizeBlocks.y));
            } else {
                face.position = glm::vec3(
                    center.x,
                    center.y,
                    static_cast<float>(minBlock.z) - 0.5f
                );
                face.scale = glm::vec2(static_cast<float>(sizeBlocks.x), static_cast<float>(sizeBlocks.y));
            }

            face.uvScale = farTerrainTileUvScale(face.scale);
            faces[static_cast<size_t>(faceType)].push_back(face);
            visibleFaceCount += 1;
        }
    }

    void farTerrainAppendProxyUnitVoxel(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                        size_t& visibleFaceCount,
                                        const glm::ivec3& block,
                                        int tileIndex,
                                        const glm::vec3& fallbackColor) {
        std::array<int, 6> tiles{};
        std::array<glm::vec3, 6> colors{};
        FarTerrainCachedCell litCell{};
        litCell.fallbackColor = fallbackColor;
        for (int faceType = 0; faceType < 6; ++faceType) {
            tiles[static_cast<size_t>(faceType)] = tileIndex;
            colors[static_cast<size_t>(faceType)] = farTerrainLitColor(litCell, faceType, tileIndex >= 0);
        }
        farTerrainAppendProxyBoxFaces(
            faces,
            visibleFaceCount,
            block,
            glm::ivec3(1, 1, 1),
            tiles,
            colors
        );
    }

    struct FarTerrainProxySurfaceCell {
        FarTerrainCachedCell cell;
        int x = 0;
        int z = 0;
    };

    const FarTerrainProxySurfaceCell* farTerrainFindProxySurfaceCell(
        const std::vector<FarTerrainProxySurfaceCell>& cells,
        const std::unordered_map<uint64_t, size_t>& cellIndex,
        int x,
        int z
    ) {
        auto it = cellIndex.find(farTerrainCellKey(x, z));
        if (it == cellIndex.end()) return nullptr;
        return &cells[it->second];
    }

    glm::vec4 farTerrainProxyTopAo(
        const FarTerrainProxySurfaceCell& cell,
        const std::vector<FarTerrainProxySurfaceCell>& cells,
        const std::unordered_map<uint64_t, size_t>& cellIndex
    ) {
        const FarTerrainProxySurfaceCell* west = farTerrainFindProxySurfaceCell(cells, cellIndex, cell.x - 1, cell.z);
        const FarTerrainProxySurfaceCell* east = farTerrainFindProxySurfaceCell(cells, cellIndex, cell.x + 1, cell.z);
        const FarTerrainProxySurfaceCell* north = farTerrainFindProxySurfaceCell(cells, cellIndex, cell.x, cell.z - 1);
        const FarTerrainProxySurfaceCell* south = farTerrainFindProxySurfaceCell(cells, cellIndex, cell.x, cell.z + 1);
        const FarTerrainProxySurfaceCell* northWest = farTerrainFindProxySurfaceCell(cells, cellIndex, cell.x - 1, cell.z - 1);
        const FarTerrainProxySurfaceCell* northEast = farTerrainFindProxySurfaceCell(cells, cellIndex, cell.x + 1, cell.z - 1);
        const FarTerrainProxySurfaceCell* southWest = farTerrainFindProxySurfaceCell(cells, cellIndex, cell.x - 1, cell.z + 1);
        const FarTerrainProxySurfaceCell* southEast = farTerrainFindProxySurfaceCell(cells, cellIndex, cell.x + 1, cell.z + 1);

        const int topY = cell.cell.topY;
        const bool w = west && west->cell.topY >= topY;
        const bool e = east && east->cell.topY >= topY;
        const bool n = north && north->cell.topY >= topY;
        const bool s = south && south->cell.topY >= topY;
        return glm::vec4(
            farTerrainCornerAo(w, n, northWest && northWest->cell.topY >= topY),
            farTerrainCornerAo(e, n, northEast && northEast->cell.topY >= topY),
            farTerrainCornerAo(e, s, southEast && southEast->cell.topY >= topY),
            farTerrainCornerAo(w, s, southWest && southWest->cell.topY >= topY)
        );
    }

    void farTerrainAppendProxySurfaceTopFace(
        std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
        size_t& visibleFaceCount,
        FarTerrainMaterialTileCache& tileCache,
        const FarTerrainProxySurfaceCell& cell,
        const std::vector<FarTerrainProxySurfaceCell>& cells,
        const std::unordered_map<uint64_t, size_t>& cellIndex
    ) {
        const int tile = tileCache.get(cell.cell.prototypeName, 2);
        FaceInstanceRenderData face{};
        face.position = glm::vec3(
            static_cast<float>(cell.x),
            static_cast<float>(cell.cell.topY) + 0.5f,
            static_cast<float>(cell.z)
        );
        face.tileIndex = tile;
        face.color = farTerrainLitColor(cell.cell, 2, tile >= 0);
        face.alpha = 1.0f;
        face.ao = farTerrainProxyTopAo(cell, cells, cellIndex);
        face.scale = glm::vec2(1.0f);
        face.uvScale = glm::vec2(1.0f);
        faces[2].push_back(face);
        visibleFaceCount += 1;
    }

    void farTerrainAppendProxySurfaceSideFace(
        std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
        size_t& visibleFaceCount,
        FarTerrainMaterialTileCache& tileCache,
        const FarTerrainProxySurfaceCell& cell,
        int faceType,
        int y
    ) {
        if (faceType != 0 && faceType != 1 && faceType != 4 && faceType != 5) return;

        constexpr int kSoilDepthBlocks = 5;
        const bool deep = y < cell.cell.topY - kSoilDepthBlocks;
        const char* prototypeName = deep ? cell.cell.deepPrototypeName : cell.cell.sidePrototypeName;
        const glm::vec3 fallbackColor = deep ? cell.cell.deepFallbackColor : cell.cell.sideFallbackColor;
        const int tile = tileCache.get(prototypeName, faceType);

        FaceInstanceRenderData face{};
        face.tileIndex = tile;
        FarTerrainCachedCell litCell = cell.cell;
        litCell.fallbackColor = fallbackColor;
        face.color = farTerrainLitColor(litCell, faceType, tile >= 0);
        face.alpha = 1.0f;
        face.ao = farTerrainVerticalAo(1, false);
        face.scale = glm::vec2(1.0f);
        face.uvScale = glm::vec2(1.0f);

        if (faceType == 0) {
            face.position = glm::vec3(static_cast<float>(cell.x) + 0.5f, static_cast<float>(y), static_cast<float>(cell.z));
        } else if (faceType == 1) {
            face.position = glm::vec3(static_cast<float>(cell.x) - 0.5f, static_cast<float>(y), static_cast<float>(cell.z));
        } else if (faceType == 4) {
            face.position = glm::vec3(static_cast<float>(cell.x), static_cast<float>(y), static_cast<float>(cell.z) + 0.5f);
        } else {
            face.position = glm::vec3(static_cast<float>(cell.x), static_cast<float>(y), static_cast<float>(cell.z) - 0.5f);
        }

        faces[static_cast<size_t>(faceType)].push_back(face);
        visibleFaceCount += 1;
    }

    void farTerrainAppendProxyWaterSurfaceFace(
        std::array<std::vector<WaterFaceInstanceRenderData>, 6>& waterSurfaceFaces,
        size_t& visibleFaceCount,
        const FarTerrainProxySurfaceCell& cell
    ) {
        if (!cell.cell.hasWaterSurface || cell.cell.waterWaveClass <= 0) return;

        WaterFaceInstanceRenderData face{};
        face.position = glm::vec3(
            static_cast<float>(cell.x),
            static_cast<float>(cell.cell.waterSurfaceY) + 0.5f,
            static_cast<float>(cell.z)
        );
        face.waveClass = static_cast<float>(cell.cell.waterWaveClass);
        const float depth = static_cast<float>(std::max(1, cell.cell.waterSurfaceY - cell.cell.waterFloorY + 1));
        face.metrics = glm::vec4(depth, depth, 0.0f, 0.0f);
        face.scale = glm::vec2(1.0f);
        face.uvScale = glm::vec2(1.0f);
        waterSurfaceFaces[2].push_back(face);
        visibleFaceCount += 1;
    }

    void farTerrainAppendProxySurfaceHeightfieldFaces(
        std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
        std::array<std::vector<WaterFaceInstanceRenderData>, 6>& waterSurfaceFaces,
        size_t& visibleFaceCount,
        FarTerrainMaterialTileCache& tileCache,
        const std::vector<FarTerrainProxySurfaceCell>& surfaceCells,
        const std::unordered_map<uint64_t, size_t>& surfaceCellIndex,
        const FarTerrainProxyVoxelConfig& proxyConfig
    ) {
        if (surfaceCells.empty()) return;

        for (const FarTerrainProxySurfaceCell& surfaceCell : surfaceCells) {
            farTerrainAppendProxySurfaceTopFace(
                faces,
                visibleFaceCount,
                tileCache,
                surfaceCell,
                surfaceCells,
                surfaceCellIndex
            );
            farTerrainAppendProxyWaterSurfaceFace(waterSurfaceFaces, visibleFaceCount, surfaceCell);
        }

        const int boundaryColumnHeight = std::max(1, proxyConfig.columnHeight);
        const auto appendSideToNeighbor = [&](const FarTerrainProxySurfaceCell& surfaceCell,
                                             int faceType,
                                             int neighborX,
                                             int neighborZ) {
            const FarTerrainProxySurfaceCell* neighbor =
                farTerrainFindProxySurfaceCell(surfaceCells, surfaceCellIndex, neighborX, neighborZ);
            if (neighbor && neighbor->cell.topY >= surfaceCell.cell.topY) return;

            const int bottomY = neighbor
                ? neighbor->cell.topY + 1
                : surfaceCell.cell.topY - boundaryColumnHeight + 1;
            for (int y = std::min(bottomY, surfaceCell.cell.topY); y <= surfaceCell.cell.topY; ++y) {
                farTerrainAppendProxySurfaceSideFace(
                    faces,
                    visibleFaceCount,
                    tileCache,
                    surfaceCell,
                    faceType,
                    y
                );
            }
        };

        for (const FarTerrainProxySurfaceCell& surfaceCell : surfaceCells) {
            appendSideToNeighbor(surfaceCell, 0, surfaceCell.x + 1, surfaceCell.z);
            appendSideToNeighbor(surfaceCell, 1, surfaceCell.x - 1, surfaceCell.z);
            appendSideToNeighbor(surfaceCell, 4, surfaceCell.x, surfaceCell.z + 1);
            appendSideToNeighbor(surfaceCell, 5, surfaceCell.x, surfaceCell.z - 1);
        }
    }

    int farTerrainFirstCompressedProxyRing(int baseCellSize, int fullCoverMaxCellSize) {
        int ring = 0;
        int cellSize = std::max(1, baseCellSize);
        const int maxFullCover = std::max(1, fullCoverMaxCellSize);
        while (cellSize <= maxFullCover && ring < 24) {
            ring += 1;
            if (cellSize > (std::numeric_limits<int>::max() / 2)) break;
            cellSize <<= 1;
        }
        return ring;
    }

    int farTerrainSourceRingInnerRadius(int nearRadiusBlocks, int baseCellSize, int ring) {
        int innerRadius = 0;
        const int safeRing = std::max(0, ring);
        const int safeBaseCellSize = std::max(1, baseCellSize);
        for (int i = 0; i < safeRing; ++i) {
            const int cellSize = safeBaseCellSize << i;
            innerRadius += cellSize * 4;
            if (i == 0) {
                innerRadius += std::max(0, nearRadiusBlocks);
            }
        }
        return innerRadius;
    }

    int farTerrainProjectProxyHeight(int y, int originY, float scale) {
        return originY + static_cast<int>(std::round(static_cast<float>(y - originY) * scale));
    }

    FarTerrainCachedCell farTerrainCompressedProxyCell(const FarTerrainCachedCell& source,
                                                       int waterFloorY,
                                                       const FarTerrainProxyVoxelConfig& proxyConfig) {
        FarTerrainCachedCell projected = source;
        projected.size = 1;
        projected.topY = farTerrainProjectProxyHeight(
            source.topY,
            waterFloorY,
            proxyConfig.terrainHeightScale
        );
        if (source.hasWaterSurface) {
            projected.waterSurfaceY = farTerrainProjectProxyHeight(
                source.waterSurfaceY,
                waterFloorY,
                proxyConfig.terrainHeightScale
            );
            projected.waterFloorY = farTerrainProjectProxyHeight(
                source.waterFloorY,
                waterFloorY,
                proxyConfig.terrainHeightScale
            );
            if (projected.waterFloorY > projected.waterSurfaceY) {
                projected.waterFloorY = projected.waterSurfaceY;
            }
            projected.topY = std::max(projected.topY, projected.waterFloorY);
        }
        return projected;
    }

    struct FarTerrainProxyFootprint {
        glm::ivec2 min{0};
        int spanX = 1;
        int spanZ = 1;
    };

    glm::vec2 farTerrainProjectedProxyPoint(const glm::vec2& source,
                                            const glm::ivec2& anchor,
                                            int sourceStartRadius,
                                            const FarTerrainProxyVoxelConfig& proxyConfig) {
        const glm::vec2 delta(
            source.x - static_cast<float>(anchor.x),
            source.y - static_cast<float>(anchor.y)
        );
        const float sourceChebyshev = std::max(std::abs(delta.x), std::abs(delta.y));
        if (sourceChebyshev <= 0.0001f) {
            return glm::vec2(anchor);
        }

        const float visualRadius = static_cast<float>(sourceStartRadius)
            + std::max(0.0f, sourceChebyshev - static_cast<float>(sourceStartRadius))
                * proxyConfig.terrainScale;
        const float positionScale = glm::clamp(
            visualRadius / sourceChebyshev,
            proxyConfig.terrainScale,
            1.0f
        );
        return glm::vec2(anchor) + delta * positionScale;
    }

    FarTerrainProxyFootprint farTerrainProjectedProxyFootprint(const FarTerrainCachedCell& cell,
                                                               const glm::ivec2& anchor,
                                                               int nearRadiusBlocks,
                                                               int baseCellSize,
                                                               const FarTerrainProxyVoxelConfig& proxyConfig) {
        const int cellSize = std::max(1, cell.size);
        const int firstCompressedRing = farTerrainFirstCompressedProxyRing(
            baseCellSize,
            proxyConfig.fullCoverMaxCellSize
        );
        const int sourceStartRadius = farTerrainSourceRingInnerRadius(
            nearRadiusBlocks,
            baseCellSize,
            firstCompressedRing
        );

        const float sourceMinX = static_cast<float>(cell.x);
        const float sourceMaxX = static_cast<float>(cell.x + cellSize - 1);
        const float sourceMinZ = static_cast<float>(cell.z);
        const float sourceMaxZ = static_cast<float>(cell.z + cellSize - 1);
        const glm::vec2 corners[4] = {
            farTerrainProjectedProxyPoint(glm::vec2(sourceMinX, sourceMinZ), anchor, sourceStartRadius, proxyConfig),
            farTerrainProjectedProxyPoint(glm::vec2(sourceMaxX, sourceMinZ), anchor, sourceStartRadius, proxyConfig),
            farTerrainProjectedProxyPoint(glm::vec2(sourceMinX, sourceMaxZ), anchor, sourceStartRadius, proxyConfig),
            farTerrainProjectedProxyPoint(glm::vec2(sourceMaxX, sourceMaxZ), anchor, sourceStartRadius, proxyConfig)
        };

        float minX = corners[0].x;
        float maxX = corners[0].x;
        float minZ = corners[0].y;
        float maxZ = corners[0].y;
        for (const glm::vec2& corner : corners) {
            minX = std::min(minX, corner.x);
            maxX = std::max(maxX, corner.x);
            minZ = std::min(minZ, corner.y);
            maxZ = std::max(maxZ, corner.y);
        }

        const int padding = proxyConfig.seamPadding;
        int blockMinX = static_cast<int>(std::floor(minX)) - padding;
        int blockMaxX = static_cast<int>(std::ceil(maxX)) + padding;
        int blockMinZ = static_cast<int>(std::floor(minZ)) - padding;
        int blockMaxZ = static_cast<int>(std::ceil(maxZ)) + padding;

        if (blockMaxX - blockMinX + 1 > proxyConfig.maxProjectedFootprint) {
            const int center = (blockMinX + blockMaxX) / 2;
            blockMinX = center - proxyConfig.maxProjectedFootprint / 2;
            blockMaxX = blockMinX + proxyConfig.maxProjectedFootprint - 1;
        }
        if (blockMaxZ - blockMinZ + 1 > proxyConfig.maxProjectedFootprint) {
            const int center = (blockMinZ + blockMaxZ) / 2;
            blockMinZ = center - proxyConfig.maxProjectedFootprint / 2;
            blockMaxZ = blockMinZ + proxyConfig.maxProjectedFootprint - 1;
        }

        return FarTerrainProxyFootprint{
            glm::ivec2(blockMinX, blockMinZ),
            std::max(1, blockMaxX - blockMinX + 1),
            std::max(1, blockMaxZ - blockMinZ + 1)
        };
    }

    void farTerrainAppendWorldProxyTerrainFaces(
        std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
        std::array<std::vector<WaterFaceInstanceRenderData>, 6>& waterSurfaceFaces,
        size_t& visibleFaceCount,
        FarTerrainMaterialTileCache& tileCache,
        const std::vector<FarTerrainCachedCell>& cells,
        const FarTerrainProxyVoxelConfig& proxyConfig,
        const glm::ivec2& anchor,
        int nearRadiusBlocks,
        int baseCellSize,
        int waterFloorY
    ) {
        if (cells.empty()) return;

        std::vector<FarTerrainProxySurfaceCell> surfaceCells;
        std::unordered_map<uint64_t, size_t> surfaceCellIndex;
        surfaceCells.reserve(cells.size() * 4);
        surfaceCellIndex.reserve(cells.size() * 8);

        auto appendSurfaceCell = [&](const FarTerrainCachedCell& cell, int x, int z) {
            const uint64_t key = farTerrainCellKey(x, z);
            auto existing = surfaceCellIndex.find(key);
            if (existing != surfaceCellIndex.end()) {
                FarTerrainProxySurfaceCell& surfaceCell = surfaceCells[existing->second];
                if (cell.topY > surfaceCell.cell.topY) {
                    surfaceCell.cell = cell;
                    surfaceCell.cell.x = x;
                    surfaceCell.cell.z = z;
                    surfaceCell.cell.size = 1;
                }
                return;
            }

            const size_t index = surfaceCells.size();
            FarTerrainProxySurfaceCell surfaceCell{};
            surfaceCell.cell = cell;
            surfaceCell.cell.x = x;
            surfaceCell.cell.z = z;
            surfaceCell.cell.size = 1;
            surfaceCell.x = x;
            surfaceCell.z = z;
            surfaceCells.push_back(surfaceCell);
            surfaceCellIndex.emplace(key, index);
        };

        for (const FarTerrainCachedCell& cell : cells) {
            const int cellSize = std::max(1, cell.size);
            if (cellSize <= proxyConfig.fullCoverMaxCellSize) {
                for (int dz = 0; dz < cellSize; ++dz) {
                    for (int dx = 0; dx < cellSize; ++dx) {
                        appendSurfaceCell(cell, cell.x + dx, cell.z + dz);
                    }
                }
                continue;
            }

            const FarTerrainProxyFootprint footprint = farTerrainProjectedProxyFootprint(
                cell,
                anchor,
                nearRadiusBlocks,
                baseCellSize,
                proxyConfig
            );
            const FarTerrainCachedCell projectedCell = farTerrainCompressedProxyCell(
                cell,
                waterFloorY,
                proxyConfig
            );
            for (int dz = 0; dz < footprint.spanZ; ++dz) {
                for (int dx = 0; dx < footprint.spanX; ++dx) {
                    appendSurfaceCell(projectedCell, footprint.min.x + dx, footprint.min.y + dz);
                }
            }
        }

        farTerrainAppendProxySurfaceHeightfieldFaces(
            faces,
            waterSurfaceFaces,
            visibleFaceCount,
            tileCache,
            surfaceCells,
            surfaceCellIndex,
            proxyConfig
        );
    }

    glm::vec2 farTerrainTileUvScale(const glm::vec2& faceScale) {
        // Face scale is measured in world blocks. Repeat the atlas tile once per
        // block so LOD proxy cells keep pixel density instead of stretching.
        return glm::max(faceScale, glm::vec2(1.0f));
    }

    uint32_t farTerrainPackColor(const glm::vec3& color) {
        const float rf = std::clamp(static_cast<float>(std::round(color.r * 255.0f)), 0.0f, 255.0f);
        const float gf = std::clamp(static_cast<float>(std::round(color.g * 255.0f)), 0.0f, 255.0f);
        const float bf = std::clamp(static_cast<float>(std::round(color.b * 255.0f)), 0.0f, 255.0f);
        const uint32_t r = static_cast<uint32_t>(rf);
        const uint32_t g = static_cast<uint32_t>(gf);
        const uint32_t b = static_cast<uint32_t>(bf);
        return (r << 16u) | (g << 8u) | b;
    }

    struct FarTerrainTopCellInfo {
        int gridX = 0;
        int gridZ = 0;
        int topY = 0;
        int tileIndex = -1;
        uint32_t packedColor = 0;
        glm::vec3 color = glm::vec3(1.0f);
        glm::vec4 ao = glm::vec4(1.0f);
    };

    bool farTerrainTopCellsCompatible(const FarTerrainTopCellInfo& a,
                                      const FarTerrainTopCellInfo& b) {
        return a.topY == b.topY
            && a.tileIndex == b.tileIndex
            && a.packedColor == b.packedColor;
    }

    bool farTerrainAppendGreedyTopQuad(FaceInstanceRenderData& face,
                                       const glm::ivec3& worldMin,
                                       int spanXCells,
                                       int spanZCells,
                                       int cellSize) {
        if (spanXCells <= 0 || spanZCells <= 0 || cellSize <= 0) return false;

        const int spanXBlocks = spanXCells * cellSize;
        const int spanZBlocks = spanZCells * cellSize;
        face.position = glm::vec3(
            static_cast<float>(worldMin.x) + 0.5f * static_cast<float>(spanXBlocks - 1),
            static_cast<float>(worldMin.y) + 0.5f,
            static_cast<float>(worldMin.z) + 0.5f * static_cast<float>(spanZBlocks - 1)
        );
        face.scale = glm::vec2(static_cast<float>(spanXBlocks), static_cast<float>(spanZBlocks));
        face.uvScale = farTerrainTileUvScale(face.scale);
        face.alpha = 1.0f;
        return true;
    }

    void farTerrainAppendGreedyTopFaces(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                        size_t& visibleFaceCount,
                                        FarTerrainMaterialTileCache& tileCache,
                                        const std::vector<FarTerrainCachedCell>& cells,
                                        const std::unordered_map<uint64_t, size_t>& cellIndex,
                                        int cellSize,
                                        FarTerrainBuildPerfStats* perfStats = nullptr) {
        if (cells.empty() || cellSize <= 0) return;

        int minCellX = std::numeric_limits<int>::max();
        int maxCellX = std::numeric_limits<int>::min();
        int minCellZ = std::numeric_limits<int>::max();
        int maxCellZ = std::numeric_limits<int>::min();
        std::vector<FarTerrainTopCellInfo> topCells;
        topCells.reserve(cells.size());
        std::unordered_map<uint64_t, size_t> topCellIndex;
        topCellIndex.reserve(cells.size() * 2);

        for (const FarTerrainCachedCell& cell : cells) {
            const int gridX = farTerrainFloorDivInt(cell.x, cellSize);
            const int gridZ = farTerrainFloorDivInt(cell.z, cellSize);
            minCellX = std::min(minCellX, gridX);
            maxCellX = std::max(maxCellX, gridX);
            minCellZ = std::min(minCellZ, gridZ);
            maxCellZ = std::max(maxCellZ, gridZ);

            const int tileIndex = tileCache.get(cell.prototypeName, 2);
            const glm::vec3 color = farTerrainLitColor(cell, 2, tileIndex >= 0);
            const size_t index = topCells.size();
            topCells.push_back({
                gridX,
                gridZ,
                cell.topY,
                tileIndex,
                farTerrainPackColor(color),
                color,
                farTerrainTopAo(cell, cells, cellIndex)
            });
            topCellIndex.emplace(farTerrainCellKey(gridX, gridZ), index);
        }

        const auto greedyStart = std::chrono::steady_clock::now();
        std::vector<uint8_t> consumed(topCells.size(), 0u);

        for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
            for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
                auto startIt = topCellIndex.find(farTerrainCellKey(cellX, cellZ));
                if (startIt == topCellIndex.end()) continue;
                const size_t startIndex = startIt->second;
                if (consumed[startIndex]) continue;

                const FarTerrainTopCellInfo& startCell = topCells[startIndex];
                int spanXCells = 1;
                while (true) {
                    const int nextX = cellX + spanXCells;
                    auto nextIt = topCellIndex.find(farTerrainCellKey(nextX, cellZ));
                    if (nextIt == topCellIndex.end()) break;
                    const size_t nextIndex = nextIt->second;
                    if (consumed[nextIndex]
                        || !farTerrainTopCellsCompatible(startCell, topCells[nextIndex])) {
                        break;
                    }
                    spanXCells += 1;
                }

                int spanZCells = 1;
                while (true) {
                    const int nextZ = cellZ + spanZCells;
                    bool rowMatches = true;
                    for (int dx = 0; dx < spanXCells; ++dx) {
                        auto rowIt = topCellIndex.find(farTerrainCellKey(cellX + dx, nextZ));
                        if (rowIt == topCellIndex.end()) {
                            rowMatches = false;
                            break;
                        }
                        const size_t rowIndex = rowIt->second;
                        if (consumed[rowIndex]
                            || !farTerrainTopCellsCompatible(startCell, topCells[rowIndex])) {
                            rowMatches = false;
                            break;
                        }
                    }
                    if (!rowMatches) break;
                    spanZCells += 1;
                }

                for (int dz = 0; dz < spanZCells; ++dz) {
                    for (int dx = 0; dx < spanXCells; ++dx) {
                        auto markIt = topCellIndex.find(farTerrainCellKey(cellX + dx, cellZ + dz));
                        if (markIt != topCellIndex.end()) {
                            consumed[markIt->second] = 1u;
                        }
                    }
                }

                FaceInstanceRenderData face{};
                face.tileIndex = startCell.tileIndex;
                face.color = startCell.color;
                face.ao = startCell.ao;
                const glm::ivec3 worldMin(
                    cellX * cellSize,
                    startCell.topY,
                    cellZ * cellSize
                );
                if (!farTerrainAppendGreedyTopQuad(
                        face,
                        worldMin,
                        spanXCells,
                        spanZCells,
                        cellSize)) {
                    continue;
                }
                faces[2].push_back(face);
                visibleFaceCount += 1;
            }
        }
        if (perfStats) {
            perfStats->topGreedyMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - greedyStart
            ).count();
        }
    }

    struct FarTerrainWaterCellInfo {
        int gridX = 0;
        int gridZ = 0;
        int waterSurfaceY = 0;
        int waterFloorY = 0;
        int waterWaveClass = 0;
    };

    bool farTerrainWaterCellsCompatible(const FarTerrainWaterCellInfo& a,
                                        const FarTerrainWaterCellInfo& b) {
        return a.waterSurfaceY == b.waterSurfaceY
            && a.waterFloorY == b.waterFloorY
            && a.waterWaveClass == b.waterWaveClass;
    }

    void farTerrainAppendGreedyWaterSurfaceFaces(
        std::array<std::vector<WaterFaceInstanceRenderData>, 6>& waterSurfaceFaces,
        size_t& visibleFaceCount,
        const std::vector<FarTerrainCachedCell>& cells,
        int cellSize
    ) {
        if (cells.empty() || cellSize <= 0) return;

        int minCellX = std::numeric_limits<int>::max();
        int maxCellX = std::numeric_limits<int>::min();
        int minCellZ = std::numeric_limits<int>::max();
        int maxCellZ = std::numeric_limits<int>::min();
        std::vector<FarTerrainWaterCellInfo> waterCells;
        waterCells.reserve(cells.size());
        std::unordered_map<uint64_t, size_t> waterCellIndex;
        waterCellIndex.reserve(cells.size() * 2);

        for (const FarTerrainCachedCell& cell : cells) {
            if (!cell.hasWaterSurface || cell.waterWaveClass <= 0) continue;
            const int gridX = farTerrainFloorDivInt(cell.x, cellSize);
            const int gridZ = farTerrainFloorDivInt(cell.z, cellSize);
            minCellX = std::min(minCellX, gridX);
            maxCellX = std::max(maxCellX, gridX);
            minCellZ = std::min(minCellZ, gridZ);
            maxCellZ = std::max(maxCellZ, gridZ);
            const size_t index = waterCells.size();
            waterCells.push_back({
                gridX,
                gridZ,
                cell.waterSurfaceY,
                cell.waterFloorY,
                cell.waterWaveClass
            });
            waterCellIndex.emplace(farTerrainCellKey(gridX, gridZ), index);
        }
        if (waterCells.empty()) return;

        std::vector<uint8_t> consumed(waterCells.size(), 0u);
        for (int cellZ = minCellZ; cellZ <= maxCellZ; ++cellZ) {
            for (int cellX = minCellX; cellX <= maxCellX; ++cellX) {
                auto startIt = waterCellIndex.find(farTerrainCellKey(cellX, cellZ));
                if (startIt == waterCellIndex.end()) continue;
                const size_t startIndex = startIt->second;
                if (consumed[startIndex]) continue;

                const FarTerrainWaterCellInfo& startCell = waterCells[startIndex];
                int spanXCells = 1;
                while (true) {
                    const int nextX = cellX + spanXCells;
                    auto nextIt = waterCellIndex.find(farTerrainCellKey(nextX, cellZ));
                    if (nextIt == waterCellIndex.end()) break;
                    const size_t nextIndex = nextIt->second;
                    if (consumed[nextIndex]
                        || !farTerrainWaterCellsCompatible(startCell, waterCells[nextIndex])) {
                        break;
                    }
                    spanXCells += 1;
                }

                int spanZCells = 1;
                while (true) {
                    const int nextZ = cellZ + spanZCells;
                    bool rowMatches = true;
                    for (int dx = 0; dx < spanXCells; ++dx) {
                        auto rowIt = waterCellIndex.find(farTerrainCellKey(cellX + dx, nextZ));
                        if (rowIt == waterCellIndex.end()) {
                            rowMatches = false;
                            break;
                        }
                        const size_t rowIndex = rowIt->second;
                        if (consumed[rowIndex]
                            || !farTerrainWaterCellsCompatible(startCell, waterCells[rowIndex])) {
                            rowMatches = false;
                            break;
                        }
                    }
                    if (!rowMatches) break;
                    spanZCells += 1;
                }

                for (int dz = 0; dz < spanZCells; ++dz) {
                    for (int dx = 0; dx < spanXCells; ++dx) {
                        auto markIt = waterCellIndex.find(farTerrainCellKey(cellX + dx, cellZ + dz));
                        if (markIt != waterCellIndex.end()) {
                            consumed[markIt->second] = 1u;
                        }
                    }
                }

                const int spanXBlocks = spanXCells * cellSize;
                const int spanZBlocks = spanZCells * cellSize;
                if (spanXBlocks <= 0 || spanZBlocks <= 0) continue;

                WaterFaceInstanceRenderData face{};
                face.position = glm::vec3(
                    static_cast<float>(cellX * cellSize) + 0.5f * static_cast<float>(spanXBlocks - 1),
                    static_cast<float>(startCell.waterSurfaceY) + 0.5f,
                    static_cast<float>(cellZ * cellSize) + 0.5f * static_cast<float>(spanZBlocks - 1)
                );
                face.waveClass = static_cast<float>(startCell.waterWaveClass);
                const float depth = static_cast<float>(std::max(1, startCell.waterSurfaceY - startCell.waterFloorY + 1));
                face.metrics = glm::vec4(depth, depth, 0.0f, 0.0f);
                face.scale = glm::vec2(static_cast<float>(spanXBlocks), static_cast<float>(spanZBlocks));
                face.uvScale = face.scale;
                waterSurfaceFaces[2].push_back(face);
                visibleFaceCount += 1;
            }
        }
    }

    void farTerrainAppendTreeProxyVoxelFaces(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                             size_t& visibleFaceCount,
                                             int worldX,
                                             int groundY,
                                             int worldZ,
                                             int kind,
                                             int sourceHeightBlocks,
                                             int leafTile,
                                             int trunkTile,
                                             const FarTerrainProxyVoxelConfig& proxyConfig) {
        const int proxyHeight = std::clamp(
            static_cast<int>(std::round(static_cast<float>(sourceHeightBlocks) * proxyConfig.treeScale)),
            3,
            std::max(3, proxyConfig.treeMaxHeight)
        );
        const int trunkHeight = std::clamp(proxyHeight / 2, 1, std::max(1, proxyHeight - 1));
        const int canopyHeight = std::max(1, proxyHeight - trunkHeight + 1);
        const int canopyRadius = std::clamp((proxyHeight + 3) / 4, 1, 2);
        const int baseY = groundY + 1;
        const glm::vec3 leafFallback = (kind == 1)
            ? glm::vec3(0.62f, 0.46f, 0.28f)
            : (kind == 2 ? glm::vec3(0.22f, 0.56f, 0.20f) : glm::vec3(0.28f, 0.62f, 0.22f));
        const glm::vec3 trunkFallback(0.42f, 0.25f, 0.13f);

        for (int y = 0; y < trunkHeight; ++y) {
            farTerrainAppendProxyUnitVoxel(
                faces,
                visibleFaceCount,
                glm::ivec3(worldX, baseY + y, worldZ),
                trunkTile,
                trunkFallback
            );
        }

        std::vector<glm::ivec3> emittedLeaves;
        emittedLeaves.reserve(32);
        auto emitLeaf = [&](const glm::ivec3& block) {
            for (const glm::ivec3& existing : emittedLeaves) {
                if (existing.x == block.x && existing.y == block.y && existing.z == block.z) return;
            }
            emittedLeaves.push_back(block);
            farTerrainAppendProxyUnitVoxel(
                faces,
                visibleFaceCount,
                block,
                leafTile,
                leafFallback
            );
        };

        const int canopyBaseY = baseY + std::max(1, trunkHeight - 1);
        for (int layer = 0; layer < canopyHeight; ++layer) {
            int radius = canopyRadius;
            if (kind == 0) {
                radius = std::max(0, canopyRadius - layer / 2);
            } else if (layer == canopyHeight - 1) {
                radius = std::max(0, canopyRadius - 1);
            }
            const int y = canopyBaseY + layer;
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int manhattan = std::abs(dx) + std::abs(dz);
                    const bool keep = (kind == 0)
                        ? manhattan <= radius
                        : manhattan <= radius + 1;
                    if (!keep) continue;
                    emitLeaf(glm::ivec3(worldX + dx, y, worldZ + dz));
                }
            }
        }
    }

    void farTerrainAppendTreeBillboardFaces(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                            size_t& visibleFaceCount,
                                            const BaseSystem& baseSystem,
                                            const WorldContext& world,
                                            const std::vector<FarTerrainCachedCell>& cells,
                                            const FarTerrainProxyVoxelConfig& proxyConfig) {
        if (cells.empty()) return;
        if (!farTerrainGetRegistryBool(baseSystem, "FarTerrainTreeBillboardsEnabled", true)) return;

        const int minCellSize = std::max(2, farTerrainGetRegistryInt(baseSystem, "FarTerrainTreeBillboardMinCellSize", 2));
        const int maxPerCell = std::max(1, std::min(16, farTerrainGetRegistryInt(baseSystem, "FarTerrainTreeBillboardMaxPerCell", 4)));
        const int attemptsPerBillboard = std::max(8, std::min(256, farTerrainGetRegistryInt(baseSystem, "FarTerrainTreeBillboardAttemptsPerBillboard", 64)));
        const int faceType = farTerrainTreeBillboardFaceTypeForCamera(baseSystem);

        const int pineCanopyBaseHeight = std::max(2, farTerrainGetRegistryInt(baseSystem, "PineCanopyBaseHeight", 10));
        constexpr int kPineBaseTrunkHeight = 30;
        constexpr int kPineBaseCanopyLayers = 30;
        const int pineBaseCanopyOffset = std::max(1, kPineBaseTrunkHeight - pineCanopyBaseHeight);
        const int pineBaseCanopyBase = std::max(2, kPineBaseTrunkHeight - pineBaseCanopyOffset);
        const int pineBaseTopOverhang = kPineBaseCanopyLayers - pineBaseCanopyOffset - 1;
        const int pineMinTrunkHeight = std::max(6, farTerrainGetRegistryInt(baseSystem, "PineTrunkHeightMin", 15));
        const int pineMaxTrunkHeight = std::max(
            pineMinTrunkHeight,
            farTerrainGetRegistryInt(baseSystem, "PineTrunkHeightMax", kPineBaseTrunkHeight)
        );
        const bool meadowTreeGenerationEnabled = farTerrainGetRegistryBool(baseSystem, "MeadowTreeGenerationEnabled", true);
        const int meadowTreeSpawnModulo = std::max(1, farTerrainGetRegistryInt(baseSystem, "MeadowTreeSpawnModulo", 140));
        const int jungleTreeSpawnModulo = std::max(1, farTerrainGetRegistryInt(baseSystem, "JungleTreeSpawnModulo", 1000));
        const int jungleTreeTrunkMin = std::max(4, farTerrainGetRegistryInt(baseSystem, "JungleTreeTrunkHeightMin", 6));
        const int jungleTreeTrunkMax = std::max(jungleTreeTrunkMin, farTerrainGetRegistryInt(baseSystem, "JungleTreeTrunkHeightMax", 9));
        const int jungleTreeCanopyRadius = std::max(2, std::min(8, farTerrainGetRegistryInt(baseSystem, "JungleTreeCanopyRadius", 4)));
        const int bareTreeSpawnModulo = std::max(1, farTerrainGetRegistryInt(baseSystem, "BareTreeSpawnModulo", 380));
        const int bareTreeTrunkMin = std::max(4, farTerrainGetRegistryInt(baseSystem, "BareTreeTrunkHeightMin", 7));
        const int bareTreeTrunkMax = std::max(bareTreeTrunkMin, farTerrainGetRegistryInt(baseSystem, "BareTreeTrunkHeightMax", 12));
        const bool islandQuadrants = (world.expanse.islandRadius > 0.0f) && world.expanse.secondaryBiomeEnabled;
        const int waterSurfaceY = static_cast<int>(std::floor(world.expanse.waterSurface));
        const bool hasSeaLevelWater = world.expanse.waterSurface > world.expanse.waterFloor;

        for (const FarTerrainCachedCell& cell : cells) {
            if (cell.size < minCellSize || cell.size <= 0 || cell.hasWaterSurface) continue;
            const int area = cell.size * cell.size;
            const int targetCount = std::max(1, std::min(maxPerCell, area / 96));
            const int maxAttempts = std::min(4096, targetCount * attemptsPerBillboard);
            int emitted = 0;
            std::unordered_set<uint64_t> emittedPositions;
            emittedPositions.reserve(static_cast<size_t>(targetCount * 2));

            auto tryEmitTreeBillboard = [&](int worldX, int worldZ) {
                const uint64_t posKey = farTerrainCellKey(worldX, worldZ);
                if (!emittedPositions.emplace(posKey).second) return false;

                const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(
                    world,
                    static_cast<float>(worldX),
                    static_cast<float>(worldZ)
                );
                if (biomeID == 2 || biomeID == 5) return false;
                if (farTerrainIsWithinJungleVolcano(world.expanse, biomeID, worldX, worldZ)) return false;

                int kind = -1;
                if (biomeID == 0
                    && (farTerrainHash2DInt(worldX, worldZ) % 100u) == 0u) {
                    kind = 0;
                } else if (meadowTreeGenerationEnabled
                    && biomeID == 1
                    && (farTerrainHash2DInt(worldX + 571, worldZ - 313) % static_cast<uint32_t>(meadowTreeSpawnModulo)) == 0u) {
                    kind = 0;
                } else if (islandQuadrants
                    && (biomeID == 0 || biomeID == 3)
                    && (farTerrainHash2DInt(worldX + 173, worldZ - 911) % static_cast<uint32_t>(jungleTreeSpawnModulo)) == 0u) {
                    kind = 2;
                } else if (biomeID == 4
                    && (farTerrainHash2DInt(worldX - 433, worldZ + 1259) % static_cast<uint32_t>(bareTreeSpawnModulo)) == 0u) {
                    kind = 1;
                }
                if (kind < 0) return false;

                float terrainHeight = 0.0f;
                const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                    world,
                    static_cast<float>(worldX),
                    static_cast<float>(worldZ),
                    terrainHeight
                );
                if (!isLand) return false;
                const int groundY = static_cast<int>(std::floor(terrainHeight));
                if (hasSeaLevelWater && groundY <= waterSurfaceY) return false;

                float billboardHeight = 8.0f;
                float billboardWidth = 4.0f;
                float alphaTag = kFarTerrainPineBillboardAlpha;
                const uint32_t tileSeed = farTerrainHash2DInt(worldX + 2111, worldZ - 3499);
                int leafTile = 382 + static_cast<int>(tileSeed & 3u);
                int trunkTile = 12 + static_cast<int>((tileSeed >> 5u) & 1u);
                if (kind == 0) {
                    const int pineHeightSpan = pineMaxTrunkHeight - pineMinTrunkHeight + 1;
                    const uint32_t pineSeed = farTerrainHash2DInt(worldX + 9091, worldZ - 7919);
                    const int trunkHeight = pineMinTrunkHeight
                        + static_cast<int>(pineSeed % static_cast<uint32_t>(pineHeightSpan));
                    const int canopyTop = std::max(pineBaseCanopyBase, trunkHeight + pineBaseTopOverhang);
                    const int treeHeight = std::max(trunkHeight, canopyTop);
                    billboardHeight = static_cast<float>(treeHeight) + 1.0f;
                    billboardWidth = glm::clamp(billboardHeight * 0.34f, 5.0f, 14.0f);
                    alphaTag = kFarTerrainPineBillboardAlpha;
                    leafTile = 382 + static_cast<int>(tileSeed & 3u);
                    trunkTile = 12 + static_cast<int>((tileSeed >> 5u) & 1u);
                } else if (kind == 1) {
                    const uint32_t bareSeed = farTerrainHash2DInt(worldX + 6097, worldZ - 2953);
                    const int bareHeightSpan = bareTreeTrunkMax - bareTreeTrunkMin + 1;
                    const int trunkHeight = bareTreeTrunkMin
                        + static_cast<int>(bareSeed % static_cast<uint32_t>(bareHeightSpan));
                    billboardHeight = static_cast<float>(trunkHeight + 3);
                    billboardWidth = glm::clamp(billboardHeight * 0.46f, 3.5f, 7.0f);
                    alphaTag = kFarTerrainBareBillboardAlpha;
                    leafTile = 376 + static_cast<int>(tileSeed & 1u);
                    trunkTile = 18;
                } else {
                    const uint32_t jungleSeed = farTerrainHash2DInt(worldX + 4041, worldZ - 7927);
                    const int jungleHeightSpan = jungleTreeTrunkMax - jungleTreeTrunkMin + 1;
                    const int trunkHeight = jungleTreeTrunkMin
                        + static_cast<int>(jungleSeed % static_cast<uint32_t>(jungleHeightSpan));
                    billboardHeight = static_cast<float>(trunkHeight + 2 + jungleTreeCanopyRadius);
                    billboardWidth = glm::clamp(static_cast<float>(jungleTreeCanopyRadius) * 2.6f, 5.5f, 15.0f);
                    alphaTag = kFarTerrainJungleBillboardAlpha;
                    leafTile = 376 + static_cast<int>(tileSeed & 1u);
                    trunkTile = 18;
                }
                billboardWidth = std::max(1.0f, std::round(billboardWidth));
                billboardHeight = std::max(1.0f, std::round(billboardHeight));

                if (proxyConfig.treeVoxelsEnabled
                    && farTerrainCellUsesProxyVoxelLod(cell, proxyConfig)) {
                    farTerrainAppendTreeProxyVoxelFaces(
                        faces,
                        visibleFaceCount,
                        worldX,
                        groundY,
                        worldZ,
                        kind,
                        static_cast<int>(billboardHeight),
                        leafTile,
                        trunkTile,
                        proxyConfig
                    );
                    emitted += 1;
                    return true;
                }

                FaceInstanceRenderData face{};
                face.position = glm::vec3(
                    static_cast<float>(worldX) + 0.5f,
                    static_cast<float>(groundY + 1) + billboardHeight * 0.5f,
                    static_cast<float>(worldZ) + 0.5f
                );
                face.tileIndex = farTerrainEncodeTreeBillboardTiles(leafTile, trunkTile);
                face.color = glm::vec3(1.0f);
                face.alpha = alphaTag;
                face.ao = glm::vec4(1.0f);
                face.scale = glm::vec2(billboardWidth, billboardHeight);
                face.uvScale = face.scale;
                faces[static_cast<size_t>(faceType)].push_back(face);
                visibleFaceCount += 1;
                emitted += 1;
                return true;
            };

            if (cell.size <= 16) {
                const int totalCandidates = cell.size * cell.size;
                const int startOffset = static_cast<int>(
                    farTerrainHash3DInt(cell.x, cell.lodRing * 997, cell.z)
                    % static_cast<uint64_t>(std::max(1, totalCandidates))
                );
                for (int i = 0; i < totalCandidates && emitted < targetCount; ++i) {
                    const int offset = (startOffset + i) % totalCandidates;
                    const int worldX = cell.x + (offset % cell.size);
                    const int worldZ = cell.z + (offset / cell.size);
                    tryEmitTreeBillboard(worldX, worldZ);
                }
                continue;
            }

            for (int attempt = 0; attempt < maxAttempts && emitted < targetCount; ++attempt) {
                const uint64_t h = farTerrainHash3DInt(cell.x, attempt + cell.lodRing * 997, cell.z);
                const int worldX = cell.x + static_cast<int>(h % static_cast<uint64_t>(cell.size));
                const int worldZ = cell.z + static_cast<int>((h >> 17u) % static_cast<uint64_t>(cell.size));
                tryEmitTreeBillboard(worldX, worldZ);
            }
        }
    }

    const char* farTerrainGrassBillboardPrototypeForBiome(int biomeID,
                                                          bool islandQuadrants,
                                                          uint32_t seed,
                                                          int shortGrassPercent) {
        if (biomeID == 2 || biomeID == 5) return nullptr;
        if (biomeID == 1) {
            if (static_cast<int>((seed >> 11u) % 100u) < shortGrassPercent) {
                return "GrassTuftShortMeadow";
            }
            return "GrassTuftMeadow";
        }
        if (islandQuadrants && biomeID == 3) {
            return "GrassTuftJungle";
        }
        if (biomeID == 4) {
            static const std::array<const char*, 4> kBareGrass = {
                "GrassTuftBareV001",
                "GrassTuftBareV002",
                "GrassTuftBareV003",
                "GrassTuftBareV004"
            };
            return kBareGrass[static_cast<size_t>((seed >> 14u) & 3u)];
        }
        if (static_cast<int>((seed >> 11u) % 100u) < shortGrassPercent) {
            return "GrassTuftShort";
        }
        return "GrassTuft";
    }

    void farTerrainAppendGrassBillboardFaces(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                             size_t& visibleFaceCount,
                                             const BaseSystem& baseSystem,
                                             const WorldContext& world,
                                             FarTerrainMaterialTileCache& tileCache,
                                             const std::vector<FarTerrainCachedCell>& cells) {
        if (cells.empty()) return;
        if (!farTerrainGetRegistryBool(baseSystem, "FarTerrainGrassBillboardsEnabled", true)) return;

        const int minCellSize = std::max(2, farTerrainGetRegistryInt(baseSystem, "FarTerrainGrassBillboardMinCellSize", 4));
        const int maxPerCell = std::max(1, std::min(32, farTerrainGetRegistryInt(baseSystem, "FarTerrainGrassBillboardMaxPerCell", 3)));
        const int attemptsPerBillboard = std::max(4, std::min(128, farTerrainGetRegistryInt(baseSystem, "FarTerrainGrassBillboardAttemptsPerBillboard", 24)));
        const int grassSpawnModulo = std::max(1, farTerrainGetRegistryInt(baseSystem, "GrassSpawnModulo", 1));
        const int grassTuftPercent = std::max(0, std::min(100, farTerrainGetRegistryInt(baseSystem, "GrassTuftPercent", 45)));
        const int shortGrassPercent = std::max(0, std::min(100, farTerrainGetRegistryInt(baseSystem, "ShortGrassPercent", 50)));
        if (grassTuftPercent <= 0) return;

        const int faceType = farTerrainTreeBillboardFaceTypeForCamera(baseSystem);
        const bool islandQuadrants = (world.expanse.islandRadius > 0.0f) && world.expanse.secondaryBiomeEnabled;
        const int waterSurfaceY = static_cast<int>(std::floor(world.expanse.waterSurface));
        const bool hasSeaLevelWater = world.expanse.waterSurface > world.expanse.waterFloor;

        for (const FarTerrainCachedCell& cell : cells) {
            if (cell.size < minCellSize || cell.size <= 0 || cell.hasWaterSurface) continue;
            const int area = cell.size * cell.size;
            const int targetCount = std::max(1, std::min(maxPerCell, area / 96));
            const int maxAttempts = std::min(2048, targetCount * attemptsPerBillboard);
            int emitted = 0;
            std::unordered_set<uint64_t> emittedPositions;
            emittedPositions.reserve(static_cast<size_t>(targetCount * 2));

            auto tryEmitGrassBillboard = [&](int worldX, int worldZ) {
                const uint64_t posKey = farTerrainCellKey(worldX, worldZ);
                if (!emittedPositions.emplace(posKey).second) return false;

                const int biomeID = ExpanseBiomeSystemLogic::ResolveBiome(
                    world,
                    static_cast<float>(worldX),
                    static_cast<float>(worldZ)
                );
                if (biomeID == 2 || biomeID == 5) return false;

                const uint32_t seed = farTerrainHash2DInt(worldX, worldZ);
                if (((seed >> 1u) % static_cast<uint32_t>(grassSpawnModulo)) != 0u) return false;
                const uint32_t tuftSeed = farTerrainHash2DInt(worldX + 1847, worldZ - 563);
                if (static_cast<int>(tuftSeed % 100u) >= grassTuftPercent) return false;

                float terrainHeight = 0.0f;
                const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                    world,
                    static_cast<float>(worldX),
                    static_cast<float>(worldZ),
                    terrainHeight
                );
                if (!isLand) return false;
                const int groundY = static_cast<int>(std::floor(terrainHeight));
                if (hasSeaLevelWater && groundY <= waterSurfaceY) return false;

                const char* prototypeName = farTerrainGrassBillboardPrototypeForBiome(
                    biomeID,
                    islandQuadrants,
                    seed,
                    shortGrassPercent
                );
                if (!prototypeName) return false;

                const int tileIndex = tileCache.get(prototypeName, 2);
                if (tileIndex < 0) return false;
                const std::string prototypeNameString(prototypeName);
                const bool shortGrass = prototypeNameString.find("Short") != std::string::npos;
                const float billboardHeight = shortGrass ? 0.72f : 1.05f;
                const float billboardWidth = shortGrass ? 0.72f : 0.92f;

                FaceInstanceRenderData face{};
                face.position = glm::vec3(
                    static_cast<float>(worldX) + 0.5f,
                    static_cast<float>(groundY + 1) + billboardHeight * 0.5f,
                    static_cast<float>(worldZ) + 0.5f
                );
                face.tileIndex = tileIndex;
                face.color = glm::vec3(1.0f);
                face.alpha = kFarTerrainGrassBillboardAlpha;
                face.ao = glm::vec4(1.0f);
                face.scale = glm::vec2(billboardWidth, billboardHeight);
                face.uvScale = glm::vec2(1.0f);
                faces[static_cast<size_t>(faceType)].push_back(face);
                visibleFaceCount += 1;
                emitted += 1;
                return true;
            };

            if (cell.size <= 8) {
                for (int dz = 0; dz < cell.size && emitted < targetCount; ++dz) {
                    for (int dx = 0; dx < cell.size && emitted < targetCount; ++dx) {
                        tryEmitGrassBillboard(cell.x + dx, cell.z + dz);
                    }
                }
                continue;
            }

            for (int attempt = 0; attempt < maxAttempts && emitted < targetCount; ++attempt) {
                const uint32_t h = farTerrainHash2DInt(
                    cell.x + attempt * 733 + cell.size * 17,
                    cell.z - attempt * 977 - cell.size * 29
                );
                const int localX = static_cast<int>(h % static_cast<uint32_t>(cell.size));
                const int localZ = static_cast<int>((h >> 16u) % static_cast<uint32_t>(cell.size));
                tryEmitGrassBillboard(cell.x + localX, cell.z + localZ);
            }
        }
    }

    glm::vec3 farTerrainBoundsDebugColor(int ring) {
        static const glm::vec3 kColors[] = {
            glm::vec3(1.00f, 0.12f, 0.10f),
            glm::vec3(1.00f, 0.58f, 0.05f),
            glm::vec3(1.00f, 0.92f, 0.08f),
            glm::vec3(0.20f, 1.00f, 0.18f),
            glm::vec3(0.10f, 0.95f, 0.95f),
            glm::vec3(0.16f, 0.42f, 1.00f),
            glm::vec3(0.72f, 0.18f, 1.00f),
            glm::vec3(1.00f, 0.16f, 0.72f),
            glm::vec3(0.95f, 0.95f, 0.95f),
            glm::vec3(0.15f, 0.15f, 0.15f)
        };
        constexpr int kColorCount = static_cast<int>(sizeof(kColors) / sizeof(kColors[0]));
        return kColors[static_cast<size_t>(std::clamp(ring, 0, kColorCount - 1))];
    }

    void farTerrainAppendBoundsDebugFace(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                         size_t& visibleFaceCount,
                                         int faceType,
                                         const glm::vec3& position,
                                         const glm::vec2& scale,
                                         const glm::vec3& color) {
        FaceInstanceRenderData face{};
        face.position = position;
        face.tileIndex = -1;
        face.color = color;
        face.alpha = 0.88f;
        face.ao = glm::vec4(1.0f);
        face.scale = glm::max(scale, glm::vec2(0.05f));
        face.uvScale = glm::vec2(1.0f);
        faces[static_cast<size_t>(faceType)].push_back(face);
        visibleFaceCount += 1;
    }

    struct FarTerrainDebugLineKey {
        int ring = 0;
        int topY = 0;
        int fixedHalf = 0;

        bool operator==(const FarTerrainDebugLineKey& other) const {
            return ring == other.ring && topY == other.topY && fixedHalf == other.fixedHalf;
        }
    };

    struct FarTerrainDebugLineKeyHash {
        size_t operator()(const FarTerrainDebugLineKey& key) const {
            size_t h = static_cast<size_t>(key.ring);
            h = (h * 1315423911u) ^ static_cast<size_t>(key.topY * 92821);
            h = (h * 2654435761u) ^ static_cast<size_t>(key.fixedHalf);
            return h;
        }
    };

    bool farTerrainSameDebugPlaneNeighbor(const FarTerrainCachedCell* neighbor, const FarTerrainCachedCell& cell) {
        return neighbor
            && neighbor->topY == cell.topY
            && neighbor->lodRing == cell.lodRing
            && neighbor->size == cell.size;
    }

    void farTerrainAppendMergedBoundsDebugRuns(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                               size_t& visibleFaceCount,
                                               const std::vector<FarTerrainCachedCell>& cells,
                                               const std::unordered_map<uint64_t, size_t>& cellIndex) {
        std::unordered_map<FarTerrainDebugLineKey, std::vector<std::pair<int, int>>, FarTerrainDebugLineKeyHash> xRuns;
        std::unordered_map<FarTerrainDebugLineKey, std::vector<std::pair<int, int>>, FarTerrainDebugLineKeyHash> zRuns;

        for (const FarTerrainCachedCell& cell : cells) {
            const int x0 = cell.x * 2 - 1;
            const int x1 = (cell.x + cell.size) * 2 - 1;
            const int z0 = cell.z * 2 - 1;
            const int z1 = (cell.z + cell.size) * 2 - 1;

            xRuns[{cell.lodRing, cell.topY, z0}].push_back({x0, x1});
            zRuns[{cell.lodRing, cell.topY, x0}].push_back({z0, z1});

            const FarTerrainCachedCell* east = farTerrainFindCellAt(cells, cellIndex, cell.x + cell.size, cell.z);
            if (!farTerrainSameDebugPlaneNeighbor(east, cell)) {
                zRuns[{cell.lodRing, cell.topY, x1}].push_back({z0, z1});
            }

            const FarTerrainCachedCell* south = farTerrainFindCellAt(cells, cellIndex, cell.x, cell.z + cell.size);
            if (!farTerrainSameDebugPlaneNeighbor(south, cell)) {
                xRuns[{cell.lodRing, cell.topY, z1}].push_back({x0, x1});
            }
        }

        auto emitMergedRuns = [&](const auto& runMap, bool alongX) {
            for (const auto& entry : runMap) {
                const FarTerrainDebugLineKey& key = entry.first;
                std::vector<std::pair<int, int>> intervals = entry.second;
                if (intervals.empty()) continue;
                std::sort(intervals.begin(), intervals.end());
                std::vector<std::pair<int, int>> merged;
                merged.reserve(intervals.size());
                for (const auto& interval : intervals) {
                    if (merged.empty() || interval.first > merged.back().second) {
                        merged.push_back(interval);
                    } else {
                        merged.back().second = std::max(merged.back().second, interval.second);
                    }
                }

                const glm::vec3 color = farTerrainBoundsDebugColor(key.ring);
                for (const auto& interval : merged) {
                    const float start = static_cast<float>(interval.first) * 0.5f;
                    const float end = static_cast<float>(interval.second) * 0.5f;
                    const float fixed = static_cast<float>(key.fixedHalf) * 0.5f;
                    const float y = static_cast<float>(key.topY) + 1.03f;
                    const float length = std::max(0.05f, end - start);
                    const float thickness = glm::clamp(length * 0.0125f, 0.05f, 0.22f);
                    if (alongX) {
                        farTerrainAppendBoundsDebugFace(
                            faces,
                            visibleFaceCount,
                            2,
                            glm::vec3((start + end) * 0.5f, y, fixed),
                            glm::vec2(length, thickness),
                            color
                        );
                    } else {
                        farTerrainAppendBoundsDebugFace(
                            faces,
                            visibleFaceCount,
                            2,
                            glm::vec3(fixed, y, (start + end) * 0.5f),
                            glm::vec2(thickness, length),
                            color
                        );
                    }
                }
            }
        };

        emitMergedRuns(xRuns, true);
        emitMergedRuns(zRuns, false);
    }

    void farTerrainAppendTopFace(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                 size_t& visibleFaceCount,
                                 const WorldContext& world,
                                 const std::vector<Entity>& prototypes,
                                 const FarTerrainCachedCell& cell,
                                 const std::vector<FarTerrainCachedCell>& cells,
                                 const std::unordered_map<uint64_t, size_t>& cellIndex) {
        const int tile = farTerrainMaterialTile(world, prototypes, cell.prototypeName, 2);

        FaceInstanceRenderData face{};
        face.position = glm::vec3(
            static_cast<float>(cell.x) + 0.5f * static_cast<float>(cell.size - 1),
            static_cast<float>(cell.topY) + 0.5f,
            static_cast<float>(cell.z) + 0.5f * static_cast<float>(cell.size - 1)
        );
        face.tileIndex = tile;
        face.color = farTerrainLitColor(cell, 2, tile >= 0);
        face.alpha = 1.0f;
        face.ao = farTerrainTopAo(cell, cells, cellIndex);
        face.scale = glm::vec2(static_cast<float>(cell.size), static_cast<float>(cell.size));
        face.uvScale = farTerrainTileUvScale(face.scale);
        faces[2].push_back(face);
        visibleFaceCount += 1;
    }

    void farTerrainAppendVerticalFaceSegment(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                             size_t& visibleFaceCount,
                                             const WorldContext& world,
                                             const std::vector<Entity>& prototypes,
                                             const FarTerrainCachedCell& cell,
                                             int faceType,
                                             int bottomY,
                                             int topY,
                                             const char* prototypeName,
                                             const glm::vec3& fallbackColor,
                                             bool upperContact) {
        const int heightSpan = topY - bottomY;
        if (heightSpan <= 0) return;

        const int tile = farTerrainMaterialTile(world, prototypes, prototypeName, faceType);
        FaceInstanceRenderData face{};
        face.tileIndex = tile;
        FarTerrainCachedCell litCell = cell;
        litCell.fallbackColor = fallbackColor;
        face.color = farTerrainLitColor(litCell, faceType, tile >= 0);
        face.alpha = 1.0f;
        face.ao = farTerrainVerticalAo(heightSpan, upperContact);

        if (faceType == 0) {
            face.position = glm::vec3(
                static_cast<float>(cell.x + cell.size - 1) + 0.5f,
                static_cast<float>(bottomY) + 0.5f * static_cast<float>(heightSpan - 1),
                static_cast<float>(cell.z) + 0.5f * static_cast<float>(cell.size - 1)
            );
            face.scale = glm::vec2(static_cast<float>(cell.size), static_cast<float>(heightSpan));
        } else if (faceType == 1) {
            face.position = glm::vec3(
                static_cast<float>(cell.x) - 0.5f,
                static_cast<float>(bottomY) + 0.5f * static_cast<float>(heightSpan - 1),
                static_cast<float>(cell.z) + 0.5f * static_cast<float>(cell.size - 1)
            );
            face.scale = glm::vec2(static_cast<float>(cell.size), static_cast<float>(heightSpan));
        } else if (faceType == 4) {
            face.position = glm::vec3(
                static_cast<float>(cell.x) + 0.5f * static_cast<float>(cell.size - 1),
                static_cast<float>(bottomY) + 0.5f * static_cast<float>(heightSpan - 1),
                static_cast<float>(cell.z + cell.size - 1) + 0.5f
            );
            face.scale = glm::vec2(static_cast<float>(cell.size), static_cast<float>(heightSpan));
        } else if (faceType == 5) {
            face.position = glm::vec3(
                static_cast<float>(cell.x) + 0.5f * static_cast<float>(cell.size - 1),
                static_cast<float>(bottomY) + 0.5f * static_cast<float>(heightSpan - 1),
                static_cast<float>(cell.z) - 0.5f
            );
            face.scale = glm::vec2(static_cast<float>(cell.size), static_cast<float>(heightSpan));
        } else {
            return;
        }

        face.uvScale = farTerrainTileUvScale(face.scale);
        faces[static_cast<size_t>(faceType)].push_back(face);
        visibleFaceCount += 1;
    }

    void farTerrainAppendVerticalFace(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                      size_t& visibleFaceCount,
                                      const WorldContext& world,
                                      const std::vector<Entity>& prototypes,
                                      const FarTerrainCachedCell& cell,
                                      int faceType,
                                      int bottomY,
                                      bool upperContact) {
        const int heightSpan = cell.topY - bottomY;
        if (heightSpan <= 0) return;

        constexpr int kSoilDepthBlocks = 5;
        const int soilBottomY = std::max(bottomY, cell.topY - kSoilDepthBlocks);
        if (soilBottomY > bottomY) {
            farTerrainAppendVerticalFaceSegment(
                faces,
                visibleFaceCount,
                world,
                prototypes,
                cell,
                faceType,
                bottomY,
                soilBottomY,
                cell.deepPrototypeName,
                cell.deepFallbackColor,
                false
            );
        }
        farTerrainAppendVerticalFaceSegment(
            faces,
            visibleFaceCount,
            world,
            prototypes,
            cell,
            faceType,
            soilBottomY,
            cell.topY,
            cell.sidePrototypeName,
            cell.sideFallbackColor,
            upperContact
        );
    }

    struct FarTerrainVerticalRunKey {
        int faceType = 0;
        int fixedHalf = 0;
        int bottomY = 0;
        int topY = 0;
        int tileIndex = -1;
        uint32_t packedColor = 0;
        bool upperContact = false;

        bool operator==(const FarTerrainVerticalRunKey& other) const noexcept {
            return faceType == other.faceType
                && fixedHalf == other.fixedHalf
                && bottomY == other.bottomY
                && topY == other.topY
                && tileIndex == other.tileIndex
                && packedColor == other.packedColor
                && upperContact == other.upperContact;
        }
    };

    struct FarTerrainVerticalRunKeyHash {
        size_t operator()(const FarTerrainVerticalRunKey& key) const noexcept {
            size_t h = std::hash<int>()(key.faceType);
            h ^= std::hash<int>()(key.fixedHalf) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            h ^= std::hash<int>()(key.bottomY) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            h ^= std::hash<int>()(key.topY) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            h ^= std::hash<int>()(key.tileIndex) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            h ^= std::hash<uint32_t>()(key.packedColor) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            h ^= std::hash<int>()(key.upperContact ? 1 : 0) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
            return h;
        }
    };

    struct FarTerrainVerticalRunInfo {
        glm::vec3 color = glm::vec3(1.0f);
    };

    void farTerrainCollectVerticalFaceSegment(
        std::unordered_map<FarTerrainVerticalRunKey, std::vector<std::pair<int, int>>, FarTerrainVerticalRunKeyHash>& runs,
        std::unordered_map<FarTerrainVerticalRunKey, FarTerrainVerticalRunInfo, FarTerrainVerticalRunKeyHash>& infos,
        FarTerrainMaterialTileCache& tileCache,
        const FarTerrainCachedCell& cell,
        int faceType,
        int bottomY,
        int topY,
        const char* prototypeName,
        const glm::vec3& fallbackColor,
        bool upperContact
    ) {
        const int heightSpan = topY - bottomY;
        if (heightSpan <= 0) return;

        const int tileIndex = tileCache.get(prototypeName, faceType);
        FarTerrainCachedCell litCell = cell;
        litCell.fallbackColor = fallbackColor;
        const glm::vec3 color = farTerrainLitColor(litCell, faceType, tileIndex >= 0);

        int fixedHalf = 0;
        int runStart = 0;
        int runEnd = 0;
        if (faceType == 0) {
            fixedHalf = 2 * (cell.x + cell.size) - 1;
            runStart = cell.z;
            runEnd = cell.z + cell.size;
        } else if (faceType == 1) {
            fixedHalf = 2 * cell.x - 1;
            runStart = cell.z;
            runEnd = cell.z + cell.size;
        } else if (faceType == 4) {
            fixedHalf = 2 * (cell.z + cell.size) - 1;
            runStart = cell.x;
            runEnd = cell.x + cell.size;
        } else if (faceType == 5) {
            fixedHalf = 2 * cell.z - 1;
            runStart = cell.x;
            runEnd = cell.x + cell.size;
        } else {
            return;
        }

        FarTerrainVerticalRunKey key{};
        key.faceType = faceType;
        key.fixedHalf = fixedHalf;
        key.bottomY = bottomY;
        key.topY = topY;
        key.tileIndex = tileIndex;
        key.packedColor = farTerrainPackColor(color);
        key.upperContact = upperContact;
        runs[key].push_back({runStart, runEnd});
        infos.emplace(key, FarTerrainVerticalRunInfo{color});
    }

    void farTerrainCollectVerticalFace(
        std::unordered_map<FarTerrainVerticalRunKey, std::vector<std::pair<int, int>>, FarTerrainVerticalRunKeyHash>& runs,
        std::unordered_map<FarTerrainVerticalRunKey, FarTerrainVerticalRunInfo, FarTerrainVerticalRunKeyHash>& infos,
        FarTerrainMaterialTileCache& tileCache,
        const FarTerrainCachedCell& cell,
        int faceType,
        int bottomY,
        bool upperContact
    ) {
        const int heightSpan = cell.topY - bottomY;
        if (heightSpan <= 0) return;

        constexpr int kSoilDepthBlocks = 5;
        const int soilBottomY = std::max(bottomY, cell.topY - kSoilDepthBlocks);
        if (soilBottomY > bottomY) {
            farTerrainCollectVerticalFaceSegment(
                runs,
                infos,
                tileCache,
                cell,
                faceType,
                bottomY,
                soilBottomY,
                cell.deepPrototypeName,
                cell.deepFallbackColor,
                false
            );
        }
        farTerrainCollectVerticalFaceSegment(
            runs,
            infos,
            tileCache,
            cell,
            faceType,
            soilBottomY,
            cell.topY,
            cell.sidePrototypeName,
            cell.sideFallbackColor,
            upperContact
        );
    }

    void farTerrainAppendMergedVerticalRuns(
        std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
        size_t& visibleFaceCount,
        const std::unordered_map<FarTerrainVerticalRunKey, std::vector<std::pair<int, int>>, FarTerrainVerticalRunKeyHash>& runs,
        const std::unordered_map<FarTerrainVerticalRunKey, FarTerrainVerticalRunInfo, FarTerrainVerticalRunKeyHash>& infos
    ) {
        for (const auto& entry : runs) {
            const FarTerrainVerticalRunKey& key = entry.first;
            auto infoIt = infos.find(key);
            if (infoIt == infos.end()) continue;

            std::vector<std::pair<int, int>> intervals = entry.second;
            if (intervals.empty()) continue;
            std::sort(intervals.begin(), intervals.end());

            std::vector<std::pair<int, int>> merged;
            merged.reserve(intervals.size());
            for (const auto& interval : intervals) {
                if (merged.empty() || interval.first > merged.back().second) {
                    merged.push_back(interval);
                } else {
                    merged.back().second = std::max(merged.back().second, interval.second);
                }
            }

            const int heightSpan = key.topY - key.bottomY;
            if (heightSpan <= 0) continue;
            const glm::vec4 ao = farTerrainVerticalAo(heightSpan, key.upperContact);

            for (const auto& interval : merged) {
                const int runLength = interval.second - interval.first;
                if (runLength <= 0) continue;

                FaceInstanceRenderData face{};
                face.tileIndex = key.tileIndex;
                face.color = infoIt->second.color;
                face.alpha = 1.0f;
                face.ao = ao;
                face.scale = glm::vec2(static_cast<float>(runLength), static_cast<float>(heightSpan));
                face.uvScale = farTerrainTileUvScale(face.scale);

                const float fixedCoord = static_cast<float>(key.fixedHalf) * 0.5f;
                const float runCenter = static_cast<float>(interval.first) + 0.5f * static_cast<float>(runLength - 1);
                const float yCenter = static_cast<float>(key.bottomY) + 0.5f * static_cast<float>(heightSpan - 1);

                if (key.faceType == 0 || key.faceType == 1) {
                    face.position = glm::vec3(fixedCoord, yCenter, runCenter);
                } else if (key.faceType == 4 || key.faceType == 5) {
                    face.position = glm::vec3(runCenter, yCenter, fixedCoord);
                } else {
                    continue;
                }

                faces[static_cast<size_t>(key.faceType)].push_back(face);
                visibleFaceCount += 1;
            }
        }
    }

    void farTerrainAppendCellFaces(std::array<std::vector<FaceInstanceRenderData>, 6>& faces,
                                   std::array<std::vector<WaterFaceInstanceRenderData>, 6>& waterSurfaceFaces,
                                   size_t& visibleFaceCount,
                                   const BaseSystem& baseSystem,
                                   const WorldContext& world,
                                   const std::vector<Entity>& prototypes,
                                   const std::vector<FarTerrainCachedCell>& cells,
                                   int skirtDepth,
                                   int waterFloorY,
                                   bool boundsDebugEnabled,
                                   const glm::ivec2& proxyAnchor,
                                   int proxyNearRadius,
                                   int proxyBaseCellSize,
                                   FarTerrainBuildPerfStats* perfStats = nullptr) {
        const FarTerrainProxyVoxelConfig proxyConfig = farTerrainReadProxyVoxelConfig(baseSystem);
        const bool oldSlabLodEnabled = farTerrainGetRegistryBool(baseSystem, "FarTerrainOldSlabLodEnabled", false);
        FarTerrainMaterialTileCache tileCache(world, prototypes);
        std::vector<FarTerrainCachedCell> slabCells;
        std::vector<FarTerrainCachedCell> proxyCells;
        slabCells.reserve(cells.size());
        proxyCells.reserve(cells.size());
        for (const FarTerrainCachedCell& cell : cells) {
            if (farTerrainCellUsesProxyTerrainLod(cell, proxyConfig)) {
                proxyCells.push_back(cell);
            } else if (oldSlabLodEnabled) {
                slabCells.push_back(cell);
            }
        }

        std::unordered_map<uint64_t, size_t> slabCellIndex;
        slabCellIndex.reserve(slabCells.size() * 2);
        for (size_t i = 0; i < slabCells.size(); ++i) {
            slabCellIndex.emplace(farTerrainCellKey(slabCells[i].x, slabCells[i].z), i);
        }

        if (boundsDebugEnabled && !slabCells.empty()) {
            farTerrainAppendMergedBoundsDebugRuns(faces, visibleFaceCount, slabCells, slabCellIndex);
        }

        std::unordered_map<int, std::vector<FarTerrainCachedCell>> greedyTopGroups;
        greedyTopGroups.reserve(4);
        for (const FarTerrainCachedCell& cell : slabCells) {
            if (cell.size > 0) {
                greedyTopGroups[cell.size].push_back(cell);
            }
        }

        for (const auto& entry : greedyTopGroups) {
            farTerrainAppendGreedyTopFaces(
                faces,
                visibleFaceCount,
                tileCache,
                entry.second,
                slabCellIndex,
                entry.first,
                perfStats
            );
            farTerrainAppendGreedyWaterSurfaceFaces(
                waterSurfaceFaces,
                visibleFaceCount,
                entry.second,
                entry.first
            );
        }

        farTerrainAppendTreeBillboardFaces(
            faces,
            visibleFaceCount,
            baseSystem,
            world,
            cells,
            proxyConfig
        );
        farTerrainAppendGrassBillboardFaces(
            faces,
            visibleFaceCount,
            baseSystem,
            world,
            tileCache,
            cells
        );

        farTerrainAppendWorldProxyTerrainFaces(
            faces,
            waterSurfaceFaces,
            visibleFaceCount,
            tileCache,
            proxyCells,
            proxyConfig,
            proxyAnchor,
            proxyNearRadius,
            proxyBaseCellSize,
            waterFloorY
        );

        std::unordered_map<FarTerrainVerticalRunKey, std::vector<std::pair<int, int>>, FarTerrainVerticalRunKeyHash> verticalRuns;
        std::unordered_map<FarTerrainVerticalRunKey, FarTerrainVerticalRunInfo, FarTerrainVerticalRunKeyHash> verticalRunInfos;
        verticalRuns.reserve(slabCells.size() * 4);
        verticalRunInfos.reserve(slabCells.size() * 4);
        const auto verticalStart = std::chrono::steady_clock::now();

        for (const FarTerrainCachedCell& cell : slabCells) {
            const auto appendEdge = [&](int faceType, int neighborX, int neighborZ) {
                auto it = slabCellIndex.find(farTerrainCellKey(neighborX, neighborZ));
                if (it == slabCellIndex.end()) {
                    // Missing neighbors are LOD/frustum/near-terrain boundaries, not real exposed cliffs.
                    return;
                }

                const FarTerrainCachedCell& neighbor = slabCells[it->second];
                int targetBottom = std::max(waterFloorY, cell.topY - skirtDepth);
                bool upperContact = neighbor.topY >= cell.topY - 1;
                targetBottom = std::max(targetBottom, neighbor.topY);
                farTerrainCollectVerticalFace(
                    verticalRuns,
                    verticalRunInfos,
                    tileCache,
                    cell,
                    faceType,
                    targetBottom,
                    upperContact
                );
            };

            appendEdge(0, cell.x + cell.size, cell.z);
            appendEdge(1, cell.x - cell.size, cell.z);
            appendEdge(4, cell.x, cell.z + cell.size);
            appendEdge(5, cell.x, cell.z - cell.size);
        }

        farTerrainAppendMergedVerticalRuns(
            faces,
            visibleFaceCount,
            verticalRuns,
            verticalRunInfos
        );
        if (perfStats) {
            perfStats->verticalBuildMs += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - verticalStart
            ).count();
        }
    }

    bool farTerrainSectionHasRealMesh(const BaseSystem& baseSystem, const VoxelSectionKey& key) {
        if (!baseSystem.voxelWorld || !baseSystem.voxelRender) return false;
        const VoxelChunkLifecycleState* state = baseSystem.voxelWorld->findChunkState(key);
        if (!state || !state->isRenderable()) return false;
        return baseSystem.voxelRender->renderBuffers.count(key) > 0;
    }

    bool farTerrainCellCoveredByRealTerrain(const BaseSystem& baseSystem,
                                            const FarTerrainCachedCell& cell,
                                            int sectionSize,
                                            std::unordered_map<VoxelSectionKey, bool, VoxelSectionKeyHash>* coverageCache = nullptr) {
        if (!baseSystem.voxelWorld || !baseSystem.voxelRender || sectionSize <= 0) return false;

        const int minSectionX = farTerrainFloorDivInt(cell.x, sectionSize);
        const int maxSectionX = farTerrainFloorDivInt(cell.x + cell.size - 1, sectionSize);
        const int minSectionZ = farTerrainFloorDivInt(cell.z, sectionSize);
        const int maxSectionZ = farTerrainFloorDivInt(cell.z + cell.size - 1, sectionSize);
        const int surfaceSectionY = farTerrainFloorDivInt(cell.topY, sectionSize);

        for (int sz = minSectionZ; sz <= maxSectionZ; ++sz) {
            for (int sx = minSectionX; sx <= maxSectionX; ++sx) {
                const VoxelSectionKey surfaceKey{glm::ivec3(sx, surfaceSectionY, sz)};
                bool covered = false;
                if (coverageCache) {
                    auto cached = coverageCache->find(surfaceKey);
                    if (cached != coverageCache->end()) {
                        covered = cached->second;
                    } else {
                        for (int dy = -1; dy <= 1; ++dy) {
                            const VoxelSectionKey key{glm::ivec3(sx, surfaceSectionY + dy, sz)};
                            if (farTerrainSectionHasRealMesh(baseSystem, key)) {
                                covered = true;
                                break;
                            }
                        }
                        coverageCache->emplace(surfaceKey, covered);
                    }
                } else {
                    for (int dy = -1; dy <= 1; ++dy) {
                        const VoxelSectionKey key{glm::ivec3(sx, surfaceSectionY + dy, sz)};
                        if (farTerrainSectionHasRealMesh(baseSystem, key)) {
                            covered = true;
                            break;
                        }
                    }
                }
                if (!covered) return false;
            }
        }
        return true;
    }

    uint64_t farTerrainBoundaryCoverageSignature(const BaseSystem& baseSystem,
                                                 const glm::ivec2& anchor,
                                                 int nearRadiusBlocks,
                                                 int baseCellSize,
                                                 int sectionSize) {
        if (!baseSystem.voxelWorld || !baseSystem.voxelRender || sectionSize <= 0) return 0;

        const int bandInner = 0;
        constexpr int kCoverageHandoffRadialCells = 4;
        const int handoffBandWidth = std::max(baseCellSize, baseCellSize * kCoverageHandoffRadialCells);
        const int bandOuter = std::max(sectionSize * 2, std::max(0, nearRadiusBlocks) + handoffBandWidth + sectionSize);
        uint64_t signature = 1469598103934665603ull;
        size_t includedCount = 0;

        for (const auto& entry : baseSystem.voxelRender->renderBuffers) {
            const VoxelSectionKey& key = entry.first;
            const VoxelChunkLifecycleState* state = baseSystem.voxelWorld->findChunkState(key);
            if (!state || !state->isRenderable()) continue;

            const int sectionCenterX = key.coord.x * sectionSize + sectionSize / 2;
            const int sectionCenterZ = key.coord.z * sectionSize + sectionSize / 2;
            const int dx = std::abs(sectionCenterX - anchor.x);
            const int dz = std::abs(sectionCenterZ - anchor.y);
            const int chebyshev = std::max(dx, dz);
            if (chebyshev < bandInner || chebyshev > bandOuter) continue;

            signature = farTerrainHashMix(signature, key.coord.x);
            signature = farTerrainHashMix(signature, key.coord.y);
            signature = farTerrainHashMix(signature, key.coord.z);
            includedCount += 1;
        }

        signature = farTerrainHashMix(signature, static_cast<int>(includedCount & 0x7fffffffu));
        return signature;
    }

    void farTerrainRefreshHandoffFaces(BaseSystem& baseSystem,
                                       const WorldContext& world,
                                       const std::vector<Entity>& prototypes,
                                       FarTerrainClipmapContext& ctx,
                                       int skirtDepth,
                                       int waterFloorY,
                                       int voxelSectionSize,
                                       bool hideWhenRealTerrainReady,
                                       bool boundsDebugEnabled,
                                       size_t* outSuppressedCellCount = nullptr,
                                       FarTerrainBuildPerfStats* perfStats = nullptr) {
        farTerrainClearFaceSet(ctx.handoffFaces, ctx.handoffVisibleFaceCount);
        farTerrainClearWaterFaceSet(ctx.handoffWaterSurfaceFaces);
        std::vector<FarTerrainCachedCell> visibleCells;
        visibleCells.reserve(ctx.handoffCells.size());
        std::unordered_map<VoxelSectionKey, bool, VoxelSectionKeyHash> coverageCache;
        if (hideWhenRealTerrainReady) {
            coverageCache.reserve(std::max<size_t>(64, ctx.handoffCells.size() / 4));
        }
        size_t suppressedCellCount = 0;
        const int worldMinY = static_cast<int>(std::floor(world.expanse.minY));
        for (const FarTerrainCachedCell& cell : ctx.handoffCells) {
            ctx.sectorCellTests += 1;
            if (!farTerrainHorizontalSectorVisible(
                    baseSystem,
                    cell.x,
                    cell.z,
                    cell.x + cell.size,
                    cell.z + cell.size
                )) {
                ctx.sectorCellRejected += 1;
                continue;
            }
            ctx.frustumCellTests += 1;
            const int cellRenderTopY = farTerrainCellTopYForBounds(cell);
            const int cellBottomY = std::max(worldMinY, std::max(waterFloorY, cell.topY - skirtDepth));
            if (!farTerrainConservativeAabbVisible(
                    baseSystem,
                    cell.x,
                    cellBottomY,
                    cell.z,
                    cell.x + cell.size,
                    cellRenderTopY + 1,
                    cell.z + cell.size
                )) {
                ctx.frustumCellRejected += 1;
                continue;
            }
            if (hideWhenRealTerrainReady
                && farTerrainCellCoveredByRealTerrain(baseSystem, cell, voxelSectionSize, &coverageCache)) {
                suppressedCellCount += 1;
                continue;
            }
            visibleCells.push_back(cell);
        }
        farTerrainAppendCellFaces(
            ctx.handoffFaces,
            ctx.handoffWaterSurfaceFaces,
            ctx.handoffVisibleFaceCount,
            baseSystem,
            world,
            prototypes,
            visibleCells,
            skirtDepth,
            waterFloorY,
            boundsDebugEnabled,
            ctx.anchorCell,
            ctx.nearRadiusBlocks,
            ctx.baseCellSize,
            perfStats
        );
        ctx.handoffRenderBuffersDirty = true;
        farTerrainSyncVisibleFaceCount(ctx);
        if (outSuppressedCellCount) *outSuppressedCellCount = suppressedCellCount;
    }
}

namespace FarTerrainClipmapSystemLogic {
    void UpdateFarTerrainClipmap(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float, PlatformWindowHandle) {
        if (!baseSystem.farTerrain || !baseSystem.world || !baseSystem.player) return;

        FarTerrainClipmapContext& ctx = *baseSystem.farTerrain;
        WorldContext& world = *baseSystem.world;
        const auto updateStart = std::chrono::steady_clock::now();
        const bool enabled = farTerrainGetRegistryBool(baseSystem, "FarTerrainEnabled", true);
        if (!enabled || !world.expanse.loaded) {
            if (ctx.enabled
                || ctx.visibleFaceCount > 0
                || ctx.handoffRenderBuffersValid
                || ctx.bodyRenderBuffersValid
                || !ctx.handoffRenderClusters.empty()
                || !ctx.bodyRenderClusters.empty()
                || !ctx.handoffCells.empty()
                || !ctx.bodyCells.empty()) {
                farTerrainClearAllFaces(ctx);
                ctx.handoffCells.clear();
                ctx.bodyCells.clear();
                ctx.resolvedCellCache.clear();
                farTerrainDestroyRenderBuffers(baseSystem, ctx);
            }
            ctx.enabled = false;
            ctx.initialized = false;
            ctx.lastBuildSkipped = true;
            ctx.sectorRingTests = 0;
            ctx.sectorRingRejected = 0;
            ctx.sectorCellTests = 0;
            ctx.sectorCellRejected = 0;
            ctx.frustumRingTests = 0;
            ctx.frustumRingRejected = 0;
            ctx.frustumCellTests = 0;
            ctx.frustumCellRejected = 0;
            ctx.lastSetupMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - updateStart
            ).count();
            ctx.lastBodyBuildMs = 0.0f;
            ctx.lastHandoffBuildMs = 0.0f;
            ctx.lastUploadMs = 0.0f;
            ctx.lastCellResolveMs = 0.0f;
            ctx.lastTopGreedyMs = 0.0f;
            ctx.lastTopMergeMs = 0.0f;
            ctx.lastVerticalBuildMs = 0.0f;
            ctx.lastClusterPrepMs = 0.0f;
            ctx.lastClusterUploadMs = 0.0f;
            ctx.lastUpdateMs = ctx.lastSetupMs;
            return;
        }

        constexpr int kSectionSizeBlocks = 16;
        constexpr int kFirstLodCellBlocks = 2;
        constexpr int kRingRadialCells = 4;
        constexpr int kRingCount = 10;
        constexpr int kSkirtDepthBlocks = 96;
        constexpr int kRebuildIntervalFrames = 90;
        constexpr int kForwardBiasBlocks = 96;
        constexpr float kFrustumViewBucketDegrees = 45.0f;
        const int normalDetailRadiusChunks = std::clamp(
            farTerrainGetRegistryInt(baseSystem, "FarTerrainNearProxyRadiusChunks", 4),
            2,
            12
        );
        const int nearProxyRadiusBlocks = normalDetailRadiusChunks * kSectionSizeBlocks;
        const int maxRadiusBlocks =
            nearProxyRadiusBlocks
            + (kFirstLodCellBlocks * kRingRadialCells)
            + (kFirstLodCellBlocks * 2 * kRingRadialCells)
            + (kFirstLodCellBlocks * 4 * kRingRadialCells)
            + (kFirstLodCellBlocks * 8 * kRingRadialCells)
            + (kFirstLodCellBlocks * 16 * kRingRadialCells)
            + (kFirstLodCellBlocks * 32 * kRingRadialCells)
            + (kFirstLodCellBlocks * 64 * kRingRadialCells)
            + (kFirstLodCellBlocks * 128 * kRingRadialCells)
            + (kFirstLodCellBlocks * 256 * kRingRadialCells)
            + (kFirstLodCellBlocks * 512 * kRingRadialCells);
        const int baseCell = kFirstLodCellBlocks;
        const int nearRadius = nearProxyRadiusBlocks;
        const int maxRadius = maxRadiusBlocks;
        const int ringCount = kRingCount;
        const int skirtDepth = kSkirtDepthBlocks;
        const int rebuildIntervalFrames = std::max(
            1,
            farTerrainGetRegistryInt(baseSystem, "FarTerrainRebuildIntervalFrames", kRebuildIntervalFrames)
        );
        const int forwardBiasBlocks = kForwardBiasBlocks;
        constexpr bool kHideWhenRealTerrainReady = true;
        constexpr bool kDebugLog = false;
        const bool hideWhenRealTerrainReady = kHideWhenRealTerrainReady;
        const bool debugLog = kDebugLog;
        const bool boundsDebugEnabled = farTerrainGetRegistryBool(
            baseSystem,
            "FarTerrainLodBoundsDebugEnabled",
            false
        );
        const FarTerrainProxyVoxelConfig proxyConfig = farTerrainReadProxyVoxelConfig(baseSystem);
        const uint64_t proxyVoxelConfigSignature = farTerrainProxyVoxelConfigSignature(proxyConfig);
        const int anchorStep = std::max(
            baseCell,
            ((std::max(baseCell * 4, nearRadius / 2) + baseCell - 1) / baseCell) * baseCell
        );
        const int waterFloorY = static_cast<int>(std::floor(world.expanse.waterFloor));
        const int conservativeMinY = std::min(
            static_cast<int>(std::floor(world.expanse.minY)),
            waterFloorY - kSkirtDepthBlocks
        );
        const int conservativeMaxY = farTerrainConservativeMaxY(baseSystem, world);
        const int voxelSectionSize = (baseSystem.voxelWorld && baseSystem.voxelWorld->sectionSize > 0)
            ? baseSystem.voxelWorld->sectionSize
            : 16;
        ctx.sectorRingTests = 0;
        ctx.sectorRingRejected = 0;
        ctx.sectorCellTests = 0;
        ctx.sectorCellRejected = 0;
        ctx.frustumRingTests = 0;
        ctx.frustumRingRejected = 0;
        ctx.frustumCellTests = 0;
        ctx.frustumCellRejected = 0;
        const bool frustumFrozen = FrustumCullingSystemLogic::IsDebugFrozen(baseSystem);
        const glm::vec3 cameraPos = FrustumCullingSystemLogic::GetCullingCameraPosition(baseSystem);
        const glm::vec3 prevCameraPos = FrustumCullingSystemLogic::GetCullingPreviousCameraPosition(baseSystem);
        const int viewYawBucket = farTerrainAngleBucket(
            FrustumCullingSystemLogic::GetCullingCameraYaw(baseSystem),
            kFrustumViewBucketDegrees,
            180.0f
        );
        const int viewPitchBucket = farTerrainAngleBucket(
            FrustumCullingSystemLogic::GetCullingCameraPitch(baseSystem),
            kFrustumViewBucketDegrees,
            90.0f
        );
        glm::vec2 forwardXZ(
            cameraPos.x - prevCameraPos.x,
            cameraPos.z - prevCameraPos.z
        );
        if (glm::dot(forwardXZ, forwardXZ) > 0.0025f) {
            forwardXZ = glm::normalize(forwardXZ);
        } else {
            forwardXZ = glm::vec2(0.0f, 0.0f);
        }
        const glm::ivec2 anchor(
            farTerrainFloorToMultiple(cameraPos.x + forwardXZ.x * static_cast<float>(forwardBiasBlocks), anchorStep),
            farTerrainFloorToMultiple(cameraPos.z + forwardXZ.y * static_cast<float>(forwardBiasBlocks), anchorStep)
        );
        const uint64_t boundaryCoverageSignature = farTerrainBoundaryCoverageSignature(
            baseSystem,
            anchor,
            nearRadius,
            baseCell,
            voxelSectionSize
        );

        const bool paramsChanged =
            !ctx.initialized
            || ctx.baseCellSize != baseCell
            || ctx.nearRadiusBlocks != nearRadius
            || ctx.maxRadiusBlocks != maxRadius
            || ctx.ringCount != ringCount
            || ctx.boundsDebugEnabled != boundsDebugEnabled
            || ctx.proxyVoxelConfigSignature != proxyVoxelConfigSignature;
        const bool anchorChanged = !frustumFrozen && (ctx.anchorCell != anchor);
        const glm::ivec2 anchorDelta = anchor - ctx.anchorCell;
        const bool realMeshCoverageChanged =
            ctx.initialized
            && !frustumFrozen
            && (ctx.lastBoundaryCoverageSignature != boundaryCoverageSignature);
        const bool viewBucketChanged =
            !ctx.initialized
            || (!frustumFrozen && (
                ctx.lastViewYawBucket != viewYawBucket
                || ctx.lastViewPitchBucket != viewPitchBucket
            ));
        const bool periodicRefreshDue =
            !ctx.initialized
            || (!frustumFrozen && (baseSystem.frameIndex >= ctx.lastBuildFrame + static_cast<uint64_t>(rebuildIntervalFrames)));
        const int handoffCoverageRefreshIntervalFrames = std::max(
            1,
            farTerrainGetRegistryInt(baseSystem, "FarTerrainHandoffCoverageRefreshIntervalFrames", 30)
        );
        const bool coverageRefreshDue =
            realMeshCoverageChanged
            && (!frustumFrozen && (
                !ctx.initialized
                || baseSystem.frameIndex >= ctx.lastBuildFrame + static_cast<uint64_t>(handoffCoverageRefreshIntervalFrames)
            ));
        const bool fullRebuildNeeded = paramsChanged || anchorChanged || viewBucketChanged;
        const bool handoffRefreshNeeded =
            !fullRebuildNeeded
            && ((realMeshCoverageChanged && coverageRefreshDue) || periodicRefreshDue);
        ctx.lastParamsChanged = paramsChanged;
        ctx.lastAnchorChanged = anchorChanged;
        ctx.lastCoverageChanged = realMeshCoverageChanged;
        ctx.lastPeriodicRefreshDue = periodicRefreshDue;
        ctx.lastViewBucketChanged = viewBucketChanged;
        ctx.lastAnchorDeltaX = anchorDelta.x;
        ctx.lastAnchorDeltaZ = anchorDelta.y;
        const auto setupEnd = std::chrono::steady_clock::now();
        ctx.lastSetupMs = std::chrono::duration<float, std::milli>(
            setupEnd - updateStart
        ).count();
        ctx.lastCellResolveMs = 0.0f;
        ctx.lastTopGreedyMs = 0.0f;
        ctx.lastTopMergeMs = 0.0f;
        ctx.lastVerticalBuildMs = 0.0f;
        ctx.lastClusterPrepMs = 0.0f;
        ctx.lastClusterUploadMs = 0.0f;
        FarTerrainBuildPerfStats perfStats{};
        if (!fullRebuildNeeded
            && !handoffRefreshNeeded
            && ctx.enabled) {
            ctx.lastFullRebuild = false;
            ctx.lastHandoffRefresh = false;
            const auto uploadStart = std::chrono::steady_clock::now();
            farTerrainUploadRenderBuffers(baseSystem, ctx, &perfStats);
            ctx.uploadOnlyCount += 1;
            ctx.lastBodyBuildMs = 0.0f;
            ctx.lastHandoffBuildMs = 0.0f;
            ctx.lastUploadMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - uploadStart
            ).count();
            ctx.lastCellResolveMs = static_cast<float>(perfStats.cellResolveMs);
            ctx.lastTopGreedyMs = static_cast<float>(perfStats.topGreedyMs);
            ctx.lastTopMergeMs = static_cast<float>(perfStats.topMergeMs);
            ctx.lastVerticalBuildMs = static_cast<float>(perfStats.verticalBuildMs);
            ctx.lastClusterPrepMs = static_cast<float>(perfStats.clusterPrepMs);
            ctx.lastClusterUploadMs = static_cast<float>(perfStats.clusterUploadMs);
            ctx.lastUpdateMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - updateStart
            ).count();
            return;
        }

        ctx.enabled = true;
        ctx.initialized = true;
        ctx.lastBuildSkipped = false;
        ctx.lastBoundaryCoverageSignature = boundaryCoverageSignature;
        ctx.boundsDebugEnabled = boundsDebugEnabled;
        ctx.proxyVoxelConfigSignature = proxyVoxelConfigSignature;
        ctx.lastViewYawBucket = viewYawBucket;
        ctx.lastViewPitchBucket = viewPitchBucket;
        size_t suppressedCellCount = 0;
        ctx.lastFullRebuild = fullRebuildNeeded;
        ctx.lastHandoffRefresh = handoffRefreshNeeded;
        ctx.lastBodyBuildMs = 0.0f;
        ctx.lastHandoffBuildMs = 0.0f;
        if (fullRebuildNeeded) {
            if (paramsChanged) ctx.fullRebuildByParamsCount += 1;
            if (anchorChanged) ctx.fullRebuildByAnchorCount += 1;
            if (viewBucketChanged) ctx.fullRebuildByViewCount += 1;
        }

        if (handoffRefreshNeeded && ctx.enabled) {
            ctx.handoffRefreshCount += 1;
            if (realMeshCoverageChanged) ctx.handoffRefreshByCoverageCount += 1;
            if (periodicRefreshDue) ctx.handoffRefreshByPeriodicCount += 1;
            const auto handoffStart = std::chrono::steady_clock::now();
            farTerrainRefreshHandoffFaces(
                baseSystem,
                world,
                prototypes,
                ctx,
                skirtDepth,
                waterFloorY,
                voxelSectionSize,
                hideWhenRealTerrainReady,
                boundsDebugEnabled,
                &suppressedCellCount,
                &perfStats
            );
            ctx.lastHandoffBuildMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - handoffStart
            ).count();
            ctx.lastBuildFrame = baseSystem.frameIndex;
            ctx.lastSuppressedCellCount = suppressedCellCount;
            ctx.lastHandoffCellCount = ctx.handoffCells.size();
            ctx.lastBodyCellCount = 0;
            for (size_t i = 0; i < ctx.lodCellCounts.size(); ++i) {
                if (i > 0) ctx.lastBodyCellCount += ctx.lodCellCounts[i];
            }
        } else {
            farTerrainClearAllFaces(ctx);
            ctx.handoffCells.clear();
            ctx.bodyCells.clear();
            ctx.anchorCell = anchor;
            ctx.baseCellSize = baseCell;
            ctx.nearRadiusBlocks = nearRadius;
            ctx.maxRadiusBlocks = maxRadius;
            ctx.ringCount = ringCount;
            ctx.visibleRingCount = ringCount;
            ctx.rebuildCount += 1;
            ctx.lastBuildFrame = baseSystem.frameIndex;
            ctx.handoffRenderBuffersDirty = true;
            ctx.bodyRenderBuffersDirty = true;
            ctx.lodCellCounts.fill(0);
            ctx.lodFaceCounts.fill(0);
            ctx.lodTriangleCounts.fill(0);
            constexpr size_t kMaxResolvedCellCacheEntries = 65536;
            if (paramsChanged || ctx.resolvedCellCache.size() > kMaxResolvedCellCacheEntries) {
                ctx.resolvedCellCache.clear();
            }

            const auto bodyBuildStart = std::chrono::steady_clock::now();
            const int handoffBandWidth = kFirstLodCellBlocks * kRingRadialCells;
            const int handoffOuterRadius = std::min(maxRadius, nearRadius + handoffBandWidth);
            FarTerrainHydrologySampler hydrologySampler = farTerrainCreateHydrologySampler(baseSystem, world);

            int innerRadius = 0;
            for (int ring = 0; ring < ctx.visibleRingCount && innerRadius < maxRadius; ++ring) {
                const int cellSize = kFirstLodCellBlocks << ring;
                const int targetOuter = innerRadius
                    + cellSize * kRingRadialCells
                    + (ring == 0 ? nearRadius : 0);
                const int outerRadius = (ring == ringCount - 1)
                    ? maxRadius
                    : std::min(maxRadius, targetOuter);
                const int minX = farTerrainFloorToMultiple(static_cast<float>(anchor.x - outerRadius), cellSize);
                const int maxX = farTerrainFloorToMultiple(static_cast<float>(anchor.x + outerRadius), cellSize);
                const int minZ = farTerrainFloorToMultiple(static_cast<float>(anchor.y - outerRadius), cellSize);
                const int maxZ = farTerrainFloorToMultiple(static_cast<float>(anchor.y + outerRadius), cellSize);
                ctx.sectorRingTests += 1;
                if (!farTerrainHorizontalSectorVisible(
                        baseSystem,
                        minX,
                        minZ,
                        maxX + cellSize,
                        maxZ + cellSize
                    )) {
                    ctx.sectorRingRejected += 1;
                    innerRadius = outerRadius;
                    continue;
                }
                ctx.frustumRingTests += 1;
                if (!farTerrainConservativeAabbVisible(
                        baseSystem,
                        minX,
                        conservativeMinY,
                        minZ,
                        maxX + cellSize,
                        conservativeMaxY,
                        maxZ + cellSize
                    )) {
                    ctx.frustumRingRejected += 1;
                    innerRadius = outerRadius;
                    continue;
                }
                const auto cellResolveStart = std::chrono::steady_clock::now();

                for (int z = minZ; z <= maxZ; z += cellSize) {
                    for (int x = minX; x <= maxX; x += cellSize) {
                        const float centerX = static_cast<float>(x) + 0.5f * static_cast<float>(cellSize);
                        const float centerZ = static_cast<float>(z) + 0.5f * static_cast<float>(cellSize);
                        const float dx = std::abs(centerX - static_cast<float>(anchor.x));
                        const float dz = std::abs(centerZ - static_cast<float>(anchor.y));
                        const float chebyshev = std::max(dx, dz);
                        if (chebyshev <= static_cast<float>(innerRadius) || chebyshev > static_cast<float>(outerRadius)) {
                            continue;
                        }
                        ctx.sectorCellTests += 1;
                        if (!farTerrainHorizontalSectorVisible(
                                baseSystem,
                                x,
                                z,
                                x + cellSize,
                                z + cellSize
                            )) {
                            ctx.sectorCellRejected += 1;
                            continue;
                        }
                        ctx.frustumCellTests += 1;
                        if (!farTerrainConservativeAabbVisible(
                                baseSystem,
                                x,
                                conservativeMinY,
                                z,
                                x + cellSize,
                                conservativeMaxY,
                                z + cellSize
                            )) {
                            ctx.frustumCellRejected += 1;
                            continue;
                        }

                        const uint64_t resolveCacheKey = farTerrainCellResolveCacheKey(x, z, cellSize);
                        FarTerrainCachedCell cell{};
                        auto cachedCell = ctx.resolvedCellCache.find(resolveCacheKey);
                        if (cachedCell != ctx.resolvedCellCache.end()) {
                            cell = cachedCell->second;
                            cell.lodRing = ring;
                        } else {
                            float height = 0.0f;
                            const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(world, centerX, centerZ, height);
                            const int rawSurfaceY = static_cast<int>(std::floor(height));
                            FarTerrainHydrologySample hydrology{};
                            int resolvedSurfaceY = waterFloorY;
                            if (isLand) {
                                hydrology = farTerrainHydrologySample(
                                    hydrologySampler,
                                    world,
                                    centerX,
                                    centerZ,
                                    isLand,
                                    rawSurfaceY
                                );
                                resolvedSurfaceY = hydrology.terrainSurfaceY;
                            }

                            const int biome = isLand
                                ? ExpanseBiomeSystemLogic::ResolveBiome(world, centerX, centerZ)
                                : -1;
                            const FarTerrainMaterial material = isLand
                                ? farTerrainMaterialForBiome(world, biome)
                                : farTerrainSeabedMaterialForSample(world);
                            const FarTerrainMaterial sideMaterial = isLand
                                ? farTerrainSideMaterialForBiome(world, biome)
                                : farTerrainSeabedMaterialForSample(world);
                            const FarTerrainMaterial deepMaterial = farTerrainDeepMaterialForSample(world);
                            cell.x = x;
                            cell.z = z;
                            cell.size = cellSize;
                            cell.topY = std::max(resolvedSurfaceY, waterFloorY);
                            cell.lodRing = ring;
                            if (isLand && hydrology.hasWater) {
                                cell.hasWaterSurface = true;
                                cell.waterSurfaceY = hydrology.waterSurfaceY;
                                cell.waterFloorY = hydrology.waterFloorY;
                                cell.waterWaveClass = hydrology.waterWaveClass;
                            } else if (!isLand && world.expanse.waterSurface > world.expanse.waterFloor) {
                                cell.hasWaterSurface = true;
                                cell.waterSurfaceY = static_cast<int>(std::floor(world.expanse.waterSurface));
                                cell.waterFloorY = waterFloorY;
                                cell.waterWaveClass = kFarTerrainWaterWaveClassOcean;
                            }
                            cell.prototypeName = material.prototypeName ? material.prototypeName : "GrassBlockTex";
                            cell.sidePrototypeName = sideMaterial.prototypeName ? sideMaterial.prototypeName : "DirtBlockTex";
                            cell.deepPrototypeName = deepMaterial.prototypeName ? deepMaterial.prototypeName : "StoneBlockTex";
                            cell.fallbackColor = material.fallbackColor;
                            cell.sideFallbackColor = sideMaterial.fallbackColor;
                            cell.deepFallbackColor = deepMaterial.fallbackColor;
                            ctx.resolvedCellCache.emplace(resolveCacheKey, cell);
                        }
                        ctx.lastLandCellCount += 1;
                        if (ring >= 0 && ring < static_cast<int>(ctx.lodCellCounts.size())) {
                            ctx.lodCellCounts[static_cast<size_t>(ring)] += 1;
                        }

                        if (chebyshev <= static_cast<float>(handoffOuterRadius)) {
                            ctx.handoffCells.push_back(cell);
                        } else {
                            ctx.bodyCells.push_back(cell);
                        }
                    }
                }
                perfStats.cellResolveMs += std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - cellResolveStart
                ).count();
                innerRadius = outerRadius;
            }
            if (!ctx.bodyCells.empty()) {
                const size_t facesBefore = ctx.bodyVisibleFaceCount;
                farTerrainAppendCellFaces(
                    ctx.bodyFaces,
                    ctx.bodyWaterSurfaceFaces,
                    ctx.bodyVisibleFaceCount,
                    baseSystem,
                    world,
                    prototypes,
                    ctx.bodyCells,
                    skirtDepth,
                    waterFloorY,
                    boundsDebugEnabled,
                    anchor,
                    nearRadius,
                    baseCell,
                    &perfStats
                );

                const size_t faceDelta = ctx.bodyVisibleFaceCount - facesBefore;
                if (faceDelta > 0) {
                    std::array<size_t, 10> bodyCellCounts{};
                    size_t bodyCellTotal = 0;
                    for (const FarTerrainCachedCell& cell : ctx.bodyCells) {
                        if (cell.lodRing >= 0 && cell.lodRing < static_cast<int>(bodyCellCounts.size())) {
                            bodyCellCounts[static_cast<size_t>(cell.lodRing)] += 1;
                            bodyCellTotal += 1;
                        }
                    }
                    if (bodyCellTotal > 0) {
                        size_t assignedFaces = 0;
                        int lastRingWithCells = -1;
                        for (size_t i = 0; i < bodyCellCounts.size(); ++i) {
                            if (bodyCellCounts[i] == 0) continue;
                            lastRingWithCells = static_cast<int>(i);
                            const size_t ringFaces = (faceDelta * bodyCellCounts[i]) / bodyCellTotal;
                            ctx.lodFaceCounts[i] += ringFaces;
                            ctx.lodTriangleCounts[i] += ringFaces * 2u;
                            assignedFaces += ringFaces;
                        }
                        if (lastRingWithCells >= 0 && assignedFaces < faceDelta) {
                            const size_t remainder = faceDelta - assignedFaces;
                            ctx.lodFaceCounts[static_cast<size_t>(lastRingWithCells)] += remainder;
                            ctx.lodTriangleCounts[static_cast<size_t>(lastRingWithCells)] += remainder * 2u;
                        }
                    }
                }
            }
            ctx.lastBodyBuildMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - bodyBuildStart
            ).count();

            size_t handoffSuppressedCellCount = 0;
            const auto handoffStart = std::chrono::steady_clock::now();
            farTerrainRefreshHandoffFaces(
                baseSystem,
                world,
                prototypes,
                ctx,
                skirtDepth,
                waterFloorY,
                voxelSectionSize,
                hideWhenRealTerrainReady,
                boundsDebugEnabled,
                &handoffSuppressedCellCount,
                &perfStats
            );
            ctx.lastHandoffBuildMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - handoffStart
            ).count();
            ctx.lastSuppressedCellCount = suppressedCellCount + handoffSuppressedCellCount;
            ctx.lastHandoffCellCount = ctx.handoffCells.size();
            ctx.lastBodyCellCount = ctx.lastLandCellCount >= ctx.lastHandoffCellCount
                ? (ctx.lastLandCellCount - ctx.lastHandoffCellCount)
                : 0;
        }

        if (debugLog) {
            std::cout << "[FarTerrain] rebuild=" << ctx.rebuildCount
                      << " faces=" << ctx.visibleFaceCount
                      << " handoffFaces=" << ctx.handoffVisibleFaceCount
                      << " bodyFaces=" << ctx.bodyVisibleFaceCount
                      << " landCells=" << ctx.lastLandCellCount
                      << " handoffCells=" << ctx.lastHandoffCellCount
                      << " bodyCells=" << ctx.lastBodyCellCount
                      << " suppressed=" << ctx.lastSuppressedCellCount
                      << " anchor=(" << ctx.anchorCell.x << "," << ctx.anchorCell.y << ")"
                      << " near=" << ctx.nearRadiusBlocks
                      << " max=" << ctx.maxRadiusBlocks
                      << " rings=" << ctx.ringCount
                      << " visibleRings=" << ctx.visibleRingCount
                      << std::endl;
        }
        const auto uploadStart = std::chrono::steady_clock::now();
        farTerrainUploadRenderBuffers(baseSystem, ctx, &perfStats);
        ctx.lastUploadMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - uploadStart
        ).count();
        ctx.lastCellResolveMs = static_cast<float>(perfStats.cellResolveMs);
        ctx.lastTopGreedyMs = static_cast<float>(perfStats.topGreedyMs);
        ctx.lastTopMergeMs = static_cast<float>(perfStats.topMergeMs);
        ctx.lastVerticalBuildMs = static_cast<float>(perfStats.verticalBuildMs);
        ctx.lastClusterPrepMs = static_cast<float>(perfStats.clusterPrepMs);
        ctx.lastClusterUploadMs = static_cast<float>(perfStats.clusterUploadMs);
        ctx.lastUpdateMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - updateStart
        ).count();
    }
}
