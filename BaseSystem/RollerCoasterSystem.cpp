#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "Host/PlatformInput.h"

namespace RollerCoasterSystemLogic {

    namespace {
        constexpr int kDefaultLeftRailTile = 147;
        constexpr int kDefaultRightRailTile = 148;
        constexpr float kRailHalfWidth = 1.0f;
        constexpr float kRailSurfaceLift = 0.08f;
        constexpr float kTiePreviewLength = 0.36f;
        constexpr int kSelectedTieDebugTile = -2;
        constexpr int kTieDebugTile = -3;

        struct CoasterVertex {
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec2 uv = glm::vec2(0.0f);
            int32_t tileIndex = -1;
        };

        struct EvaluatedPose {
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 right = glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 forward = glm::vec3(0.0f, 0.0f, -1.0f);
        };

        std::string toLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool parseBool(const std::string& value, bool fallback) {
            const std::string lower = toLowerCopy(value);
            if (lower == "1" || lower == "true" || lower == "yes" || lower == "on") return true;
            if (lower == "0" || lower == "false" || lower == "no" || lower == "off") return false;
            return fallback;
        }

        bool registryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (std::holds_alternative<std::string>(it->second)) return parseBool(std::get<std::string>(it->second), fallback);
            return fallback;
        }

