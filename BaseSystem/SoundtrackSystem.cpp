#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <opus/opusfile.h>

namespace SoundtrackSystemLogic {

    namespace {
        struct WavInfo {
            uint16_t audioFormat = 0;
            uint16_t numChannels = 0;
            uint32_t sampleRate = 0;
            uint16_t bitsPerSample = 0;
            uint32_t dataSize = 0;
            std::streampos dataPos = 0;
        };

        struct SoundtrackHeuristic {
            std::string biome = "unknown";
            bool day = true;
            bool underground = false;
            bool underwater = false;
            std::vector<std::string> folders;
        };

        bool readChunkHeader(std::ifstream& file, char outId[4], uint32_t& outSize) {
            if (!file.read(outId, 4)) return false;
            if (!file.read(reinterpret_cast<char*>(&outSize), sizeof(outSize))) return false;
            return true;
        }

        bool readWavInfo(std::ifstream& file, WavInfo& info) {
            char riff[4] = {0};
            if (!file.read(riff, 4)) return false;
            uint32_t riffSize = 0;
            if (!file.read(reinterpret_cast<char*>(&riffSize), sizeof(riffSize))) return false;
            char wave[4] = {0};
            if (!file.read(wave, 4)) return false;
            if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") return false;

            bool fmtFound = false;
            bool dataFound = false;
            while (file && (!fmtFound || !dataFound)) {
                char chunkId[4] = {0};
                uint32_t chunkSize = 0;
                if (!readChunkHeader(file, chunkId, chunkSize)) break;
                std::string id(chunkId, 4);
                if (id == "fmt ") {
                    fmtFound = true;
                    uint16_t audioFormat = 0;
                    uint16_t numChannels = 0;
                    uint32_t sampleRate = 0;
                    uint32_t byteRate = 0;
                    uint16_t blockAlign = 0;
                    uint16_t bitsPerSample = 0;
                    file.read(reinterpret_cast<char*>(&audioFormat), sizeof(audioFormat));
                    file.read(reinterpret_cast<char*>(&numChannels), sizeof(numChannels));
                    file.read(reinterpret_cast<char*>(&sampleRate), sizeof(sampleRate));
                    file.read(reinterpret_cast<char*>(&byteRate), sizeof(byteRate));
                    file.read(reinterpret_cast<char*>(&blockAlign), sizeof(blockAlign));
                    file.read(reinterpret_cast<char*>(&bitsPerSample), sizeof(bitsPerSample));
                    if (chunkSize > 16) {
                        file.seekg(chunkSize - 16, std::ios::cur);
                    }
                    info.audioFormat = audioFormat;
                    info.numChannels = numChannels;
                    info.sampleRate = sampleRate;
                    info.bitsPerSample = bitsPerSample;
                } else if (id == "data") {
                    dataFound = true;
                    info.dataSize = chunkSize;
                    info.dataPos = file.tellg();
                    file.seekg(chunkSize, std::ios::cur);
                } else {
                    file.seekg(chunkSize, std::ios::cur);
                }
            }
            return fmtFound && dataFound;
        }

        bool loadWavMono(const std::string& path, std::vector<float>& outSamples, uint32_t& outRate) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) return false;
            WavInfo info;
            if (!readWavInfo(file, info)) return false;
            if (info.dataSize == 0 || info.numChannels == 0) return false;

            outRate = info.sampleRate;
            file.seekg(info.dataPos);

            if (info.audioFormat == 3 && info.bitsPerSample == 32) {
                size_t frameCount = info.dataSize / (sizeof(float) * info.numChannels);
                outSamples.assign(frameCount, 0.0f);
                for (size_t i = 0; i < frameCount; ++i) {
                    float sample = 0.0f;
                    for (uint16_t ch = 0; ch < info.numChannels; ++ch) {
                        float v = 0.0f;
                        file.read(reinterpret_cast<char*>(&v), sizeof(float));
                        sample += v;
                    }
                    outSamples[i] = sample / static_cast<float>(info.numChannels);
                }
                return true;
            }

