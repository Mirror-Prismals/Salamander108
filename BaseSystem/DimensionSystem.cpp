#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>

namespace ExpanseBiomeSystemLogic {
    bool SampleTerrain(const WorldContext& worldCtx, float x, float z, float& outHeight);
    void LoadExpanseConfig(BaseSystem&, std::vector<Entity>&, float, PlatformWindowHandle);
}
namespace TerrainSystemLogic { void ResetExpanseVoxelStreaming(BaseSystem&, bool flushActiveWorld); }

namespace DimensionSystemLogic {

    namespace {
        constexpr const char* kOverworldDimension = "overworld";
        constexpr const char* kNightworldDimension = "nightworld";
        constexpr uint32_t kWhiteColor = 0x00ffffffu;

        enum class PortalAxis { AlongX, AlongZ };

        float g_portalCooldownSeconds = 0.0f;

        std::string getRegistryString(const BaseSystem& baseSystem,
                                      const std::string& key,
                                      const std::string& fallback = "") {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            return std::get<std::string>(it->second);
        }

        int findPrototypeIDByName(const std::vector<Entity>& prototypes, const char* name) {
            if (!name) return -1;
            for (const Entity& proto : prototypes) {
                if (proto.name == name) return proto.prototypeID;
            }
            return -1;
        }

        uint32_t blockAt(const VoxelWorldContext& voxelWorld, const glm::ivec3& cell) {
            return voxelWorld.getBlockWorld(cell);
        }

        glm::ivec3 frameCell(PortalAxis axis, int planeCoord, int baseU, int baseY, int du, int dy) {
            if (axis == PortalAxis::AlongX) {
                return glm::ivec3(baseU + du, baseY + dy, planeCoord);
            }
            return glm::ivec3(planeCoord, baseY + dy, baseU + du);
        }

        bool cellMatchesFrame(const glm::ivec3& cell,
                              PortalAxis axis,
                              int planeCoord,
                              int baseU,
                              int baseY,
                              int width,
                              int height) {
            for (int dy = 0; dy < height; ++dy) {
                for (int du = 0; du < width; ++du) {
                    const bool boundary = (du == 0 || du == width - 1 || dy == 0 || dy == height - 1);
                    if (!boundary) continue;
                    if (frameCell(axis, planeCoord, baseU, baseY, du, dy) == cell) return true;
                }
            }
            return false;
        }

        bool activatePortalFrameAt(BaseSystem& baseSystem,
                                   const std::vector<Entity>& prototypes,
                                   const glm::ivec3& targetCell) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const int framePrototypeID = findPrototypeIDByName(prototypes, "DepthStoneBlockTex");
            const int portalPrototypeID = findPrototypeIDByName(prototypes, "VoidPortalBlockTex");
            if (framePrototypeID < 0 || portalPrototypeID < 0) return false;
            if (blockAt(voxelWorld, targetCell) != static_cast<uint32_t>(framePrototypeID)) return false;

            struct PortalSize { int width; int height; };
            static const std::array<PortalSize, 2> kPortalSizes = {{{4, 5}, {3, 4}}};