        int registryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<std::string>(it->second)) {
                try {
                    return std::stoi(std::get<std::string>(it->second));
                } catch (...) {
                    return fallback;
                }
            }
            return fallback;
        }

        float registryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<std::string>(it->second)) {
                try {
                    return std::stof(std::get<std::string>(it->second));
                } catch (...) {
                    return fallback;
                }
            }
            return fallback;
        }

        void setRegistryString(BaseSystem& baseSystem, const std::string& key, const std::string& value) {
            if (!baseSystem.registry) return;
            (*baseSystem.registry)[key] = value;
        }

        void logStatus(const RollerCoasterContext& coaster) {
            if (!coaster.statusMessage.empty()) {
                std::cout << "[RollerCoaster] " << coaster.statusMessage << std::endl;
            }
        }

        glm::vec3 safeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
            const float len2 = glm::dot(v, v);
            if (len2 <= 1e-8f) return fallback;
            return v / std::sqrt(len2);
        }

        int activeWorldIndex(const BaseSystem& baseSystem) {
            if (!baseSystem.level || baseSystem.level->worlds.empty()) return -1;
            int worldIndex = baseSystem.level->activeWorldIndex;
            if (worldIndex < 0 || worldIndex >= static_cast<int>(baseSystem.level->worlds.size())) {
                worldIndex = 0;
            }
            return worldIndex;
        }

        CoasterTie* findTie(RollerCoasterContext& coaster, int tieId) {
            for (CoasterTie& tie : coaster.ties) {
                if (tie.id == tieId) return &tie;
            }
            return nullptr;
        }

        const CoasterTie* findTie(const RollerCoasterContext& coaster, int tieId) {
            for (const CoasterTie& tie : coaster.ties) {
                if (tie.id == tieId) return &tie;
            }
            return nullptr;
        }

        bool tieExists(const RollerCoasterContext& coaster, int tieId) {
            return findTie(coaster, tieId) != nullptr;
        }

        void publishStatus(BaseSystem& baseSystem) {
            if (!baseSystem.rollerCoaster) return;
            const RollerCoasterContext& coaster = *baseSystem.rollerCoaster;
            std::ostringstream ss;
            ss << "ties=" << coaster.ties.size()
               << " segments=" << coaster.segments.size()
               << " selected=" << coaster.selectedTieId;
            if (!coaster.statusMessage.empty()) {
                ss << " :: " << coaster.statusMessage;
            }
            setRegistryString(baseSystem, "RollerCoasterStatus", ss.str());
            setRegistryString(baseSystem, "RollerCoasterTieCount", std::to_string(coaster.ties.size()));
            setRegistryString(baseSystem, "RollerCoasterSegmentCount", std::to_string(coaster.segments.size()));
        }

        glm::vec3 fallbackForwardForNormal(const glm::vec3& normal) {
            const glm::vec3 n = safeNormalize(normal, glm::vec3(0.0f, 1.0f, 0.0f));
            glm::vec3 candidate = glm::cross(n, glm::vec3(0.0f, 0.0f, 1.0f));
            if (glm::dot(candidate, candidate) < 1e-6f) {
                candidate = glm::cross(n, glm::vec3(1.0f, 0.0f, 0.0f));
            }
            return safeNormalize(glm::cross(candidate, n), glm::vec3(0.0f, 0.0f, -1.0f));
        }

        glm::vec3 surfaceForwardFromPlayer(const PlayerContext& player, const glm::vec3& normal) {
            const float yaw = glm::radians(player.cameraYaw);
            glm::vec3 forward(std::cos(yaw), 0.0f, std::sin(yaw));
            forward -= normal * glm::dot(forward, normal);
            return safeNormalize(forward, fallbackForwardForNormal(normal));
        }

        glm::vec3 cameraEyePosition(const BaseSystem& baseSystem, const PlayerContext& player) {
            if (baseSystem.gamemode == "survival") {
                return player.cameraPosition + glm::vec3(0.0f, 0.6f, 0.0f);
            }
            return player.cameraPosition;
        }

        glm::vec3 cameraForwardDirection(const PlayerContext& player) {
            glm::vec3 dir;
            dir.x = std::cos(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            dir.y = std::sin(glm::radians(player.cameraPitch));
            dir.z = std::sin(glm::radians(player.cameraYaw)) * std::cos(glm::radians(player.cameraPitch));
            return safeNormalize(dir, glm::vec3(0.0f, 0.0f, -1.0f));
        }

        int findExistingTie(const RollerCoasterContext& coaster,
                            int worldIndex,
                            const glm::ivec3& cell,
                            const glm::vec3& normal) {
            for (const CoasterTie& tie : coaster.ties) {
                if (tie.worldIndex != worldIndex) continue;
                if (tie.cell != cell) continue;
                if (glm::dot(tie.normal, normal) < 0.98f) continue;
                return tie.id;
            }
            return -1;
        }

        struct CoasterPlacementHit {
            bool hit = false;
            int worldIndex = -1;
            glm::ivec3 cell = glm::ivec3(0);
            glm::vec3 center = glm::vec3(0.0f);
            glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
            float distance = std::numeric_limits<float>::max();
            std::string source;
        };

        bool isCoasterBlockPrototype(const std::vector<Entity>& prototypes, uint32_t id) {
            if (id == 0u) return false;
            const int protoID = static_cast<int>(id);
            if (protoID < 0 || protoID >= static_cast<int>(prototypes.size())) return false;
            return prototypes[protoID].isBlock;
        }

        bool raycastAabbForCoaster(const glm::vec3& origin,
                                   const glm::vec3& direction,
                                   const glm::vec3& minBounds,
                                   const glm::vec3& maxBounds,
                                   float maxDistance,
                                   glm::vec3& outNormal,
                                   float& outDistance) {
            const glm::vec3 dir = safeNormalize(direction, glm::vec3(0.0f, 0.0f, -1.0f));
            float tNear = 0.0f;
            float tFar = maxDistance;
            glm::vec3 hitNormal(0.0f);
            constexpr float kEpsilon = 1e-6f;

            for (int axis = 0; axis < 3; ++axis) {
                const float o = origin[axis];
                const float d = dir[axis];
                const float minB = minBounds[axis];
                const float maxB = maxBounds[axis];

                if (std::abs(d) < kEpsilon) {
                    if (o < minB || o > maxB) return false;
                    continue;
                }

                const float invD = 1.0f / d;
                float t1 = (minB - o) * invD;
                float t2 = (maxB - o) * invD;
                glm::vec3 n1(0.0f);
                glm::vec3 n2(0.0f);
                n1[axis] = -1.0f;
                n2[axis] = 1.0f;
                if (t1 > t2) {
                    std::swap(t1, t2);
                    std::swap(n1, n2);
                }

                if (t1 > tNear) {
                    tNear = t1;
                    hitNormal = n1;
                }
                tFar = std::min(tFar, t2);
                if (tNear > tFar) return false;
            }

            if (tNear < 0.0f || tNear > maxDistance) return false;
            outDistance = tNear;
            outNormal = safeNormalize(hitNormal, safeNormalize(-dir, glm::vec3(0.0f, 1.0f, 0.0f)));
            return true;
        }

        bool raycastWorldInstancesForCoaster(const BaseSystem& baseSystem,
                                             const std::vector<Entity>& prototypes,
                                             const glm::vec3& origin,
                                             const glm::vec3& direction,
                                             float maxDistance,
                                             CoasterPlacementHit& outHit) {
            const int worldIndex = activeWorldIndex(baseSystem);
            if (worldIndex < 0 || !baseSystem.level) return false;
            const Entity& world = baseSystem.level->worlds[static_cast<size_t>(worldIndex)];

            bool found = false;
            CoasterPlacementHit best;
            best.distance = maxDistance;
            for (const EntityInstance& inst : world.instances) {
                if (inst.prototypeID < 0 || inst.prototypeID >= static_cast<int>(prototypes.size())) continue;
                if (!prototypes[inst.prototypeID].isBlock) continue;

                glm::vec3 normal(0.0f);
                float distance = maxDistance;
                const glm::vec3 minBounds = inst.position - glm::vec3(0.5f);
                const glm::vec3 maxBounds = inst.position + glm::vec3(0.5f);
                if (!raycastAabbForCoaster(origin, direction, minBounds, maxBounds, maxDistance, normal, distance)) continue;
                if (found && distance >= best.distance) continue;

                found = true;
                best.hit = true;
                best.worldIndex = worldIndex;
                best.cell = glm::ivec3(glm::round(inst.position));
                best.center = inst.position;
                best.normal = normal;
                best.distance = distance;
                best.source = "world";
            }

            if (!found) return false;
            outHit = best;
            return true;
        }

        bool raycastVoxelWorldForCoaster(const BaseSystem& baseSystem,
                                         const std::vector<Entity>& prototypes,
                                         const glm::vec3& origin,
                                         const glm::vec3& direction,
                                         float maxDistance,
                                         CoasterPlacementHit& outHit) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return false;
            const int worldIndex = activeWorldIndex(baseSystem);
            if (worldIndex < 0) return false;

            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const glm::vec3 dir = safeNormalize(direction, glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 rayPos = origin + glm::vec3(0.5f);
            glm::ivec3 cell = glm::ivec3(glm::floor(rayPos));

            glm::vec3 deltaDist(
                std::abs(dir.x) > 1e-6f ? std::abs(1.0f / dir.x) : std::numeric_limits<float>::infinity(),
                std::abs(dir.y) > 1e-6f ? std::abs(1.0f / dir.y) : std::numeric_limits<float>::infinity(),
                std::abs(dir.z) > 1e-6f ? std::abs(1.0f / dir.z) : std::numeric_limits<float>::infinity()
            );
            glm::ivec3 step(dir.x >= 0.0f ? 1 : -1, dir.y >= 0.0f ? 1 : -1, dir.z >= 0.0f ? 1 : -1);

            glm::vec3 sideDist;
            sideDist.x = (dir.x >= 0.0f) ? (cell.x + 1.0f - rayPos.x) : (rayPos.x - cell.x);
            sideDist.y = (dir.y >= 0.0f) ? (cell.y + 1.0f - rayPos.y) : (rayPos.y - cell.y);
            sideDist.z = (dir.z >= 0.0f) ? (cell.z + 1.0f - rayPos.z) : (rayPos.z - cell.z);
            sideDist *= deltaDist;

            glm::vec3 entryNormal = safeNormalize(-dir, glm::vec3(0.0f, 1.0f, 0.0f));
            float travel = 0.0f;
            const int maxSteps = 768;
            for (int i = 0; i < maxSteps && travel <= maxDistance; ++i) {
                const uint32_t id = voxelWorld.getBlockWorld(cell);
                if (isCoasterBlockPrototype(prototypes, id)) {
                    outHit.hit = true;
                    outHit.worldIndex = worldIndex;
                    outHit.cell = cell;
                    outHit.center = glm::vec3(cell);
                    outHit.normal = safeNormalize(entryNormal, safeNormalize(-dir, glm::vec3(0.0f, 1.0f, 0.0f)));
                    outHit.distance = travel;
                    outHit.source = "voxel";
                    return true;
                }

                if (sideDist.x < sideDist.y) {
                    if (sideDist.x < sideDist.z) {
                        travel = sideDist.x;
                        sideDist.x += deltaDist.x;
                        cell.x += step.x;
                        entryNormal = glm::vec3(-step.x, 0.0f, 0.0f);
                    } else {
                        travel = sideDist.z;
                        sideDist.z += deltaDist.z;
                        cell.z += step.z;
                        entryNormal = glm::vec3(0.0f, 0.0f, -step.z);
                    }
                } else {
                    if (sideDist.y < sideDist.z) {
                        travel = sideDist.y;
                        sideDist.y += deltaDist.y;
                        cell.y += step.y;
                        entryNormal = glm::vec3(0.0f, -step.y, 0.0f);
                    } else {
                        travel = sideDist.z;
                        sideDist.z += deltaDist.z;
                        cell.z += step.z;
                        entryNormal = glm::vec3(0.0f, 0.0f, -step.z);
                    }
                }
            }
            return false;
        }

        bool resolvePlacementHit(BaseSystem& baseSystem,
                                 const std::vector<Entity>& prototypes,
                                 CoasterPlacementHit& outHit) {
            if (!baseSystem.player) return false;
            const PlayerContext& player = *baseSystem.player;
            if (player.hasBlockTarget && glm::length(player.targetedBlockNormal) > 0.1f) {
                int worldIndex = player.targetedWorldIndex;
                if (worldIndex < 0) worldIndex = activeWorldIndex(baseSystem);
                if (worldIndex >= 0) {
                    outHit.hit = true;
                    outHit.worldIndex = worldIndex;
                    outHit.cell = glm::ivec3(glm::round(player.targetedBlockPosition));
                    outHit.center = player.targetedBlockPosition;
                    outHit.normal = safeNormalize(player.targetedBlockNormal, glm::vec3(0.0f, 1.0f, 0.0f));
                    outHit.distance = glm::distance(cameraEyePosition(baseSystem, player), player.targetedBlockHitPosition);
                    outHit.source = "selection";
                    return true;
                }
            }

            const glm::vec3 origin = cameraEyePosition(baseSystem, player);
            const glm::vec3 direction = cameraForwardDirection(player);
            const float maxDistance = std::max(1.0f, registryFloat(baseSystem, "RollerCoasterPlacementDistance", 12.0f));

            CoasterPlacementHit worldHit;
            CoasterPlacementHit voxelHit;
            const bool foundWorld = raycastWorldInstancesForCoaster(baseSystem, prototypes, origin, direction, maxDistance, worldHit);
            const bool foundVoxel = raycastVoxelWorldForCoaster(baseSystem, prototypes, origin, direction, maxDistance, voxelHit);
            if (!foundWorld && !foundVoxel) return false;
            if (foundWorld && (!foundVoxel || worldHit.distance <= voxelHit.distance)) {
                outHit = worldHit;
            } else {
                outHit = voxelHit;
            }
            return true;
        }

        int createOrReuseTie(BaseSystem& baseSystem, const std::vector<Entity>& prototypes) {
            RollerCoasterContext& coaster = *baseSystem.rollerCoaster;
            PlayerContext& player = *baseSystem.player;
            CoasterPlacementHit hit;
            if (!resolvePlacementHit(baseSystem, prototypes, hit)) {
                coaster.statusMessage = "No block hit for coaster splice.";
                logStatus(coaster);
                return -1;
            }

            const int worldIndex = hit.worldIndex;
            if (worldIndex < 0) {
                coaster.statusMessage = "No active world for coaster splice.";
                logStatus(coaster);
                return -1;
            }

            const glm::vec3 normal = safeNormalize(hit.normal, glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::ivec3 cell = hit.cell;
            const int existingId = findExistingTie(coaster, worldIndex, cell, normal);
            if (existingId >= 0) {
                CoasterTie* tie = findTie(coaster, existingId);
                if (tie) {
                    tie->forward = surfaceForwardFromPlayer(player, normal);
                }
                coaster.statusMessage = "Selected existing coaster splice " + std::to_string(existingId)
                    + " from " + hit.source + " hit.";
                coaster.meshDirty = true;
                logStatus(coaster);
                return existingId;
            }

            CoasterTie tie;
            tie.id = coaster.nextTieId++;
            tie.worldIndex = worldIndex;
            tie.cell = cell;
            tie.normal = normal;
            tie.forward = surfaceForwardFromPlayer(player, normal);
            tie.position = hit.center + normal * (0.5f + kRailSurfaceLift);
            coaster.ties.push_back(tie);
            coaster.meshDirty = true;
            coaster.statusMessage = "Created coaster splice " + std::to_string(tie.id)
                + " from " + hit.source + " hit at "
                + std::to_string(cell.x) + ","
                + std::to_string(cell.y) + ","
                + std::to_string(cell.z) + ".";
            logStatus(coaster);
            return tie.id;
        }

        void removeSegmentsMatching(RollerCoasterContext& coaster,
                                    const std::function<bool(const CoasterSegment&)>& predicate) {
            for (int i = static_cast<int>(coaster.segments.size()) - 1; i >= 0; --i) {
                const CoasterSegment segment = coaster.segments[static_cast<size_t>(i)];
                if (!predicate(segment)) continue;
                if (CoasterTie* start = findTie(coaster, segment.startTieId)) {
                    if (start->nextTieId == segment.endTieId) start->nextTieId = -1;
                }
                if (CoasterTie* end = findTie(coaster, segment.endTieId)) {
                    if (end->prevTieId == segment.startTieId) end->prevTieId = -1;
                }
                coaster.segments.erase(coaster.segments.begin() + i);
                coaster.meshDirty = true;
            }
        }

        bool connectTies(RollerCoasterContext& coaster, int startId, int endId) {
            if (startId == endId) {
                coaster.statusMessage = "Cannot connect a splice to itself.";
                logStatus(coaster);
                return false;
            }
            CoasterTie* start = findTie(coaster, startId);
            CoasterTie* end = findTie(coaster, endId);
            if (!start || !end) {
                coaster.statusMessage = "Cannot connect missing coaster splice.";
                logStatus(coaster);
                return false;
            }
            if (start->worldIndex != end->worldIndex) {
                coaster.statusMessage = "Cannot connect coaster splices across worlds.";
                logStatus(coaster);
                return false;
            }

            removeSegmentsMatching(coaster, [&](const CoasterSegment& segment) {
                return segment.startTieId == startId || segment.endTieId == endId;
            });

            CoasterSegment segment;
            segment.id = coaster.nextSegmentId++;
            segment.worldIndex = start->worldIndex;
            segment.startTieId = startId;
            segment.endTieId = endId;
            segment.approximateLength = glm::distance(start->position, end->position);
            coaster.segments.push_back(segment);
            start = findTie(coaster, startId);
            end = findTie(coaster, endId);
            if (start) start->nextTieId = endId;
            if (end) end->prevTieId = startId;
            coaster.meshDirty = true;
            coaster.statusMessage = "Connected coaster splice " + std::to_string(startId)
                + " -> " + std::to_string(endId) + ".";
            logStatus(coaster);
            return true;
        }

        void handlePlacementInput(BaseSystem& baseSystem, const std::vector<Entity>& prototypes, PlatformWindowHandle win) {
            if (!win || !baseSystem.rollerCoaster || !baseSystem.player) return;
            RollerCoasterContext& coaster = *baseSystem.rollerCoaster;
            if (!registryBool(baseSystem, "RollerCoasterPlacementEnabled", true)) {
                coaster.nWasDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::N);
                coaster.bWasDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::B);
                return;
            }

            const bool shiftDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::LeftShift)
                || PlatformInput::IsKeyDown(win, PlatformInput::Key::RightShift);
            const bool nDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::N);
            const bool bDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::B);

            if (nDown && !coaster.nWasDown) {
                const int newTieId = createOrReuseTie(baseSystem, prototypes);
                if (newTieId >= 0) {
                    if (!shiftDown && tieExists(coaster, coaster.selectedTieId) && coaster.selectedTieId != newTieId) {
                        connectTies(coaster, coaster.selectedTieId, newTieId);
                    }
                    coaster.selectedTieId = newTieId;
                    coaster.meshDirty = true;
                }
            }

            if (bDown && !coaster.bWasDown) {
                if (shiftDown) {
                    coaster.ties.clear();
                    coaster.segments.clear();
                    coaster.carts.clear();
                    coaster.selectedTieId = -1;
                    coaster.nextTieId = 1;
                    coaster.nextSegmentId = 1;
                    coaster.meshDirty = true;
                    coaster.statusMessage = "Cleared coaster graph.";
                    logStatus(coaster);
                } else {
                    coaster.selectedTieId = -1;
                    coaster.statusMessage = "Cleared coaster chain selection.";
                    coaster.meshDirty = true;
                    logStatus(coaster);
                }
            }

            coaster.nWasDown = nDown;
            coaster.bWasDown = bDown;
        }

        glm::vec3 hermitePoint(const CoasterTie& a, const CoasterTie& b, float t) {
            const float distance = std::max(1.0f, glm::distance(a.position, b.position));
            const glm::vec3 p0 = a.position;
            const glm::vec3 p1 = b.position;
            const glm::vec3 m0 = a.forward * distance;
            const glm::vec3 m1 = b.forward * distance;
            const float t2 = t * t;
            const float t3 = t2 * t;
            return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0
                + (t3 - 2.0f * t2 + t) * m0
                + (-2.0f * t3 + 3.0f * t2) * p1
                + (t3 - t2) * m1;
        }

        glm::vec3 hermiteDerivative(const CoasterTie& a, const CoasterTie& b, float t) {
            const float distance = std::max(1.0f, glm::distance(a.position, b.position));
            const glm::vec3 p0 = a.position;
            const glm::vec3 p1 = b.position;
            const glm::vec3 m0 = a.forward * distance;
            const glm::vec3 m1 = b.forward * distance;
            const float t2 = t * t;
            return (6.0f * t2 - 6.0f * t) * p0
                + (3.0f * t2 - 4.0f * t + 1.0f) * m0
                + (-6.0f * t2 + 6.0f * t) * p1
                + (3.0f * t2 - 2.0f * t) * m1;
        }

        EvaluatedPose evaluateSegmentPose(const CoasterTie& a, const CoasterTie& b, float t) {
            EvaluatedPose pose;
            pose.position = hermitePoint(a, b, t);
            pose.forward = safeNormalize(hermiteDerivative(a, b, t), safeNormalize(b.position - a.position, a.forward));

            glm::vec3 up = safeNormalize(glm::mix(a.normal, b.normal, glm::clamp(t, 0.0f, 1.0f)), a.normal);
            up -= pose.forward * glm::dot(up, pose.forward);
            up = safeNormalize(up, fallbackForwardForNormal(pose.forward));
            pose.right = safeNormalize(glm::cross(up, pose.forward), glm::vec3(1.0f, 0.0f, 0.0f));
            pose.up = safeNormalize(glm::cross(pose.forward, pose.right), up);
            return pose;
        }

        EvaluatedPose tiePose(const CoasterTie& tie) {
            EvaluatedPose pose;
            pose.position = tie.position;
            pose.up = safeNormalize(tie.normal, glm::vec3(0.0f, 1.0f, 0.0f));
            pose.forward = safeNormalize(tie.forward, fallbackForwardForNormal(pose.up));
            pose.right = safeNormalize(glm::cross(pose.up, pose.forward), glm::vec3(1.0f, 0.0f, 0.0f));
            pose.forward = safeNormalize(glm::cross(pose.right, pose.up), pose.forward);
            return pose;
        }

        void appendQuad(std::vector<CoasterVertex>& vertices,
                        const glm::vec3& p00,
                        const glm::vec3& p10,
                        const glm::vec3& p11,
                        const glm::vec3& p01,
                        const glm::vec3& normal,
                        float v0,
                        float v1,
                        int tileIndex) {
            const glm::vec3 n = safeNormalize(normal, glm::vec3(0.0f, 1.0f, 0.0f));
            auto emit = [&](const glm::vec3& p, float u, float v) {
                CoasterVertex vertex;
                vertex.position = p;
                vertex.normal = n;
                vertex.uv = glm::vec2(u, v);
                vertex.tileIndex = tileIndex;
                vertices.push_back(vertex);
            };
            emit(p00, 0.02f, v0);
            emit(p10, 0.02f, v1);
            emit(p11, 0.98f, v1);
            emit(p00, 0.02f, v0);
            emit(p11, 0.98f, v1);
            emit(p01, 0.98f, v0);
        }

        void appendRibbonStep(std::vector<CoasterVertex>& vertices,
                              const EvaluatedPose& a,
                              const EvaluatedPose& b,
                              float x0,
                              float x1,
                              float v0,
                              float v1,
                              int tileIndex) {
            const glm::vec3 p00 = a.position + a.right * x0;
            const glm::vec3 p01 = a.position + a.right * x1;
            const glm::vec3 p10 = b.position + b.right * x0;
            const glm::vec3 p11 = b.position + b.right * x1;
            appendQuad(vertices, p00, p10, p11, p01, glm::normalize(a.up + b.up), v0, v1, tileIndex);
        }

        void appendTieCap(std::vector<CoasterVertex>& vertices,
                          const CoasterTie& tie,
                          int leftTile,
                          int rightTile) {
            EvaluatedPose center = tiePose(tie);
            EvaluatedPose a = center;
            EvaluatedPose b = center;
            a.position += center.forward * (-kTiePreviewLength * 0.5f);
            b.position += center.forward * (kTiePreviewLength * 0.5f);
            appendRibbonStep(vertices, a, b, -kRailHalfWidth, 0.0f, 0.0f, kTiePreviewLength, leftTile);
            appendRibbonStep(vertices, a, b, 0.0f, kRailHalfWidth, 0.0f, kTiePreviewLength, rightTile);
        }

        void appendDebugTieMarker(std::vector<CoasterVertex>& vertices,
                                  const CoasterTie& tie,
                                  bool selected) {
            const EvaluatedPose pose = tiePose(tie);
            const glm::vec3 base = pose.position + pose.up * 0.07f;
            const glm::vec3 top = base + pose.up * 0.45f;
            const float halfWidth = selected ? 0.28f : 0.20f;
            const int tileIndex = selected ? kSelectedTieDebugTile : kTieDebugTile;

            appendQuad(
                vertices,
                base - pose.right * halfWidth,
                top - pose.right * halfWidth,
                top + pose.right * halfWidth,
                base + pose.right * halfWidth,
                pose.forward,
                0.0f,
                1.0f,
                tileIndex
            );
            appendQuad(
                vertices,
                base - pose.forward * halfWidth,
                top - pose.forward * halfWidth,
                top + pose.forward * halfWidth,
                base + pose.forward * halfWidth,
                pose.right,
                0.0f,
                1.0f,
                tileIndex
            );
        }

        void appendSegmentMesh(const RollerCoasterContext& coaster,
                               const CoasterSegment& segment,
                               std::vector<CoasterVertex>& vertices,
                               int leftTile,
                               int rightTile) {
            const CoasterTie* start = findTie(coaster, segment.startTieId);
            const CoasterTie* end = findTie(coaster, segment.endTieId);
            if (!start || !end) return;
            const float distance = glm::distance(start->position, end->position);
            const int steps = glm::clamp(static_cast<int>(std::ceil(distance * 4.0f)), 8, 96);
            EvaluatedPose prev = evaluateSegmentPose(*start, *end, 0.0f);
            float v = 0.0f;
            for (int i = 1; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                EvaluatedPose next = evaluateSegmentPose(*start, *end, t);
                const float nextV = v + glm::distance(prev.position, next.position);
                appendRibbonStep(vertices, prev, next, -kRailHalfWidth, 0.0f, v, nextV, leftTile);
                appendRibbonStep(vertices, prev, next, 0.0f, kRailHalfWidth, v, nextV, rightTile);
                prev = next;
                v = nextV;
            }
        }

        bool ensureRenderResources(BaseSystem& baseSystem) {
            if (!baseSystem.renderer || !baseSystem.world || !baseSystem.renderBackend) return false;
            RendererContext& renderer = *baseSystem.renderer;
            WorldContext& world = *baseSystem.world;
            IRenderBackend& renderBackend = *baseSystem.renderBackend;

            if (!renderer.coasterShader) {
                const auto vertexIt = world.shaders.find("COASTER_VERTEX_SHADER");
                const auto fragmentIt = world.shaders.find("COASTER_FRAGMENT_SHADER");
                if (vertexIt == world.shaders.end() || fragmentIt == world.shaders.end()) {
                    static bool warned = false;
                    if (!warned) {
                        warned = true;
                        std::cerr << "RollerCoasterSystem: missing coaster shader sources." << std::endl;
                    }
                    return false;
                }
                renderer.coasterShader = std::make_unique<Shader>(
                    vertexIt->second.c_str(),
                    fragmentIt->second.c_str()
                );
                if (!renderer.coasterShader->isValid()) {
                    renderer.coasterShader.reset();
                    return false;
                }
            }

            if (renderer.coasterVAO == 0) {
                static const std::vector<VertexAttribLayout> kCoasterVertexLayout = {
                    {0u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(CoasterVertex)), offsetof(CoasterVertex, position), 0u},
                    {1u, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(CoasterVertex)), offsetof(CoasterVertex, normal), 0u},
                    {2u, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(CoasterVertex)), offsetof(CoasterVertex, uv), 0u},
                    {3u, 1, VertexAttribType::Int, false, static_cast<unsigned int>(sizeof(CoasterVertex)), offsetof(CoasterVertex, tileIndex), 0u}
                };
                renderBackend.ensureVertexArray(renderer.coasterVAO);
                renderBackend.ensureArrayBuffer(renderer.coasterVBO);
                renderBackend.configureVertexArray(renderer.coasterVAO, renderer.coasterVBO, kCoasterVertexLayout, 0, {});
                renderBackend.unbindVertexArray();
            }
            return renderer.coasterShader && renderer.coasterVAO != 0 && renderer.coasterVBO != 0;
        }

        void rebuildRailMesh(BaseSystem& baseSystem) {
            if (!baseSystem.rollerCoaster || !baseSystem.renderer || !baseSystem.renderBackend) return;
            RollerCoasterContext& coaster = *baseSystem.rollerCoaster;
            RendererContext& renderer = *baseSystem.renderer;
            IRenderBackend& renderBackend = *baseSystem.renderBackend;

            const int worldIndex = activeWorldIndex(baseSystem);
            const int leftTile = registryInt(baseSystem, "RollerCoasterRailLeftTile", kDefaultLeftRailTile);
            const int rightTile = registryInt(baseSystem, "RollerCoasterRailRightTile", kDefaultRightRailTile);
            std::vector<CoasterVertex> vertices;
            vertices.reserve(coaster.segments.size() * 96u + coaster.ties.size() * 12u);

            for (const CoasterSegment& segment : coaster.segments) {
                if (worldIndex >= 0 && segment.worldIndex != worldIndex) continue;
                appendSegmentMesh(coaster, segment, vertices, leftTile, rightTile);
            }
            for (const CoasterTie& tie : coaster.ties) {
                if (worldIndex >= 0 && tie.worldIndex != worldIndex) continue;
                if (tie.prevTieId >= 0 && tie.nextTieId >= 0 && tie.id != coaster.selectedTieId) continue;
                appendTieCap(vertices, tie, leftTile, rightTile);
            }
            for (const CoasterTie& tie : coaster.ties) {
                if (worldIndex >= 0 && tie.worldIndex != worldIndex) continue;
                appendDebugTieMarker(vertices, tie, tie.id == coaster.selectedTieId);
            }

            renderer.coasterVertexCount = static_cast<int>(vertices.size());
            if (vertices.empty()) {
                renderBackend.uploadArrayBufferData(renderer.coasterVBO, nullptr, 0, true);
            } else {
                renderBackend.uploadArrayBufferData(
                    renderer.coasterVBO,
                    vertices.data(),
                    vertices.size() * sizeof(CoasterVertex),
                    true
                );
            }
            coaster.meshDirty = false;
        }

        void renderCoasters(BaseSystem& baseSystem) {
            if (!baseSystem.rollerCoaster || !baseSystem.renderer || !baseSystem.player || !baseSystem.renderBackend) return;
            RollerCoasterContext& coaster = *baseSystem.rollerCoaster;
            RendererContext& renderer = *baseSystem.renderer;
            PlayerContext& player = *baseSystem.player;
            IRenderBackend& renderBackend = *baseSystem.renderBackend;

            if (!ensureRenderResources(baseSystem)) return;
            if (coaster.meshDirty) {
                rebuildRailMesh(baseSystem);
            }
            if (renderer.coasterVertexCount <= 0 || !renderer.coasterShader) return;

            renderBackend.setDepthTestEnabled(true);
            renderBackend.setDepthWriteEnabled(true);
            renderBackend.setBlendEnabled(false);
            renderBackend.setCullEnabled(false);

            renderer.coasterShader->use();
            renderer.coasterShader->setMat4("model", glm::mat4(1.0f));
            renderer.coasterShader->setMat4("view", player.viewMatrix);
            renderer.coasterShader->setMat4("projection", player.projectionMatrix);
            PaniniProjectionSystemLogic::ApplyProjectionWarpUniforms(player, *renderer.coasterShader);
            renderer.coasterShader->setVec3("cameraPos", player.cameraPosition);
            renderer.coasterShader->setVec3("lightDir", glm::normalize(glm::vec3(-0.35f, -1.0f, -0.25f)));
            renderer.coasterShader->setVec3("ambientLight", glm::vec3(0.46f));
            renderer.coasterShader->setVec3("diffuseLight", glm::vec3(0.66f));
            renderer.coasterShader->setVec2("atlasTileSize", glm::vec2(renderer.atlasTileSize));
            renderer.coasterShader->setVec2("atlasTextureSize", glm::vec2(renderer.atlasTextureSize));
            renderer.coasterShader->setInt("atlasEnabled", (renderer.atlasTexture != 0 && renderer.atlasTilesPerRow > 0 && renderer.atlasTilesPerCol > 0) ? 1 : 0);
            renderer.coasterShader->setInt("tilesPerRow", renderer.atlasTilesPerRow);
            renderer.coasterShader->setInt("tilesPerCol", renderer.atlasTilesPerCol);
            renderer.coasterShader->setInt("atlasTexture", 0);
            if (renderer.atlasTexture != 0) {
                renderBackend.bindTexture2D(renderer.atlasTexture, 0);
            }

            renderBackend.bindVertexArray(renderer.coasterVAO);
            renderBackend.drawArraysTriangles(0, renderer.coasterVertexCount);
            renderBackend.unbindVertexArray();
            renderBackend.setCullEnabled(true);
        }
    }

    void UpdateRollerCoasters(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float, PlatformWindowHandle win) {
        if (!baseSystem.rollerCoaster || !baseSystem.player) return;
        RollerCoasterContext& coaster = *baseSystem.rollerCoaster;
        if (!coaster.initialized) {
            coaster.initialized = true;
            coaster.meshDirty = true;
            coaster.statusMessage = "N creates/connects splices, Shift+N selects without connecting, B clears selection, Shift+B clears graph.";
        }
        handlePlacementInput(baseSystem, prototypes, win);
        renderCoasters(baseSystem);
        publishStatus(baseSystem);
    }

    void CleanupRollerCoasters(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle) {
        if (!baseSystem.renderer || !baseSystem.renderBackend) return;
        RendererContext& renderer = *baseSystem.renderer;
        IRenderBackend& renderBackend = *baseSystem.renderBackend;
        renderBackend.destroyArrayBuffer(renderer.coasterVBO);
        renderBackend.destroyVertexArray(renderer.coasterVAO);
        renderer.coasterVertexCount = 0;
        renderer.coasterShader.reset();
    }
}
