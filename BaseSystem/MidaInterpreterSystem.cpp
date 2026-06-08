#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>

#include <jack/ringbuffer.h>

#include "stb_easy_font.h"

namespace Vst3SystemLogic {
    void EnsureMidiTrackCount(Vst3Context& ctx, int trackCount);
}
namespace MidiTrackSystemLogic {
    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex);
}
namespace DawLaneTimelineSystemLogic {
    struct LaneLayout;
    LaneLayout ComputeLaneLayout(const BaseSystem& baseSystem, const DawContext& daw, PlatformWindowHandle win);
    bool hasDawUiWorld(const LevelContext& level);
}

namespace MidaInterpreterSystemLogic {

    namespace {
        constexpr int kTicksPerQuarter = 4;
        constexpr int kMidaMidiVelocity = 100;
        constexpr float kMidaNormalizedVelocity = static_cast<float>(kMidaMidiVelocity) / 127.0f;
        constexpr const char* kDefaultMidaSource = "*C4~E4~G4 . . . | - - - -*";
        constexpr const char* kBackendTrackPrefix = "mida backend track ";
        constexpr float kLaneAlpha = 0.88f;
        constexpr float kTrackHandleSize = 60.0f;
        constexpr float kTrackHandleInset = 12.0f;
        constexpr float kTrackControlStripRight = 360.0f;
        constexpr float kDoubleClickSeconds = 0.35f;

        struct MidaNoteTick {
            int pitch = 0;
            uint64_t startTick = 0;
            uint64_t lengthTicks = 0;
        };

        struct MidaExpression {
            std::vector<MidaNoteTick> notes;
            uint64_t durationTicks = 0;
            bool valid = false;
        };

        struct TokenPosition {
            int line = 1;
            int column = 1;
        };

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

        static std::vector<UiVertex> g_vertices;
        static double g_lastLaneClickTime = -1.0;
        static int g_lastLaneClickTrack = -1;
        static std::unordered_map<int, uint8_t> g_keyDown;

        bool getRegistryBool(const BaseSystem& baseSystem, const std::string& key, bool fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<bool>(it->second)) return std::get<bool>(it->second);
            return fallback;
        }

