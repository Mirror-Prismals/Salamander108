#pragma once

#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <random>
#include <vector>
#include <iostream>
#include <string.h>
#include <thread>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

// Forward declarations
struct AudioContext; 
struct BaseSystem;
struct Entity;
struct RayTracedAudioContext;
struct PlayerContext;
struct AudioSourceState;

namespace PinkNoiseSystemLogic {

    namespace {
        constexpr const char* kFollowerWorldName = "PlayerHeadAudioVisualizerWorld";

        struct HeadSpeakerAmbientState {
            float rainTarget = 0.0f;
            float waterTarget = 0.0f;
            float lavaTarget = 0.0f;
            float rainGain = 0.0f;
            float waterGain = 0.0f;
            float lavaGain = 0.0f;
            bool scanInitialized = false;
            uint64_t lastScanFrame = 0;
            glm::vec3 lastScanPosition = glm::vec3(std::numeric_limits<float>::max());
        };

        HeadSpeakerAmbientState& headSpeakerAmbientState() {
            static HeadSpeakerAmbientState state;
            return state;
        }

        bool readRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (std::holds_alternative<std::string>(it->second)) {
                std::string value = std::get<std::string>(it->second);
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
                if (value == "0" || value == "false" || value == "no" || value == "off") return false;
            }
            return fallback;
        }