            static const std::array<PortalAxis, 2> kAxes = {PortalAxis::AlongX, PortalAxis::AlongZ};
            for (PortalAxis axis : kAxes) {
                const int planeCoord = (axis == PortalAxis::AlongX) ? targetCell.z : targetCell.x;
                const int targetU = (axis == PortalAxis::AlongX) ? targetCell.x : targetCell.z;
                for (const PortalSize size : kPortalSizes) {
                    for (int baseY = targetCell.y - size.height + 1; baseY <= targetCell.y; ++baseY) {
                        for (int baseU = targetU - size.width + 1; baseU <= targetU; ++baseU) {
                            if (!cellMatchesFrame(targetCell, axis, planeCoord, baseU, baseY, size.width, size.height)) {
                                continue;
                            }

                            bool validFrame = true;
                            for (int dy = 0; dy < size.height && validFrame; ++dy) {
                                for (int du = 0; du < size.width; ++du) {
                                    const bool boundary = (du == 0 || du == size.width - 1 || dy == 0 || dy == size.height - 1);
                                    if (!boundary) continue;
                                    const glm::ivec3 cell = frameCell(axis, planeCoord, baseU, baseY, du, dy);
                                    if (blockAt(voxelWorld, cell) != static_cast<uint32_t>(framePrototypeID)) {
                                        validFrame = false;
                                        break;
                                    }
                                }
                            }
                            if (!validFrame) continue;

                            for (int dy = 1; dy < size.height - 1; ++dy) {
                                for (int du = 1; du < size.width - 1; ++du) {
                                    const glm::ivec3 cell = frameCell(axis, planeCoord, baseU, baseY, du, dy);
                                    const uint32_t id = blockAt(voxelWorld, cell);
                                    if (id != 0u && id != static_cast<uint32_t>(portalPrototypeID)) {
                                        validFrame = false;
                                        break;
                                    }
                                }
                                if (!validFrame) break;
                            }
                            if (!validFrame) continue;

                            for (int dy = 1; dy < size.height - 1; ++dy) {
                                for (int du = 1; du < size.width - 1; ++du) {
                                    voxelWorld.setBlockWorld(
                                        frameCell(axis, planeCoord, baseU, baseY, du, dy),
                                        static_cast<uint32_t>(portalPrototypeID),
                                        kWhiteColor
                                    );
                                }
                            }
                            std::cout << "DimensionSystem: activated portal frame at ["
                                      << targetCell.x << ", " << targetCell.y << ", " << targetCell.z << "]"
                                      << std::endl;
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        bool isTouchingPortal(const BaseSystem& baseSystem,
                              int portalPrototypeID,
                              glm::ivec3* outTouchedCell) {
            if (!baseSystem.player || !baseSystem.voxelWorld) return false;
            const PlayerContext& player = *baseSystem.player;
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            if (!voxelWorld.enabled) return false;

            const glm::vec3 camera = player.cameraPosition;
            const float feetY = camera.y - 1.5f;
            static const std::array<float, 5> kOffsets = {0.0f, -0.45f, 0.45f, -0.2f, 0.2f};
            static const std::array<float, 6> kHeights = {-0.6f, -0.1f, 0.0f, 0.9f, 1.4f, 1.9f};

            for (float oy : kHeights) {
                for (float ox : kOffsets) {
                    for (float oz : kOffsets) {
                        glm::ivec3 cell(
                            static_cast<int>(std::floor(camera.x + ox)),
                            static_cast<int>(std::floor(feetY + oy)),
                            static_cast<int>(std::floor(camera.z + oz))
                        );
                        const uint32_t id = voxelWorld.getBlockWorld(cell);
                        if (id == static_cast<uint32_t>(portalPrototypeID)) {
                            if (outTouchedCell) *outTouchedCell = cell;
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        PortalAxis detectPortalAxis(const VoxelWorldContext& voxelWorld,
                                    const glm::ivec3& portalCell,
                                    int portalPrototypeID) {
            const uint32_t portalID = static_cast<uint32_t>(portalPrototypeID);
            const int alongX =
                (blockAt(voxelWorld, portalCell + glm::ivec3(-1, 0, 0)) == portalID ? 1 : 0) +
                (blockAt(voxelWorld, portalCell + glm::ivec3(1, 0, 0)) == portalID ? 1 : 0);
            const int alongZ =
                (blockAt(voxelWorld, portalCell + glm::ivec3(0, 0, -1)) == portalID ? 1 : 0) +
                (blockAt(voxelWorld, portalCell + glm::ivec3(0, 0, 1)) == portalID ? 1 : 0);
            return (alongZ > alongX) ? PortalAxis::AlongZ : PortalAxis::AlongX;
        }

        glm::vec3 portalExitPosition(const PlayerContext& player,
                                     const glm::ivec3& portalCell,
                                     PortalAxis axis) {
            glm::vec3 out = player.cameraPosition;
            if (axis == PortalAxis::AlongX) {
                const float side = (player.cameraPosition.z >= static_cast<float>(portalCell.z)) ? 1.0f : -1.0f;
                out.x = static_cast<float>(portalCell.x) + 0.5f;
                out.z = static_cast<float>(portalCell.z) + 0.5f + side * 2.0f;
            } else {
                const float side = (player.cameraPosition.x >= static_cast<float>(portalCell.x)) ? 1.0f : -1.0f;
                out.x = static_cast<float>(portalCell.x) + 0.5f + side * 2.0f;
                out.z = static_cast<float>(portalCell.z) + 0.5f;
            }
            return out;
        }

        void placeSimpleArrivalPortal(BaseSystem& baseSystem,
                                      const std::vector<Entity>& prototypes,
                                      const glm::ivec3& sourcePortalCell,
                                      PortalAxis axis) {
            if (!baseSystem.world || !baseSystem.voxelWorld || !baseSystem.player) return;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            PlayerContext& player = *baseSystem.player;
            const int framePrototypeID = findPrototypeIDByName(prototypes, "DepthStoneBlockTex");
            const int portalPrototypeID = findPrototypeIDByName(prototypes, "VoidPortalBlockTex");
            if (framePrototypeID < 0 || portalPrototypeID < 0) return;

            float sampledHeight = 0.0f;
            const bool isLand = ExpanseBiomeSystemLogic::SampleTerrain(
                *baseSystem.world,
                static_cast<float>(sourcePortalCell.x),
                static_cast<float>(sourcePortalCell.z),
                sampledHeight
            );
            const int surfaceY = isLand
                ? static_cast<int>(std::floor(sampledHeight))
                : static_cast<int>(std::floor(baseSystem.world->expanse.waterSurface));
            const int baseY = surfaceY + 1;
            const int width = 4;
            const int height = 5;
            const int planeCoord = (axis == PortalAxis::AlongX) ? sourcePortalCell.z : sourcePortalCell.x;
            const int centerU = (axis == PortalAxis::AlongX) ? sourcePortalCell.x : sourcePortalCell.z;
            const int baseU = centerU - 1;

            for (int dz = -3; dz <= 3; ++dz) {
                for (int dx = -3; dx <= 3; ++dx) {
                    const glm::ivec3 floorCell(sourcePortalCell.x + dx, baseY - 1, sourcePortalCell.z + dz);
                    voxelWorld.setBlockWorld(floorCell, static_cast<uint32_t>(framePrototypeID), kWhiteColor);
                    for (int dy = 0; dy <= 3; ++dy) {
                        voxelWorld.setBlockWorld(floorCell + glm::ivec3(0, dy + 1, 0), 0u, 0u);
                    }
                }
            }

            for (int dy = 0; dy < height; ++dy) {
                for (int du = 0; du < width; ++du) {
                    const bool boundary = (du == 0 || du == width - 1 || dy == 0 || dy == height - 1);
                    const glm::ivec3 cell = frameCell(axis, planeCoord, baseU, baseY, du, dy);
                    voxelWorld.setBlockWorld(
                        cell,
                        boundary ? static_cast<uint32_t>(framePrototypeID) : static_cast<uint32_t>(portalPrototypeID),
                        kWhiteColor
                    );
                }
            }

            player.cameraPosition = portalExitPosition(player, sourcePortalCell + glm::ivec3(0, baseY - sourcePortalCell.y + 2, 0), axis);
            player.cameraPosition.y = static_cast<float>(baseY) + 1.501f;
            player.prevCameraPosition = player.cameraPosition;
            player.verticalVelocity = 0.0f;
            player.onGround = false;
        }

        void resetUiTravelFlags(BaseSystem& baseSystem) {
            if (baseSystem.registry) {
                (*baseSystem.registry)["spawn_ready"] = true;
                (*baseSystem.registry)["DimensionArrivalPending"] = false;
            }
            if (!baseSystem.ui) return;
            baseSystem.ui->levelSwitchPending = false;
            baseSystem.ui->pendingActionType.clear();
            baseSystem.ui->pendingActionKey.clear();
            baseSystem.ui->pendingActionValue.clear();
            baseSystem.ui->actionDelayFrames = 0;
            baseSystem.ui->loadingActive = false;
            baseSystem.ui->loadingTimer = 0.0f;
        }

        bool switchDimension(BaseSystem& baseSystem,
                             std::vector<Entity>& prototypes,
                             const glm::ivec3& touchedPortalCell) {
            if (!baseSystem.worldSave || !baseSystem.player || !baseSystem.voxelWorld) return false;
            if (!baseSystem.worldSave->activeWorldLoaded || baseSystem.worldSave->activeWorldId.empty()) return false;

            const int portalPrototypeID = findPrototypeIDByName(prototypes, "VoidPortalBlockTex");
            if (portalPrototypeID < 0) return false;

            const std::string currentDimension = WorldSaveSystemLogic::GetActiveDimensionId(baseSystem);
            const std::string destination = (currentDimension == kNightworldDimension)
                ? std::string(kOverworldDimension)
                : std::string(kNightworldDimension);
            const PortalAxis axis = detectPortalAxis(*baseSystem.voxelWorld, touchedPortalCell, portalPrototypeID);

            const glm::vec3 departureExit = portalExitPosition(*baseSystem.player, touchedPortalCell, axis);
            baseSystem.player->cameraPosition = departureExit;
            baseSystem.player->prevCameraPosition = departureExit;
            baseSystem.player->verticalVelocity = 0.0f;
            baseSystem.player->onGround = false;
            WorldSaveSystemLogic::FlushActiveWorld(baseSystem, false);

            WorldSaveSystemLogic::SetActiveDimensionId(baseSystem, destination);
            if (baseSystem.registry) {
                (*baseSystem.registry)["DimensionArrivalPending"] = true;
                (*baseSystem.registry)["spawn_ready"] = false;
            }

            ExpanseBiomeSystemLogic::LoadExpanseConfig(baseSystem, prototypes, 0.0f, nullptr);
            LeyLineSystemLogic::LoadLeyLines(baseSystem, prototypes, 0.0f, nullptr);
            TerrainSystemLogic::ResetExpanseVoxelStreaming(baseSystem, false);

            const bool restored = WorldSaveSystemLogic::RestoreDimensionPlayerState(baseSystem, destination);
            if (!restored) {
                placeSimpleArrivalPortal(baseSystem, prototypes, touchedPortalCell, axis);
            }
            WorldSaveSystemLogic::FlushActiveWorld(baseSystem, false);

            resetUiTravelFlags(baseSystem);
            g_portalCooldownSeconds = 1.25f;
            std::cout << "DimensionSystem: switched " << currentDimension
                      << " -> " << destination
                      << " via portal at [" << touchedPortalCell.x << ", "
                      << touchedPortalCell.y << ", " << touchedPortalCell.z << "]"
                      << std::endl;
            return true;
        }
    } // namespace

    void UpdateDimension(BaseSystem& baseSystem,
                         std::vector<Entity>& prototypes,
                         float dt,
                         PlatformWindowHandle win) {
        (void)win;
        if (g_portalCooldownSeconds > 0.0f) {
            g_portalCooldownSeconds = std::max(0.0f, g_portalCooldownSeconds - std::max(0.0f, dt));
        }

        if (!baseSystem.player || !baseSystem.voxelWorld || !baseSystem.worldSave) return;
        if (!baseSystem.voxelWorld->enabled) return;
        if (getRegistryString(baseSystem, "level", "") != "the_expanse") return;
        if (g_portalCooldownSeconds > 0.0f) return;

        PlayerContext& player = *baseSystem.player;
        if (player.hasBlockTarget && (player.leftMousePressed || player.rightMousePressed)) {
            const glm::ivec3 targetCell = glm::ivec3(glm::round(player.targetedBlockPosition));
            if (activatePortalFrameAt(baseSystem, prototypes, targetCell)) {
                player.leftMousePressed = false;
                player.rightMousePressed = false;
                g_portalCooldownSeconds = 0.35f;
                return;
            }
        }

        glm::ivec3 touchedCell(0);
        const int portalPrototypeID = findPrototypeIDByName(prototypes, "VoidPortalBlockTex");
        if (portalPrototypeID < 0) return;
        if (!isTouchingPortal(baseSystem, portalPrototypeID, &touchedCell)) return;

        switchDimension(baseSystem, prototypes, touchedCell);
    }
}
