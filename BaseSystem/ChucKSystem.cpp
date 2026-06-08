#pragma once
#include <fstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cctype>

namespace ChucKSystemLogic {

    static bool file_mtime(const std::string& path, std::time_t& out) {
        struct stat st{};
        if (stat(path.c_str(), &st) == 0) { out = st.st_mtime; return true; }
        return false;
    }

    // All helpers below assume the caller already holds `audio.chuck_vm_mutex`.
    static bool compile_script_with_args(AudioContext& audio,
                                         const std::string& path,
                                         const std::string& args,
                                         t_CKUINT& outShredId) {
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "ChucK script not found at '" << path << "'. Skipping compile." << std::endl;
            return false;
        }
        std::vector<t_CKUINT> ids;
        bool ok = audio.chuck->compileFile(path, args, 1, FALSE, &ids);
        if (!ok || ids.empty()) {
            std::cerr << "ChucK failed to compile script: " << path << std::endl;
            return false;
        }
        outShredId = ids.front();
        std::cout << "ChucK script compiled: " << path << " (shred " << outShredId << ")" << std::endl;
        return true;
    }

    static bool compile_script(AudioContext& audio, const std::string& path, t_CKUINT& outShredId) {
        return compile_script_with_args(audio, path, "", outShredId);
    }

    static bool path_matches(const std::string& candidate, const std::string& target) {
        if (candidate == target) return true;
        auto normalize = [](std::string s) {
            std::replace(s.begin(), s.end(), '\\', '/');
            return s;
        };
        std::string cand = normalize(candidate);
        std::string tgt = normalize(target);
        if (cand == tgt) return true;
        if (cand.size() >= tgt.size() &&
            cand.compare(cand.size() - tgt.size(), tgt.size(), tgt) == 0) {
            return true;
        }
        return false;
    }

    static bool find_noise_shred(AudioContext& audio, t_CKUINT& outId) {
        if (!audio.chuck) return false;
        auto* vm = audio.chuck->vm();
        if (!vm) return false;
        std::vector<Chuck_VM_Shred*> shreds;
        vm->shreduler()->get_all_shreds(shreds);
        for (auto* shred : shreds) {
            if (!shred || !shred->code_orig) continue;
            if (path_matches(shred->code_orig->filename, audio.chuckNoiseScript)) {
                outId = shred->get_id();
                return true;
            }
        }
        return false;
    }

    static bool find_script_shred(AudioContext& audio, const std::string& scriptPath, t_CKUINT& outId) {
        if (!audio.chuck) return false;
        auto* vm = audio.chuck->vm();
        if (!vm) return false;
        std::vector<Chuck_VM_Shred*> shreds;
        vm->shreduler()->get_all_shreds(shreds);
        for (auto* shred : shreds) {
            if (!shred || !shred->code_orig) continue;
            if (path_matches(shred->code_orig->filename, scriptPath)) {
                outId = shred->get_id();
                return true;
            }
        }
        return false;
    }

    static int count_script_shreds(AudioContext& audio, const std::string& scriptPath) {
        if (!audio.chuck) return 0;
        auto* vm = audio.chuck->vm();
        if (!vm) return 0;
        std::vector<Chuck_VM_Shred*> shreds;
        vm->shreduler()->get_all_shreds(shreds);
        int count = 0;
        for (auto* shred : shreds) {
            if (!shred || !shred->code_orig) continue;
            if (path_matches(shred->code_orig->filename, scriptPath)) {
                ++count;
            }
        }
        return count;
    }

    static bool is_sparkle_wrapper_path(const std::string& path) {
        return path.find(".salamander_sparkle_wrapped_") != std::string::npos;
    }

    static bool is_sparkle_source_path(const std::string& path) {
        return path.find("ice5_growth.ck") != std::string::npos;
    }

    static bool is_transient_gameplay_path(const std::string& path) {
        return path.find("Procedures/chuck/gameplay/") != std::string::npos
            || path.find("Procedures/chuck/fishing/") != std::string::npos
            || path.find("Procedures/chuck/daw/") != std::string::npos
            || path.find(".salamander_chuck_lane_") != std::string::npos
            || path.find("salamander_chuck_lanes/") != std::string::npos;
    }

    static int read_registry_int(const BaseSystem& baseSystem, const std::string& key, int fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) return fallback;
        try {
            return std::stoi(std::get<std::string>(it->second));
        } catch (...) {
            return fallback;
        }
    }

    static bool read_registry_bool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
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

    static std::string read_registry_string(const BaseSystem& baseSystem,
                                            const std::string& key,
                                            const std::string& fallback) {
        if (!baseSystem.registry) return fallback;
        auto it = baseSystem.registry->find(key);
        if (it == baseSystem.registry->end()) return fallback;
        if (std::holds_alternative<std::string>(it->second)) return std::get<std::string>(it->second);
        if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second) ? "true" : "false";
        return fallback;
    }

    static void remove_script_shreds_unlocked(AudioContext& audio,
                                              const std::string& scriptPath,
                                              t_CKUINT& shredId) {
        if (!audio.chuck) return;
        auto* vm = audio.chuck->vm();
        if (!vm) return;
        if (shredId) {
            if (auto* sh = vm->shreduler()->lookup(shredId)) {
                vm->shreduler()->remove(sh);
            }
        }
        if (!scriptPath.empty()) {
            std::vector<Chuck_VM_Shred*> shreds;
            vm->shreduler()->get_all_shreds(shreds);
            for (auto* shred : shreds) {
                if (!shred || !shred->code_orig) continue;
                if (path_matches(shred->code_orig->filename, scriptPath)) {
                    vm->shreduler()->remove(shred);
                }
            }
        }
        shredId = 0;
    }

    static void stop_noise_shred_unlocked(AudioContext& audio) {
        if (!audio.chuck) return;
        auto* vm = audio.chuck->vm();
        if (!vm) return;
        std::vector<Chuck_VM_Shred*> shreds;
        vm->shreduler()->get_all_shreds(shreds);
        for (auto* shred : shreds) {
            if (!shred || !shred->code_orig) continue;
            if (path_matches(shred->code_orig->filename, audio.chuckNoiseScript)) {
                vm->shreduler()->remove(shred);
            }
        }
        audio.chuckNoiseShredId = 0;
        audio.chuckNoiseShouldRun = false;
    }

    static void stop_soundtrack_chuck_shred_unlocked(AudioContext& audio) {
        if (!audio.chuck) return;
        auto* vm = audio.chuck->vm();
        if (!vm) return;
        if (audio.soundtrackChuckShredId) {
            if (auto* sh = vm->shreduler()->lookup(audio.soundtrackChuckShredId)) {
                vm->shreduler()->remove(sh);
            }
        } else if (!audio.soundtrackChuckScriptPath.empty()) {
            std::vector<Chuck_VM_Shred*> shreds;
            vm->shreduler()->get_all_shreds(shreds);
            for (auto* shred : shreds) {
                if (!shred || !shred->code_orig) continue;
                if (path_matches(shred->code_orig->filename, audio.soundtrackChuckScriptPath)) {
                    vm->shreduler()->remove(shred);
                }
            }
        }
        audio.soundtrackChuckShredId = 0;
        {
            std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
            audio.soundtrackChuckActive = false;
            audio.soundtrackChuckGain = 0.0f;
        }
        audio.soundtrackChuckStartRequested = false;
        audio.soundtrackChuckStopRequested = false;
    }

    static void stop_sparkle_ray_chuck_shred_unlocked(AudioContext& audio) {
        if (!audio.chuck) return;
        auto* vm = audio.chuck->vm();
        if (!vm) return;
        const std::string sparkleScriptPath = audio.sparkleRayChuckScriptPath;
        if (audio.sparkleRayChuckShredId) {
            if (auto* sh = vm->shreduler()->lookup(audio.sparkleRayChuckShredId)) {
                vm->shreduler()->remove(sh);
            }
        }
        std::vector<Chuck_VM_Shred*> shreds;
        vm->shreduler()->get_all_shreds(shreds);
        for (auto* shred : shreds) {
            if (!shred || !shred->code_orig) continue;
            const std::string filename = shred->code_orig->filename;
            const bool matchesTracked = !sparkleScriptPath.empty() && path_matches(filename, sparkleScriptPath);
            if (matchesTracked || is_sparkle_wrapper_path(filename)) {
                vm->shreduler()->remove(shred);
            }
        }
        audio.sparkleRayChuckShredId = 0;
        {
            std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
            audio.sparkleRayChuckActive = false;
        }
        audio.sparkleRayChuckStartRequested = false;
        audio.sparkleRayChuckStopRequested = false;
        audio.sparkleRayChuckScriptPath.clear();
    }

    void StopNoiseShred(BaseSystem& baseSystem) {
        if (!baseSystem.audio || !baseSystem.audio->chuck) return;
        AudioContext& audio = *baseSystem.audio;
        std::lock_guard<std::mutex> chuckLock(audio.chuck_vm_mutex);
        stop_noise_shred_unlocked(audio);
    }

    void StopSoundtrackChuckShred(BaseSystem& baseSystem) {
        if (!baseSystem.audio || !baseSystem.audio->chuck) return;
        AudioContext& audio = *baseSystem.audio;
        std::lock_guard<std::mutex> chuckLock(audio.chuck_vm_mutex);
        stop_soundtrack_chuck_shred_unlocked(audio);
    }

    void StopSparkleRayChuckShred(BaseSystem& baseSystem) {
        if (!baseSystem.audio || !baseSystem.audio->chuck) return;
        AudioContext& audio = *baseSystem.audio;
        std::lock_guard<std::mutex> chuckLock(audio.chuck_vm_mutex);
        stop_sparkle_ray_chuck_shred_unlocked(audio);
    }

    // Update loop: handle pending compile/bypass requests
    void UpdateChucK(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        if (!baseSystem.audio || !baseSystem.audio->chuck) return;
        AudioContext& audio = *baseSystem.audio;

        if (audio.chuckBypass) {
            std::unique_lock<std::mutex> chuckLock(audio.chuck_vm_mutex, std::try_to_lock);
            if (!chuckLock.owns_lock()) return;
            {
                std::lock_guard<std::mutex> oneShotLock(audio.chuckOneShotMutex);
                audio.chuckOneShotScriptQueue.clear();
            }
            if (audio.chuckMainShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckMainShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckMainShredId = 0;
            }
            if (audio.chuckHeadShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckHeadShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckHeadShredId = 0;
            }
            remove_script_shreds_unlocked(audio, audio.chuckHeadRainScript, audio.chuckHeadRainShredId);
            remove_script_shreds_unlocked(audio, audio.chuckHeadWaterScript, audio.chuckHeadWaterShredId);
            remove_script_shreds_unlocked(audio, audio.chuckHeadLavaScript, audio.chuckHeadLavaShredId);
            if (audio.chuckNoiseShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckNoiseShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckNoiseShredId = 0;
            }
            stop_soundtrack_chuck_shred_unlocked(audio);
            stop_sparkle_ray_chuck_shred_unlocked(audio);
            audio.chuckMainActiveShredCount.store(0, std::memory_order_relaxed);
            audio.chuckMainHasActiveShreds.store(false, std::memory_order_relaxed);
            audio.chuckHeadActiveShredCount.store(0, std::memory_order_relaxed);
            audio.chuckHeadHasActiveShreds.store(false, std::memory_order_relaxed);
            audio.chuckHeadRainActiveShredCount.store(0, std::memory_order_relaxed);
            audio.chuckHeadRainHasActiveShreds.store(false, std::memory_order_relaxed);
            audio.chuckHeadWaterActiveShredCount.store(0, std::memory_order_relaxed);
            audio.chuckHeadWaterHasActiveShreds.store(false, std::memory_order_relaxed);
            audio.chuckHeadLavaActiveShredCount.store(0, std::memory_order_relaxed);
            audio.chuckHeadLavaHasActiveShreds.store(false, std::memory_order_relaxed);
            audio.headRainAmbientGain.store(0.0f, std::memory_order_relaxed);
            audio.headWaterAmbientGain.store(0.0f, std::memory_order_relaxed);
            audio.headLavaAmbientGain.store(0.0f, std::memory_order_relaxed);
            return;
        }

        const bool headAmbientEnabled = read_registry_bool(baseSystem, "HeadSpeakerEnvironmentalAudioEnabled", true);
        static std::time_t lastHeadRainMTime = 0;
        static std::time_t lastHeadWaterMTime = 0;
        static std::time_t lastHeadLavaMTime = 0;

        auto syncHeadAmbientLayerConfig = [&](std::string& scriptPath,
                                              const char* scriptRegistryKey,
                                              int& channel,
                                              const char* channelRegistryKey,
                                              bool& compileRequested,
                                              std::time_t& lastMTime) {
            const std::string configuredPath = read_registry_string(baseSystem, scriptRegistryKey, scriptPath);
            const int configuredChannel = read_registry_int(baseSystem, channelRegistryKey, channel);
            if (configuredPath == scriptPath && configuredChannel == channel) return;
            scriptPath = configuredPath;
            {
                std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                channel = configuredChannel;
                if (channel >= 0 && channel < static_cast<int>(audio.channelGains.size())) {
                    audio.channelGains[static_cast<size_t>(channel)] = 0.0f;
                }
            }
            compileRequested = true;
            lastMTime = 0;
        };

        syncHeadAmbientLayerConfig(
            audio.chuckHeadRainScript,
            "HeadSpeakerRainChuckScript",
            audio.chuckHeadRainChannel,
            "HeadSpeakerRainChuckChannel",
            audio.chuckHeadRainCompileRequested,
            lastHeadRainMTime
        );
        syncHeadAmbientLayerConfig(
            audio.chuckHeadWaterScript,
            "HeadSpeakerWaterChuckScript",
            audio.chuckHeadWaterChannel,
            "HeadSpeakerWaterChuckChannel",
            audio.chuckHeadWaterCompileRequested,
            lastHeadWaterMTime
        );
        syncHeadAmbientLayerConfig(
            audio.chuckHeadLavaScript,
            "HeadSpeakerLavaChuckScript",
            audio.chuckHeadLavaChannel,
            "HeadSpeakerLavaChuckChannel",
            audio.chuckHeadLavaCompileRequested,
            lastHeadLavaMTime
        );

        // Hot reload based on file mtime without rebuild
        {
            static std::time_t lastMainMTime = 0;
            std::time_t m;
            if (file_mtime(audio.chuckMainScript, m) && m != lastMainMTime) {
                audio.chuckMainCompileRequested = true;
                lastMainMTime = m;
            }

            static std::time_t lastHeadMTime = 0;
            if (file_mtime(audio.chuckHeadScript, m) && m != lastHeadMTime) {
                audio.chuckHeadCompileRequested = true;
                lastHeadMTime = m;
            }

            static std::time_t lastNoiseMTime = 0;
            if (file_mtime(audio.chuckNoiseScript, m) && m != lastNoiseMTime) {
                audio.chuckNoiseShredId = 0;
                lastNoiseMTime = m;
            }

            auto watchHeadAmbientScript = [&](const std::string& scriptPath,
                                              std::time_t& lastMTime,
                                              bool& compileRequested) {
                if (!headAmbientEnabled || scriptPath.empty()) return;
                std::time_t ambientMTime = 0;
                if (file_mtime(scriptPath, ambientMTime) && ambientMTime != lastMTime) {
                    compileRequested = true;
                    lastMTime = ambientMTime;
                }
            };
            watchHeadAmbientScript(audio.chuckHeadRainScript, lastHeadRainMTime, audio.chuckHeadRainCompileRequested);
            watchHeadAmbientScript(audio.chuckHeadWaterScript, lastHeadWaterMTime, audio.chuckHeadWaterCompileRequested);
            watchHeadAmbientScript(audio.chuckHeadLavaScript, lastHeadLavaMTime, audio.chuckHeadLavaCompileRequested);
        }

        bool soundtrackStopRequested = false;
        bool soundtrackStartRequested = false;
        bool soundtrackActive = false;
        std::string soundtrackScriptPath;
        bool sparkleStopRequested = false;
        bool sparkleStartRequested = false;
        bool sparkleActive = false;
        bool sparkleEnabled = false;
        std::string sparkleScriptPath;
        bool oneShotQueued = false;
        {
            std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
            soundtrackStopRequested = audio.soundtrackChuckStopRequested;
            soundtrackStartRequested = audio.soundtrackChuckStartRequested;
            soundtrackActive = audio.soundtrackChuckActive;
            soundtrackScriptPath = audio.soundtrackChuckScriptPath;
            sparkleStopRequested = audio.sparkleRayChuckStopRequested;
            sparkleStartRequested = audio.sparkleRayChuckStartRequested;
            sparkleActive = audio.sparkleRayChuckActive;
            sparkleEnabled = audio.sparkleRayEnabled;
            sparkleScriptPath = audio.sparkleRayChuckScriptPath;
        }
        {
            std::lock_guard<std::mutex> oneShotLock(audio.chuckOneShotMutex);
            oneShotQueued = !audio.chuckOneShotScriptQueue.empty();
        }
        const bool headAmbientStopDueDisabled = !headAmbientEnabled
            && (audio.chuckHeadRainShredId != 0
                || audio.chuckHeadWaterShredId != 0
                || audio.chuckHeadLavaShredId != 0
                || audio.chuckHeadRainHasActiveShreds.load(std::memory_order_relaxed)
                || audio.chuckHeadWaterHasActiveShreds.load(std::memory_order_relaxed)
                || audio.chuckHeadLavaHasActiveShreds.load(std::memory_order_relaxed));

        using Clock = std::chrono::steady_clock;
        static auto lastMaintenanceTime = Clock::now();
        const auto now = Clock::now();
        const bool maintenanceDue = (now - lastMaintenanceTime) >= std::chrono::milliseconds(100);

        const bool needsVmWork =
            audio.chuckMainCompileRequested
            || audio.chuckHeadCompileRequested
            || audio.chuckHeadRainCompileRequested
            || audio.chuckHeadWaterCompileRequested
            || audio.chuckHeadLavaCompileRequested
            || headAmbientStopDueDisabled
            || audio.chuckNoiseShouldRun
            || audio.chuckNoiseShredId != 0
            || soundtrackStopRequested
            || soundtrackStartRequested
            || sparkleStopRequested
            || sparkleStartRequested
            || oneShotQueued
            || maintenanceDue;
        if (!needsVmWork) return;

        std::unique_lock<std::mutex> chuckLock(audio.chuck_vm_mutex, std::try_to_lock);
        if (!chuckLock.owns_lock()) return;
        if (maintenanceDue) {
            lastMaintenanceTime = now;
        }

        std::vector<std::string> oneShotScripts;
        {
            std::lock_guard<std::mutex> oneShotLock(audio.chuckOneShotMutex);
            oneShotScripts.swap(audio.chuckOneShotScriptQueue);
        }

        // Compile main script on request
        if (audio.chuckMainCompileRequested) {
            if (audio.chuckMainShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckMainShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckMainShredId = 0;
            }
            compile_script(audio, audio.chuckMainScript, audio.chuckMainShredId);
            audio.chuckMainCompileRequested = false;
        }

        if (audio.chuckHeadCompileRequested) {
            if (audio.chuckHeadShredId) {
                if (auto* sh = audio.chuck->vm()->shreduler()->lookup(audio.chuckHeadShredId)) {
                    audio.chuck->vm()->shreduler()->remove(sh);
                }
                audio.chuckHeadShredId = 0;
            }
            compile_script(audio, audio.chuckHeadScript, audio.chuckHeadShredId);
            audio.chuckHeadCompileRequested = false;
        }

        auto stopHeadAmbientLayer = [&](const std::string& scriptPath,
                                        t_CKUINT& shredId,
                                        std::atomic<int>& activeCount,
                                        std::atomic<bool>& hasActiveShreds) {
            remove_script_shreds_unlocked(audio, scriptPath, shredId);
            activeCount.store(0, std::memory_order_relaxed);
            hasActiveShreds.store(false, std::memory_order_relaxed);
        };

        if (!headAmbientEnabled) {
            stopHeadAmbientLayer(
                audio.chuckHeadRainScript,
                audio.chuckHeadRainShredId,
                audio.chuckHeadRainActiveShredCount,
                audio.chuckHeadRainHasActiveShreds
            );
            stopHeadAmbientLayer(
                audio.chuckHeadWaterScript,
                audio.chuckHeadWaterShredId,
                audio.chuckHeadWaterActiveShredCount,
                audio.chuckHeadWaterHasActiveShreds
            );
            stopHeadAmbientLayer(
                audio.chuckHeadLavaScript,
                audio.chuckHeadLavaShredId,
                audio.chuckHeadLavaActiveShredCount,
                audio.chuckHeadLavaHasActiveShreds
            );
            audio.chuckHeadRainCompileRequested = false;
            audio.chuckHeadWaterCompileRequested = false;
            audio.chuckHeadLavaCompileRequested = false;
        }

        auto compileHeadAmbientLayer = [&](const std::string& scriptPath,
                                           int channel,
                                           t_CKUINT& shredId,
                                           bool& compileRequested,
                                           std::atomic<int>& activeCount,
                                           std::atomic<bool>& hasActiveShreds) {
            if (!compileRequested) return;
            stopHeadAmbientLayer(scriptPath, shredId, activeCount, hasActiveShreds);
            if (headAmbientEnabled && !scriptPath.empty() && channel >= 0 && channel < audio.chuckOutputChannels) {
                compile_script_with_args(audio, scriptPath, std::to_string(channel), shredId);
            }
            compileRequested = false;
        };

        compileHeadAmbientLayer(
            audio.chuckHeadRainScript,
            audio.chuckHeadRainChannel,
            audio.chuckHeadRainShredId,
            audio.chuckHeadRainCompileRequested,
            audio.chuckHeadRainActiveShredCount,
            audio.chuckHeadRainHasActiveShreds
        );
        compileHeadAmbientLayer(
            audio.chuckHeadWaterScript,
            audio.chuckHeadWaterChannel,
            audio.chuckHeadWaterShredId,
            audio.chuckHeadWaterCompileRequested,
            audio.chuckHeadWaterActiveShredCount,
            audio.chuckHeadWaterHasActiveShreds
        );
        compileHeadAmbientLayer(
            audio.chuckHeadLavaScript,
            audio.chuckHeadLavaChannel,
            audio.chuckHeadLavaShredId,
            audio.chuckHeadLavaCompileRequested,
            audio.chuckHeadLavaActiveShredCount,
            audio.chuckHeadLavaHasActiveShreds
        );

        for (const auto& scriptPath : oneShotScripts) {
            t_CKUINT oneShotShredId = 0;
            if (compile_script(audio, scriptPath, oneShotShredId)) {
                audio.chuckOneShotCompileCount.fetch_add(1, std::memory_order_relaxed);
            } else {
                audio.chuckOneShotCompileFailures.fetch_add(1, std::memory_order_relaxed);
            }
        }

        // Manage noise script based on flag
        if (audio.chuckNoiseShouldRun || audio.chuckNoiseShredId != 0) {
            t_CKUINT existingId = 0;
            bool hasNoise = find_noise_shred(audio, existingId);
            if (audio.chuckNoiseShredId == 0 && hasNoise) {
                stop_noise_shred_unlocked(audio);
                hasNoise = false;
            }
            if (audio.chuckNoiseShouldRun) {
                if (!hasNoise) {
                    compile_script(audio, audio.chuckNoiseScript, audio.chuckNoiseShredId);
                } else {
                    audio.chuckNoiseShredId = existingId;
                }
            } else {
                if (hasNoise) {
                    stop_noise_shred_unlocked(audio);
                } else {
                    audio.chuckNoiseShredId = 0;
                }
            }
        }

        if (soundtrackStopRequested) {
            stop_soundtrack_chuck_shred_unlocked(audio);
        }

        if (soundtrackStartRequested) {
            stop_soundtrack_chuck_shred_unlocked(audio);
            if (!soundtrackScriptPath.empty()) {
                if (!compile_script(audio, soundtrackScriptPath, audio.soundtrackChuckShredId)) {
                    audio.soundtrackChuckShredId = 0;
                    std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                    audio.soundtrackChuckActive = false;
                    audio.soundtrackChuckGain = 0.0f;
                } else {
                    std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                    audio.soundtrackChuckScriptPath = soundtrackScriptPath;
                    audio.soundtrackChuckActive = true;
                }
            } else {
                audio.soundtrackChuckShredId = 0;
                std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                audio.soundtrackChuckActive = false;
                audio.soundtrackChuckGain = 0.0f;
            }
            audio.soundtrackChuckStartRequested = false;
        }

        if (maintenanceDue) {
            if (audio.soundtrackChuckShredId != 0) {
                if (!audio.chuck->vm()->shreduler()->lookup(audio.soundtrackChuckShredId)) {
                    audio.soundtrackChuckShredId = 0;
                    std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                    audio.soundtrackChuckActive = false;
                    audio.soundtrackChuckGain = 0.0f;
                }
            } else if (soundtrackActive && !soundtrackScriptPath.empty()) {
                t_CKUINT existingId = 0;
                if (find_script_shred(audio, soundtrackScriptPath, existingId)) {
                    audio.soundtrackChuckShredId = existingId;
                } else {
                    std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                    audio.soundtrackChuckActive = false;
                    audio.soundtrackChuckGain = 0.0f;
                }
            }
        }

        if (sparkleStopRequested) {
            stop_sparkle_ray_chuck_shred_unlocked(audio);
        }

        if (sparkleStartRequested) {
            stop_sparkle_ray_chuck_shred_unlocked(audio);
            if (!sparkleScriptPath.empty()) {
                if (!compile_script(audio, sparkleScriptPath, audio.sparkleRayChuckShredId)) {
                    audio.sparkleRayChuckShredId = 0;
                    std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                    audio.sparkleRayChuckActive = false;
                } else {
                    std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                    audio.sparkleRayChuckScriptPath = sparkleScriptPath;
                    audio.sparkleRayChuckActive = true;
                }
            } else {
                audio.sparkleRayChuckShredId = 0;
                std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                audio.sparkleRayChuckActive = false;
            }
            audio.sparkleRayChuckStartRequested = false;
        }

        if (maintenanceDue) {
            if (audio.sparkleRayChuckShredId != 0) {
                if (!audio.chuck->vm()->shreduler()->lookup(audio.sparkleRayChuckShredId)) {
                    audio.sparkleRayChuckShredId = 0;
                    std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                    audio.sparkleRayChuckActive = false;
                }
            } else if (sparkleActive && !sparkleScriptPath.empty()) {
                t_CKUINT existingId = 0;
                if (find_script_shred(audio, sparkleScriptPath, existingId)) {
                    audio.sparkleRayChuckShredId = existingId;
                } else {
                    std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                    audio.sparkleRayChuckActive = false;
                }
            }
        }

        if (maintenanceDue && sparkleActive && !sparkleScriptPath.empty()) {
            std::vector<Chuck_VM_Shred*> shreds;
            audio.chuck->vm()->shreduler()->get_all_shreds(shreds);
            Chuck_VM_Shred* keep = nullptr;
            for (auto* shred : shreds) {
                if (!shred || !shred->code_orig) continue;
                const std::string filename = shred->code_orig->filename;
                if (!path_matches(filename, sparkleScriptPath)
                    && !is_sparkle_wrapper_path(filename)
                    && !is_sparkle_source_path(filename)) {
                    continue;
                }
                if (!keep) {
                    keep = shred;
                } else {
                    audio.chuck->vm()->shreduler()->remove(shred);
                }
            }
            if (keep) {
                audio.sparkleRayChuckShredId = keep->get_id();
            }
        }

        // Fail-safe: if sparkle routing is disabled, there should be no sparkle shreds alive.
        if (maintenanceDue && !sparkleEnabled) {
            std::vector<Chuck_VM_Shred*> shreds;
            audio.chuck->vm()->shreduler()->get_all_shreds(shreds);
            for (auto* shred : shreds) {
                if (!shred || !shred->code_orig) continue;
                const std::string filename = shred->code_orig->filename;
                if (is_sparkle_wrapper_path(filename) || is_sparkle_source_path(filename)) {
                    audio.chuck->vm()->shreduler()->remove(shred);
                }
            }
            audio.sparkleRayChuckShredId = 0;
            {
                std::lock_guard<std::mutex> audioStateLock(audio.audio_state_mutex);
                audio.sparkleRayChuckActive = false;
            }
            audio.sparkleRayChuckStartRequested = false;
            audio.sparkleRayChuckStopRequested = false;
            audio.sparkleRayChuckScriptPath.clear();
        }

        if (maintenanceDue) {
            const int maxTransientShreds = std::clamp(
                read_registry_int(baseSystem, "ChuckMaxTransientGameplayShreds", 24),
                1,
                256
            );
            std::vector<Chuck_VM_Shred*> shreds;
            audio.chuck->vm()->shreduler()->get_all_shreds(shreds);
            std::vector<t_CKUINT> transientIds;
            transientIds.reserve(shreds.size());
            for (auto* shred : shreds) {
                if (!shred || !shred->code_orig) continue;
                const std::string filename = shred->code_orig->filename;
                if (!is_transient_gameplay_path(filename)) continue;
                if (!soundtrackScriptPath.empty() && path_matches(filename, soundtrackScriptPath)) continue;
                if (!sparkleScriptPath.empty() && path_matches(filename, sparkleScriptPath)) continue;
                transientIds.push_back(shred->get_id());
            }
            if (static_cast<int>(transientIds.size()) > maxTransientShreds) {
                std::sort(transientIds.begin(), transientIds.end());
                const int removeCount = static_cast<int>(transientIds.size()) - maxTransientShreds;
                for (int i = 0; i < removeCount; ++i) {
                    const t_CKUINT id = transientIds[static_cast<size_t>(i)];
                    if (auto* sh = audio.chuck->vm()->shreduler()->lookup(id)) {
                        audio.chuck->vm()->shreduler()->remove(sh);
                    }
                }
            }
        }

        if (maintenanceDue) {
            int mainShredCount = count_script_shreds(audio, audio.chuckMainScript);
            audio.chuckMainActiveShredCount.store(mainShredCount, std::memory_order_relaxed);
            audio.chuckMainHasActiveShreds.store(mainShredCount > 0, std::memory_order_relaxed);
            int headShredCount = count_script_shreds(audio, audio.chuckHeadScript);
            audio.chuckHeadActiveShredCount.store(headShredCount, std::memory_order_relaxed);
            audio.chuckHeadHasActiveShreds.store(headShredCount > 0, std::memory_order_relaxed);
            int rainShredCount = audio.chuckHeadRainScript.empty()
                ? 0
                : count_script_shreds(audio, audio.chuckHeadRainScript);
            audio.chuckHeadRainActiveShredCount.store(rainShredCount, std::memory_order_relaxed);
            audio.chuckHeadRainHasActiveShreds.store(rainShredCount > 0, std::memory_order_relaxed);
            int waterShredCount = audio.chuckHeadWaterScript.empty()
                ? 0
                : count_script_shreds(audio, audio.chuckHeadWaterScript);
            audio.chuckHeadWaterActiveShredCount.store(waterShredCount, std::memory_order_relaxed);
            audio.chuckHeadWaterHasActiveShreds.store(waterShredCount > 0, std::memory_order_relaxed);
            int lavaShredCount = audio.chuckHeadLavaScript.empty()
                ? 0
                : count_script_shreds(audio, audio.chuckHeadLavaScript);
            audio.chuckHeadLavaActiveShredCount.store(lavaShredCount, std::memory_order_relaxed);
            audio.chuckHeadLavaHasActiveShreds.store(lavaShredCount > 0, std::memory_order_relaxed);
        }
    }

}
