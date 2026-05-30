#pragma once

#include "../Host.h"
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>

namespace DawIOSystemLogic {
    bool SaveSession(BaseSystem& baseSystem, const std::string& path);
    bool LoadSession(BaseSystem& baseSystem, const std::string& path);
}

namespace WorldSaveSystemLogic {
    namespace {
        namespace fs = std::filesystem;

        constexpr const char* kDefaultLevelKey = "the_expanse";
        constexpr const char* kDefaultDimensionId = "overworld";
        constexpr const char* kNightworldDimensionId = "nightworld";
        constexpr const char* kWorldFormat = "salamander_world";
        constexpr uint32_t kWorldFormatVersion = 1;
        constexpr char kRegionMagic[8] = {'S', 'L', 'M', 'R', 'E', 'G', '1', '\0'};
        constexpr char kColumnMagic[8] = {'S', 'L', 'M', 'C', 'O', 'L', '1', '\0'};
        constexpr uint32_t kRegionVersion = 1;
        constexpr uint32_t kColumnVersion = 1;
        constexpr int kRegionWidth = 32;
        constexpr int kRegionEntryCount = kRegionWidth * kRegionWidth;

        struct RegionEntry {
            uint64_t offset = 0;
            uint32_t length = 0;
            uint64_t timestamp = 0;
        };

        struct RegionHeader {
            std::array<RegionEntry, kRegionEntryCount> entries{};
        };

        struct ColumnRun {
            uint32_t length = 0;
            uint32_t id = 0;
            uint32_t color = 0;
        };

        int64_t nowEpochSeconds() {
            return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count());
        }

        int floorDivInt(int value, int divisor) {
            if (divisor <= 0) return 0;
            if (value >= 0) return value / divisor;
            return -(((-value) + divisor - 1) / divisor);
        }

