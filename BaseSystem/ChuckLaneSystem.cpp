#pragma once

#include "Host/PlatformInput.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "stb_easy_font.h"

namespace DawLaneTimelineSystemLogic {
    struct LaneLayout;
    bool hasDawUiWorld(const LevelContext& level);
    LaneLayout ComputeLaneLayout(const BaseSystem& baseSystem, const DawContext& daw, PlatformWindowHandle win);
    double GridSecondsForZoom(double secondsPerScreen, double secondsPerBeat);
}
namespace DawTimelineRebaseLogic { void ShiftTimelineRight(BaseSystem& baseSystem, uint64_t shiftSamples); }

namespace ChuckLaneSystemLogic {
    namespace {
        constexpr int kLaneTypeChuck = 3;
        constexpr float kLaneAlpha = 0.88f;
        constexpr float kTrackHandleSize = 60.0f;
        constexpr float kTrackHandleInset = 12.0f;
        constexpr float kEventMinWidth = 18.0f;
        constexpr float kEventMaxWidth = 54.0f;
        constexpr float kEventVerticalInset = 9.0f;
        constexpr double kDoubleClickSeconds = 0.45;

        struct UiVertex { glm::vec2 pos; glm::vec3 color; };
        static const std::vector<VertexAttribLayout> kUiVertexLayout = {
            {0, 2, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(UiVertex)), offsetof(UiVertex, pos), 0},
            {1, 3, VertexAttribType::Float, false, static_cast<unsigned int>(sizeof(UiVertex)), offsetof(UiVertex, color), 0}
        };

        struct Rect {
            float left = 0.0f;
            float top = 0.0f;
            float right = 0.0f;
            float bottom = 0.0f;
        };

        struct EventHit {
            bool valid = false;
            int track = -1;
            int eventIndex = -1;
            int laneIndex = -1;
            int eventId = 0;
            float distance = FLT_MAX;
        };

        static std::vector<UiVertex> g_vertices;
        static bool g_rightMouseWasDown = false;
        static double g_lastEventClickTime = -1.0;
        static int g_lastEventClickTrack = -1;
        static int g_lastEventClickId = 0;
        static double g_lastLaneClickTime = -1.0;
        static int g_lastLaneClickTrack = -1;
        static std::unordered_map<int, uint8_t> g_keyDown;
        static uint64_t g_scriptSerial = 1;

        glm::vec2 pixelToNDC(const glm::vec2& pixel, double width, double height) {
            float ndcX = static_cast<float>((pixel.x / width) * 2.0 - 1.0);
            float ndcY = static_cast<float>(1.0 - (pixel.y / height) * 2.0);
            return {ndcX, ndcY};
        }

        void pushQuad(std::vector<UiVertex>& verts,
                      const glm::vec2& a,
                      const glm::vec2& b,
                      const glm::vec2& c,
                      const glm::vec2& d,
                      const glm::vec3& color) {
            verts.push_back({a, color});
            verts.push_back({b, color});
            verts.push_back({c, color});
            verts.push_back({a, color});
            verts.push_back({c, color});
            verts.push_back({d, color});
        }

        void pushRect(std::vector<UiVertex>& verts,
                      const Rect& r,
                      const glm::vec3& color,
                      double width,
                      double height) {
            pushQuad(verts,
                     pixelToNDC({r.left, r.top}, width, height),
                     pixelToNDC({r.right, r.top}, width, height),
                     pixelToNDC({r.right, r.bottom}, width, height),
                     pixelToNDC({r.left, r.bottom}, width, height),
                     color);
        }

        void pushText(std::vector<UiVertex>& verts,
                      float x,
                      float y,
                      const char* text,
                      const glm::vec3& color,
                      double width,
                      double height) {
            if (!text || text[0] == '\0') return;
            char buffer[99999];
            int numQuads = stb_easy_font_print(x, y, const_cast<char*>(text), nullptr, buffer, sizeof(buffer));
            float* raw = reinterpret_cast<float*>(buffer);
            for (int i = 0; i < numQuads; ++i) {
                const int base = i * 16;
                glm::vec2 v0{raw[base + 0], raw[base + 1]};
                glm::vec2 v1{raw[base + 4], raw[base + 5]};
                glm::vec2 v2{raw[base + 8], raw[base + 9]};
                glm::vec2 v3{raw[base + 12], raw[base + 13]};
                pushQuad(verts,
                         pixelToNDC(v0, width, height),
                         pixelToNDC(v1, width, height),
                         pixelToNDC(v2, width, height),
                         pixelToNDC(v3, width, height),
                         color);
            }
        }

        bool contains(const Rect& r, double x, double y) {
            return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
        }

        bool isCommandDown(PlatformWindowHandle win) {
            if (!win) return false;
            return PlatformInput::IsKeyDown(win, PlatformInput::Key::LeftSuper)
                || PlatformInput::IsKeyDown(win, PlatformInput::Key::RightSuper)
                || PlatformInput::IsKeyDown(win, PlatformInput::Key::LeftControl)
                || PlatformInput::IsKeyDown(win, PlatformInput::Key::RightControl);
        }

        bool isShiftDown(PlatformWindowHandle win) {
            if (!win) return false;
            return PlatformInput::IsKeyDown(win, PlatformInput::Key::LeftShift)
                || PlatformInput::IsKeyDown(win, PlatformInput::Key::RightShift);
        }

        bool keyPressed(PlatformWindowHandle win, PlatformInput::Key key) {
            const int id = static_cast<int>(key);
            bool down = PlatformInput::IsKeyDown(win, key);
            bool pressed = down && (g_keyDown[id] == 0u);
            g_keyDown[id] = down ? 1u : 0u;
            return pressed;
        }