            if (info.audioFormat == 1 && info.bitsPerSample == 16) {
                size_t frameCount = info.dataSize / (sizeof(int16_t) * info.numChannels);
                outSamples.assign(frameCount, 0.0f);
                for (size_t i = 0; i < frameCount; ++i) {
                    int32_t sum = 0;
                    for (uint16_t ch = 0; ch < info.numChannels; ++ch) {
                        int16_t v = 0;
                        file.read(reinterpret_cast<char*>(&v), sizeof(int16_t));
                        sum += v;
                    }
                    outSamples[i] = static_cast<float>(sum) / (static_cast<float>(info.numChannels) * 32768.0f);
                }
                return true;
            }

            return false;
        }

        bool loadOpusStereo(const std::string& path, std::vector<float>& outSamples, uint32_t& outRate, int& outChannels) {
            int error = 0;
            OggOpusFile* file = op_open_file(path.c_str(), &error);
            if (!file) return false;

            const OpusHead* head = op_head(file, -1);
            if (!head || head->channel_count <= 0) {
                op_free(file);
                return false;
            }

            constexpr int kOpusRate = 48000;
            constexpr int kMaxFrames = 120 * kOpusRate / 1000;
            constexpr int kChannels = 2;
            std::vector<float> interleaved(static_cast<size_t>(kMaxFrames) * static_cast<size_t>(kChannels));
            outSamples.clear();
            outRate = kOpusRate;
            outChannels = kChannels;

            while (true) {
                int frames = op_read_float_stereo(file, interleaved.data(), static_cast<int>(interleaved.size()));
                if (frames == 0) break;
                if (frames < 0) {
                    op_free(file);
                    outSamples.clear();
                    outRate = 0;
                    outChannels = 0;
                    return false;
                }
                const size_t count = static_cast<size_t>(frames) * static_cast<size_t>(kChannels);
                outSamples.insert(outSamples.end(), interleaved.begin(), interleaved.begin() + static_cast<std::ptrdiff_t>(count));
            }

            op_free(file);
            return !outSamples.empty();
        }

        std::string toLower(std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return s;
        }

        bool parseBool(const std::string& s, bool fallback) {
            std::string v = toLower(s);
            if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
            if (v == "0" || v == "false" || v == "no" || v == "off") return false;
            return fallback;
        }

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            if (std::holds_alternative<std::string>(it->second)) return parseBool(std::get<std::string>(it->second), fallback);
            return fallback;
        }

        std::string getRegistryString(const BaseSystem& baseSystem, const std::string& key, const std::string& fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<std::string>(it->second)) return std::get<std::string>(it->second);
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? "true" : "false";
            return fallback;
        }

        double getRegistryDouble(const BaseSystem& baseSystem, const std::string& key, double fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<std::string>(it->second)) {
                try {
                    return std::stod(std::get<std::string>(it->second));
                } catch (...) {
                    return fallback;
                }
            }
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? 1.0 : 0.0;
            return fallback;
        }

        double randomRange(std::mt19937& rng, double minValue, double maxValue) {
            if (maxValue <= minValue) return minValue;
            std::uniform_real_distribution<double> dist(minValue, maxValue);
            return dist(rng);
        }

        bool hasOpusExtension(const std::filesystem::path& path) {
            return toLower(path.extension().string()) == ".opus";
        }

        std::string formatTimer(double seconds) {
            if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
            int total = static_cast<int>(std::ceil(seconds));
            int minutes = total / 60;
            int secs = total % 60;
            std::ostringstream ss;
            ss << minutes << ":" << std::setw(2) << std::setfill('0') << secs;
            return ss.str();
        }

        std::string joinStrings(const std::vector<std::string>& values, const std::string& sep) {
            std::string out;
            for (size_t i = 0; i < values.size(); ++i) {
                if (i > 0) out += sep;
                out += values[i];
            }
            return out;
        }

        bool isDayFromLocalClock() {
            std::time_t now = std::time(nullptr);
            std::tm lt{};
#if defined(_WIN32)
            localtime_s(&lt, &now);
#else
            localtime_r(&now, &lt);
#endif
            return lt.tm_hour >= 6 && lt.tm_hour < 18;
        }

        bool isDayFromRegistry(const BaseSystem& baseSystem) {
            const std::string skyLevelText = getRegistryString(baseSystem, "VoxelLightingCurrentSkyLevel", "");
            if (!skyLevelText.empty()) {
                try {
                    return std::stoi(skyLevelText) >= 8;
                } catch (...) {
                }
            }
            return isDayFromLocalClock();
        }

        std::string biomeNameFromId(int biomeId) {
            switch (biomeId) {
                case 0: return "conifer";
                case 1: return "meadow";
                case 2: return "desert";
                case 3: return "jungle";
                case 4: return "winter";
                case 5: return "grass";
                default: return "unknown";
            }
        }

        SoundtrackHeuristic buildHeuristic(const BaseSystem& baseSystem) {
            SoundtrackHeuristic h;
            h.day = isDayFromRegistry(baseSystem);
            const std::string phase = h.day ? "day" : "night";

            if (baseSystem.player) {
                const glm::vec3& p = baseSystem.player->cameraPosition;
                h.underground = p.y <= 74.0f;
                if (baseSystem.world && baseSystem.world->expanse.loaded) {
                    h.biome = biomeNameFromId(ExpanseBiomeSystemLogic::ResolveBiome(*baseSystem.world, p.x, p.z));
                    if (p.y <= baseSystem.world->expanse.waterSurface) {
                        h.underwater = true;
                    }
                }
            }

            if (baseSystem.colorEmotion && baseSystem.colorEmotion->underwater) {
                h.underwater = true;
            }

            h.folders.push_back("all_day_night");
            h.folders.push_back(std::string("all_") + phase);
            if (h.biome != "unknown") {
                h.folders.push_back(h.biome + "_day_night");
                h.folders.push_back(h.biome + "_" + phase);
            }
            if (h.underground) h.folders.push_back("underground");
            if (h.underwater) h.folders.push_back("underwater");
            return h;
        }

        void scanEligibleTracks(const std::filesystem::path& root,
                                const std::vector<std::string>& folders,
                                std::vector<std::string>& outTracks,
                                std::string& lastScanError) {
            outTracks.clear();
            namespace fs = std::filesystem;
            std::error_code ec;
            if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
                const std::string key = "missing:" + root.string();
                if (lastScanError != key) {
                    std::cerr << "SoundtrackSystem: soundtrack folder missing '" << root.string() << "'." << std::endl;
                    lastScanError = key;
                }
                return;
            }

            for (const std::string& folder : folders) {
                const fs::path dir = root / folder;
                ec.clear();
                if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) continue;
                fs::directory_iterator it(dir, ec);
                fs::directory_iterator end;
                for (; !ec && it != end; it.increment(ec)) {
                    const fs::directory_entry& entry = *it;
                    if (!entry.is_regular_file(ec)) continue;
                    if (hasOpusExtension(entry.path())) outTracks.push_back(entry.path().string());
                }
                if (ec) {
                    const std::string key = "scan:" + dir.string() + ":" + ec.message();
                    if (lastScanError != key) {
                        std::cerr << "SoundtrackSystem: failed scanning '" << dir.string()
                                  << "' (" << ec.message() << ")." << std::endl;
                        lastScanError = key;
                    }
                    outTracks.clear();
                    return;
                }
            }

            std::sort(outTracks.begin(), outTracks.end());
            outTracks.erase(std::unique(outTracks.begin(), outTracks.end()), outTracks.end());
            lastScanError.clear();
        }

        void publishDebug(BaseSystem& baseSystem,
                          const SoundtrackHeuristic& h,
                          double silenceRemaining,
                          bool playing,
                          const std::string& currentTrack,
                          const std::vector<std::string>& eligibleTracks,
                          const std::vector<std::string>& activeFolders) {
            if (!baseSystem.registry) return;
            (*baseSystem.registry)["SoundtrackDebugState"] = std::string("CTX: ")
                + (h.day ? "day" : "night")
                + " biome=" + h.biome
                + " underground=" + (h.underground ? "true" : "false")
                + " underwater=" + (h.underwater ? "true" : "false");
            (*baseSystem.registry)["SoundtrackDebugTimer"] = playing
                ? "MUSIC: playing"
                : "MUSIC: waiting " + formatTimer(silenceRemaining);
            (*baseSystem.registry)["SoundtrackDebugPool"] = "POOL: " + joinStrings(activeFolders, ", ");
            (*baseSystem.registry)["SoundtrackDebugTracks"] = "TRACKS: " + std::to_string(eligibleTracks.size());
            (*baseSystem.registry)["SoundtrackDebugCurrent"] = currentTrack.empty()
                ? "NOW: none"
                : "NOW: " + std::filesystem::path(currentTrack).filename().string();
        }
    }

    void UpdateSoundtracks(BaseSystem& baseSystem, std::vector<Entity>&, float dt, PlatformWindowHandle) {
        if (!baseSystem.audio) return;
        AudioContext& audio = *baseSystem.audio;
        static std::string lastRayPath;
        static std::string lastHeadPath;
        static std::string lastRayError;
        static std::string lastHeadError;
        static std::string lastScanError;
        static std::string currentTrack;
        static std::string lastTrackPath;
        static std::vector<std::string> eligibleTracks;
        static double scanTimerSec = 0.0;
        static double silenceRemainingSec = -1.0;
        static bool wasPlaying = false;
        static bool warnedNoTracks = false;
        static std::mt19937 rng(std::random_device{}());

        auto withAudioState = [&](auto&& fn) {
            std::lock_guard<std::mutex> lock(audio.audio_state_mutex);
            fn();
        };

        auto loadWavTrack = [&](const std::string& path,
                                std::vector<float>& buffer,
                                uint32_t& sampleRate,
                                double& pos,
                                const char* label,
                                std::string& lastPath,
                                std::string& lastError) {
            if (path.empty()) return;
            bool alreadyLoaded = false;
            withAudioState([&]() {
                alreadyLoaded = !buffer.empty() && lastPath == path;
            });
            if (alreadyLoaded) return;
            std::vector<float> samples;
            uint32_t rate = 0;
            if (!loadWavMono(path, samples, rate)) {
                if (lastError != path) {
                    std::cerr << "SoundtrackSystem: failed to load " << label << " '" << path
                              << "' (expect mono 16-bit PCM or 32-bit float WAV)." << std::endl;
                    lastError = path;
                }
                return;
            }
            withAudioState([&]() {
                buffer = std::move(samples);
                sampleRate = rate;
                pos = 0.0;
            });
            lastPath = path;
        };

        loadWavTrack(audio.rayTestPath, audio.rayTestBuffer, audio.rayTestSampleRate, audio.rayTestPos,
                     "ray track", lastRayPath, lastRayError);

        const bool soundtrackEnabled = getRegistryBool(baseSystem, "SoundtrackPlaylistEnabled", true);
        double gapMinSec = getRegistryDouble(baseSystem, "SoundtrackGapMinSeconds", 1200.0);
        double gapMaxSec = getRegistryDouble(baseSystem, "SoundtrackGapMaxSeconds", 3600.0);
        const double soundtrackGain = std::clamp(getRegistryDouble(baseSystem, "SoundtrackGain", 1.0), 0.0, 4.0);
        const bool skipRequested = getRegistryBool(baseSystem, "SoundtrackNextRequested", false);
        if (skipRequested && baseSystem.registry) {
            (*baseSystem.registry)["SoundtrackNextRequested"] = false;
        }
        if (gapMinSec < 0.0) gapMinSec = 0.0;
        if (gapMaxSec < 0.0) gapMaxSec = 0.0;
        if (gapMaxSec < gapMinSec) std::swap(gapMinSec, gapMaxSec);

        const double dtSec = (std::isfinite(dt) && dt > 0.0f) ? static_cast<double>(dt) : 0.0;
        SoundtrackHeuristic heuristic = buildHeuristic(baseSystem);

        bool playing = false;
        withAudioState([&]() {
            playing = audio.headTrackActive && !audio.headTrackBuffer.empty();
            audio.soundtrackChuckStopRequested = true;
            audio.soundtrackChuckStartRequested = false;
            audio.soundtrackChuckActive = false;
            audio.soundtrackChuckGain = 0.0f;
        });

        if (skipRequested) {
            withAudioState([&]() {
                audio.headTrackActive = false;
                audio.headTrackGain = 0.0f;
                audio.headTrackPos = static_cast<double>(audio.headTrackBuffer.size());
            });
            playing = false;
            wasPlaying = false;
            currentTrack.clear();
            silenceRemainingSec = 0.0;
            scanTimerSec = 0.0;
        }

        if (!soundtrackEnabled) {
            withAudioState([&]() {
                audio.headTrackActive = false;
                audio.headTrackGain = 0.0f;
            });
            currentTrack.clear();
            silenceRemainingSec = -1.0;
            publishDebug(baseSystem, heuristic, 0.0, false, currentTrack, eligibleTracks, heuristic.folders);
            return;
        }

        if (wasPlaying && !playing) {
            currentTrack.clear();
            silenceRemainingSec = randomRange(rng, gapMinSec, gapMaxSec);
        }
        wasPlaying = playing;

        if (silenceRemainingSec < 0.0) {
            silenceRemainingSec = randomRange(rng, gapMinSec, gapMaxSec);
        }

        const std::filesystem::path soundtrackRoot = getRegistryString(baseSystem, "SoundtrackFolder", "Procedures/soundtrack");
        scanTimerSec -= dtSec;
        if (scanTimerSec <= 0.0) {
            scanEligibleTracks(soundtrackRoot, heuristic.folders, eligibleTracks, lastScanError);
            scanTimerSec = 5.0;
        }

        if (playing) {
            withAudioState([&]() {
                audio.headTrackGain = static_cast<float>(soundtrackGain);
                audio.headTrackLoop = false;
            });
            publishDebug(baseSystem, heuristic, silenceRemainingSec, true, currentTrack, eligibleTracks, heuristic.folders);
            return;
        }

        silenceRemainingSec = std::max(0.0, silenceRemainingSec - dtSec);
        if (silenceRemainingSec > 0.0) {
            publishDebug(baseSystem, heuristic, silenceRemainingSec, false, currentTrack, eligibleTracks, heuristic.folders);
            return;
        }

        if (eligibleTracks.empty()) {
            if (!warnedNoTracks) {
                std::cerr << "SoundtrackSystem: no eligible .opus files found under '"
                          << soundtrackRoot.string() << "' for folders: "
                          << joinStrings(heuristic.folders, ", ") << "." << std::endl;
                warnedNoTracks = true;
            }
            silenceRemainingSec = 60.0;
            publishDebug(baseSystem, heuristic, silenceRemainingSec, false, currentTrack, eligibleTracks, heuristic.folders);
            return;
        }
        warnedNoTracks = false;

        std::uniform_int_distribution<size_t> dist(0, eligibleTracks.size() - 1);
        size_t pickIndex = dist(rng);
        if (eligibleTracks.size() > 1 && eligibleTracks[pickIndex] == lastTrackPath) {
            pickIndex = (pickIndex + 1 + (dist(rng) % (eligibleTracks.size() - 1))) % eligibleTracks.size();
        }

        const std::string chosenTrack = eligibleTracks[pickIndex];
        std::vector<float> samples;
        uint32_t rate = 0;
        int channels = 0;
        if (!loadOpusStereo(chosenTrack, samples, rate, channels) || samples.empty() || rate == 0 || channels <= 0) {
            if (lastHeadError != chosenTrack) {
                std::cerr << "SoundtrackSystem: failed to load opus soundtrack '" << chosenTrack << "'." << std::endl;
                lastHeadError = chosenTrack;
            }
            eligibleTracks.erase(eligibleTracks.begin() + static_cast<std::ptrdiff_t>(pickIndex));
            silenceRemainingSec = 60.0;
            publishDebug(baseSystem, heuristic, silenceRemainingSec, false, currentTrack, eligibleTracks, heuristic.folders);
            return;
        }

        withAudioState([&]() {
            audio.headTrackPath = chosenTrack;
            audio.headTrackBuffer = std::move(samples);
            audio.headTrackChannels = channels;
            audio.headTrackSampleRate = rate;
            audio.headTrackPos = 0.0;
            audio.headTrackGain = static_cast<float>(soundtrackGain);
            audio.headTrackLoop = false;
            audio.headTrackActive = true;
        });
        lastHeadPath = chosenTrack;
        lastTrackPath = chosenTrack;
        currentTrack = chosenTrack;
        std::cout << "SoundtrackSystem: playing opus '" << chosenTrack << "'." << std::endl;
        publishDebug(baseSystem, heuristic, silenceRemainingSec, true, currentTrack, eligibleTracks, heuristic.folders);
    }
}