        std::string getRegistryString(const BaseSystem& baseSystem, const std::string& key, const std::string& fallback = "") {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end()) return fallback;
            if (std::holds_alternative<std::string>(it->second)) return std::get<std::string>(it->second);
            return fallback;
        }

        bool isMenuLevel(const BaseSystem& baseSystem) {
            return getRegistryString(baseSystem, "level") == "menu";
        }

        bool readTextFile(const std::string& path, std::string& out) {
            if (path.empty()) return false;
            std::ifstream file(path);
            if (!file.is_open()) return false;
            std::ostringstream ss;
            ss << file.rdbuf();
            out = ss.str();
            return true;
        }

        std::string defaultTrackSource(BaseSystem& baseSystem) {
            std::string path = getRegistryString(baseSystem, "MidaSourcePath", "Procedures/mida/default.mida");
            std::string fileSource;
            if (readTextFile(path, fileSource) && !fileSource.empty()) return fileSource;
            return kDefaultMidaSource;
        }

        size_t hashSource(const std::string& source) {
            return std::hash<std::string>{}(source);
        }

        uint64_t addSaturating(uint64_t a, uint64_t b) {
            if (b > std::numeric_limits<uint64_t>::max() - a) return std::numeric_limits<uint64_t>::max();
            return a + b;
        }

        int pitchClassFor(char c) {
            switch (static_cast<char>(std::toupper(static_cast<unsigned char>(c)))) {
                case 'C': return 0;
                case 'D': return 2;
                case 'E': return 4;
                case 'F': return 5;
                case 'G': return 7;
                case 'A': return 9;
                case 'B': return 11;
                default: return -1;
            }
        }

        class Parser {
        public:
            explicit Parser(const std::string& source, std::vector<MidaDiagnostic>& diagnostics)
                : source_(source), diagnostics_(diagnostics) {}

            MidaExpression parse() {
                MidaExpression out = parseSequence('\0');
                skipWhitespace();
                if (pos_ < source_.size()) {
                    addDiagnostic("Unexpected trailing Mida text.");
                }
                out.valid = diagnostics_.empty();
                return out;
            }

        private:
            const std::string& source_;
            std::vector<MidaDiagnostic>& diagnostics_;
            size_t pos_ = 0;
            int line_ = 1;
            int column_ = 1;

            bool atEnd() const { return pos_ >= source_.size(); }
            char peek() const { return atEnd() ? '\0' : source_[pos_]; }
            bool startsWith(const char* value) const {
                size_t len = std::char_traits<char>::length(value);
                return pos_ + len <= source_.size() && source_.compare(pos_, len, value) == 0;
            }
            char advance() {
                if (atEnd()) return '\0';
                char c = source_[pos_++];
                if (c == '\n') {
                    line_ += 1;
                    column_ = 1;
                } else {
                    column_ += 1;
                }
                return c;
            }
            void advanceCount(size_t count) {
                for (size_t i = 0; i < count; ++i) advance();
            }
            void skipWhitespace() {
                while (!atEnd() && std::isspace(static_cast<unsigned char>(peek()))) advance();
            }
            void addDiagnostic(const std::string& message) {
                diagnostics_.push_back({line_, column_, message});
            }
            TokenPosition currentPosition() const { return {line_, column_}; }
            void addDiagnosticAt(const TokenPosition& pos, const std::string& message) {
                diagnostics_.push_back({pos.line, pos.column, message});
            }
            void appendWithOffset(MidaExpression& target, const MidaExpression& source, uint64_t offsetTicks) {
                target.notes.reserve(target.notes.size() + source.notes.size());
                for (const auto& note : source.notes) {
                    MidaNoteTick out = note;
                    out.startTick = addSaturating(out.startTick, offsetTicks);
                    target.notes.push_back(out);
                }
            }
            MidaExpression parseSequence(char stopChar) {
                MidaExpression out;
                out.valid = true;
                uint64_t cursorTicks = 0;

                while (true) {
                    skipWhitespace();
                    if (atEnd()) break;
                    if (stopChar != '\0' && peek() == stopChar) break;

                    if (startsWith("|||")) {
                        addDiagnostic("Fence operator needs an expression before it.");
                        advanceCount(3);
                        continue;
                    }
                    if (peek() == '~') {
                        addDiagnostic("Parallel operator needs an expression before it.");
                        advance();
                        continue;
                    }

                    MidaExpression first = parseFactor(stopChar);
                    if (!first.valid) continue;

                    std::vector<MidaExpression> group;
                    group.push_back(std::move(first));

                    while (true) {
                        skipWhitespace();
                        if (startsWith("|||")) {
                            advanceCount(3);
                            MidaExpression rhs = parseFactor(stopChar);
                            if (rhs.valid) group.push_back(std::move(rhs));
                            continue;
                        }
                        if (peek() == '~') {
                            advance();
                            MidaExpression rhs = parseFactor(stopChar);
                            if (rhs.valid) group.push_back(std::move(rhs));
                            continue;
                        }
                        break;
                    }

                    uint64_t groupDuration = 0;
                    for (const auto& expr : group) {
                        appendWithOffset(out, expr, cursorTicks);
                        groupDuration = std::max(groupDuration, expr.durationTicks);
                    }
                    cursorTicks = addSaturating(cursorTicks, groupDuration);
                }

                out.durationTicks = cursorTicks;
                return out;
            }
            MidaExpression parseFactor(char stopChar) {
                skipWhitespace();
                MidaExpression out;
                if (atEnd() || (stopChar != '\0' && peek() == stopChar)) return out;

                if (std::isdigit(static_cast<unsigned char>(peek()))) return parseLoop();
                if (peek() == '{') {
                    advance();
                    MidaExpression body = parseSequence('}');
                    if (peek() == '}') advance();
                    else addDiagnostic("Expected '}' to close Mida group.");
                    body.valid = true;
                    return body;
                }
                if (peek() == '*') return parseAudicle();

                TokenPosition pos = currentPosition();
                std::string token = readLooseToken();
                if (token.empty()) token = std::string(1, advance());
                addDiagnosticAt(pos, "Unexpected token '" + token + "' in Mida source.");
                return out;
            }
            std::string readLooseToken() {
                std::string token;
                while (!atEnd()) {
                    char c = peek();
                    if (std::isspace(static_cast<unsigned char>(c)) || c == '{' || c == '}' || c == '*') break;
                    if (startsWith("|||")) break;
                    if (c == '~') break;
                    token.push_back(advance());
                }
                return token;
            }
            MidaExpression parseLoop() {
                TokenPosition start = currentPosition();
                uint64_t repeatCount = 0;
                while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    int digit = advance() - '0';
                    if (repeatCount <= (std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(digit)) / 10ull) {
                        repeatCount = repeatCount * 10ull + static_cast<uint64_t>(digit);
                    }
                }
                skipWhitespace();
                if (peek() != '{') {
                    addDiagnosticAt(start, "Numeric Mida prefix must be followed by a loop group.");
                    return {};
                }
                advance();

                MidaExpression body = parseSequence('}');
                if (peek() == '}') advance();
                else addDiagnostic("Expected '}' to close Mida loop.");

                MidaExpression out;
                out.valid = true;
                if (repeatCount == 0 || body.durationTicks == 0) return out;

                uint64_t offset = 0;
                for (uint64_t i = 0; i < repeatCount; ++i) {
                    appendWithOffset(out, body, offset);
                    offset = addSaturating(offset, body.durationTicks);
                    if (offset == std::numeric_limits<uint64_t>::max()) break;
                }
                out.durationTicks = addSaturating(0, body.durationTicks * repeatCount);
                return out;
            }
            MidaExpression parseAudicle() {
                TokenPosition start = currentPosition();
                advance();
                std::string body;
                while (!atEnd() && peek() != '*') body.push_back(advance());
                if (peek() == '*') advance();
                else addDiagnosticAt(start, "Unclosed Mida audicle.");
                return compileAudicle(body, start);
            }
            std::vector<std::string> tokenizeAudicle(const std::string& body) const {
                std::vector<std::string> tokens;
                std::string current;
                for (char c : body) {
                    if (std::isspace(static_cast<unsigned char>(c)) || c == '|') {
                        if (!current.empty()) {
                            tokens.push_back(current);
                            current.clear();
                        }
                    } else {
                        current.push_back(c);
                    }
                }
                if (!current.empty()) tokens.push_back(current);
                return tokens;
            }
            bool parseNoteToken(const std::string& token, int& outPitch) {
                if (token.empty()) return false;
                int pc = pitchClassFor(token[0]);
                if (pc < 0) return false;
                size_t i = 1;
                if (i < token.size() && (token[i] == '#' || token[i] == 'b')) {
                    pc += token[i] == '#' ? 1 : -1;
                    pc = (pc + 12) % 12;
                    ++i;
                }
                if (i >= token.size()) return false;
                int sign = 1;
                if (token[i] == '-') {
                    sign = -1;
                    ++i;
                }
                if (i >= token.size() || !std::isdigit(static_cast<unsigned char>(token[i]))) return false;
                int octave = 0;
                while (i < token.size() && std::isdigit(static_cast<unsigned char>(token[i]))) {
                    octave = octave * 10 + (token[i] - '0');
                    ++i;
                }
                if (i != token.size()) return false;
                octave *= sign;
                int pitch = (octave + 1) * 12 + pc;
                if (pitch < 0 || pitch > 127) return false;
                outPitch = pitch;
                return true;
            }
            MidaExpression compileAudicle(const std::string& body, const TokenPosition& start) {
                MidaExpression out;
                out.valid = true;
                uint64_t cursorTicks = 0;
                std::vector<size_t> sustainIndices;
                std::vector<std::string> tokens = tokenizeAudicle(body);

                for (const std::string& token : tokens) {
                    if (token == "-") {
                        sustainIndices.clear();
                        cursorTicks = addSaturating(cursorTicks, 1);
                        continue;
                    }
                    if (token == ".") {
                        for (size_t idx : sustainIndices) {
                            if (idx < out.notes.size()) {
                                out.notes[idx].lengthTicks = addSaturating(out.notes[idx].lengthTicks, 1);
                            }
                        }
                        cursorTicks = addSaturating(cursorTicks, 1);
                        continue;
                    }

                    std::vector<int> pitches;
                    size_t partStart = 0;
                    bool ok = true;
                    while (partStart <= token.size()) {
                        size_t partEnd = token.find('~', partStart);
                        std::string part = token.substr(partStart, partEnd == std::string::npos ? std::string::npos : partEnd - partStart);
                        int pitch = 0;
                        if (!parseNoteToken(part, pitch)) {
                            ok = false;
                            break;
                        }
                        pitches.push_back(pitch);
                        if (partEnd == std::string::npos) break;
                        partStart = partEnd + 1;
                    }

                    sustainIndices.clear();
                    if (!ok || pitches.empty()) {
                        addDiagnosticAt(start, "Invalid Mida note token '" + token + "'.");
                        cursorTicks = addSaturating(cursorTicks, 1);
                        continue;
                    }

                    for (int pitch : pitches) {
                        MidaNoteTick note;
                        note.pitch = pitch;
                        note.startTick = cursorTicks;
                        note.lengthTicks = 1;
                        out.notes.push_back(note);
                        sustainIndices.push_back(out.notes.size() - 1);
                    }
                    cursorTicks = addSaturating(cursorTicks, 1);
                }

                out.durationTicks = cursorTicks;
                return out;
            }
        };

        std::string diagnosticSummary(const std::vector<MidaDiagnostic>& diagnostics);
        bool validateMidaMidiSubset(const std::string& source, std::vector<MidaDiagnostic>& diagnostics);

        MidiClip toMidiClip(const MidaExpression& expr, double bpm, float sampleRate) {
            if (bpm <= 0.0) bpm = 120.0;
            if (sampleRate <= 0.0f) sampleRate = 44100.0f;
            const double samplesPerTick = (static_cast<double>(sampleRate) * 60.0) / (bpm * static_cast<double>(kTicksPerQuarter));

            MidiClip clip;
            clip.startSample = 0;
            clip.length = static_cast<uint64_t>(std::max(0.0, std::round(static_cast<double>(expr.durationTicks) * samplesPerTick)));
            clip.notes.reserve(expr.notes.size());

            for (const auto& noteTick : expr.notes) {
                if (noteTick.lengthTicks == 0) continue;
                MidiNote note;
                note.pitch = noteTick.pitch;
                note.startSample = static_cast<uint64_t>(std::max(0.0, std::round(static_cast<double>(noteTick.startTick) * samplesPerTick)));
                note.length = static_cast<uint64_t>(std::max(1.0, std::round(static_cast<double>(noteTick.lengthTicks) * samplesPerTick)));
                note.velocity = kMidaNormalizedVelocity;
                clip.notes.push_back(note);
                clip.length = std::max(clip.length, note.startSample + note.length);
            }

            std::sort(clip.notes.begin(), clip.notes.end(), [](const MidiNote& a, const MidiNote& b) {
                if (a.startSample == b.startSample) return a.pitch < b.pitch;
                return a.startSample < b.startSample;
            });
            return clip;
        }

        std::string pitchToMidaName(int pitch) {
            static const std::array<const char*, 12> names = {
                "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
            };
            pitch = std::clamp(pitch, 0, 127);
            int pc = pitch % 12;
            int octave = (pitch / 12) - 1;
            return std::string(names[static_cast<size_t>(pc)]) + std::to_string(octave);
        }

        std::string tokensToAudicle(const std::vector<std::string>& tokens) {
            std::ostringstream out;
            out << "*";
            for (size_t i = 0; i < tokens.size(); ++i) {
                if (i > 0) out << " ";
                if (i > 0 && (i % 16) == 0) out << "| ";
                out << tokens[i];
            }
            out << "*";
            return out.str();
        }

        double samplesPerMidaTick(const BaseSystem& baseSystem) {
            double bpm = 120.0;
            double sampleRate = 44100.0;
            if (baseSystem.daw) {
                bpm = baseSystem.daw->bpm.load(std::memory_order_relaxed);
                if (baseSystem.daw->sampleRate > 0.0f) sampleRate = static_cast<double>(baseSystem.daw->sampleRate);
            }
            if (baseSystem.audio && baseSystem.audio->sampleRate > 0.0f) {
                sampleRate = static_cast<double>(baseSystem.audio->sampleRate);
            }
            if (baseSystem.midi && baseSystem.midi->sampleRate > 0.0f) {
                sampleRate = static_cast<double>(baseSystem.midi->sampleRate);
            }
            if (bpm <= 0.0) bpm = 120.0;
            if (sampleRate <= 0.0) sampleRate = 44100.0;
            return (sampleRate * 60.0) / (bpm * static_cast<double>(kTicksPerQuarter));
        }

        std::string midiClipToMidaSource(const BaseSystem& baseSystem, const MidiClip& clip) {
            double samplesPerTick = samplesPerMidaTick(baseSystem);
            if (samplesPerTick <= 0.0) samplesPerTick = 1.0;
            uint64_t durationTicks = static_cast<uint64_t>(std::max(1.0, std::ceil(static_cast<double>(clip.length) / samplesPerTick)));

            struct NoteGroupKey {
                uint64_t startTick = 0;
                uint64_t lengthTicks = 1;
                bool operator<(const NoteGroupKey& other) const {
                    if (startTick != other.startTick) return startTick < other.startTick;
                    return lengthTicks < other.lengthTicks;
                }
            };
            std::map<NoteGroupKey, std::vector<int>> groups;
            for (const MidiNote& note : clip.notes) {
                const long long roundedStartTick = std::llround(static_cast<double>(note.startSample) / samplesPerTick);
                const long long roundedLengthTicks = std::llround(static_cast<double>(note.length) / samplesPerTick);
                uint64_t startTick = static_cast<uint64_t>(std::max(0LL, roundedStartTick));
                uint64_t lengthTicks = static_cast<uint64_t>(std::max(1LL, roundedLengthTicks));
                durationTicks = std::max(durationTicks, startTick + lengthTicks);
                groups[{startTick, lengthTicks}].push_back(std::clamp(note.pitch, 0, 127));
            }

            if (groups.empty()) {
                std::vector<std::string> rests(static_cast<size_t>(durationTicks), "-");
                return tokensToAudicle(rests);
            }

            std::vector<std::string> audicles;
            for (auto& entry : groups) {
                auto& pitches = entry.second;
                std::sort(pitches.begin(), pitches.end());
                pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
                std::vector<std::string> tokens;
                tokens.reserve(static_cast<size_t>(entry.first.startTick + entry.first.lengthTicks));
                for (uint64_t i = 0; i < entry.first.startTick; ++i) {
                    tokens.push_back("-");
                }
                std::ostringstream chord;
                for (size_t i = 0; i < pitches.size(); ++i) {
                    if (i > 0) chord << "~";
                    chord << pitchToMidaName(pitches[i]);
                }
                tokens.push_back(chord.str());
                for (uint64_t i = 1; i < entry.first.lengthTicks; ++i) {
                    tokens.push_back(".");
                }
                audicles.push_back(tokensToAudicle(tokens));
            }

            std::ostringstream out;
            for (size_t i = 0; i < audicles.size(); ++i) {
                if (i > 0) out << "~";
                out << audicles[i];
            }
            return out.str();
        }

        bool compileMidaSourceToMidiClip(BaseSystem& baseSystem,
                                         const std::string& source,
                                         MidiClip& clip,
                                         bool requireMidiSubset,
                                         std::string* errorMessage) {
            std::vector<MidaDiagnostic> diagnostics;
            if (requireMidiSubset && !validateMidaMidiSubset(source, diagnostics)) {
                if (errorMessage) *errorMessage = diagnosticSummary(diagnostics);
                return false;
            }
            Parser parser(source, diagnostics);
            MidaExpression expr = parser.parse();
            if (!diagnostics.empty()) {
                if (errorMessage) *errorMessage = diagnosticSummary(diagnostics);
                return false;
            }

            double bpm = 120.0;
            float sampleRate = 44100.0f;
            if (baseSystem.daw) {
                bpm = baseSystem.daw->bpm.load(std::memory_order_relaxed);
                if (baseSystem.daw->sampleRate > 0.0f) sampleRate = baseSystem.daw->sampleRate;
            }
            if (baseSystem.audio && baseSystem.audio->sampleRate > 0.0f) sampleRate = baseSystem.audio->sampleRate;
            if (baseSystem.midi && baseSystem.midi->sampleRate > 0.0f) sampleRate = baseSystem.midi->sampleRate;
            MidiClip compiled = toMidiClip(expr, bpm, sampleRate);
            clip.notes = std::move(compiled.notes);
            if (compiled.length > 0 || !clip.notes.empty()) {
                clip.length = std::max<uint64_t>(1, compiled.length);
            }
            clip.hasMidaSource = true;
            clip.midaSource = source;
            clip.midaSourceDirty = false;
            return true;
        }

        void invalidateDawUi(BaseSystem& baseSystem) {
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            if (baseSystem.daw) baseSystem.daw->uiCacheBuilt = false;
            if (baseSystem.midi) baseSystem.midi->uiCacheBuilt = false;
        }

        void initBackendMidiTrack(MidiTrack& track, const MidiContext& midi) {
            track.outputBus.store(2, std::memory_order_relaxed);
            track.outputBusL.store(2, std::memory_order_relaxed);
            track.outputBusR.store(2, std::memory_order_relaxed);
            track.physicalInputIndex = 0;
            track.armMode.store(0, std::memory_order_relaxed);
            track.recordEnabled.store(false, std::memory_order_relaxed);
            if (!track.recordRing) {
                size_t ringBytes = static_cast<size_t>(std::max(1.0f, midi.sampleRate)) * 2u * sizeof(float);
                track.recordRing = jack_ringbuffer_create(ringBytes);
                if (track.recordRing) jack_ringbuffer_mlock(track.recordRing);
            }
        }

        std::string backendTrackName(int midaTrackIndex) {
            return std::string(kBackendTrackPrefix) + std::to_string(midaTrackIndex + 1);
        }

        bool validBackendTrack(const MidaTrack& track, const MidiContext& midi, int midaTrackIndex) {
            int idx = track.midiTrackIndex;
            if (idx < 0 || idx >= static_cast<int>(midi.tracks.size())) return false;
            if (idx >= static_cast<int>(midi.trackNames.size())) return false;
            return midi.trackNames[static_cast<size_t>(idx)] == backendTrackName(midaTrackIndex);
        }

        int ensureBackendMidiTrack(BaseSystem& baseSystem, MidaTrack& track, int midaTrackIndex) {
            if (!baseSystem.midi || !baseSystem.audio) return -1;
            MidiContext& midi = *baseSystem.midi;
            midi.sampleRate = baseSystem.audio->sampleRate > 0.0f ? baseSystem.audio->sampleRate : midi.sampleRate;
            if (midi.trackNames.size() < midi.tracks.size()) midi.trackNames.resize(midi.tracks.size());
            if (midi.trackVisible.size() < midi.tracks.size()) midi.trackVisible.resize(midi.tracks.size(), true);

            if (validBackendTrack(track, midi, midaTrackIndex)) {
                midi.trackVisible[static_cast<size_t>(track.midiTrackIndex)] = false;
                return track.midiTrackIndex;
            }

            std::string desiredName = backendTrackName(midaTrackIndex);
            for (int i = 0; i < static_cast<int>(midi.trackNames.size()) && i < static_cast<int>(midi.tracks.size()); ++i) {
                if (midi.trackNames[static_cast<size_t>(i)] == desiredName) {
                    track.midiTrackIndex = i;
                    midi.trackVisible[static_cast<size_t>(i)] = false;
                    return i;
                }
            }

            std::lock_guard<std::mutex> lock(midi.trackMutex);
            int index = static_cast<int>(midi.tracks.size());
            midi.tracks.emplace_back();
            midi.trackNames.resize(midi.tracks.size());
            midi.trackVisible.resize(midi.tracks.size(), true);
            initBackendMidiTrack(midi.tracks.back(), midi);
            midi.trackNames[static_cast<size_t>(index)] = desiredName;
            midi.trackVisible[static_cast<size_t>(index)] = false;
            midi.trackCount = static_cast<int>(midi.tracks.size());
            track.midiTrackIndex = index;
            if (baseSystem.vst3) {
                Vst3SystemLogic::EnsureMidiTrackCount(*baseSystem.vst3, midi.trackCount);
            }
            invalidateDawUi(baseSystem);
            std::cout << "MidaInterpreter: created hidden backend MIDI track " << (index + 1)
                      << " for Mida track " << (midaTrackIndex + 1) << std::endl;
            return index;
        }

        std::string diagnosticSummary(const std::vector<MidaDiagnostic>& diagnostics) {
            if (diagnostics.empty()) return "";
            const MidaDiagnostic& d = diagnostics.front();
            return std::to_string(d.line) + ":" + std::to_string(d.column) + ": " + d.message;
        }

        bool validateMidaMidiSubset(const std::string& source, std::vector<MidaDiagnostic>& diagnostics) {
            bool inAudicle = false;
            int line = 1;
            int column = 1;
            auto advancePosition = [&](char c) {
                if (c == '\n') {
                    line += 1;
                    column = 1;
                } else {
                    column += 1;
                }
            };

            for (size_t i = 0; i < source.size(); ++i) {
                char c = source[i];
                if (c == '*') {
                    inAudicle = !inAudicle;
                    advancePosition(c);
                    continue;
                }
                if (!inAudicle && i + 3 <= source.size() && source.compare(i, 3, "|||") == 0) {
                    diagnostics.push_back({line, column, "Mida MIDI tracks cannot use fences; use a Mida track for full Mida syntax."});
                    return false;
                }
                if (!inAudicle && std::isdigit(static_cast<unsigned char>(c))) {
                    size_t j = i;
                    while (j < source.size() && std::isdigit(static_cast<unsigned char>(source[j]))) {
                        ++j;
                    }
                    while (j < source.size() && std::isspace(static_cast<unsigned char>(source[j]))) {
                        ++j;
                    }
                    if (j < source.size() && source[j] == '{') {
                        diagnostics.push_back({line, column, "Mida MIDI tracks cannot use loops; use a Mida track for full Mida syntax."});
                        return false;
                    }
                }
                advancePosition(c);
            }
            return true;
        }

        void clearBackendClip(BaseSystem& baseSystem, MidaTrack& track) {
            if (!baseSystem.midi) return;
            MidiContext& midi = *baseSystem.midi;
            int idx = track.midiTrackIndex;
            if (idx < 0 || idx >= static_cast<int>(midi.tracks.size())) return;
            MidiTrack& backend = midi.tracks[static_cast<size_t>(idx)];
            backend.clips.clear();
            backend.loopTakeClips.clear();
            backend.activeLoopTakeIndex = -1;
            backend.takeStackExpanded = false;
            backend.waveformVersion += 1;
        }

        void compileTrack(BaseSystem& baseSystem, MidaContext& mida, MidaTrack& track, int trackIndex) {
            if (!baseSystem.midi || !baseSystem.daw || !baseSystem.audio) return;
            MidiContext& midi = *baseSystem.midi;
            DawContext& daw = *baseSystem.daw;
            if (!midi.initialized) return;

            double bpm = daw.bpm.load(std::memory_order_relaxed);
            float sampleRate = midi.sampleRate > 0.0f ? midi.sampleRate : 44100.0f;
            size_t sourceHash = hashSource(track.source);
            bool backendMissingClip = track.midiTrackIndex >= 0
                && track.midiTrackIndex < static_cast<int>(midi.tracks.size())
                && midi.tracks[static_cast<size_t>(track.midiTrackIndex)].clips.empty()
                && !track.source.empty();
            bool needsCompile = backendMissingClip
                || sourceHash != track.sourceHash
                || std::abs(track.compiledBpm - bpm) > 0.0001
                || std::abs(track.compiledSampleRate - sampleRate) > 0.01f;
            if (!needsCompile) {
                if (track.midiTrackIndex >= 0 && track.midiTrackIndex < static_cast<int>(midi.tracks.size())) {
                    midi.tracks[static_cast<size_t>(track.midiTrackIndex)].mute.store(track.mute, std::memory_order_relaxed);
                }
                return;
            }

            track.sourceHash = sourceHash;
            track.compiledBpm = bpm;
            track.compiledSampleRate = sampleRate;
            track.diagnostics.clear();

            if (track.source.empty()) {
                clearBackendClip(baseSystem, track);
                track.compiledTickLength = 0;
                track.compiledNoteCount = 0;
                track.compileOk = true;
                mida.statusMessage = "Mida lane cleared";
                invalidateDawUi(baseSystem);
                return;
            }

            if (track.midiEditable && !validateMidaMidiSubset(track.source, track.diagnostics)) {
                clearBackendClip(baseSystem, track);
                track.compiledTickLength = 0;
                track.compiledNoteCount = 0;
                track.compileOk = false;
                mida.statusMessage = "Mida MIDI error " + diagnosticSummary(track.diagnostics);
                std::cout << "MidaInterpreter: Mida MIDI subset error on track " << (trackIndex + 1)
                          << ": " << diagnosticSummary(track.diagnostics) << std::endl;
                invalidateDawUi(baseSystem);
                return;
            }

            Parser parser(track.source, track.diagnostics);
            MidaExpression expr = parser.parse();
            track.compileOk = track.diagnostics.empty();
            if (!track.compileOk) {
                clearBackendClip(baseSystem, track);
                mida.statusMessage = "Mida error " + diagnosticSummary(track.diagnostics);
                std::cout << "MidaInterpreter: parse error on Mida track " << (trackIndex + 1)
                          << ": " << diagnosticSummary(track.diagnostics) << std::endl;
                invalidateDawUi(baseSystem);
                return;
            }

            int backendIndex = ensureBackendMidiTrack(baseSystem, track, trackIndex);
            if (backendIndex < 0 || backendIndex >= static_cast<int>(midi.tracks.size())) return;

            MidiClip clip = toMidiClip(expr, bpm, sampleRate);
            track.compiledTickLength = expr.durationTicks;
            track.compiledNoteCount = clip.notes.size();
            track.compileOk = true;

            MidiTrack& backend = midi.tracks[static_cast<size_t>(backendIndex)];
            backend.clips.clear();
            backend.loopTakeClips.clear();
            backend.activeLoopTakeIndex = -1;
            backend.takeStackExpanded = false;
            backend.nextTakeId = 1;
            backend.mute.store(track.mute, std::memory_order_relaxed);
            if (clip.length > 0 || !clip.notes.empty()) backend.clips.push_back(std::move(clip));
            backend.waveformVersion += 1;
            if (backendIndex < static_cast<int>(midi.trackVisible.size())) {
                midi.trackVisible[static_cast<size_t>(backendIndex)] = false;
            }

            mida.statusMessage = "Mida compiled " + std::to_string(track.compiledNoteCount) + " notes";
            if (baseSystem.registry) {
                (*baseSystem.registry)["MidaLastStatus"] = "ok";
                (*baseSystem.registry)["MidaLastError"] = "";
                (*baseSystem.registry)["MidaCompiledNotes"] = std::to_string(track.compiledNoteCount);
                (*baseSystem.registry)["MidaCompiledTicks"] = std::to_string(track.compiledTickLength);
            }
            invalidateDawUi(baseSystem);
            std::cout << "MidaInterpreter: compiled Mida track " << (trackIndex + 1)
                      << " to hidden MIDI track " << (backendIndex + 1)
                      << " with " << track.compiledNoteCount << " notes" << std::endl;
        }

        bool keyPressed(PlatformWindowHandle win, PlatformInput::Key key) {
            if (!win) return false;
            const int id = static_cast<int>(key);
            bool down = PlatformInput::IsKeyDown(win, key);
            bool pressed = down && (g_keyDown[id] == 0u);
            g_keyDown[id] = down ? 1u : 0u;
            return pressed;
        }

        bool isCommandDown(PlatformWindowHandle win) {
            if (!win) return false;
            return PlatformInput::IsKeyDown(win, PlatformInput::Key::LeftSuper)
                || PlatformInput::IsKeyDown(win, PlatformInput::Key::RightSuper);
        }

        bool isShiftDown(PlatformWindowHandle win) {
            if (!win) return false;
            return PlatformInput::IsKeyDown(win, PlatformInput::Key::LeftShift)
                || PlatformInput::IsKeyDown(win, PlatformInput::Key::RightShift);
        }

        bool contains(const Rect& r, double x, double y) {
            return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
        }

        void openEditor(MidaContext& mida, int trackIndex) {
            if (trackIndex < 0 || trackIndex >= static_cast<int>(mida.tracks.size())) return;
            mida.editorOpen = true;
            mida.editorTrack = trackIndex;
            mida.editorMidiTrack = -1;
            mida.editorMidiClip = -1;
            mida.editorBuffer = mida.tracks[static_cast<size_t>(trackIndex)].source;
            mida.selectedTrack = trackIndex;
        }

        void closeEditor(MidaContext& mida) {
            mida.editorOpen = false;
            mida.editorTrack = -1;
            mida.editorMidiTrack = -1;
            mida.editorMidiClip = -1;
            mida.editorBuffer.clear();
        }

        void saveEditor(BaseSystem& baseSystem, MidaContext& mida) {
            if (mida.editorMidiTrack >= 0 && mida.editorMidiClip >= 0 && baseSystem.midi
                && mida.editorMidiTrack < static_cast<int>(baseSystem.midi->tracks.size())) {
                MidiTrack& midiTrack = baseSystem.midi->tracks[static_cast<size_t>(mida.editorMidiTrack)];
                if (mida.editorMidiClip < static_cast<int>(midiTrack.clips.size())) {
                    MidiClip& clip = midiTrack.clips[static_cast<size_t>(mida.editorMidiClip)];
                    std::string error;
                    if (compileMidaSourceToMidiClip(baseSystem, mida.editorBuffer, clip, true, &error)) {
                        midiTrack.waveformVersion += 1;
                        mida.statusMessage = "Mida MIDI clip saved";
                    } else {
                        mida.statusMessage = "Mida MIDI error " + error;
                        return;
                    }
                }
            } else if (mida.editorTrack >= 0 && mida.editorTrack < static_cast<int>(mida.tracks.size())) {
                MidaTrack& track = mida.tracks[static_cast<size_t>(mida.editorTrack)];
                track.source = mida.editorBuffer;
                track.sourceHash = 0;
                mida.statusMessage = "Mida source saved";
                mida.selectedTrack = mida.editorTrack;
            }
            closeEditor(mida);
            invalidateDawUi(baseSystem);
        }

        void appendTypedChar(std::string& buffer, char ch) {
            if (ch != '\0') buffer.push_back(ch);
        }

        void updateEditorInput(BaseSystem& baseSystem, MidaContext& mida, UIContext& ui, PlatformWindowHandle win, const Rect& saveBtn, const Rect& cancelBtn) {
            if (!mida.editorOpen) return;
            ui.consumeClick = true;
            if (ui.uiLeftPressed) {
                if (contains(saveBtn, ui.cursorX, ui.cursorY)) {
                    saveEditor(baseSystem, mida);
                    return;
                }
                if (contains(cancelBtn, ui.cursorX, ui.cursorY)) {
                    closeEditor(mida);
                    return;
                }
            }

            bool cmd = isCommandDown(win);
            bool shift = isShiftDown(win);
            if (keyPressed(win, PlatformInput::Key::Escape)) {
                closeEditor(mida);
                return;
            }
            if (keyPressed(win, PlatformInput::Key::Backspace)) {
                if (!mida.editorBuffer.empty()) mida.editorBuffer.pop_back();
            }
            if (keyPressed(win, PlatformInput::Key::Enter) || keyPressed(win, PlatformInput::Key::KpEnter)) {
                if (cmd) {
                    saveEditor(baseSystem, mida);
                    return;
                }
                mida.editorBuffer.push_back('\n');
            }
            if (cmd && keyPressed(win, PlatformInput::Key::V)) {
                const char* clip = PlatformInput::GetClipboardText(win);
                if (clip) mida.editorBuffer += clip;
            }
            if (cmd && keyPressed(win, PlatformInput::Key::S)) {
                saveEditor(baseSystem, mida);
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
                    appendTypedChar(mida.editorBuffer, ch);
                }
            }
            static const std::array<std::pair<PlatformInput::Key, char>, 10> digits = {{
                {PlatformInput::Key::Num0, '0'}, {PlatformInput::Key::Num1, '1'}, {PlatformInput::Key::Num2, '2'},
                {PlatformInput::Key::Num3, '3'}, {PlatformInput::Key::Num4, '4'}, {PlatformInput::Key::Num5, '5'},
                {PlatformInput::Key::Num6, '6'}, {PlatformInput::Key::Num7, '7'}, {PlatformInput::Key::Num8, '8'},
                {PlatformInput::Key::Num9, '9'}
            }};
            for (const auto& entry : digits) {
                if (keyPressed(win, entry.first)) appendTypedChar(mida.editorBuffer, entry.second);
            }
            if (keyPressed(win, PlatformInput::Key::Space)) appendTypedChar(mida.editorBuffer, ' ');
            if (keyPressed(win, PlatformInput::Key::Minus)) appendTypedChar(mida.editorBuffer, shift ? '_' : '-');
            if (keyPressed(win, PlatformInput::Key::Period)) appendTypedChar(mida.editorBuffer, '.');
            if (keyPressed(win, PlatformInput::Key::Equal)) appendTypedChar(mida.editorBuffer, shift ? '+' : '=');
            if (keyPressed(win, PlatformInput::Key::Num8) && shift) appendTypedChar(mida.editorBuffer, '*');
            if (keyPressed(win, PlatformInput::Key::Key8) && shift) appendTypedChar(mida.editorBuffer, '*');
            if (keyPressed(win, PlatformInput::Key::Backslash)) appendTypedChar(mida.editorBuffer, shift ? '|' : '\\');
            if (keyPressed(win, PlatformInput::Key::GraveAccent)) appendTypedChar(mida.editorBuffer, shift ? '~' : '`');
            if (keyPressed(win, PlatformInput::Key::LeftBracket)) appendTypedChar(mida.editorBuffer, shift ? '{' : '[');
            if (keyPressed(win, PlatformInput::Key::RightBracket)) appendTypedChar(mida.editorBuffer, shift ? '}' : ']');
        }

        glm::vec2 pixelToNDC(const glm::vec2& p, double width, double height) {
            return {
                static_cast<float>((p.x / width) * 2.0 - 1.0),
                static_cast<float>(1.0 - (p.y / height) * 2.0)
            };
        }

        void pushQuad(std::vector<UiVertex>& verts, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, const glm::vec2& d, const glm::vec3& color) {
            verts.push_back({a, color});
            verts.push_back({b, color});
            verts.push_back({c, color});
            verts.push_back({a, color});
            verts.push_back({c, color});
            verts.push_back({d, color});
        }

        void pushRect(std::vector<UiVertex>& verts, const Rect& r, const glm::vec3& color, double width, double height) {
            glm::vec2 a = pixelToNDC({r.left, r.top}, width, height);
            glm::vec2 b = pixelToNDC({r.right, r.top}, width, height);
            glm::vec2 c = pixelToNDC({r.right, r.bottom}, width, height);
            glm::vec2 d = pixelToNDC({r.left, r.bottom}, width, height);
            pushQuad(verts, a, b, c, d, color);
        }

        void pushText(std::vector<UiVertex>& verts, float x, float y, const char* text, const glm::vec3& color, double width, double height) {
            if (!text || text[0] == '\0') return;
            char buffer[99999];
            int numQuads = stb_easy_font_print(x, y, const_cast<char*>(text), nullptr, buffer, sizeof(buffer));
            float* raw = reinterpret_cast<float*>(buffer);
            for (int i = 0; i < numQuads; ++i) {
                int base = i * 16;
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

        std::vector<std::string> visibleLines(const std::string& text, int maxLines, size_t maxChars) {
            std::vector<std::string> lines;
            std::stringstream ss(text);
            std::string line;
            while (static_cast<int>(lines.size()) < maxLines && std::getline(ss, line)) {
                if (line.size() > maxChars) line = line.substr(0, maxChars - 3) + "...";
                lines.push_back(line);
            }
            if (lines.empty()) lines.push_back("");
            return lines;
        }

        Rect editorPanelRect(const DawLaneTimelineSystemLogic::LaneLayout& layout) {
            float w = std::min(760.0f, static_cast<float>(layout.screenWidth) - 48.0f);
            float h = std::min(460.0f, static_cast<float>(layout.screenHeight) - 48.0f);
            float x = static_cast<float>((layout.screenWidth - w) * 0.5);
            float y = static_cast<float>((layout.screenHeight - h) * 0.5);
            return {x, y, x + w, y + h};
        }

        void editorButtons(const DawLaneTimelineSystemLogic::LaneLayout& layout, Rect& saveBtn, Rect& cancelBtn) {
            Rect panel = editorPanelRect(layout);
            saveBtn = {panel.right - 184.0f, panel.bottom - 38.0f, panel.right - 106.0f, panel.bottom - 14.0f};
            cancelBtn = {panel.right - 94.0f, panel.bottom - 38.0f, panel.right - 16.0f, panel.bottom - 14.0f};
        }

        void renderEditor(MidaContext& mida, const DawLaneTimelineSystemLogic::LaneLayout& layout, const Rect& saveBtn, const Rect& cancelBtn) {
            if (!mida.editorOpen) return;
            Rect panel = editorPanelRect(layout);
            Rect textArea{panel.left + 18.0f, panel.top + 54.0f, panel.right - 18.0f, panel.bottom - 58.0f};
            pushRect(g_vertices, {0.0f, 0.0f, static_cast<float>(layout.screenWidth), static_cast<float>(layout.screenHeight)}, {0.02f, 0.02f, 0.025f}, layout.screenWidth, layout.screenHeight);
            pushRect(g_vertices, panel, {0.08f, 0.11f, 0.10f}, layout.screenWidth, layout.screenHeight);
            pushRect(g_vertices, {panel.left, panel.top, panel.right, panel.top + 34.0f}, {0.10f, 0.36f, 0.24f}, layout.screenWidth, layout.screenHeight);
            pushRect(g_vertices, textArea, {0.015f, 0.02f, 0.018f}, layout.screenWidth, layout.screenHeight);
            pushRect(g_vertices, saveBtn, {0.08f, 0.42f, 0.26f}, layout.screenWidth, layout.screenHeight);
            pushRect(g_vertices, cancelBtn, {0.26f, 0.18f, 0.18f}, layout.screenWidth, layout.screenHeight);

            const char* title = mida.editorMidiTrack >= 0 ? "MIDA MIDI CLIP SOURCE" : "MIDA SOURCE";
            pushText(g_vertices, panel.left + 18.0f, panel.top + 12.0f, title, {0.92f, 0.96f, 0.92f}, layout.screenWidth, layout.screenHeight);
            pushText(g_vertices, saveBtn.left + 18.0f, saveBtn.top + 7.0f, "SAVE", {0.92f, 0.96f, 0.94f}, layout.screenWidth, layout.screenHeight);
            pushText(g_vertices, cancelBtn.left + 12.0f, cancelBtn.top + 7.0f, "CANCEL", {0.92f, 0.86f, 0.86f}, layout.screenWidth, layout.screenHeight);

            auto lines = visibleLines(mida.editorBuffer, 24, 102);
            float y = textArea.top + 12.0f;
            for (const std::string& line : lines) {
                if (y > textArea.bottom - 14.0f) break;
                pushText(g_vertices, textArea.left + 10.0f, y, line.c_str(), {0.74f, 0.95f, 0.80f}, layout.screenWidth, layout.screenHeight);
                y += 14.0f;
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
            renderBackend.uploadArrayBufferData(renderer.uiMidiLaneVBO,
                                                g_vertices.data(),
                                                g_vertices.size() * sizeof(UiVertex),
                                                true);
            renderer.uiColorShader->use();
            renderer.uiColorShader->setFloat("alpha", kLaneAlpha);
            renderBackend.drawArraysTriangles(0, static_cast<int>(g_vertices.size()));
            renderBackend.setBlendModeAlpha();
            renderBackend.setDepthTestEnabled(true);
        }

        float sampleToX(uint64_t sample, double offsetSamples, double windowSamples, float laneLeft, float laneRight) {
            if (windowSamples <= 0.0) return laneLeft;
            double t = (static_cast<double>(sample) - offsetSamples) / windowSamples;
            return laneLeft + static_cast<float>(t) * (laneRight - laneLeft);
        }

        std::vector<int> buildMidaLaneIndex(const DawContext& daw, int midaTrackCount) {
            std::vector<int> out(static_cast<size_t>(std::max(0, midaTrackCount)), -1);
            for (int laneIndex = 0; laneIndex < static_cast<int>(daw.laneOrder.size()); ++laneIndex) {
                const auto& entry = daw.laneOrder[static_cast<size_t>(laneIndex)];
                if (entry.type == DawContext::kLaneMida && entry.trackIndex >= 0 && entry.trackIndex < midaTrackCount) {
                    out[static_cast<size_t>(entry.trackIndex)] = laneIndex;
                }
            }
            return out;
        }

        int midaLaneUnderCursor(const std::vector<int>& midaLaneIndex, float cursorY, float startY, float laneHalfH, float rowSpan) {
            for (int trackIndex = 0; trackIndex < static_cast<int>(midaLaneIndex.size()); ++trackIndex) {
                int laneIndex = midaLaneIndex[static_cast<size_t>(trackIndex)];
                if (laneIndex < 0) continue;
                float centerY = startY + static_cast<float>(laneIndex) * rowSpan;
                if (cursorY >= centerY - laneHalfH && cursorY <= centerY + laneHalfH) return trackIndex;
            }
            return -1;
        }

        void renderMidaLanes(BaseSystem& baseSystem,
                             MidaContext& mida,
                             DawContext& daw,
                             const DawLaneTimelineSystemLogic::LaneLayout& layout,
                             const std::vector<int>& midaLaneIndex) {
            if (!baseSystem.midi) return;
            MidiContext& midi = *baseSystem.midi;
            double offsetSamples = static_cast<double>(daw.timelineOffsetSamples);
            double windowSamples = layout.secondsPerScreen * static_cast<double>(daw.sampleRate);
            if (windowSamples <= 0.0) windowSamples = 1.0;
            glm::vec3 bodyColor(0.08f, 0.36f, 0.24f);
            glm::vec3 topColor(0.14f, 0.55f, 0.34f);
            glm::vec3 mutedColor(0.15f, 0.19f, 0.17f);
            glm::vec3 selectedColor(0.45f, 0.72f, 1.0f);
            glm::vec3 laneColor(0.14f, 0.14f, 0.14f);
            glm::vec3 laneHighlight(0.20f, 0.20f, 0.20f);
            glm::vec3 laneShadow(0.08f, 0.08f, 0.08f);
            if (baseSystem.world) {
                auto itBase = baseSystem.world->colorLibrary.find("MiraLaneBase");
                if (itBase != baseSystem.world->colorLibrary.end()) {
                    laneColor = itBase->second;
                    auto itHighlight = baseSystem.world->colorLibrary.find("MiraLaneHighlight");
                    if (itHighlight != baseSystem.world->colorLibrary.end()) laneHighlight = itHighlight->second;
                    else laneHighlight = glm::clamp(laneColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
                    auto itShadow = baseSystem.world->colorLibrary.find("MiraLaneShadow");
                    if (itShadow != baseSystem.world->colorLibrary.end()) laneShadow = itShadow->second;
                    else laneShadow = glm::clamp(laneColor - glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
                }
                auto itSel = baseSystem.world->colorLibrary.find("MiraLaneSelected");
                if (itSel != baseSystem.world->colorLibrary.end()) selectedColor = itSel->second;
            }

            for (int trackIndex = 0; trackIndex < static_cast<int>(mida.tracks.size()); ++trackIndex) {
                int laneIndex = midaLaneIndex[static_cast<size_t>(trackIndex)];
                if (laneIndex < 0) continue;
                const MidaTrack& track = mida.tracks[static_cast<size_t>(trackIndex)];
                float centerY = layout.startY + static_cast<float>(laneIndex) * layout.rowSpan;
                bool selected = (mida.selectedTrack == trackIndex);
                float laneTop = centerY - layout.laneHalfH;
                float laneBottom = centerY + layout.laneHalfH;
                glm::vec3 laneBody = track.mute ? mutedColor : laneColor;
                glm::vec3 laneLip = track.mute
                    ? glm::clamp(mutedColor + glm::vec3(0.05f), glm::vec3(0.0f), glm::vec3(1.0f))
                    : laneHighlight;
                pushRect(g_vertices, {layout.laneLeft, laneTop, layout.laneRight, laneBottom}, laneBody, layout.screenWidth, layout.screenHeight);
                pushRect(g_vertices, {layout.laneLeft, laneTop, layout.laneRight, laneTop + 6.0f}, laneLip, layout.screenWidth, layout.screenHeight);
                pushRect(g_vertices, {layout.laneLeft, laneTop, layout.laneLeft + 2.0f, laneBottom}, laneShadow, layout.screenWidth, layout.screenHeight);

                float handleSize = std::min(kTrackHandleSize, std::max(14.0f, layout.laneHeight));
                float half = handleSize * 0.5f;
                float centerX = layout.laneRight + kTrackHandleInset + half;
                pushRect(g_vertices,
                         {centerX - half, centerY - half, centerX + half, centerY + half},
                         selected ? selectedColor : laneLip,
                         layout.screenWidth,
                         layout.screenHeight);

                float top = centerY - layout.laneHalfH + 2.0f;
                float bottom = centerY + layout.laneHalfH - 2.0f;
                float lipBottom = std::min(bottom, top + std::clamp((bottom - top) * 0.18f, 6.0f, 12.0f));

                uint64_t clipLength = 0;
                if (track.midiTrackIndex >= 0 && track.midiTrackIndex < static_cast<int>(midi.tracks.size())) {
                    const MidiTrack& backend = midi.tracks[static_cast<size_t>(track.midiTrackIndex)];
                    if (!backend.clips.empty()) clipLength = backend.clips.front().length;
                }
                if (clipLength == 0 && track.compiledTickLength > 0) {
                    double bpm = daw.bpm.load(std::memory_order_relaxed);
                    if (bpm <= 0.0) bpm = 120.0;
                    double samplesPerTick = (static_cast<double>(daw.sampleRate) * 60.0) / (bpm * static_cast<double>(kTicksPerQuarter));
                    clipLength = static_cast<uint64_t>(std::round(static_cast<double>(track.compiledTickLength) * samplesPerTick));
                }
                if (clipLength == 0 && !track.source.empty()) clipLength = static_cast<uint64_t>(daw.sampleRate);

                float left = sampleToX(0, offsetSamples, windowSamples, layout.laneLeft, layout.laneRight);
                float right = sampleToX(clipLength, offsetSamples, windowSamples, layout.laneLeft, layout.laneRight);
                left = std::max(left, layout.laneLeft);
                right = std::min(right, layout.laneRight);
                if (right > left) {
                    glm::vec3 body = track.mute ? mutedColor : bodyColor;
                    glm::vec3 lip = track.mute ? glm::clamp(mutedColor + glm::vec3(0.05f), glm::vec3(0.0f), glm::vec3(1.0f)) : topColor;
                    if (!track.compileOk && !track.diagnostics.empty()) {
                        body = {0.40f, 0.12f, 0.10f};
                        lip = {0.62f, 0.20f, 0.16f};
                    } else if (selected) {
                        body = glm::clamp(selectedColor * 0.62f, glm::vec3(0.0f), glm::vec3(1.0f));
                        lip = glm::clamp(selectedColor + glm::vec3(0.08f), glm::vec3(0.0f), glm::vec3(1.0f));
                    }
                    Rect clipRect{left, top, right, bottom};
                    pushRect(g_vertices, clipRect, body, layout.screenWidth, layout.screenHeight);
                    pushRect(g_vertices, {left, top, right, lipBottom}, lip, layout.screenWidth, layout.screenHeight);
                    std::string label = (track.midiEditable ? "MIDA MIDI " : "MIDA ")
                        + std::to_string(track.compiledNoteCount) + "n";
                    pushText(g_vertices, left + 5.0f, top + 13.0f, label.c_str(), {0.90f, 1.0f, 0.92f}, layout.screenWidth, layout.screenHeight);
                }
            }
        }

        void processPendingAction(BaseSystem& baseSystem, MidaContext& mida, DawContext& daw, UIContext& ui) {
            if (!ui.active || ui.actionDelayFrames != 0 || ui.pendingActionType != "DawMidaTrack") return;
            int trackIndex = -1;
            if (!ui.pendingActionValue.empty()) {
                try {
                    trackIndex = std::stoi(ui.pendingActionValue);
                } catch (...) {
                    trackIndex = -1;
                }
            }
            if (ui.pendingActionKey == "add") {
                InsertTrackAt(baseSystem, static_cast<int>(daw.laneOrder.size()));
            } else if (ui.pendingActionKey == "remove") {
                if (trackIndex < 0 && !mida.tracks.empty()) trackIndex = static_cast<int>(mida.tracks.size()) - 1;
                RemoveTrackAt(baseSystem, trackIndex);
            } else if (trackIndex >= 0 && trackIndex < static_cast<int>(mida.tracks.size())) {
                MidaTrack& track = mida.tracks[static_cast<size_t>(trackIndex)];
                if (ui.pendingActionKey == "clear") {
                    track.source.clear();
                    track.sourceHash = 0;
                    track.clearPending = false;
                    mida.statusMessage = "Mida lane cleared";
                } else if (ui.pendingActionKey == "mute") {
                    track.mute = !track.mute;
                    if (baseSystem.midi && track.midiTrackIndex >= 0
                        && track.midiTrackIndex < static_cast<int>(baseSystem.midi->tracks.size())) {
                        baseSystem.midi->tracks[static_cast<size_t>(track.midiTrackIndex)].mute.store(track.mute, std::memory_order_relaxed);
                    }
                } else if (ui.pendingActionKey == "edit") {
                    openEditor(mida, trackIndex);
                }
                mida.selectedTrack = trackIndex;
            }
            ui.pendingActionType.clear();
            ui.pendingActionKey.clear();
            ui.pendingActionValue.clear();
            invalidateDawUi(baseSystem);
        }
    }

    namespace {
        bool insertTrackAt(BaseSystem& baseSystem, int laneIndex, bool midiEditable) {
            if (!baseSystem.mida || !baseSystem.daw) return false;
            MidaContext& mida = *baseSystem.mida;
            DawContext& daw = *baseSystem.daw;
            MidaTrack track;
            track.source = defaultTrackSource(baseSystem);
            track.midiEditable = midiEditable;
            mida.tracks.emplace_back(std::move(track));
            mida.trackCount = static_cast<int>(mida.tracks.size());
            int trackIndex = mida.trackCount - 1;
            if (laneIndex < 0) laneIndex = static_cast<int>(daw.laneOrder.size());
            if (laneIndex > static_cast<int>(daw.laneOrder.size())) laneIndex = static_cast<int>(daw.laneOrder.size());
            daw.laneOrder.insert(daw.laneOrder.begin() + laneIndex, {DawContext::kLaneMida, trackIndex});
            if (daw.selectedLaneIndex >= laneIndex && daw.selectedLaneIndex >= 0) daw.selectedLaneIndex += 1;
            daw.selectedLaneIndex = laneIndex;
            daw.selectedLaneType = DawContext::kLaneMida;
            daw.selectedLaneTrack = trackIndex;
            mida.selectedTrack = trackIndex;
            mida.statusMessage = midiEditable ? "Mida MIDI track added" : "Mida track added";
            invalidateDawUi(baseSystem);
            return true;
        }
    }

    bool InsertTrackAt(BaseSystem& baseSystem, int laneIndex) {
        return insertTrackAt(baseSystem, laneIndex, false);
    }

    bool InsertMidiTrackAt(BaseSystem& baseSystem, int laneIndex) {
        return insertTrackAt(baseSystem, laneIndex, true);
    }

    bool RemoveTrackAt(BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.mida || !baseSystem.daw) return false;
        MidaContext& mida = *baseSystem.mida;
        DawContext& daw = *baseSystem.daw;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(mida.tracks.size())) return false;

        int backendIndex = mida.tracks[static_cast<size_t>(trackIndex)].midiTrackIndex;
        if (backendIndex >= 0 && baseSystem.midi && backendIndex < static_cast<int>(baseSystem.midi->tracks.size())) {
            MidiTrackSystemLogic::RemoveTrackAt(baseSystem, backendIndex);
            for (auto& track : mida.tracks) {
                if (track.midiTrackIndex == backendIndex) track.midiTrackIndex = -1;
                else if (track.midiTrackIndex > backendIndex) track.midiTrackIndex -= 1;
            }
        }

        mida.tracks.erase(mida.tracks.begin() + trackIndex);
        mida.trackCount = static_cast<int>(mida.tracks.size());

        int removedLane = -1;
        for (auto it = daw.laneOrder.begin(); it != daw.laneOrder.end(); ) {
            if (it->type == DawContext::kLaneMida && it->trackIndex == trackIndex) {
                if (removedLane < 0) removedLane = static_cast<int>(std::distance(daw.laneOrder.begin(), it));
                it = daw.laneOrder.erase(it);
            } else {
                if (it->type == DawContext::kLaneMida && it->trackIndex > trackIndex) it->trackIndex -= 1;
                ++it;
            }
        }
        for (int i = 0; i < static_cast<int>(mida.tracks.size()); ++i) {
            if (mida.tracks[static_cast<size_t>(i)].midiTrackIndex >= 0 && baseSystem.midi
                && mida.tracks[static_cast<size_t>(i)].midiTrackIndex < static_cast<int>(baseSystem.midi->trackNames.size())) {
                baseSystem.midi->trackNames[static_cast<size_t>(mida.tracks[static_cast<size_t>(i)].midiTrackIndex)] = backendTrackName(i);
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
        if (mida.selectedTrack == trackIndex) mida.selectedTrack = -1;
        else if (mida.selectedTrack > trackIndex) mida.selectedTrack -= 1;
        if (mida.editorTrack == trackIndex) closeEditor(mida);
        else if (mida.editorTrack > trackIndex) mida.editorTrack -= 1;
        daw.selectedMidaTrack = mida.selectedTrack;
        mida.statusMessage = "Mida track removed";
        invalidateDawUi(baseSystem);
        return true;
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
        invalidateDawUi(baseSystem);
        return true;
    }

    int BackendMidiTrackIndexForTrack(const BaseSystem& baseSystem, int trackIndex) {
        if (!baseSystem.mida) return -1;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(baseSystem.mida->tracks.size())) return -1;
        return baseSystem.mida->tracks[static_cast<size_t>(trackIndex)].midiTrackIndex;
    }

    bool CompileMidiClipSource(BaseSystem& baseSystem, MidiClip& clip, std::string* errorMessage) {
        if (!clip.hasMidaSource && clip.midaSource.empty()) {
            clip.midaSource = midiClipToMidaSource(baseSystem, clip);
            clip.hasMidaSource = true;
        }
        return compileMidaSourceToMidiClip(baseSystem, clip.midaSource, clip, true, errorMessage);
    }

    bool RegenerateMidiClipSource(BaseSystem& baseSystem, int trackIndex, int clipIndex) {
        if (!baseSystem.midi) return false;
        MidiContext& midi = *baseSystem.midi;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return false;
        MidiTrack& track = midi.tracks[static_cast<size_t>(trackIndex)];
        if (clipIndex < 0 || clipIndex >= static_cast<int>(track.clips.size())) return false;
        MidiClip& clip = track.clips[static_cast<size_t>(clipIndex)];
        if (!track.midaMidiTrack && !clip.hasMidaSource) return false;

        clip.midaSource = midiClipToMidaSource(baseSystem, clip);
        clip.hasMidaSource = true;
        clip.midaSourceDirty = false;
        if (baseSystem.mida) {
            baseSystem.mida->statusMessage = "Mida source regenerated";
        }
        invalidateDawUi(baseSystem);
        return true;
    }

    bool OpenMidiClipEditor(BaseSystem& baseSystem, int trackIndex, int clipIndex) {
        if (!baseSystem.mida || !baseSystem.midi) return false;
        MidiContext& midi = *baseSystem.midi;
        MidaContext& mida = *baseSystem.mida;
        if (trackIndex < 0 || trackIndex >= static_cast<int>(midi.tracks.size())) return false;
        MidiTrack& track = midi.tracks[static_cast<size_t>(trackIndex)];
        if (clipIndex < 0 || clipIndex >= static_cast<int>(track.clips.size())) return false;
        MidiClip& clip = track.clips[static_cast<size_t>(clipIndex)];
        if (!track.midaMidiTrack && !clip.hasMidaSource) return false;

        if (!clip.hasMidaSource || clip.midaSource.empty() || clip.midaSourceDirty) {
            if (!RegenerateMidiClipSource(baseSystem, trackIndex, clipIndex)) return false;
        }

        mida.editorOpen = true;
        mida.editorTrack = -1;
        mida.editorMidiTrack = trackIndex;
        mida.editorMidiClip = clipIndex;
        mida.editorBuffer = clip.midaSource;
        mida.selectedTrack = -1;
        mida.statusMessage = "Editing Mida MIDI clip";
        midi.selectedTrackIndex = trackIndex;
        midi.selectedClipTrack = trackIndex;
        midi.selectedClipIndex = clipIndex;
        if (baseSystem.daw) {
            baseSystem.daw->selectedClipTrack = -1;
            baseSystem.daw->selectedClipIndex = -1;
        }
        invalidateDawUi(baseSystem);
        return true;
    }

    void UpdateMidaInterpreter(BaseSystem& baseSystem, std::vector<Entity>&, float, PlatformWindowHandle win) {
        if (!baseSystem.mida || !baseSystem.daw) return;
        if (!getRegistryBool(baseSystem, "MidaInterpreterEnabled", true)) return;

        MidaContext& mida = *baseSystem.mida;
        DawContext& daw = *baseSystem.daw;
        mida.trackCount = static_cast<int>(mida.tracks.size());

        if (baseSystem.ui) {
            processPendingAction(baseSystem, mida, daw, *baseSystem.ui);
        }

        if (!isMenuLevel(baseSystem) && baseSystem.midi && baseSystem.audio && baseSystem.midi->initialized) {
            for (int i = 0; i < static_cast<int>(mida.tracks.size()); ++i) {
                compileTrack(baseSystem, mida, mida.tracks[static_cast<size_t>(i)], i);
            }
        }
        daw.selectedMidaTrack = mida.selectedTrack;

        if (!baseSystem.ui || !baseSystem.renderer || !baseSystem.world || !baseSystem.level || !baseSystem.renderBackend || !win) return;
        UIContext& ui = *baseSystem.ui;
        if (!ui.active || ui.loadingActive) return;
        if (baseSystem.midi && baseSystem.midi->pianoRollActive) return;
        if (!DawLaneTimelineSystemLogic::hasDawUiWorld(*baseSystem.level)) return;

        RendererContext& renderer = *baseSystem.renderer;
        WorldContext& world = *baseSystem.world;
        auto& renderBackend = *baseSystem.renderBackend;
        const auto layout = DawLaneTimelineSystemLogic::ComputeLaneLayout(baseSystem, daw, win);
        std::vector<int> midaLaneIndex = buildMidaLaneIndex(daw, static_cast<int>(mida.tracks.size()));
        g_vertices.clear();

        Rect saveBtn;
        Rect cancelBtn;
        editorButtons(layout, saveBtn, cancelBtn);
        if (mida.editorOpen) {
            updateEditorInput(baseSystem, mida, ui, win, saveBtn, cancelBtn);
            renderEditor(mida, layout, saveBtn, cancelBtn);
            uploadAndDraw(renderer, world, renderBackend);
            return;
        }

        bool allowInput = !daw.exportMenuOpen && !daw.settingsMenuOpen;
        if (allowInput && ui.uiLeftPressed && !ui.consumeClick) {
            int trackIndex = midaLaneUnderCursor(midaLaneIndex,
                                                static_cast<float>(ui.cursorY),
                                                layout.startY,
                                                layout.laneHalfH,
                                                layout.rowSpan);
            bool inTimeArea = ui.cursorX >= layout.laneLeft && ui.cursorX <= layout.laneRight;
            bool inTrackControlStrip = ui.cursorX <= kTrackControlStripRight;
            if (trackIndex >= 0 && inTimeArea && !inTrackControlStrip) {
                const double now = PlatformInput::GetTimeSeconds();
                bool doubleClick = (trackIndex == g_lastLaneClickTrack
                    && g_lastLaneClickTime > 0.0
                    && (now - g_lastLaneClickTime) <= kDoubleClickSeconds);
                g_lastLaneClickTrack = trackIndex;
                g_lastLaneClickTime = now;
                mida.selectedTrack = trackIndex;
                daw.selectedLaneType = DawContext::kLaneMida;
                daw.selectedLaneTrack = trackIndex;
                if (trackIndex < static_cast<int>(midaLaneIndex.size())) {
                    daw.selectedLaneIndex = midaLaneIndex[static_cast<size_t>(trackIndex)];
                }
                if (doubleClick) openEditor(mida, trackIndex);
                ui.consumeClick = true;
            }
        }

        if (!mida.tracks.empty()) renderMidaLanes(baseSystem, mida, daw, layout, midaLaneIndex);
        uploadAndDraw(renderer, world, renderBackend);
    }
}