        std::string registryString(const BaseSystem& baseSystem,
                                   const std::string& key,
                                   const std::string& fallback = "") {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<std::string>(it->second)) return fallback;
            return std::get<std::string>(it->second);
        }

        bool registryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (!std::holds_alternative<bool>(it->second)) return fallback;
            return std::get<bool>(it->second);
        }

        int registryInt(const BaseSystem& baseSystem, const std::string& key, int fallback) {
            const std::string raw = registryString(baseSystem, key, "");
            if (raw.empty()) return fallback;
            try {
                return std::stoi(raw);
            } catch (...) {
                return fallback;
            }
        }

        std::string currentLevelKey(const BaseSystem& baseSystem) {
            return registryString(baseSystem, "level", "");
        }

        bool isMenuLevel(const BaseSystem& baseSystem) {
            return currentLevelKey(baseSystem) == "menu";
        }

        void clearPendingAction(UIContext& ui) {
            ui.pendingActionType.clear();
            ui.pendingActionKey.clear();
            ui.pendingActionValue.clear();
        }

        std::string sanitizeWorldIdPart(std::string value) {
            std::string out;
            out.reserve(value.size());
            for (char c : value) {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc)) {
                    out.push_back(static_cast<char>(std::tolower(uc)));
                } else if (c == '-' || c == '_' || c == ' ') {
                    if (out.empty() || out.back() == '_') continue;
                    out.push_back('_');
                }
            }
            while (!out.empty() && out.back() == '_') out.pop_back();
            if (out.empty()) out = "world";
            return out;
        }

        std::string sanitizeDimensionIdPart(std::string value) {
            std::string out;
            out.reserve(value.size());
            for (char c : value) {
                const unsigned char uc = static_cast<unsigned char>(c);
                if (std::isalnum(uc)) {
                    out.push_back(static_cast<char>(std::tolower(uc)));
                } else if (c == '-' || c == '_') {
                    if (out.empty() || out.back() == '_') continue;
                    out.push_back('_');
                }
            }
            while (!out.empty() && out.back() == '_') out.pop_back();
            if (out.empty()) out = kDefaultDimensionId;
            return out;
        }

        std::string normalizeDimensionId(const std::string& raw) {
            const std::string sanitized = sanitizeDimensionIdPart(raw);
            return sanitized.empty() ? std::string(kDefaultDimensionId) : sanitized;
        }

        fs::path saveRootPath(const BaseSystem& baseSystem) {
            if (!baseSystem.worldSave) return fs::path("Saves");
            std::string root = baseSystem.worldSave->saveRoot.empty()
                ? registryString(baseSystem, "WorldSaveRoot", "Saves")
                : baseSystem.worldSave->saveRoot;
            if (root.empty()) root = "Saves";
            return fs::path(root);
        }

        fs::path worldPath(const BaseSystem& baseSystem, const std::string& worldId) {
            return saveRootPath(baseSystem) / worldId;
        }

        fs::path manifestPath(const BaseSystem& baseSystem, const std::string& worldId) {
            return worldPath(baseSystem, worldId) / "world.json";
        }

        fs::path catalogPath(const BaseSystem& baseSystem) {
            return saveRootPath(baseSystem) / "worlds.json";
        }

        fs::path dawSessionPath(const BaseSystem& baseSystem, const std::string& worldId) {
            return worldPath(baseSystem, worldId) / "daw" / "session.salmproj";
        }

        int regionCoordForColumn(int columnCoord) {
            return floorDivInt(columnCoord, kRegionWidth);
        }

        int localCoordInRegion(int columnCoord, int regionCoord) {
            return columnCoord - regionCoord * kRegionWidth;
        }

        int regionSlot(const VoxelColumnKey& key) {
            const int rx = regionCoordForColumn(key.coord.x);
            const int rz = regionCoordForColumn(key.coord.y);
            const int lx = localCoordInRegion(key.coord.x, rx);
            const int lz = localCoordInRegion(key.coord.y, rz);
            if (lx < 0 || lx >= kRegionWidth || lz < 0 || lz >= kRegionWidth) return -1;
            return lx + lz * kRegionWidth;
        }

        std::string currentDimensionId(const BaseSystem& baseSystem) {
            if (baseSystem.worldSave) {
                const std::string fromContext = normalizeDimensionId(baseSystem.worldSave->activeDimensionId);
                if (!fromContext.empty()) return fromContext;
            }
            return normalizeDimensionId(registryString(baseSystem, "ActiveDimensionId", kDefaultDimensionId));
        }

        fs::path dimensionPath(const BaseSystem& baseSystem,
                               const std::string& worldId,
                               const std::string& dimensionId) {
            return worldPath(baseSystem, worldId)
                / "dimensions"
                / normalizeDimensionId(dimensionId);
        }

        fs::path regionPath(const BaseSystem& baseSystem, const std::string& worldId, const VoxelColumnKey& key) {
            const int rx = regionCoordForColumn(key.coord.x);
            const int rz = regionCoordForColumn(key.coord.y);
            return dimensionPath(baseSystem, worldId, currentDimensionId(baseSystem))
                / "regions"
                / ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".slmr");
        }

        template <typename T>
        void appendPod(std::vector<uint8_t>& bytes, T value) {
            const uint8_t* raw = reinterpret_cast<const uint8_t*>(&value);
            bytes.insert(bytes.end(), raw, raw + sizeof(T));
        }

        template <typename T>
        bool readPod(const std::vector<uint8_t>& bytes, size_t& offset, T& out) {
            if (offset + sizeof(T) > bytes.size()) return false;
            std::memcpy(&out, bytes.data() + offset, sizeof(T));
            offset += sizeof(T);
            return true;
        }

        template <typename T>
        bool readStreamPod(std::istream& in, T& out) {
            return static_cast<bool>(in.read(reinterpret_cast<char*>(&out), sizeof(T)));
        }

        template <typename T>
        void writeStreamPod(std::ostream& out, const T& value) {
            out.write(reinterpret_cast<const char*>(&value), sizeof(T));
        }

        bool writeJsonAtomic(const fs::path& path, const json& data) {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            if (ec) return false;
            fs::path tmp = path;
            tmp += ".tmp";
            {
                std::ofstream out(tmp, std::ios::out | std::ios::trunc);
                if (!out.is_open()) return false;
                out << data.dump(2) << '\n';
            }
            fs::remove(path, ec);
            ec.clear();
            fs::rename(tmp, path, ec);
            if (ec) {
                fs::remove(tmp, ec);
                return false;
            }
            return true;
        }

        bool readRegionHeader(std::istream& in, RegionHeader& header) {
            char magic[8] = {};
            uint32_t version = 0;
            uint32_t entryCount = 0;
            if (!in.read(magic, sizeof(magic))) return false;
            if (std::memcmp(magic, kRegionMagic, sizeof(magic)) != 0) return false;
            if (!readStreamPod(in, version) || version != kRegionVersion) return false;
            if (!readStreamPod(in, entryCount) || entryCount != kRegionEntryCount) return false;
            for (RegionEntry& entry : header.entries) {
                if (!readStreamPod(in, entry.offset)) return false;
                if (!readStreamPod(in, entry.length)) return false;
                if (!readStreamPod(in, entry.timestamp)) return false;
            }
            return true;
        }

        void writeRegionHeader(std::ostream& out, const RegionHeader& header) {
            out.write(kRegionMagic, sizeof(kRegionMagic));
            writeStreamPod(out, kRegionVersion);
            writeStreamPod(out, static_cast<uint32_t>(kRegionEntryCount));
            for (const RegionEntry& entry : header.entries) {
                writeStreamPod(out, entry.offset);
                writeStreamPod(out, entry.length);
                writeStreamPod(out, entry.timestamp);
            }
        }

        bool ensureRegionFile(const fs::path& path, RegionHeader& header) {
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            if (ec) return false;
            if (!fs::exists(path, ec)) {
                header = RegionHeader{};
                std::ofstream out(path, std::ios::binary | std::ios::out | std::ios::trunc);
                if (!out.is_open()) return false;
                writeRegionHeader(out, header);
                return true;
            }
            std::ifstream in(path, std::ios::binary);
            if (!in.is_open()) return false;
            if (readRegionHeader(in, header)) return true;
            header = RegionHeader{};
            std::ofstream out(path, std::ios::binary | std::ios::out | std::ios::trunc);
            if (!out.is_open()) return false;
            writeRegionHeader(out, header);
            return true;
        }

        bool readRegionPayload(const fs::path& path, int slot, std::vector<uint8_t>& outBytes) {
            if (slot < 0 || slot >= kRegionEntryCount) return false;
            std::ifstream in(path, std::ios::binary);
            if (!in.is_open()) return false;
            RegionHeader header;
            if (!readRegionHeader(in, header)) return false;
            const RegionEntry& entry = header.entries[static_cast<size_t>(slot)];
            if (entry.offset == 0 || entry.length == 0) return false;
            in.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
            if (!in.good()) return false;
            outBytes.assign(entry.length, 0);
            return static_cast<bool>(in.read(reinterpret_cast<char*>(outBytes.data()), entry.length));
        }

        bool appendRegionPayload(const fs::path& path, int slot, const std::vector<uint8_t>& payload) {
            if (slot < 0 || slot >= kRegionEntryCount || payload.empty()) return false;
            RegionHeader header;
            if (!ensureRegionFile(path, header)) return false;
            std::fstream io(path, std::ios::binary | std::ios::in | std::ios::out);
            if (!io.is_open()) return false;
            io.seekp(0, std::ios::end);
            const uint64_t offset = static_cast<uint64_t>(io.tellp());
            io.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
            if (!io.good()) return false;
            RegionEntry& entry = header.entries[static_cast<size_t>(slot)];
            entry.offset = offset;
            entry.length = static_cast<uint32_t>(payload.size());
            entry.timestamp = static_cast<uint64_t>(nowEpochSeconds());
            io.seekp(0, std::ios::beg);
            writeRegionHeader(io, header);
            io.flush();
            return io.good();
        }

        size_t expectedColumnVoxelCount(int size, int minY, int maxYExclusive) {
            const int height = std::max(0, maxYExclusive - minY);
            return static_cast<size_t>(std::max(0, size))
                * static_cast<size_t>(height)
                * static_cast<size_t>(std::max(0, size));
        }

        std::vector<ColumnRun> buildRunsForColumn(const VoxelColumn* column,
                                                  int size,
                                                  int minY,
                                                  int maxYExclusive) {
            std::vector<ColumnRun> runs;
            const size_t expectedCount = expectedColumnVoxelCount(size, minY, maxYExclusive);
            if (!column || expectedCount == 0) {
                if (expectedCount > 0) {
                    runs.push_back({static_cast<uint32_t>(expectedCount), 0u, 0u});
                }
                return runs;
            }
            if (column->ids.size() != expectedCount || column->colors.size() != expectedCount) return runs;
            size_t index = 0;
            while (index < expectedCount) {
                const uint32_t id = column->ids[index];
                const uint32_t color = (id == 0u) ? 0u : column->colors[index];
                size_t next = index + 1;
                while (next < expectedCount
                       && next - index < std::numeric_limits<uint32_t>::max()
                       && column->ids[next] == id
                       && ((column->ids[next] == 0u) ? 0u : column->colors[next]) == color) {
                    ++next;
                }
                runs.push_back({static_cast<uint32_t>(next - index), id, color});
                index = next;
            }
            return runs;
        }

        std::vector<uint8_t> serializeColumnPayload(const VoxelColumnKey& key,
                                                    const VoxelColumn* column,
                                                    int size,
                                                    int minY,
                                                    int maxYExclusive) {
            std::vector<uint8_t> bytes;
            bytes.reserve(1024);
            bytes.insert(bytes.end(), kColumnMagic, kColumnMagic + sizeof(kColumnMagic));
            appendPod<uint32_t>(bytes, kColumnVersion);
            appendPod<int32_t>(bytes, key.coord.x);
            appendPod<int32_t>(bytes, key.coord.y);
            appendPod<int32_t>(bytes, size);
            appendPod<int32_t>(bytes, minY);
            appendPod<int32_t>(bytes, maxYExclusive);
            appendPod<uint32_t>(bytes, column ? column->editVersion : 0u);
            appendPod<int32_t>(bytes, column ? column->nonAirCount : 0);

            std::vector<ColumnRun> runs = buildRunsForColumn(column, size, minY, maxYExclusive);
            appendPod<uint32_t>(bytes, static_cast<uint32_t>(runs.size()));
            for (const ColumnRun& run : runs) {
                appendPod<uint32_t>(bytes, run.length);
                appendPod<uint32_t>(bytes, run.id);
                appendPod<uint32_t>(bytes, run.color);
            }
            return bytes;
        }

        bool deserializeColumnPayload(const std::vector<uint8_t>& bytes,
                                      VoxelWorldContext& voxelWorld,
                                      const VoxelColumnKey& expectedKey,
                                      bool& outSavedEmpty) {
            outSavedEmpty = false;
            size_t offset = 0;
            if (bytes.size() < sizeof(kColumnMagic)) return false;
            if (std::memcmp(bytes.data(), kColumnMagic, sizeof(kColumnMagic)) != 0) return false;
            offset += sizeof(kColumnMagic);

            uint32_t version = 0;
            int32_t columnX = 0;
            int32_t columnZ = 0;
            int32_t size = 0;
            int32_t minY = 0;
            int32_t maxYExclusive = 0;
            uint32_t editVersion = 0;
            int32_t savedNonAir = 0;
            uint32_t runCount = 0;
            if (!readPod(bytes, offset, version) || version != kColumnVersion) return false;
            if (!readPod(bytes, offset, columnX)) return false;
            if (!readPod(bytes, offset, columnZ)) return false;
            if (!readPod(bytes, offset, size)) return false;
            if (!readPod(bytes, offset, minY)) return false;
            if (!readPod(bytes, offset, maxYExclusive)) return false;
            if (!readPod(bytes, offset, editVersion)) return false;
            if (!readPod(bytes, offset, savedNonAir)) return false;
            if (!readPod(bytes, offset, runCount)) return false;

            if (columnX != expectedKey.coord.x || columnZ != expectedKey.coord.y) return false;
            if (size != voxelWorld.sectionSize
                || minY != voxelWorld.columnMinY
                || maxYExclusive != voxelWorld.columnMaxYExclusive) {
                return false;
            }

            const size_t expectedCount = expectedColumnVoxelCount(size, minY, maxYExclusive);
            if (expectedCount == 0) return false;

            VoxelColumn column;
            column.key = expectedKey;
            column.chunkSize = size;
            column.minY = minY;
            column.maxYExclusive = maxYExclusive;
            column.ids.assign(expectedCount, 0u);
            column.colors.assign(expectedCount, 0u);
            column.skyLight.assign(expectedCount, voxelWorld.defaultSkyLightLevel);
            column.blockLight.assign(expectedCount, static_cast<uint8_t>(0));
            column.editVersion = editVersion;

            size_t writeCursor = 0;
            int nonAir = 0;
            for (uint32_t i = 0; i < runCount; ++i) {
                ColumnRun run;
                if (!readPod(bytes, offset, run.length)) return false;
                if (!readPod(bytes, offset, run.id)) return false;
                if (!readPod(bytes, offset, run.color)) return false;
                if (run.length == 0) return false;
                if (writeCursor + run.length > expectedCount) return false;
                const uint32_t storedColor = run.id == 0u ? 0u : run.color;
                for (uint32_t j = 0; j < run.length; ++j) {
                    column.ids[writeCursor] = run.id;
                    column.colors[writeCursor] = storedColor;
                    if (run.id != 0u) ++nonAir;
                    ++writeCursor;
                }
            }
            if (writeCursor != expectedCount) return false;
            column.nonAirCount = nonAir;

            voxelWorld.releaseColumn(expectedKey);
            if (nonAir <= 0 || savedNonAir <= 0) {
                outSavedEmpty = true;
                voxelWorld.dirtyColumns.erase(expectedKey);
                return true;
            }

            voxelWorld.columns[expectedKey] = std::move(column);
            VoxelChunkLifecycleState& columnState = voxelWorld.ensureColumnState(expectedKey);
            columnState.hasSection = true;

            const int minSectionY = floorDivInt(voxelWorld.columnMinY, size);
            const int maxSectionY = floorDivInt(voxelWorld.columnMaxYExclusive - 1, size);
            for (int sy = minSectionY; sy <= maxSectionY; ++sy) {
                const VoxelSectionKey sectionKey{glm::ivec3(expectedKey.coord.x, sy, expectedKey.coord.y)};
                if (voxelWorld.materializeSectionFromColumn(sectionKey)) {
                    voxelWorld.markSectionDirty(sectionKey);
                }
            }
            voxelWorld.dirtyColumns.erase(expectedKey);
            return true;
        }

        json playerToJson(const PlayerContext& player) {
            return {
                {"position", {player.cameraPosition.x, player.cameraPosition.y, player.cameraPosition.z}},
                {"yaw", player.cameraYaw},
                {"pitch", player.cameraPitch}
            };
        }

        json dimensionStateToJson(const WorldSaveDimensionState& state) {
            json data = {
                {"modifiedAt", state.modifiedAt}
            };
            if (state.hasPlayerState) {
                data["player"] = {
                    {"position", {state.playerPosition.x, state.playerPosition.y, state.playerPosition.z}},
                    {"yaw", state.playerYaw},
                    {"pitch", state.playerPitch}
                };
            }
            return data;
        }

        bool readPlayerStateJson(const json& data, WorldSaveDimensionState& state) {
            if (!data.is_object()) return false;
            if (!data.contains("position") || !data["position"].is_array() || data["position"].size() < 3) {
                return false;
            }
            state.playerPosition = glm::vec3(
                data["position"][0].get<float>(),
                data["position"][1].get<float>(),
                data["position"][2].get<float>()
            );
            state.playerYaw = data.value("yaw", state.playerYaw);
            state.playerPitch = data.value("pitch", state.playerPitch);
            state.hasPlayerState = true;
            return true;
        }

        bool readDimensionStateJson(const json& data, WorldSaveDimensionState& state) {
            if (!data.is_object()) return false;
            state.modifiedAt = data.value("modifiedAt", static_cast<int64_t>(0));
            if (data.contains("player")) {
                return readPlayerStateJson(data["player"], state);
            }
            return true;
        }

        bool applyPlayerFromJson(BaseSystem& baseSystem, const json& data) {
            if (!baseSystem.player || !data.is_object()) return false;
            PlayerContext& player = *baseSystem.player;
            if (data.contains("position") && data["position"].is_array() && data["position"].size() >= 3) {
                player.cameraPosition = glm::vec3(
                    data["position"][0].get<float>(),
                    data["position"][1].get<float>(),
                    data["position"][2].get<float>()
                );
                player.prevCameraPosition = player.cameraPosition;
            } else {
                return false;
            }
            if (data.contains("yaw")) player.cameraYaw = data["yaw"].get<float>();
            if (data.contains("pitch")) player.cameraPitch = data["pitch"].get<float>();
            player.verticalVelocity = 0.0f;
            player.onGround = false;
            return true;
        }

        json buildManifestJson(BaseSystem& baseSystem, bool includeRuntimeState) {
            WorldSaveContext& ctx = *baseSystem.worldSave;
            const int64_t now = nowEpochSeconds();
            if (ctx.activeCreatedAt <= 0) ctx.activeCreatedAt = now;
            ctx.activeModifiedAt = now;
            ctx.activeDimensionId = normalizeDimensionId(ctx.activeDimensionId);
            if (includeRuntimeState && baseSystem.player) {
                WorldSaveDimensionState& state = ctx.dimensionStates[ctx.activeDimensionId];
                state.hasPlayerState = true;
                state.playerPosition = baseSystem.player->cameraPosition;
                state.playerYaw = baseSystem.player->cameraYaw;
                state.playerPitch = baseSystem.player->cameraPitch;
                state.modifiedAt = now;
            }

            json manifest = {
                {"format", kWorldFormat},
                {"version", kWorldFormatVersion},
                {"worldId", ctx.activeWorldId},
                {"displayName", ctx.activeDisplayName.empty() ? ctx.activeWorldId : ctx.activeDisplayName},
                {"levelKey", ctx.activeLevelKey.empty() ? kDefaultLevelKey : ctx.activeLevelKey},
                {"activeDimensionId", ctx.activeDimensionId},
                {"terrainSchemaVersion", ctx.terrainSchemaVersion},
                {"createdAt", ctx.activeCreatedAt},
                {"modifiedAt", ctx.activeModifiedAt},
                {"dimension", ctx.activeDimensionId},
                {"dawSession", "daw/session.salmproj"}
            };
            json dimensions = json::object();
            for (const auto& [dimensionId, state] : ctx.dimensionStates) {
                dimensions[normalizeDimensionId(dimensionId)] = dimensionStateToJson(state);
            }
            if (!dimensions.contains(ctx.activeDimensionId)) {
                dimensions[ctx.activeDimensionId] = json::object();
            }
            manifest["dimensions"] = dimensions;
            if (baseSystem.voxelWorld) {
                manifest["voxel"] = {
                    {"sectionSize", baseSystem.voxelWorld->sectionSize},
                    {"columnMinY", baseSystem.voxelWorld->columnMinY},
                    {"columnMaxYExclusive", baseSystem.voxelWorld->columnMaxYExclusive}
                };
            }
            if (includeRuntimeState && baseSystem.player) {
                manifest["player"] = playerToJson(*baseSystem.player);
            }
            if (baseSystem.registry) {
                auto it = baseSystem.registry->find("spawn_ready");
                if (it != baseSystem.registry->end() && std::holds_alternative<bool>(it->second)) {
                    manifest["spawnInitialized"] = std::get<bool>(it->second);
                }
            }
            return manifest;
        }

        bool readManifest(BaseSystem& baseSystem,
                          const std::string& worldId,
                          json& outManifest) {
            std::ifstream in(manifestPath(baseSystem, worldId));
            if (!in.is_open()) return false;
            try {
                outManifest = json::parse(in);
            } catch (...) {
                return false;
            }
            if (!outManifest.is_object()) return false;
            if (outManifest.value("format", "") != kWorldFormat) return false;
            return true;
        }

        bool applyManifestToContext(BaseSystem& baseSystem,
                                    const std::string& worldId,
                                    const json& manifest,
                                    bool applyPlayer) {
            if (!baseSystem.worldSave) return false;
            WorldSaveContext& ctx = *baseSystem.worldSave;
            ctx.activeWorldId = manifest.value("worldId", worldId);
            if (ctx.activeWorldId.empty()) ctx.activeWorldId = worldId;
            ctx.activeDisplayName = manifest.value("displayName", ctx.activeWorldId);
            ctx.activeLevelKey = manifest.value("levelKey", std::string(kDefaultLevelKey));
            ctx.activeDimensionId = normalizeDimensionId(
                manifest.value("activeDimensionId", manifest.value("dimension", std::string(kDefaultDimensionId)))
            );
            ctx.terrainSchemaVersion = manifest.value("terrainSchemaVersion", ctx.terrainSchemaVersion);
            ctx.activeCreatedAt = manifest.value("createdAt", static_cast<int64_t>(0));
            ctx.activeModifiedAt = manifest.value("modifiedAt", static_cast<int64_t>(0));
            ctx.activeWorldLoaded = true;
            ctx.pendingDawLoad = true;
            ctx.dawLoadedForActiveWorld = false;
            ctx.dimensionStates.clear();
            if (manifest.contains("dimensions") && manifest["dimensions"].is_object()) {
                for (auto it = manifest["dimensions"].begin(); it != manifest["dimensions"].end(); ++it) {
                    WorldSaveDimensionState state;
                    if (readDimensionStateJson(it.value(), state)) {
                        ctx.dimensionStates[normalizeDimensionId(it.key())] = state;
                    }
                }
            }
            if (manifest.contains("player")) {
                WorldSaveDimensionState& state = ctx.dimensionStates[ctx.activeDimensionId];
                if (readPlayerStateJson(manifest["player"], state) && state.modifiedAt <= 0) {
                    state.modifiedAt = ctx.activeModifiedAt;
                }
            }
            if (baseSystem.registry) {
                (*baseSystem.registry)["ActiveWorldId"] = ctx.activeWorldId;
                (*baseSystem.registry)["ActiveWorldDisplayName"] = ctx.activeDisplayName;
                (*baseSystem.registry)["ActiveDimensionId"] = ctx.activeDimensionId;
                (*baseSystem.registry)["TerrainSchemaVersion"] = ctx.terrainSchemaVersion;
            }
            bool appliedPlayer = false;
            auto dimStateIt = ctx.dimensionStates.find(ctx.activeDimensionId);
            if (applyPlayer
                && dimStateIt != ctx.dimensionStates.end()
                && dimStateIt->second.hasPlayerState
                && baseSystem.player) {
                PlayerContext& player = *baseSystem.player;
                player.cameraPosition = dimStateIt->second.playerPosition;
                player.prevCameraPosition = player.cameraPosition;
                player.cameraYaw = dimStateIt->second.playerYaw;
                player.cameraPitch = dimStateIt->second.playerPitch;
                player.verticalVelocity = 0.0f;
                player.onGround = false;
                appliedPlayer = true;
            } else if (applyPlayer && manifest.contains("player")) {
                appliedPlayer = applyPlayerFromJson(baseSystem, manifest["player"]);
            }
            if (appliedPlayer && baseSystem.registry) {
                (*baseSystem.registry)["spawn_ready"] = true;
            }
            return true;
        }

        WorldSaveCatalogEntry catalogEntryFromManifest(const std::string& fallbackId, const json& manifest) {
            WorldSaveCatalogEntry entry;
            entry.worldId = manifest.value("worldId", fallbackId);
            if (entry.worldId.empty()) entry.worldId = fallbackId;
            entry.displayName = manifest.value("displayName", entry.worldId);
            entry.levelKey = manifest.value("levelKey", std::string(kDefaultLevelKey));
            entry.createdAt = manifest.value("createdAt", static_cast<int64_t>(0));
            entry.modifiedAt = manifest.value("modifiedAt", static_cast<int64_t>(0));
            return entry;
        }

        void sortCatalog(std::vector<WorldSaveCatalogEntry>& catalog) {
            std::sort(catalog.begin(), catalog.end(), [](const auto& a, const auto& b) {
                if (a.modifiedAt != b.modifiedAt) return a.modifiedAt > b.modifiedAt;
                return a.worldId < b.worldId;
            });
        }

        void refreshCatalog(BaseSystem& baseSystem) {
            if (!baseSystem.worldSave) return;
            WorldSaveContext& ctx = *baseSystem.worldSave;
            ctx.catalog.clear();

            std::unordered_set<std::string> seen;
            const fs::path root = saveRootPath(baseSystem);
            std::error_code ec;
            fs::create_directories(root, ec);

            json catalogJson;
            {
                std::ifstream in(catalogPath(baseSystem));
                if (in.is_open()) {
                    try { catalogJson = json::parse(in); } catch (...) { catalogJson = json(); }
                }
            }
            if (catalogJson.is_object() && catalogJson.contains("worlds") && catalogJson["worlds"].is_array()) {
                for (const auto& item : catalogJson["worlds"]) {
                    if (!item.is_object()) continue;
                    const std::string worldId = item.value("worldId", "");
                    if (worldId.empty() || seen.count(worldId) > 0) continue;
                    json manifest;
                    if (readManifest(baseSystem, worldId, manifest)) {
                        ctx.catalog.push_back(catalogEntryFromManifest(worldId, manifest));
                        seen.insert(worldId);
                    }
                }
            }

            if (fs::exists(root, ec)) {
                for (const auto& entry : fs::directory_iterator(root, ec)) {
                    if (ec) break;
                    if (!entry.is_directory()) continue;
                    const std::string worldId = entry.path().filename().string();
                    if (worldId.empty() || seen.count(worldId) > 0) continue;
                    json manifest;
                    if (readManifest(baseSystem, worldId, manifest)) {
                        ctx.catalog.push_back(catalogEntryFromManifest(worldId, manifest));
                        seen.insert(worldId);
                    }
                }
            }
            sortCatalog(ctx.catalog);
        }

        bool writeCatalog(BaseSystem& baseSystem) {
            if (!baseSystem.worldSave) return false;
            json worlds = json::array();
            for (const WorldSaveCatalogEntry& entry : baseSystem.worldSave->catalog) {
                worlds.push_back({
                    {"worldId", entry.worldId},
                    {"displayName", entry.displayName},
                    {"levelKey", entry.levelKey},
                    {"createdAt", entry.createdAt},
                    {"modifiedAt", entry.modifiedAt}
                });
            }
            return writeJsonAtomic(catalogPath(baseSystem), {{"worlds", worlds}});
        }

        void upsertCatalogEntry(BaseSystem& baseSystem) {
            if (!baseSystem.worldSave) return;
            WorldSaveContext& ctx = *baseSystem.worldSave;
            if (ctx.activeWorldId.empty()) return;
            bool found = false;
            for (WorldSaveCatalogEntry& entry : ctx.catalog) {
                if (entry.worldId != ctx.activeWorldId) continue;
                entry.displayName = ctx.activeDisplayName.empty() ? ctx.activeWorldId : ctx.activeDisplayName;
                entry.levelKey = ctx.activeLevelKey.empty() ? kDefaultLevelKey : ctx.activeLevelKey;
                entry.createdAt = ctx.activeCreatedAt;
                entry.modifiedAt = ctx.activeModifiedAt;
                found = true;
                break;
            }
            if (!found) {
                ctx.catalog.push_back({
                    ctx.activeWorldId,
                    ctx.activeDisplayName.empty() ? ctx.activeWorldId : ctx.activeDisplayName,
                    ctx.activeLevelKey.empty() ? kDefaultLevelKey : ctx.activeLevelKey,
                    ctx.activeCreatedAt,
                    ctx.activeModifiedAt
                });
            }
            sortCatalog(ctx.catalog);
            writeCatalog(baseSystem);
        }

        bool saveActiveManifest(BaseSystem& baseSystem, bool includeRuntimeState) {
            if (!baseSystem.worldSave) return false;
            WorldSaveContext& ctx = *baseSystem.worldSave;
            if (ctx.activeWorldId.empty()) return false;
            json manifest = buildManifestJson(baseSystem, includeRuntimeState);
            const bool ok = writeJsonAtomic(manifestPath(baseSystem, ctx.activeWorldId), manifest);
            if (ok) upsertCatalogEntry(baseSystem);
            return ok;
        }

        bool createWorld(BaseSystem& baseSystem) {
            if (!baseSystem.worldSave || !baseSystem.registry) return false;
            WorldSaveContext& ctx = *baseSystem.worldSave;
            refreshCatalog(baseSystem);

            const int worldNumber = static_cast<int>(ctx.catalog.size()) + 1;
            const std::string displayName = "World " + std::to_string(worldNumber);
            const std::string baseId = sanitizeWorldIdPart(displayName) + "_" + std::to_string(nowEpochSeconds());
            std::string worldId = baseId;
            int suffix = 2;
            std::error_code ec;
            while (fs::exists(worldPath(baseSystem, worldId), ec)) {
                worldId = baseId + "_" + std::to_string(suffix++);
            }

            ctx.activeWorldId = worldId;
            ctx.activeDisplayName = displayName;
            ctx.activeLevelKey = kDefaultLevelKey;
            ctx.activeDimensionId = kDefaultDimensionId;
            ctx.terrainSchemaVersion = "terrain_schema_column_chunks_v22";
            ctx.activeCreatedAt = nowEpochSeconds();
            ctx.activeModifiedAt = ctx.activeCreatedAt;
            ctx.activeWorldLoaded = true;
            ctx.pendingDawLoad = false;
            ctx.dawLoadedForActiveWorld = false;
            ctx.autosaveAccumulator = 0.0;
            ctx.dimensionStates.clear();
            (*baseSystem.registry)["ActiveWorldId"] = ctx.activeWorldId;
            (*baseSystem.registry)["ActiveWorldDisplayName"] = ctx.activeDisplayName;
            (*baseSystem.registry)["ActiveDimensionId"] = ctx.activeDimensionId;
            (*baseSystem.registry)["TerrainSchemaVersion"] = ctx.terrainSchemaVersion;

            fs::create_directories(dimensionPath(baseSystem, worldId, kDefaultDimensionId) / "regions", ec);
            fs::create_directories(dimensionPath(baseSystem, worldId, kNightworldDimensionId) / "regions", ec);
            fs::create_directories(worldPath(baseSystem, worldId) / "daw", ec);
            return saveActiveManifest(baseSystem, false);
        }

        bool loadWorld(BaseSystem& baseSystem, const std::string& worldId) {
            if (!baseSystem.worldSave || worldId.empty()) return false;
            json manifest;
            if (!readManifest(baseSystem, worldId, manifest)) return false;
            return applyManifestToContext(baseSystem, worldId, manifest, false);
        }

        void requestLevelSwitch(BaseSystem& baseSystem, const std::string& levelKey) {
            if (!baseSystem.ui) return;
            UIContext& ui = *baseSystem.ui;
            ui.levelSwitchPending = true;
            ui.levelSwitchTarget = levelKey.empty() ? kDefaultLevelKey : levelKey;
        }

        bool loadActiveWorldFromRegistry(BaseSystem& baseSystem, bool applyPlayer) {
            if (!baseSystem.worldSave) return false;
            const std::string worldId = registryString(baseSystem, "ActiveWorldId", "");
            if (worldId.empty()) return false;
            json manifest;
            if (!readManifest(baseSystem, worldId, manifest)) return false;
            return applyManifestToContext(baseSystem, worldId, manifest, applyPlayer);
        }

        EntityInstance makeMenuInstance(BaseSystem& baseSystem,
                                        int prototypeID,
                                        const std::string& name,
                                        const glm::vec3& position,
                                        const glm::vec3& size,
                                        const std::string& controlId,
                                        const std::string& controlRole) {
            EntityInstance inst = HostLogic::CreateInstance(
                baseSystem,
                prototypeID,
                position,
                glm::vec3(1.0f)
            );
            inst.name = name;
            inst.size = size;
            inst.controlId = controlId;
            inst.controlRole = controlRole;
            return inst;
        }

        void addMenuButton(BaseSystem& baseSystem,
                           std::vector<Entity>& prototypes,
                           Entity& menuWorld,
                           const std::string& controlId,
                           const std::string& label,
                           float y,
                           const std::string& actionKey,
                           const std::string& actionValue,
                           float halfWidth = 220.0f) {
            const Entity* buttonProto = HostLogic::findPrototype("ActionButton", prototypes);
            const Entity* textProto = HostLogic::findPrototype("Text", prototypes);
            if (!buttonProto || !textProto) return;

            EntityInstance button = makeMenuInstance(
                baseSystem,
                buttonProto->prototypeID,
                "ActionButton",
                glm::vec3(960.0f, y, 0.0f),
                glm::vec3(halfWidth, 34.0f, 10.0f),
                controlId,
                "button"
            );
            button.colorName = "ButtonFront";
            button.topColorName = "ButtonTopHighlight";
            button.sideColorName = "ButtonSideShadow";
            button.actionType = "WorldMenu";
            button.actionKey = actionKey;
            button.actionValue = actionValue;
            button.buttonMode = "managed";

            EntityInstance text = makeMenuInstance(
                baseSystem,
                textProto->prototypeID,
                "Text",
                glm::vec3(960.0f, y, 0.0f),
                glm::vec3(24.0f),
                controlId,
                "label"
            );
            text.colorName = "ButtonGlyph";
            text.font = "AlegreyaSans-Regular.ttf";
            text.text = label;
            text.textType = "UIOnly";

            menuWorld.instances.push_back(std::move(button));
            menuWorld.instances.push_back(std::move(text));
        }

        void addMenuText(BaseSystem& baseSystem,
                         std::vector<Entity>& prototypes,
                         Entity& menuWorld,
                         const std::string& controlId,
                         const std::string& textValue,
                         float y,
                         float size,
                         const std::string& colorName = "White") {
            const Entity* textProto = HostLogic::findPrototype("Text", prototypes);
            if (!textProto) return;
            EntityInstance text = makeMenuInstance(
                baseSystem,
                textProto->prototypeID,
                "Text",
                glm::vec3(960.0f, y, 0.0f),
                glm::vec3(size),
                controlId,
                "label"
            );
            text.colorName = colorName;
            text.font = "AlegreyaSans-Regular.ttf";
            text.text = textValue;
            text.textType = "UIOnly";
            menuWorld.instances.push_back(std::move(text));
        }

        void rebuildMenuWorld(BaseSystem& baseSystem, std::vector<Entity>& prototypes) {
            if (!baseSystem.worldSave || !baseSystem.level) return;
            if (!isMenuLevel(baseSystem)) {
                baseSystem.worldSave->menuBuilt = false;
                return;
            }
            if (baseSystem.worldSave->menuBuilt) return;
            refreshCatalog(baseSystem);

            Entity* menuWorld = nullptr;
            for (Entity& world : baseSystem.level->worlds) {
                if (world.name == "MenuButtonWorld") {
                    menuWorld = &world;
                    break;
                }
            }
            if (!menuWorld) return;

            menuWorld->instances.erase(
                std::remove_if(menuWorld->instances.begin(), menuWorld->instances.end(), [](const EntityInstance& inst) {
                    return inst.controlId.rfind("world_menu_", 0) == 0
                        || inst.controlId == "title_new_game";
                }),
                menuWorld->instances.end()
            );

            addMenuButton(baseSystem, prototypes, *menuWorld,
                          "world_menu_create",
                          "CREATE NEW WORLD",
                          540.0f,
                          "create",
                          "");

            if (baseSystem.worldSave->catalog.empty()) {
                addMenuText(baseSystem, prototypes, *menuWorld,
                            "world_menu_empty",
                            "NO SAVED WORLDS",
                            625.0f,
                            22.0f,
                            "ButtonGlyph");
            } else {
                addMenuText(baseSystem, prototypes, *menuWorld,
                            "world_menu_load_heading",
                            "LOAD EXISTING WORLD",
                            610.0f,
                            22.0f,
                            "White");
                const int maxSlots = std::min(5, static_cast<int>(baseSystem.worldSave->catalog.size()));
                for (int i = 0; i < maxSlots; ++i) {
                    const WorldSaveCatalogEntry& entry = baseSystem.worldSave->catalog[static_cast<size_t>(i)];
                    addMenuButton(baseSystem, prototypes, *menuWorld,
                                  "world_menu_load_" + std::to_string(i),
                                  entry.displayName,
                                  660.0f + static_cast<float>(i) * 54.0f,
                                  "load",
                                  entry.worldId,
                                  260.0f);
                }
            }

            if (baseSystem.ui) {
                baseSystem.ui->buttonCacheBuilt = false;
            }
            if (baseSystem.font) {
                baseSystem.font->textCacheBuilt = false;
            }
            baseSystem.worldSave->menuBuilt = true;
        }

        void processMenuAction(BaseSystem& baseSystem) {
            if (!baseSystem.worldSave || !baseSystem.ui) return;
            UIContext& ui = *baseSystem.ui;
            if (ui.actionDelayFrames != 0 || ui.pendingActionType != "WorldMenu") return;

            const std::string action = ui.pendingActionKey;
            const std::string value = ui.pendingActionValue;
            bool ok = false;
            if (action == "create") {
                ok = createWorld(baseSystem);
                if (ok) requestLevelSwitch(baseSystem, baseSystem.worldSave->activeLevelKey);
            } else if (action == "load") {
                ok = loadWorld(baseSystem, value);
                if (ok) requestLevelSwitch(baseSystem, baseSystem.worldSave->activeLevelKey);
            }
            clearPendingAction(ui);
            if (!ok) {
                refreshCatalog(baseSystem);
                baseSystem.worldSave->menuBuilt = false;
            }
        }

        bool shouldPersistRuntimeState(const BaseSystem& baseSystem) {
            if (!baseSystem.worldSave) return false;
            if (!baseSystem.worldSave->activeWorldLoaded) return false;
            if (baseSystem.worldSave->activeWorldId.empty()) return false;
            if (isMenuLevel(baseSystem)) return false;
            return true;
        }

        bool saveDawSessionForWorld(BaseSystem& baseSystem) {
            if (!baseSystem.worldSave || baseSystem.worldSave->activeWorldId.empty()) return false;
            if (!registryBool(baseSystem, "WorldAutosaveDaw", true)) return true;
            const fs::path session = dawSessionPath(baseSystem, baseSystem.worldSave->activeWorldId);
            std::error_code ec;
            fs::create_directories(session.parent_path(), ec);
            if (ec) return false;
            return DawIOSystemLogic::SaveSession(baseSystem, session.string());
        }

        void loadDawSessionIfNeeded(BaseSystem& baseSystem) {
            if (!baseSystem.worldSave || !baseSystem.worldSave->pendingDawLoad) return;
            if (baseSystem.worldSave->activeWorldId.empty()) {
                baseSystem.worldSave->pendingDawLoad = false;
                return;
            }
            if (isMenuLevel(baseSystem)) return;
            const fs::path session = dawSessionPath(baseSystem, baseSystem.worldSave->activeWorldId);
            std::error_code ec;
            if (fs::exists(session, ec)) {
                DawIOSystemLogic::LoadSession(baseSystem, session.string());
            }
            baseSystem.worldSave->pendingDawLoad = false;
            baseSystem.worldSave->dawLoadedForActiveWorld = true;
        }

        bool saveColumnRecord(BaseSystem& baseSystem, const VoxelColumnKey& key) {
            if (!baseSystem.worldSave || !baseSystem.voxelWorld) return false;
            WorldSaveContext& ctx = *baseSystem.worldSave;
            if (ctx.activeWorldId.empty()) return false;
            VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
            const VoxelColumn* column = nullptr;
            auto columnIt = voxelWorld.columns.find(key);
            if (columnIt != voxelWorld.columns.end()) {
                column = &columnIt->second;
            }
            const int size = voxelWorld.sectionSize;
            const int minY = voxelWorld.columnMinY;
            const int maxYExclusive = voxelWorld.columnMaxYExclusive;
            const std::vector<uint8_t> payload = serializeColumnPayload(key, column, size, minY, maxYExclusive);
            const fs::path path = regionPath(baseSystem, ctx.activeWorldId, key);
            return appendRegionPayload(path, regionSlot(key), payload);
        }
    }

    void InitializeWorldSave(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle) {
        if (!baseSystem.worldSave || !baseSystem.registry) return;
        WorldSaveContext& ctx = *baseSystem.worldSave;
        ctx.saveRoot = registryString(baseSystem, "WorldSaveRoot", "Saves");
        if (ctx.saveRoot.empty()) ctx.saveRoot = "Saves";
        ctx.activeDimensionId = normalizeDimensionId(registryString(baseSystem, "ActiveDimensionId", ctx.activeDimensionId));
        (*baseSystem.registry)["ActiveDimensionId"] = ctx.activeDimensionId;
        ctx.terrainSchemaVersion = "terrain_schema_column_chunks_v22";
        ctx.menuBuilt = false;
        refreshCatalog(baseSystem);
        const bool applyPlayer = !isMenuLevel(baseSystem);
        loadActiveWorldFromRegistry(baseSystem, applyPlayer);
        ctx.initialized = true;
    }

    std::string GetActiveDimensionId(const BaseSystem& baseSystem) {
        return currentDimensionId(baseSystem);
    }

    void SetActiveDimensionId(BaseSystem& baseSystem, const std::string& dimensionId) {
        if (!baseSystem.worldSave) return;
        WorldSaveContext& ctx = *baseSystem.worldSave;
        ctx.activeDimensionId = normalizeDimensionId(dimensionId);
        ctx.dimensionStates.try_emplace(ctx.activeDimensionId);
        if (baseSystem.registry) {
            (*baseSystem.registry)["ActiveDimensionId"] = ctx.activeDimensionId;
        }
    }

    void CaptureActiveDimensionPlayerState(BaseSystem& baseSystem, const glm::vec3* overridePosition) {
        if (!baseSystem.worldSave || !baseSystem.player) return;
        WorldSaveContext& ctx = *baseSystem.worldSave;
        ctx.activeDimensionId = normalizeDimensionId(ctx.activeDimensionId);
        WorldSaveDimensionState& state = ctx.dimensionStates[ctx.activeDimensionId];
        state.hasPlayerState = true;
        state.playerPosition = overridePosition ? *overridePosition : baseSystem.player->cameraPosition;
        state.playerYaw = baseSystem.player->cameraYaw;
        state.playerPitch = baseSystem.player->cameraPitch;
        state.modifiedAt = nowEpochSeconds();
    }

    bool RestoreDimensionPlayerState(BaseSystem& baseSystem, const std::string& dimensionId) {
        if (!baseSystem.worldSave || !baseSystem.player) return false;
        WorldSaveContext& ctx = *baseSystem.worldSave;
        const std::string normalized = normalizeDimensionId(dimensionId);
        auto it = ctx.dimensionStates.find(normalized);
        if (it == ctx.dimensionStates.end() || !it->second.hasPlayerState) return false;
        PlayerContext& player = *baseSystem.player;
        player.cameraPosition = it->second.playerPosition;
        player.prevCameraPosition = player.cameraPosition;
        player.cameraYaw = it->second.playerYaw;
        player.cameraPitch = it->second.playerPitch;
        player.verticalVelocity = 0.0f;
        player.onGround = false;
        return true;
    }

    bool TryLoadSavedColumn(BaseSystem& baseSystem, const VoxelColumnKey& key) {
        if (!baseSystem.worldSave || !baseSystem.voxelWorld) return false;
        WorldSaveContext& ctx = *baseSystem.worldSave;
        if (!ctx.activeWorldLoaded || ctx.activeWorldId.empty()) return false;
        if (isMenuLevel(baseSystem)) return false;
        const fs::path path = regionPath(baseSystem, ctx.activeWorldId, key);
        std::vector<uint8_t> payload;
        if (!readRegionPayload(path, regionSlot(key), payload)) return false;
        bool savedEmpty = false;
        const bool loaded = deserializeColumnPayload(payload, *baseSystem.voxelWorld, key, savedEmpty);
        (void)savedEmpty;
        return loaded;
    }

    bool FlushColumnIfDirty(BaseSystem& baseSystem, const VoxelColumnKey& key) {
        if (!baseSystem.worldSave || !baseSystem.voxelWorld) return false;
        VoxelWorldContext& voxelWorld = *baseSystem.voxelWorld;
        if (voxelWorld.dirtyColumns.count(key) == 0) return true;
        if (!shouldPersistRuntimeState(baseSystem)) return false;
        const bool ok = saveColumnRecord(baseSystem, key);
        if (ok) voxelWorld.dirtyColumns.erase(key);
        return ok;
    }

    void FlushActiveWorld(BaseSystem& baseSystem, bool forceDawSave) {
        if (!baseSystem.worldSave || !registryBool(baseSystem, "WorldSaveEnabled", true)) return;
        if (!shouldPersistRuntimeState(baseSystem)) {
            if (baseSystem.worldSave->catalogDirty) {
                writeCatalog(baseSystem);
                baseSystem.worldSave->catalogDirty = false;
            }
            return;
        }

        if (baseSystem.voxelWorld) {
            std::vector<VoxelColumnKey> dirty;
            dirty.reserve(baseSystem.voxelWorld->dirtyColumns.size());
            for (const VoxelColumnKey& key : baseSystem.voxelWorld->dirtyColumns) {
                dirty.push_back(key);
            }
            for (const VoxelColumnKey& key : dirty) {
                FlushColumnIfDirty(baseSystem, key);
            }
        }

        saveActiveManifest(baseSystem, true);

        const bool autosaveDaw = registryBool(baseSystem, "WorldAutosaveDaw", true);
        if (forceDawSave || autosaveDaw) {
            saveDawSessionForWorld(baseSystem);
        }
    }

    void UpdateWorldSave(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle) {
        if (!baseSystem.worldSave || !registryBool(baseSystem, "WorldSaveEnabled", true)) return;
        if (!baseSystem.worldSave->initialized) {
            InitializeWorldSave(baseSystem, prototypes, 0.0f, nullptr);
        }

        processMenuAction(baseSystem);
        rebuildMenuWorld(baseSystem, prototypes);
        loadDawSessionIfNeeded(baseSystem);

        if (!shouldPersistRuntimeState(baseSystem)) return;
        const int autosaveSeconds = std::max(1, registryInt(baseSystem, "WorldAutosaveSeconds", 30));
        baseSystem.worldSave->autosaveAccumulator += static_cast<double>(std::max(0.0f, dt));
        if (baseSystem.worldSave->autosaveAccumulator >= static_cast<double>(autosaveSeconds)) {
            baseSystem.worldSave->autosaveAccumulator = 0.0;
            FlushActiveWorld(baseSystem, registryBool(baseSystem, "WorldAutosaveDaw", true));
        }
    }

    void CleanupWorldSave(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle) {
        FlushActiveWorld(baseSystem, true);
    }
}