        int readRegistryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
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
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? 1 : 0;
            return fallback;
        }

        float readRegistryFloat(const BaseSystem& baseSystem, const std::string& key, float fallback) {
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
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? 1.0f : 0.0f;
            return fallback;
        }

        void writeRegistryFloat(BaseSystem& baseSystem, const std::string& key, float value) {
            if (!baseSystem.registry) return;
            (*baseSystem.registry)[key] = std::to_string(value);
        }

        bool isWaterLikePrototypeName(const std::string& name) {
            return name == "Water" || name.rfind("WaterSlope", 0) == 0;
        }

        bool isLavaLikePrototypeName(const std::string& name) {
            return name == "Lava"
                || name == "LavaBlockTex"
                || name.rfind("DepthLavaTile", 0) == 0;
        }

        bool isLeafRainBlockerName(const std::string& name) {
            return name == "Leaf"
                || name.rfind("LeafJungle", 0) == 0
                || name.find("LeafBlock") != std::string::npos;
        }

        std::vector<int> resolvePrototypeIDs(const std::vector<Entity>& prototypes,
                                             bool (*predicate)(const std::string&)) {
            std::vector<int> ids;
            ids.reserve(16);
            for (const Entity& proto : prototypes) {
                if (!proto.isBlock) continue;
                if (predicate(proto.name)) ids.push_back(proto.prototypeID);
            }
            return ids;
        }

        bool containsPrototypeID(const std::vector<int>& prototypeIDs, uint32_t id) {
            for (int prototypeID : prototypeIDs) {
                if (prototypeID >= 0 && id == static_cast<uint32_t>(prototypeID)) return true;
            }
            return false;
        }

        bool prototypeBlocksRain(const std::vector<Entity>& prototypes, uint32_t prototypeID) {
            if (prototypeID == 0u) return false;
            if (prototypeID >= prototypes.size()) return true;
            const Entity& proto = prototypes[static_cast<size_t>(prototypeID)];
            if (!proto.isBlock) return false;
            if (isWaterLikePrototypeName(proto.name)) return false;
            return proto.isSolid || proto.isOpaque || proto.isOccluder || isLeafRainBlockerName(proto.name);
        }

        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        float rainColumnBlockerTopY(const BaseSystem& baseSystem,
                                    const std::vector<Entity>& prototypes,
                                    int x,
                                    int z) {
            if (!baseSystem.voxelWorld) return -100000.0f;
            const VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const int size = std::max(1, voxelWorld.sectionSize);
            const VoxelColumnKey columnKey{glm::ivec2(floorDivInt(x, size), floorDivInt(z, size))};
            const auto columnIt = voxelWorld.columns.find(columnKey);
            if (columnIt == voxelWorld.columns.end()) return -100000.0f;

            const VoxelColumn& column = columnIt->second;
            const int height = std::max(0, column.maxYExclusive - column.minY);
            if (column.chunkSize <= 0 || height <= 0 || column.ids.empty()) return -100000.0f;

            const int localX = x - columnKey.coord.x * size;
            const int localZ = z - columnKey.coord.y * size;
            if (localX < 0 || localX >= column.chunkSize || localZ < 0 || localZ >= column.chunkSize) {
                return -100000.0f;
            }

            const int scanMinY = std::max(voxelWorld.columnMinY, column.minY);
            const int scanMaxY = std::min(voxelWorld.columnMaxYExclusive, column.maxYExclusive) - 1;
            if (scanMaxY < scanMinY) return -100000.0f;

            int idx = localX
                + (scanMaxY - column.minY) * column.chunkSize
                + localZ * column.chunkSize * height;
            for (int y = scanMaxY; y >= scanMinY; --y, idx -= column.chunkSize) {
                if (idx < 0 || idx >= static_cast<int>(column.ids.size())) continue;
                const uint32_t id = column.ids[static_cast<size_t>(idx)];
                if (prototypeBlocksRain(prototypes, id)) return static_cast<float>(y + 1);
            }
            return -100000.0f;
        }

        float rainExposureAtPlayer(const BaseSystem& baseSystem,
                                   const std::vector<Entity>& prototypes,
                                   const glm::vec3& cameraPosition) {
            if (!baseSystem.voxelWorld || !baseSystem.voxelWorld->enabled) return 1.0f;
            const int radius = std::clamp(readRegistryInt(baseSystem, "HeadSpeakerRainExposureRadius", 1), 0, 4);
            const float y = cameraPosition.y + readRegistryFloat(baseSystem, "HeadSpeakerRainExposureYOffset", 0.0f);
            int openSamples = 0;
            int totalSamples = 0;
            const int cx = static_cast<int>(std::floor(cameraPosition.x));
            const int cz = static_cast<int>(std::floor(cameraPosition.z));
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (radius > 0 && dx * dx + dz * dz > radius * radius) continue;
                    const float blockerTop = rainColumnBlockerTopY(baseSystem, prototypes, cx + dx, cz + dz);
                    if (y >= blockerTop - 0.02f) ++openSamples;
                    ++totalSamples;
                }
            }
            if (totalSamples <= 0) return 1.0f;
            return glm::clamp(static_cast<float>(openSamples) / static_cast<float>(totalSamples), 0.0f, 1.0f);
        }

        float smoothstep01(float t) {
            t = glm::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float proximityGainForPrototypes(const VoxelWorldContext& voxelWorld,
                                         const std::vector<int>& prototypeIDs,
                                         const glm::vec3& cameraPosition,
                                         int radius,
                                         int verticalRange,
                                         float fullGainDistance) {
            if (!voxelWorld.enabled || prototypeIDs.empty() || radius <= 0) return 0.0f;

            const int cx = static_cast<int>(std::floor(cameraPosition.x));
            const int cy = static_cast<int>(std::floor(cameraPosition.y));
            const int cz = static_cast<int>(std::floor(cameraPosition.z));
            const int radiusSq = radius * radius;
            float bestDistSq = std::numeric_limits<float>::max();

            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx * dx + dz * dz > radiusSq) continue;
                    for (int dy = -verticalRange; dy <= verticalRange; ++dy) {
                        const glm::ivec3 cell(cx + dx, cy + dy, cz + dz);
                        if (!containsPrototypeID(prototypeIDs, voxelWorld.getBlockWorld(cell))) continue;
                        const glm::vec3 cellCenter(
                            static_cast<float>(cell.x) + 0.5f,
                            static_cast<float>(cell.y) + 0.5f,
                            static_cast<float>(cell.z) + 0.5f
                        );
                        const glm::vec3 delta = cellCenter - cameraPosition;
                        bestDistSq = std::min(bestDistSq, glm::dot(delta, delta));
                    }
                }
            }

            if (bestDistSq == std::numeric_limits<float>::max()) return 0.0f;
            const float dist = std::sqrt(bestDistSq);
            const float audibleRadius = std::max(0.1f, static_cast<float>(radius));
            const float fullDistance = glm::clamp(fullGainDistance, 0.0f, audibleRadius - 0.01f);
            if (dist <= fullDistance) return 1.0f;
            const float t = (dist - fullDistance) / std::max(0.01f, audibleRadius - fullDistance);
            return 1.0f - smoothstep01(t);
        }

        void publishHeadSpeakerAmbientGains(AudioContext& audio,
                                            float rainGain,
                                            float waterGain,
                                            float lavaGain) {
            audio.headRainAmbientGain.store(rainGain, std::memory_order_relaxed);
            audio.headWaterAmbientGain.store(waterGain, std::memory_order_relaxed);
            audio.headLavaAmbientGain.store(lavaGain, std::memory_order_relaxed);
        }

        void updateHeadSpeakerEnvironmentalAudio(BaseSystem& baseSystem,
                                                 const std::vector<Entity>& prototypes,
                                                 float dt) {
            if (!baseSystem.audio) return;

            HeadSpeakerAmbientState& state = headSpeakerAmbientState();
            const bool enabled = readRegistryBool(baseSystem, "HeadSpeakerEnvironmentalAudioEnabled", true);
            const glm::vec3 cameraPosition = baseSystem.player ? baseSystem.player->cameraPosition : glm::vec3(0.0f);

            const int scanIntervalFrames = std::clamp(
                readRegistryInt(baseSystem, "HeadSpeakerEnvironmentalScanIntervalFrames", 8),
                1,
                120
            );
            const float rescanDistance = std::max(
                0.1f,
                readRegistryFloat(baseSystem, "HeadSpeakerEnvironmentalRescanDistance", 1.0f)
            );
            const bool movedEnough = glm::dot(cameraPosition - state.lastScanPosition,
                                              cameraPosition - state.lastScanPosition)
                >= rescanDistance * rescanDistance;
            const bool scanDue = !state.scanInitialized
                || baseSystem.frameIndex - state.lastScanFrame >= static_cast<uint64_t>(scanIntervalFrames)
                || movedEnough;

            if (!enabled || !baseSystem.player) {
                state.rainTarget = 0.0f;
                state.waterTarget = 0.0f;
                state.lavaTarget = 0.0f;
            } else if (scanDue) {
                const bool rainSystemEnabled = readRegistryBool(baseSystem, "RainSystemEnabled", true);
                const bool raining = rainSystemEnabled
                    && (readRegistryBool(baseSystem, "WeatherRaining", false)
                        || readRegistryBool(baseSystem, "RainForceEnabled", false)
                        || readRegistryBool(baseSystem, "WeatherForceRaining", false));
                float rainIntensity = readRegistryFloat(baseSystem, "RainVisualIntensity", -1.0f);
                if (rainIntensity < 0.0f && raining) {
                    rainIntensity = readRegistryFloat(baseSystem, "RainIntensity", 1.0f);
                }
                const float rainExposure = raining
                    ? rainExposureAtPlayer(baseSystem, prototypes, cameraPosition)
                    : 0.0f;
                state.rainTarget = raining
                    ? glm::clamp(rainIntensity, 0.0f, 1.0f) * rainExposure
                    : 0.0f;

                if (baseSystem.voxelWorld) {
                    const std::vector<int> waterIDs = resolvePrototypeIDs(prototypes, isWaterLikePrototypeName);
                    const std::vector<int> lavaIDs = resolvePrototypeIDs(prototypes, isLavaLikePrototypeName);
                    state.waterTarget = proximityGainForPrototypes(
                        *baseSystem.voxelWorld,
                        waterIDs,
                        cameraPosition,
                        std::clamp(readRegistryInt(baseSystem, "HeadSpeakerWaterAudioRadius", 18), 1, 64),
                        std::clamp(readRegistryInt(baseSystem, "HeadSpeakerWaterAudioVerticalRange", 8), 0, 64),
                        readRegistryFloat(baseSystem, "HeadSpeakerWaterAudioFullGainDistance", 3.0f)
                    );
                    state.lavaTarget = proximityGainForPrototypes(
                        *baseSystem.voxelWorld,
                        lavaIDs,
                        cameraPosition,
                        std::clamp(readRegistryInt(baseSystem, "HeadSpeakerLavaAudioRadius", 16), 1, 64),
                        std::clamp(readRegistryInt(baseSystem, "HeadSpeakerLavaAudioVerticalRange", 8), 0, 64),
                        readRegistryFloat(baseSystem, "HeadSpeakerLavaAudioFullGainDistance", 4.0f)
                    );
                } else {
                    state.waterTarget = 0.0f;
                    state.lavaTarget = 0.0f;
                }

                state.lastScanFrame = baseSystem.frameIndex;
                state.lastScanPosition = cameraPosition;
                state.scanInitialized = true;
            }

            const float dtSeconds = (std::isfinite(dt) && dt > 0.0f) ? dt : 1.0f / 60.0f;
            const float fadeRate = std::max(0.01f, readRegistryFloat(baseSystem, "HeadSpeakerEnvironmentalFadeRate", 1.8f));
            const float blend = glm::clamp(dtSeconds * fadeRate, 0.0f, 1.0f);
            state.rainGain += (state.rainTarget - state.rainGain) * blend;
            state.waterGain += (state.waterTarget - state.waterGain) * blend;
            state.lavaGain += (state.lavaTarget - state.lavaGain) * blend;

            const float rainLevel = glm::clamp(
                readRegistryFloat(baseSystem, "HeadSpeakerRainAudioLevel", 0.85f),
                0.0f,
                4.0f
            );
            const float waterLevel = glm::clamp(
                readRegistryFloat(baseSystem, "HeadSpeakerWaterAudioLevel", 0.80f),
                0.0f,
                4.0f
            );
            const float lavaLevel = glm::clamp(
                readRegistryFloat(baseSystem, "HeadSpeakerLavaAudioLevel", 0.85f),
                0.0f,
                4.0f
            );

            const float rainGain = glm::clamp(state.rainGain * rainLevel, 0.0f, 1.0f);
            const float waterGain = glm::clamp(state.waterGain * waterLevel, 0.0f, 1.0f);
            const float lavaGain = glm::clamp(state.lavaGain * lavaLevel, 0.0f, 1.0f);
            publishHeadSpeakerAmbientGains(*baseSystem.audio, rainGain, waterGain, lavaGain);

            writeRegistryFloat(baseSystem, "HeadSpeakerRainGain", rainGain);
            writeRegistryFloat(baseSystem, "HeadSpeakerWaterGain", waterGain);
            writeRegistryFloat(baseSystem, "HeadSpeakerLavaGain", lavaGain);
        }
    }

    // These values are consumed by the ChucK noise script via globals or gain.
    float alpha = 1.0f;
    float distance_gain = 1.0f;
    // use AudioContext::chuckNoiseChannel for channel index

    void ProcessPinkNoiseAudicle(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.audio || !baseSystem.world || !baseSystem.rayTracedAudio || !baseSystem.level || baseSystem.level->worlds.empty()) return;

        AudioContext& audio = *baseSystem.audio;
        RayTracedAudioContext& rtAudio = *baseSystem.rayTracedAudio;

        int visualizerProtoID = -1;
        for (auto& proto : prototypes) {
            if (proto.name == "AudioVisualizer") {
                visualizerProtoID = proto.prototypeID;
                break;
            }
        }

        if (visualizerProtoID == -1) {
            std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
            audio.active_generators = 0;
            audio.rayTestActive = false;
            audio.headRayActive = false;
            return;
        }

        LevelContext& level = *baseSystem.level;
        EntityInstance* blockEmitter = nullptr;
        EntityInstance* headEmitter = nullptr;
        EntityInstance* sparkleEmitter = nullptr;
        float bestBlockDistSq = std::numeric_limits<float>::max();
        float bestHeadDistSq = std::numeric_limits<float>::max();
        glm::vec3 listenerPos = baseSystem.player ? baseSystem.player->cameraPosition : glm::vec3(0.0f);
        const bool preferSparkleEmitter = audio.sparkleRayEnabled && audio.sparkleRayEmitterInstanceID > 0;

        for (size_t wi = 0; wi < level.worlds.size(); ++wi) {
            auto& world = level.worlds[wi];
            bool isHeadFollowerWorld = (world.name == kFollowerWorldName);
            for (auto& instance : world.instances) {
                if (instance.prototypeID != visualizerProtoID) continue;
                glm::vec3 delta = instance.position - listenerPos;
                float distSq = glm::dot(delta, delta);
                if (isHeadFollowerWorld) {
                    if (distSq < bestHeadDistSq) {
                        bestHeadDistSq = distSq;
                        headEmitter = &instance;
                    }
                } else {
                    if (preferSparkleEmitter
                        && instance.instanceID == audio.sparkleRayEmitterInstanceID
                        && (audio.sparkleRayEmitterWorldIndex < 0
                            || audio.sparkleRayEmitterWorldIndex == static_cast<int>(wi))) {
                        sparkleEmitter = &instance;
                    }
                    if (distSq < bestBlockDistSq) {
                        bestBlockDistSq = distSq;
                        blockEmitter = &instance;
                    }
                }
            }
        }

        if (preferSparkleEmitter) {
            blockEmitter = sparkleEmitter;
        }

        bool blockFound = (blockEmitter != nullptr);
        bool headFound = (headEmitter != nullptr);

        AudioSourceState blockState{};
        blockState.isOccluded = false;
        blockState.distanceGain = 1.0f;
        AudioSourceState headState{};
        headState.isOccluded = false;
        headState.distanceGain = 1.0f;
        AudioSourceState micState{};
        bool micStateFound = false;

        if (blockEmitter) {
            if (rtAudio.sourceStates.count(blockEmitter->instanceID)) {
                blockState = rtAudio.sourceStates[blockEmitter->instanceID];
            }
            if (rtAudio.micSourceStates.count(blockEmitter->instanceID)) {
                micState = rtAudio.micSourceStates[blockEmitter->instanceID];
                micStateFound = true;
            }
        }
        if (headEmitter) {
            if (rtAudio.sourceStates.count(headEmitter->instanceID)) {
                headState = rtAudio.sourceStates[headEmitter->instanceID];
            }
        }

        AudioSourceState primaryState = blockFound ? blockState : headState;
        if (!blockFound && !headFound) {
            primaryState = AudioSourceState{};
            primaryState.isOccluded = false;
            primaryState.distanceGain = 1.0f;
        }
        alpha = primaryState.isOccluded ? 0.15f : 1.0f;
        distance_gain = primaryState.distanceGain;
        updateHeadSpeakerEnvironmentalAudio(baseSystem, prototypes, dt);

        if (blockEmitter || headEmitter) {
            float peak_amplitude = 0.0f;
            float sample;
            if (audio.ring_buffer) {
                while(jack_ringbuffer_read_space(audio.ring_buffer) >= sizeof(float)) {
                    jack_ringbuffer_read(audio.ring_buffer, (char*)&sample, sizeof(float));
                    peak_amplitude = std::max(peak_amplitude, std::fabs(sample));
                }
            }

            glm::vec3 magenta = baseSystem.world->colorLibrary["Magenta"];
            glm::vec3 white = baseSystem.world->colorLibrary["White"];
            float clamped_amplitude = std::min(1.0f, peak_amplitude / audio.output_gain * 4.0f); // Normalize by gain for better sensitivity
            glm::vec3 animatedColor = glm::mix(magenta, white, clamped_amplitude);
            if (blockEmitter) {
                blockEmitter->color = animatedColor;
            }
            if (headEmitter) {
                headEmitter->color = animatedColor;
            }
        }
        
        std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
        static bool prevBlockActive = false;
        static bool prevHeadActive = false;
        static bool prevMicActive = false;
        audio.active_generators = 0;
        // Disable ChucK noise shred for the pink noise block.
        audio.chuckNoiseShouldRun = false;
        // Speaker block RT WAV path (non-follower visualizer).
        if (blockFound && !prevBlockActive) {
            audio.rayTestPos = 0.0;
        }
        audio.rayTestActive = blockFound;
        audio.rayTestGain = blockFound ? blockState.distanceGain * (blockState.isOccluded ? 0.2f : 1.0f) : 0.0f;
        audio.rayTestPan = blockFound ? blockState.pan : 0.0f;
        if (!blockFound) {
            audio.rayTestHfState = 0.0f;
        }
        audio.rayHfChannel = -1;
        audio.rayHfAlpha = blockFound ? blockState.hfAlpha : 0.0f;
        audio.rayItdChannel = -1;
        audio.rayEchoChannel = -1;
        audio.rayEchoDelaySeconds = blockFound ? blockState.echoDelaySeconds : 0.0f;
        audio.rayEchoGain = blockFound ? blockState.echoGain : 0.0f;

        // Player-head RT path fed from head.ck and environmental ChucK outputs.
        bool headShredsActive = audio.chuckHeadHasActiveShreds.load(std::memory_order_relaxed);
        bool headAmbientShredsActive =
            audio.chuckHeadRainHasActiveShreds.load(std::memory_order_relaxed)
            || audio.chuckHeadWaterHasActiveShreds.load(std::memory_order_relaxed)
            || audio.chuckHeadLavaHasActiveShreds.load(std::memory_order_relaxed);
        bool headActive = headFound && (headShredsActive || headAmbientShredsActive);
        if (headActive && !prevHeadActive) {
            if (!audio.headRayEchoBuffer.empty()) {
                std::fill(audio.headRayEchoBuffer.begin(), audio.headRayEchoBuffer.end(), 0.0f);
            }
            if (!audio.headRayItdBuffer.empty()) {
                std::fill(audio.headRayItdBuffer.begin(), audio.headRayItdBuffer.end(), 0.0f);
            }
            audio.headRayEchoWriteIndex = 0;
            audio.headRayItdWriteIndex = 0;
            audio.headRayHfState = 0.0f;
        }
        audio.headRayActive = headActive;
        audio.headRayGain = headActive ? headState.distanceGain * (headState.isOccluded ? 0.2f : 1.0f) : 0.0f;
        audio.headRayPan = headActive ? headState.pan : 0.0f;
        audio.headRayHfAlpha = headActive ? headState.hfAlpha : 0.0f;
        audio.headRayEchoDelaySeconds = headActive ? headState.echoDelaySeconds : 0.0f;
        audio.headRayEchoGain = headActive ? headState.echoGain : 0.0f;
        if (!headActive) {
            audio.headRayHfState = 0.0f;
        }
        audio.active_generators = (blockFound ? 1 : 0) + (headActive ? 1 : 0);
        prevHeadActive = headActive;
        prevBlockActive = blockFound;

        bool micActive = blockFound && rtAudio.micCaptureActive && micStateFound;
        audio.micRayActive = micActive;
        audio.micRayGain = micActive ? micState.distanceGain : 0.0f;
        audio.micRayHfAlpha = micActive ? micState.hfAlpha : 0.0f;
        audio.micRayEchoDelaySeconds = micActive ? micState.echoDelaySeconds : 0.0f;
        audio.micRayEchoGain = micActive ? micState.echoGain : 0.0f;
        if (!micActive) {
            audio.micRayHfState = 0.0f;
        }
        if (micActive && !prevMicActive) {
            if (!audio.micRayEchoBuffer.empty()) {
                std::fill(audio.micRayEchoBuffer.begin(), audio.micRayEchoBuffer.end(), 0.0f);
            }
            audio.micRayEchoWriteIndex = 0;
        }
        prevMicActive = micActive;
    }
}