        uint64_t gridStepSamples(const DawContext& daw, double secondsPerScreen) {
            double bpm = daw.bpm.load(std::memory_order_relaxed);
            if (bpm <= 0.0) bpm = 120.0;
            double secondsPerBeat = 60.0 / bpm;
            double gridSeconds = DawLaneTimelineSystemLogic::GridSecondsForZoom(secondsPerScreen, secondsPerBeat);
            double sampleRate = (daw.sampleRate > 0.0f) ? static_cast<double>(daw.sampleRate) : 44100.0;
            return std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(gridSeconds * sampleRate)));
        }

        uint64_t computeRebaseShiftSamples(const DawContext& daw, int64_t negativeSample) {
            if (negativeSample >= 0) return 0;
            double sampleRate = (daw.sampleRate > 0.0f) ? static_cast<double>(daw.sampleRate) : 44100.0;
            double bpm = daw.bpm.load(std::memory_order_relaxed);
            if (bpm <= 0.0) bpm = 120.0;
            uint64_t barSamples = std::max<uint64_t>(1,
                static_cast<uint64_t>(std::llround((60.0 / bpm) * 4.0 * sampleRate)));
            uint64_t needed = static_cast<uint64_t>(-negativeSample) + barSamples * 2ull;
            uint64_t shift = ((needed + barSamples - 1ull) / barSamples) * barSamples;
            return shift == 0 ? barSamples : shift;
        }

        uint64_t sampleFromCursorX(BaseSystem& baseSystem,
                                   DawContext& daw,
                                   float laneLeft,
                                   float laneRight,
                                   double secondsPerScreen,
                                   double cursorX,
                                   bool snap) {
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;
            double t = (laneRight > laneLeft)
                ? (cursorX - laneLeft) / static_cast<double>(laneRight - laneLeft)
                : 0.0;
            t = std::clamp(t, 0.0, 1.0);
            int64_t sample = static_cast<int64_t>(std::llround(static_cast<double>(daw.timelineOffsetSamples) + t * windowSamples));
            if (sample < 0) {
                uint64_t shiftSamples = computeRebaseShiftSamples(daw, sample);
                DawTimelineRebaseLogic::ShiftTimelineRight(baseSystem, shiftSamples);
                sample += static_cast<int64_t>(shiftSamples);
            }
            if (snap) {
                uint64_t step = gridStepSamples(daw, secondsPerScreen);
                if (step > 0) {
                    return (static_cast<uint64_t>(sample) / step) * step;
                }
            }
            return static_cast<uint64_t>(sample);
        }

        float sampleToX(uint64_t sample,
                        double offsetSamples,
                        double windowSamples,
                        float laneLeft,
                        float laneRight) {
            if (windowSamples <= 0.0) return laneLeft;
            float t = static_cast<float>((static_cast<double>(sample) - offsetSamples) / windowSamples);
            return laneLeft + (laneRight - laneLeft) * t;
        }

        void invalidateDawUi(BaseSystem& baseSystem, DawContext& daw) {
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            if (baseSystem.uiStamp) baseSystem.uiStamp->cacheBuilt = false;
            daw.uiCacheBuilt = false;
        }

        void sortEvents(ChuckTrack& track) {
            std::sort(track.events.begin(), track.events.end(), [](const ChuckEvent& a, const ChuckEvent& b) {
                if (a.startSample == b.startSample) return a.eventId < b.eventId;
                return a.startSample < b.startSample;
            });
        }

        int findEventIndexById(const ChuckTrack& track, int eventId) {
            for (int i = 0; i < static_cast<int>(track.events.size()); ++i) {
                if (track.events[static_cast<size_t>(i)].eventId == eventId) return i;
            }
            return -1;
        }

        void clearChuckSelection(DawContext& daw) {
            daw.selectedChuckTrack = -1;
            daw.selectedChuckEvent = -1;
            daw.selectedClipTrack = -1;
            daw.selectedClipIndex = -1;
            daw.selectedAutomationClipTrack = -1;
            daw.selectedAutomationClipIndex = -1;
            daw.timelineSelectionActive = false;
            daw.timelineSelectionDragActive = false;
        }

        std::string defaultChuckCode() {
            return "SinOsc s => ADSR e => Gain g => dac;\n"
                   "0.16 => g.gain;\n"
                   "660 => s.freq;\n"
                   "(5::ms, 40::ms, 0.35, 120::ms) => e.set;\n"
                   "e.keyOn();\n"
                   "80::ms => now;\n"
                   "e.keyOff();\n"
                   "160::ms => now;\n";
        }

        bool writeOneShotScript(AudioContext& audio, const ChuckEvent& event, int trackIndex, std::string& outPath) {
            std::filesystem::path dir = std::filesystem::temp_directory_path() / "salamander_chuck_lanes";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (ec) return false;
            uint64_t serial = g_scriptSerial++;
            std::filesystem::path path = dir / (".salamander_chuck_lane_t"
                + std::to_string(trackIndex + 1)
                + "_e"
                + std::to_string(event.eventId)
                + "_"
                + std::to_string(serial)
                + ".ck");
            std::ofstream out(path);
            if (!out.is_open()) return false;
            if (event.code.empty()) {
                out << defaultChuckCode();
            } else {
                out << event.code;
                if (event.code.back() != '\n') out << '\n';
            }
            out.close();
            if (!out) return false;
            {
                std::lock_guard<std::mutex> lock(audio.chuckOneShotMutex);
                audio.chuckOneShotScriptQueue.push_back(path.string());
            }
            outPath = path.string();
            return true;
        }

        void queueChuckEvent(BaseSystem& baseSystem, DawContext& daw, int trackIndex, const ChuckEvent& event) {
            if (!event.enabled) return;
            if (!baseSystem.audio || !baseSystem.audio->chuck) {
                daw.chuckStatusMessage = "CK VM offline";
                return;
            }
            std::string scriptPath;
            if (writeOneShotScript(*baseSystem.audio, event, trackIndex, scriptPath)) {
                daw.chuckStatusMessage = "CK event queued";
            } else {
                daw.chuckStatusMessage = "CK write failed";
            }
        }

        void schedulePlayback(BaseSystem& baseSystem, DawContext& daw) {
            bool playing = daw.transportPlaying.load(std::memory_order_relaxed);
            uint64_t current = daw.playheadSample.load(std::memory_order_relaxed);
            if (!playing) {
                daw.chuckSchedulerWasPlaying = false;
                daw.chuckLastSchedulerSample = current;
                return;
            }

            uint64_t last = daw.chuckLastSchedulerSample;
            bool firstFrame = !daw.chuckSchedulerWasPlaying;
            for (int trackIndex = 0; trackIndex < static_cast<int>(daw.chuckTracks.size()); ++trackIndex) {
                ChuckTrack& track = daw.chuckTracks[static_cast<size_t>(trackIndex)];
                if (track.mute) continue;
                for (const ChuckEvent& event : track.events) {
                    bool shouldQueue = false;
                    if (firstFrame) {
                        shouldQueue = (event.startSample == current);
                    } else if (current >= last) {
                        shouldQueue = (event.startSample > last && event.startSample <= current);
                    } else {
                        shouldQueue = (event.startSample > last || event.startSample <= current);
                    }
                    if (shouldQueue) {
                        queueChuckEvent(baseSystem, daw, trackIndex, event);
                    }
                }
            }
            daw.chuckLastSchedulerSample = current;
            daw.chuckSchedulerWasPlaying = true;
        }

        std::vector<int> buildChuckLaneIndex(const DawContext& daw, int chuckTrackCount) {
            std::vector<int> out(static_cast<size_t>(chuckTrackCount), -1);
            if (!daw.laneOrder.empty()) {
                for (int laneIndex = 0; laneIndex < static_cast<int>(daw.laneOrder.size()); ++laneIndex) {
                    const auto& entry = daw.laneOrder[static_cast<size_t>(laneIndex)];
                    if (entry.type == kLaneTypeChuck && entry.trackIndex >= 0 && entry.trackIndex < chuckTrackCount) {
                        out[static_cast<size_t>(entry.trackIndex)] = laneIndex;
                    }
                }
            }
            return out;
        }

        int laneTrackUnderCursor(const DawContext& daw,
                                 const std::vector<int>& chuckLaneIndex,
                                 float cursorY,
                                 float startY,
                                 float laneHalfH,
                                 float rowSpan) {
            for (int trackIndex = 0; trackIndex < static_cast<int>(chuckLaneIndex.size()); ++trackIndex) {
                int laneIndex = chuckLaneIndex[static_cast<size_t>(trackIndex)];
                if (laneIndex < 0) continue;
                float centerY = startY + static_cast<float>(laneIndex) * rowSpan;
                if (cursorY >= centerY - laneHalfH && cursorY <= centerY + laneHalfH) {
                    return trackIndex;
                }
            }
            (void)daw;
            return -1;
        }

        float eventWidthForZoom(const DawContext& daw, double secondsPerScreen, float laneLeft, float laneRight) {
            uint64_t step = gridStepSamples(daw, secondsPerScreen);
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) return kEventMinWidth;
            float px = static_cast<float>((static_cast<double>(step) / windowSamples) * static_cast<double>(laneRight - laneLeft));
            return std::clamp(px * 0.72f, kEventMinWidth, kEventMaxWidth);
        }

        EventHit findEventHit(const DawContext& daw,
                              const UIContext& ui,
                              const std::vector<int>& chuckLaneIndex,
                              float laneLeft,
                              float laneRight,
                              float laneHalfH,
                              float rowSpan,
                              float startY,
                              double secondsPerScreen) {
            EventHit hit;
            double windowSamples = secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;
            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            float eventWidth = eventWidthForZoom(daw, secondsPerScreen, laneLeft, laneRight);
            for (int trackIndex = 0; trackIndex < static_cast<int>(daw.chuckTracks.size()); ++trackIndex) {
                int laneIndex = chuckLaneIndex[static_cast<size_t>(trackIndex)];
                if (laneIndex < 0) continue;
                float centerY = startY + static_cast<float>(laneIndex) * rowSpan;
                float eventTop = centerY - laneHalfH + kEventVerticalInset;
                float eventBottom = centerY + laneHalfH - kEventVerticalInset;
                if (ui.cursorY < eventTop - 2.0 || ui.cursorY > eventBottom + 2.0) continue;
                const ChuckTrack& track = daw.chuckTracks[static_cast<size_t>(trackIndex)];
                for (int eventIndex = 0; eventIndex < static_cast<int>(track.events.size()); ++eventIndex) {
                    const ChuckEvent& event = track.events[static_cast<size_t>(eventIndex)];
                    uint64_t sample = event.startSample;
                    if (daw.chuckEventDragActive
                        && daw.chuckEventDragTrack == trackIndex
                        && daw.chuckEventDragIndex == eventIndex) {
                        sample = daw.chuckEventDragTargetSample;
                    }
                    float x = sampleToX(sample, offsetSamples, windowSamples, laneLeft, laneRight);
                    if (x < laneLeft - eventWidth || x > laneRight + eventWidth) continue;
                    float left = x - eventWidth * 0.5f;
                    float right = x + eventWidth * 0.5f;
                    if (ui.cursorX < left || ui.cursorX > right) continue;
                    float cy = (eventTop + eventBottom) * 0.5f;
                    float dx = static_cast<float>(ui.cursorX) - x;
                    float dy = static_cast<float>(ui.cursorY) - cy;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    if (dist < hit.distance) {
                        hit.valid = true;
                        hit.track = trackIndex;
                        hit.eventIndex = eventIndex;
                        hit.laneIndex = laneIndex;
                        hit.eventId = event.eventId;
                        hit.distance = dist;
                    }
                }
            }
            return hit;
        }

        void openEditor(DawContext& daw, int trackIndex, int eventIndex) {
            if (trackIndex < 0 || trackIndex >= static_cast<int>(daw.chuckTracks.size())) return;
            ChuckTrack& track = daw.chuckTracks[static_cast<size_t>(trackIndex)];
            if (eventIndex < 0 || eventIndex >= static_cast<int>(track.events.size())) return;
            ChuckEvent& event = track.events[static_cast<size_t>(eventIndex)];
            daw.chuckEventEditorOpen = true;
            daw.chuckEditorTrack = trackIndex;
            daw.chuckEditorEvent = event.eventId;
            daw.chuckEditorBuffer = event.code.empty() ? defaultChuckCode() : event.code;
            daw.selectedChuckTrack = trackIndex;
            daw.selectedChuckEvent = event.eventId;
        }

        void closeEditor(DawContext& daw) {
            daw.chuckEventEditorOpen = false;
            daw.chuckEditorTrack = -1;
            daw.chuckEditorEvent = -1;
            daw.chuckEditorBuffer.clear();
        }

        void saveEditor(DawContext& daw) {
            if (daw.chuckEditorTrack >= 0 && daw.chuckEditorTrack < static_cast<int>(daw.chuckTracks.size())) {
                ChuckTrack& track = daw.chuckTracks[static_cast<size_t>(daw.chuckEditorTrack)];
                int index = findEventIndexById(track, daw.chuckEditorEvent);
                if (index >= 0) {
                    track.events[static_cast<size_t>(index)].code = daw.chuckEditorBuffer;
                    daw.selectedChuckTrack = daw.chuckEditorTrack;
                    daw.selectedChuckEvent = daw.chuckEditorEvent;
                    daw.chuckStatusMessage = "CK event saved";
                }
            }
            closeEditor(daw);
        }

        void appendTypedChar(DawContext& daw, char ch) {
            if (ch == '\0') return;
            daw.chuckEditorBuffer.push_back(ch);
        }

        void updateEditorInput(DawContext& daw, UIContext& ui, PlatformWindowHandle win, const Rect& saveBtn, const Rect& cancelBtn) {
            if (!daw.chuckEventEditorOpen) return;
            ui.consumeClick = true;

            if (ui.uiLeftPressed) {
                if (contains(saveBtn, ui.cursorX, ui.cursorY)) {
                    saveEditor(daw);
                    return;
                }
                if (contains(cancelBtn, ui.cursorX, ui.cursorY)) {
                    closeEditor(daw);
                    return;
                }
            }

            bool cmd = isCommandDown(win);
            bool shift = isShiftDown(win);
            if (keyPressed(win, PlatformInput::Key::Escape)) {
                closeEditor(daw);
                return;
            }
            if (keyPressed(win, PlatformInput::Key::Backspace)) {
                if (!daw.chuckEditorBuffer.empty()) daw.chuckEditorBuffer.pop_back();
            }
            if (keyPressed(win, PlatformInput::Key::Enter) || keyPressed(win, PlatformInput::Key::KpEnter)) {
                if (cmd) {
                    saveEditor(daw);
                    return;
                }
                daw.chuckEditorBuffer.push_back('\n');
            }
            if (cmd && keyPressed(win, PlatformInput::Key::V)) {
                const char* clip = PlatformInput::GetClipboardText(win);
                if (clip) daw.chuckEditorBuffer += clip;
            }
            if (cmd && keyPressed(win, PlatformInput::Key::S)) {
                saveEditor(daw);
                return;
            }
            if (cmd) return;

            static const std::array<std::pair<PlatformInput::Key, char>, 26> letters = {{
                {PlatformInput::Key::A, 'a'}, {PlatformInput::Key::B, 'b'}, {PlatformInput::Key::C, 'c'},
                {PlatformInput::Key::D, 'd'}, {PlatformInput::Key::E, 'e'}, {PlatformInput::Key::F, 'f'},
                {PlatformInput::Key::G, 'g'}, {PlatformInput::Key::H, 'h'}, {PlatformInput::Key::I, 'i'},
                {PlatformInput::Key::J, 'j'}, {PlatformInput::Key::K, 'k'}, {PlatformInput::Key::L, 'l'},
                {PlatformInput::Key::M, 'm'}, {PlatformInput::Key::N, 'n'}, {PlatformInput::Key::O, 'o'},
                {PlatformInput::Key::P, 'p'}, {PlatformInput::Key::Q, 'q'}, {PlatformInput::Key::R, 'r'},
                {PlatformInput::Key::S, 's'}, {PlatformInput::Key::T, 't'}, {PlatformInput::Key::U, 'u'},
                {PlatformInput::Key::V, 'v'}, {PlatformInput::Key::W, 'w'}, {PlatformInput::Key::X, 'x'},
                {PlatformInput::Key::Y, 'y'}, {PlatformInput::Key::Z, 'z'}
            }};
            for (const auto& entry : letters) {
                if (keyPressed(win, entry.first)) {
                    char ch = entry.second;
                    if (shift) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                    appendTypedChar(daw, ch);
                }
            }

            static const std::array<std::pair<PlatformInput::Key, char>, 10> digits = {{
                {PlatformInput::Key::Num0, '0'}, {PlatformInput::Key::Num1, '1'}, {PlatformInput::Key::Num2, '2'},
                {PlatformInput::Key::Num3, '3'}, {PlatformInput::Key::Num4, '4'}, {PlatformInput::Key::Num5, '5'},
                {PlatformInput::Key::Num6, '6'}, {PlatformInput::Key::Num7, '7'}, {PlatformInput::Key::Num8, '8'},
                {PlatformInput::Key::Num9, '9'}
            }};
            for (const auto& entry : digits) {
                if (keyPressed(win, entry.first)) appendTypedChar(daw, entry.second);
            }
            if (keyPressed(win, PlatformInput::Key::Space)) appendTypedChar(daw, ' ');
            if (keyPressed(win, PlatformInput::Key::Minus)) appendTypedChar(daw, shift ? '_' : '-');
            if (keyPressed(win, PlatformInput::Key::Period)) appendTypedChar(daw, '.');
            if (keyPressed(win, PlatformInput::Key::Equal)) appendTypedChar(daw, shift ? '+' : '=');
        }

        std::vector<std::string> visibleCodeLines(const std::string& code, int maxLines, size_t maxChars) {
            std::vector<std::string> lines;
            std::stringstream ss(code);
            std::string line;
            while (static_cast<int>(lines.size()) < maxLines && std::getline(ss, line)) {
                if (line.size() > maxChars) {
                    line = line.substr(0, maxChars - 3) + "...";
                }
                lines.push_back(line);
            }
            if (lines.empty()) lines.push_back("");
            return lines;
        }

        void renderEditor(DawContext& daw,
                          const DawLaneTimelineSystemLogic::LaneLayout& layout,
                          const Rect& saveBtn,
                          const Rect& cancelBtn) {
            if (!daw.chuckEventEditorOpen) return;
            float w = std::min(640.0f, static_cast<float>(layout.screenWidth) - 48.0f);
            float h = std::min(430.0f, static_cast<float>(layout.screenHeight) - 48.0f);
            float x = static_cast<float>((layout.screenWidth - w) * 0.5);
            float y = static_cast<float>((layout.screenHeight - h) * 0.5);
            Rect panel{x, y, x + w, y + h};
            Rect textArea{x + 18.0f, y + 54.0f, x + w - 18.0f, y + h - 58.0f};
            pushRect(g_vertices, {0.0f, 0.0f, static_cast<float>(layout.screenWidth), static_cast<float>(layout.screenHeight)}, {0.02f, 0.02f, 0.025f}, layout.screenWidth, layout.screenHeight);
            pushRect(g_vertices, panel, {0.10f, 0.12f, 0.13f}, layout.screenWidth, layout.screenHeight);
            pushRect(g_vertices, {panel.left, panel.top, panel.right, panel.top + 34.0f}, {0.04f, 0.35f, 0.32f}, layout.screenWidth, layout.screenHeight);
            pushRect(g_vertices, textArea, {0.015f, 0.02f, 0.022f}, layout.screenWidth, layout.screenHeight);
            pushRect(g_vertices, saveBtn, {0.05f, 0.44f, 0.38f}, layout.screenWidth, layout.screenHeight);
            pushRect(g_vertices, cancelBtn, {0.26f, 0.18f, 0.18f}, layout.screenWidth, layout.screenHeight);

            pushText(g_vertices, x + 18.0f, y + 12.0f, "CHUCK EVENT", {0.92f, 0.96f, 0.94f}, layout.screenWidth, layout.screenHeight);
            pushText(g_vertices, saveBtn.left + 18.0f, saveBtn.top + 7.0f, "SAVE", {0.92f, 0.96f, 0.94f}, layout.screenWidth, layout.screenHeight);
            pushText(g_vertices, cancelBtn.left + 12.0f, cancelBtn.top + 7.0f, "CANCEL", {0.92f, 0.86f, 0.86f}, layout.screenWidth, layout.screenHeight);

            auto lines = visibleCodeLines(daw.chuckEditorBuffer, 22, 86);
            float lineY = textArea.top + 12.0f;
            for (const std::string& line : lines) {
                if (lineY > textArea.bottom - 14.0f) break;
                pushText(g_vertices, textArea.left + 10.0f, lineY, line.c_str(), {0.72f, 0.95f, 0.88f}, layout.screenWidth, layout.screenHeight);
                lineY += 14.0f;
            }
        }

        Rect editorPanelRect(const DawLaneTimelineSystemLogic::LaneLayout& layout) {
            float w = std::min(640.0f, static_cast<float>(layout.screenWidth) - 48.0f);
            float h = std::min(430.0f, static_cast<float>(layout.screenHeight) - 48.0f);
            float x = static_cast<float>((layout.screenWidth - w) * 0.5);
            float y = static_cast<float>((layout.screenHeight - h) * 0.5);
            return {x, y, x + w, y + h};
        }

        void editorButtons(const DawLaneTimelineSystemLogic::LaneLayout& layout, Rect& saveBtn, Rect& cancelBtn) {
            Rect panel = editorPanelRect(layout);
            saveBtn = {panel.right - 184.0f, panel.bottom - 38.0f, panel.right - 106.0f, panel.bottom - 14.0f};
            cancelBtn = {panel.right - 94.0f, panel.bottom - 38.0f, panel.right - 16.0f, panel.bottom - 14.0f};
        }

        bool insertTrackAt(BaseSystem& baseSystem, DawContext& daw, int laneIndex) {
            daw.chuckTracks.emplace_back();
            daw.chuckTrackCount = static_cast<int>(daw.chuckTracks.size());
            if (laneIndex < 0) laneIndex = static_cast<int>(daw.laneOrder.size());
            if (laneIndex > static_cast<int>(daw.laneOrder.size())) laneIndex = static_cast<int>(daw.laneOrder.size());
            daw.laneOrder.insert(daw.laneOrder.begin() + laneIndex, {kLaneTypeChuck, daw.chuckTrackCount - 1});
            if (daw.selectedLaneIndex >= laneIndex && daw.selectedLaneIndex >= 0) {
                daw.selectedLaneIndex += 1;
            }
            invalidateDawUi(baseSystem, daw);
            return true;
        }

        bool removeTrackAt(BaseSystem& baseSystem, DawContext& daw, int trackIndex) {
            int current = static_cast<int>(daw.chuckTracks.size());
            if (trackIndex < 0 || trackIndex >= current) return false;
            daw.chuckTracks.erase(daw.chuckTracks.begin() + trackIndex);
            daw.chuckTrackCount = static_cast<int>(daw.chuckTracks.size());

            int removedLane = -1;
            for (auto it = daw.laneOrder.begin(); it != daw.laneOrder.end(); ) {
                if (it->type == kLaneTypeChuck && it->trackIndex == trackIndex) {
                    if (removedLane < 0) removedLane = static_cast<int>(std::distance(daw.laneOrder.begin(), it));
                    it = daw.laneOrder.erase(it);
                } else {
                    if (it->type == kLaneTypeChuck && it->trackIndex > trackIndex) {
                        it->trackIndex -= 1;
                    }
                    ++it;
                }
            }
            if (removedLane >= 0) {
                if (daw.selectedLaneIndex == removedLane) {
                    daw.selectedLaneIndex = -1;
                    daw.selectedLaneType = -1;
                    daw.selectedLaneTrack = -1;
                } else if (daw.selectedLaneIndex > removedLane) {
                    daw.selectedLaneIndex -= 1;
                }
            }

            if (daw.selectedChuckTrack == trackIndex) {
                daw.selectedChuckTrack = -1;
                daw.selectedChuckEvent = -1;
            } else if (daw.selectedChuckTrack > trackIndex) {
                daw.selectedChuckTrack -= 1;
            }
            if (daw.chuckEditorTrack == trackIndex) {
                closeEditor(daw);
            } else if (daw.chuckEditorTrack > trackIndex) {
                daw.chuckEditorTrack -= 1;
            }
            if (daw.chuckEventDragTrack == trackIndex) {
                daw.chuckEventDragActive = false;
                daw.chuckEventDragTrack = -1;
                daw.chuckEventDragIndex = -1;
            } else if (daw.chuckEventDragTrack > trackIndex) {
                daw.chuckEventDragTrack -= 1;
            }
            invalidateDawUi(baseSystem, daw);
            return true;
        }

        void processPendingAction(BaseSystem& baseSystem, DawContext& daw, UIContext& ui) {
            if (!ui.active || ui.actionDelayFrames != 0 || ui.pendingActionType != "DawChuckTrack") return;
            int trackIndex = -1;
            if (!ui.pendingActionValue.empty()) {
                try {
                    trackIndex = std::stoi(ui.pendingActionValue);
                } catch (...) {
                    trackIndex = -1;
                }
            }
            if (ui.pendingActionKey == "add") {
                insertTrackAt(baseSystem, daw, static_cast<int>(daw.laneOrder.size()));
            } else if (trackIndex >= 0 && trackIndex < static_cast<int>(daw.chuckTracks.size())) {
                ChuckTrack& track = daw.chuckTracks[static_cast<size_t>(trackIndex)];
                if (ui.pendingActionKey == "clear") {
                    track.events.clear();
                    track.clearPending = false;
                    if (daw.selectedChuckTrack == trackIndex) {
                        daw.selectedChuckTrack = -1;
                        daw.selectedChuckEvent = -1;
                    }
                    daw.chuckStatusMessage = "CK lane cleared";
                } else if (ui.pendingActionKey == "mute") {
                    track.mute = !track.mute;
                }
            }
            ui.pendingActionType.clear();
            ui.pendingActionKey.clear();
            ui.pendingActionValue.clear();
            invalidateDawUi(baseSystem, daw);
        }

        void renderChuckLane(DawContext& daw,
                             WorldContext& world,
                             const DawLaneTimelineSystemLogic::LaneLayout& layout,
                             const std::vector<int>& chuckLaneIndex,
                             int laneCount) {
            glm::vec3 eventColor(0.04f, 0.40f, 0.36f);
            glm::vec3 eventTopColor(0.09f, 0.58f, 0.50f);
            glm::vec3 mutedColor(0.16f, 0.20f, 0.21f);
            glm::vec3 tickColor(0.62f, 1.0f, 0.90f);
            glm::vec3 selectedColor(0.45f, 0.72f, 1.0f);
            auto itSel = world.colorLibrary.find("MiraLaneSelected");
            if (itSel != world.colorLibrary.end()) selectedColor = itSel->second;

            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = layout.secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;
            float eventWidth = eventWidthForZoom(daw, layout.secondsPerScreen, layout.laneLeft, layout.laneRight);

            for (int trackIndex = 0; trackIndex < static_cast<int>(daw.chuckTracks.size()); ++trackIndex) {
                int laneIndex = chuckLaneIndex[static_cast<size_t>(trackIndex)];
                if (laneIndex < 0) continue;
                float centerY = layout.startY + static_cast<float>(laneIndex) * layout.rowSpan;
                float top = centerY - layout.laneHalfH + kEventVerticalInset;
                float bottom = centerY + layout.laneHalfH - kEventVerticalInset;
                float lipBottom = std::min(bottom, top + std::clamp((bottom - top) * 0.22f, 5.0f, 12.0f));
                ChuckTrack& track = daw.chuckTracks[static_cast<size_t>(trackIndex)];
                for (int eventIndex = 0; eventIndex < static_cast<int>(track.events.size()); ++eventIndex) {
                    ChuckEvent& event = track.events[static_cast<size_t>(eventIndex)];
                    uint64_t drawSample = event.startSample;
                    if (daw.chuckEventDragActive
                        && daw.chuckEventDragTrack == trackIndex
                        && daw.chuckEventDragIndex == eventIndex) {
                        drawSample = daw.chuckEventDragTargetSample;
                    }
                    float x = sampleToX(drawSample, offsetSamples, windowSamples, layout.laneLeft, layout.laneRight);
                    if (x < layout.laneLeft - eventWidth || x > layout.laneRight + eventWidth) continue;
                    Rect tick{x - 1.0f, top - 4.0f, x + 1.0f, bottom + 4.0f};
                    Rect eventRect{x - eventWidth * 0.5f, top, x + eventWidth * 0.5f, bottom};
                    eventRect.left = std::max(eventRect.left, layout.laneLeft);
                    eventRect.right = std::min(eventRect.right, layout.laneRight);
                    if (eventRect.right <= eventRect.left) continue;

                    bool selected = (daw.selectedChuckTrack == trackIndex && daw.selectedChuckEvent == event.eventId);
                    glm::vec3 body = track.mute ? mutedColor : eventColor;
                    glm::vec3 lip = track.mute ? glm::clamp(mutedColor + glm::vec3(0.05f), glm::vec3(0.0f), glm::vec3(1.0f)) : eventTopColor;
                    if (selected) {
                        body = glm::clamp(selectedColor * 0.68f, glm::vec3(0.0f), glm::vec3(1.0f));
                        lip = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
                    }
                    pushRect(g_vertices, tick, selected ? selectedColor : tickColor, layout.screenWidth, layout.screenHeight);
                    pushRect(g_vertices, eventRect, body, layout.screenWidth, layout.screenHeight);
                    pushRect(g_vertices, {eventRect.left, eventRect.top, eventRect.right, lipBottom}, lip, layout.screenWidth, layout.screenHeight);
                    pushText(g_vertices,
                             eventRect.left + 4.0f,
                             eventRect.top + 13.0f,
                             "CK",
                             {0.90f, 1.0f, 0.95f},
                             layout.screenWidth,
                             layout.screenHeight);
                }
            }

            if (daw.selectedLaneType == kLaneTypeChuck && daw.selectedLaneIndex >= 0 && daw.selectedLaneIndex < laneCount) {
                float centerY = layout.startY + static_cast<float>(daw.selectedLaneIndex) * layout.rowSpan;
                float handleSize = std::min(kTrackHandleSize, std::max(14.0f, layout.laneHeight));
                float handleHalf = handleSize * 0.5f;
                float centerX = layout.laneRight + kTrackHandleInset + handleHalf;
                Rect handle{centerX - handleHalf, centerY - handleHalf, centerX + handleHalf, centerY + handleHalf};
                pushRect(g_vertices, handle, selectedColor, layout.screenWidth, layout.screenHeight);
            }
        }

        void uploadAndDraw(RendererContext& renderer, WorldContext& world, IRenderBackend& renderBackend) {
            if (g_vertices.empty()) return;
            if (!renderer.uiColorShader) {
                renderer.uiColorShader = std::make_unique<Shader>(world.shaders["UI_COLOR_VERTEX_SHADER"].c_str(),
                                                                  world.shaders["UI_COLOR_FRAGMENT_SHADER"].c_str());
            }
            if (renderer.uiMidiLaneVAO == 0 || renderer.uiMidiLaneVBO == 0) {
                renderBackend.ensureVertexArray(renderer.uiMidiLaneVAO);
                renderBackend.ensureArrayBuffer(renderer.uiMidiLaneVBO);
                renderBackend.configureVertexArray(renderer.uiMidiLaneVAO, renderer.uiMidiLaneVBO, kUiVertexLayout, 0, {});
            }

            renderBackend.setDepthTestEnabled(false);
            renderBackend.setBlendEnabled(true);
            renderBackend.setBlendModeConstantAlpha(kLaneAlpha);
            renderBackend.bindVertexArray(renderer.uiMidiLaneVAO);
            renderBackend.uploadArrayBufferData(
                renderer.uiMidiLaneVBO,
                g_vertices.data(),
                g_vertices.size() * sizeof(UiVertex),
                true);
            renderer.uiColorShader->use();
            renderer.uiColorShader->setFloat("alpha", kLaneAlpha);
            renderBackend.drawArraysTriangles(0, static_cast<int>(g_vertices.size()));
            renderBackend.setBlendModeAlpha();
            renderBackend.setDepthTestEnabled(true);
        }
    }

    bool InsertTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.daw) return false;
        return insertTrackAt(baseSystem, *baseSystem.daw, trackIndex);
    }

    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.daw) return false;
        return removeTrackAt(baseSystem, *baseSystem.daw, trackIndex);
    }

    bool MoveTrack(BaseSystem& baseSystem, int fromIndex, int toIndex) {
        if (!baseSystem.daw) return false;
        DawContext& daw = *baseSystem.daw;
        int count = static_cast<int>(daw.laneOrder.size());
        if (fromIndex < 0 || fromIndex >= count) return false;
        toIndex = std::clamp(toIndex, 0, std::max(0, count - 1));
        if (fromIndex == toIndex) return false;
        DawContext::LaneEntry moved = daw.laneOrder[static_cast<size_t>(fromIndex)];
        daw.laneOrder.erase(daw.laneOrder.begin() + fromIndex);
        daw.laneOrder.insert(daw.laneOrder.begin() + toIndex, moved);
        invalidateDawUi(baseSystem, daw);
        return true;
    }

    void OnTimelineRebased(uint64_t) {}

    void UpdateChuckLane(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle win) {
        if (!baseSystem.daw) return;
        DawContext& daw = *baseSystem.daw;
        UIContext* uiPtr = baseSystem.ui.get();
        if (uiPtr) {
            processPendingAction(baseSystem, daw, *uiPtr);
        }

        schedulePlayback(baseSystem, daw);

        if (!baseSystem.ui || !baseSystem.renderer || !baseSystem.world || !baseSystem.level || !baseSystem.renderBackend || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (baseSystem.midi && baseSystem.midi->pianoRollActive) return;
        if (!DawLaneTimelineSystemLogic::hasDawUiWorld(*baseSystem.level)) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        auto& renderBackend = *baseSystem.renderBackend;
        const auto layout = DawLaneTimelineSystemLogic::ComputeLaneLayout(baseSystem, daw, win);
        const int chuckTrackCount = static_cast<int>(daw.chuckTracks.size());
        const int laneCount = layout.laneCount;
        std::vector<int> chuckLaneIndex = buildChuckLaneIndex(daw, chuckTrackCount);

        g_vertices.clear();

        Rect saveBtn;
        Rect cancelBtn;
        editorButtons(layout, saveBtn, cancelBtn);
        if (daw.chuckEventEditorOpen) {
            updateEditorInput(daw, ui, win, saveBtn, cancelBtn);
            renderEditor(daw, layout, saveBtn, cancelBtn);
            uploadAndDraw(renderer, world, renderBackend);
            g_rightMouseWasDown = PlatformInput::IsMouseButtonDown(win, PlatformInput::MouseButton::Right);
            return;
        }

        bool rightDown = PlatformInput::IsMouseButtonDown(win, PlatformInput::MouseButton::Right);
        bool rightPressed = rightDown && !g_rightMouseWasDown;
        bool allowInput = !daw.exportMenuOpen && !daw.settingsMenuOpen;

        EventHit hit;
        if (allowInput && chuckTrackCount > 0) {
            hit = findEventHit(daw,
                               ui,
                               chuckLaneIndex,
                               layout.laneLeft,
                               layout.laneRight,
                               layout.laneHalfH,
                               layout.rowSpan,
                               layout.startY,
                               layout.secondsPerScreen);
        }

        if (allowInput && rightPressed && hit.valid) {
            ChuckTrack& track = daw.chuckTracks[static_cast<size_t>(hit.track)];
            if (hit.eventIndex >= 0 && hit.eventIndex < static_cast<int>(track.events.size())) {
                track.events.erase(track.events.begin() + hit.eventIndex);
                daw.selectedChuckTrack = -1;
                daw.selectedChuckEvent = -1;
                daw.chuckStatusMessage = "CK event removed";
                ui.consumeClick = true;
                clearChuckSelection(daw);
            }
        }

        if (allowInput && ui.uiLeftPressed && hit.valid) {
            const double now = PlatformInput::GetTimeSeconds();
            bool doubleClick = (hit.track == g_lastEventClickTrack
                && hit.eventId == g_lastEventClickId
                && g_lastEventClickTime > 0.0
                && (now - g_lastEventClickTime) <= kDoubleClickSeconds);
            g_lastEventClickTrack = hit.track;
            g_lastEventClickId = hit.eventId;
            g_lastEventClickTime = now;

            clearChuckSelection(daw);
            daw.selectedChuckTrack = hit.track;
            daw.selectedChuckEvent = hit.eventId;
            daw.selectedLaneIndex = hit.laneIndex;
            daw.selectedLaneType = kLaneTypeChuck;
            daw.selectedLaneTrack = hit.track;
            ui.consumeClick = true;

            if (doubleClick) {
                openEditor(daw, hit.track, hit.eventIndex);
            } else {
                uint64_t cursorSample = sampleFromCursorX(baseSystem,
                                                          daw,
                                                          layout.laneLeft,
                                                          layout.laneRight,
                                                          layout.secondsPerScreen,
                                                          ui.cursorX,
                                                          false);
                const ChuckEvent& event = daw.chuckTracks[static_cast<size_t>(hit.track)].events[static_cast<size_t>(hit.eventIndex)];
                daw.chuckEventDragActive = true;
                daw.chuckEventDragTrack = hit.track;
                daw.chuckEventDragIndex = hit.eventIndex;
                daw.chuckEventDragStartSample = event.startSample;
                daw.chuckEventDragTargetSample = event.startSample;
                daw.chuckEventDragOffsetSamples = static_cast<int64_t>(event.startSample) - static_cast<int64_t>(cursorSample);
            }
        } else if (allowInput && ui.uiLeftPressed && chuckTrackCount > 0) {
            int trackIndex = laneTrackUnderCursor(daw,
                                                 chuckLaneIndex,
                                                 static_cast<float>(ui.cursorY),
                                                 layout.startY,
                                                 layout.laneHalfH,
                                                 layout.rowSpan);
            bool inTimeArea = (ui.cursorX >= layout.laneLeft && ui.cursorX <= layout.laneRight);
            if (trackIndex >= 0 && inTimeArea) {
                const double now = PlatformInput::GetTimeSeconds();
                bool doubleClick = (trackIndex == g_lastLaneClickTrack
                    && g_lastLaneClickTime > 0.0
                    && (now - g_lastLaneClickTime) <= kDoubleClickSeconds);
                g_lastLaneClickTrack = trackIndex;
                g_lastLaneClickTime = now;
                if (doubleClick) {
                    bool cmdDown = isCommandDown(win);
                    uint64_t sample = sampleFromCursorX(baseSystem,
                                                        daw,
                                                        layout.laneLeft,
                                                        layout.laneRight,
                                                        layout.secondsPerScreen,
                                                        ui.cursorX,
                                                        !cmdDown);
                    clearChuckSelection(daw);
                    ChuckTrack& track = daw.chuckTracks[static_cast<size_t>(trackIndex)];
                    ChuckEvent event{};
                    event.startSample = sample;
                    event.code = defaultChuckCode();
                    event.enabled = true;
                    event.eventId = track.nextEventId++;
                    track.events.push_back(std::move(event));
                    sortEvents(track);
                    int eventIndex = findEventIndexById(track, track.nextEventId - 1);
                    daw.selectedLaneIndex = chuckLaneIndex[static_cast<size_t>(trackIndex)];
                    daw.selectedLaneType = kLaneTypeChuck;
                    daw.selectedLaneTrack = trackIndex;
                    openEditor(daw, trackIndex, eventIndex);
                    ui.consumeClick = true;
                }
            }
        }

        if (daw.chuckEventDragActive) {
            if (!ui.uiLeftDown) {
                if (daw.chuckEventDragTrack >= 0
                    && daw.chuckEventDragTrack < static_cast<int>(daw.chuckTracks.size())) {
                    ChuckTrack& track = daw.chuckTracks[static_cast<size_t>(daw.chuckEventDragTrack)];
                    if (daw.chuckEventDragIndex >= 0
                        && daw.chuckEventDragIndex < static_cast<int>(track.events.size())) {
                        int eventId = track.events[static_cast<size_t>(daw.chuckEventDragIndex)].eventId;
                        track.events[static_cast<size_t>(daw.chuckEventDragIndex)].startSample = daw.chuckEventDragTargetSample;
                        sortEvents(track);
                        daw.selectedChuckTrack = daw.chuckEventDragTrack;
                        daw.selectedChuckEvent = eventId;
                    }
                }
                daw.chuckEventDragActive = false;
                daw.chuckEventDragTrack = -1;
                daw.chuckEventDragIndex = -1;
                daw.chuckEventDragOffsetSamples = 0;
            } else if (daw.chuckEventDragTrack >= 0
                       && daw.chuckEventDragTrack < static_cast<int>(daw.chuckTracks.size())) {
                bool cmdDown = isCommandDown(win);
                uint64_t cursorSample = sampleFromCursorX(baseSystem,
                                                          daw,
                                                          layout.laneLeft,
                                                          layout.laneRight,
                                                          layout.secondsPerScreen,
                                                          ui.cursorX,
                                                          !cmdDown);
                int64_t target = static_cast<int64_t>(cursorSample) + daw.chuckEventDragOffsetSamples;
                if (target < 0) target = 0;
                daw.chuckEventDragTargetSample = static_cast<uint64_t>(target);
                daw.timelineSelectionActive = false;
                daw.timelineSelectionDragActive = false;
                ui.consumeClick = true;
            }
        }

        if (chuckTrackCount > 0) {
            renderChuckLane(daw, world, layout, chuckLaneIndex, laneCount);
        }
        uploadAndDraw(renderer, world, renderBackend);
        g_rightMouseWasDown = rightDown;
    }
}
