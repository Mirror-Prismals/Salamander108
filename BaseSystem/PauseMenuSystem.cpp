#pragma once

#include "Host/PlatformInput.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace PauseMenuSystemLogic {
    namespace {
        std::string toLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::string registryString(const BaseSystem& baseSystem, const char* key, const char* fallback) {
            if (!baseSystem.registry) return fallback;
            auto it = baseSystem.registry->find(key);
            if (it == baseSystem.registry->end() || !std::holds_alternative<std::string>(it->second)) {
                return fallback;
            }
            return std::get<std::string>(it->second);
        }

        bool isMenuLevel(const BaseSystem& baseSystem) {
            const std::string level = toLowerCopy(registryString(baseSystem, "level", ""));
            return level == "menu" || level == "menu_level";
        }

        bool projectionModeIsPanini(const std::string& mode) {
            const std::string lowered = toLowerCopy(mode);
            return lowered == "panini" || lowered == "panini_projection" || lowered == "paniniprojection";
        }

        bool paniniEnabled(const BaseSystem& baseSystem) {
            return projectionModeIsPanini(registryString(baseSystem, "ProjectionMode", "rectilinear"));
        }

        void togglePaniniProjection(BaseSystem& baseSystem) {
            if (!baseSystem.registry) return;
            (*baseSystem.registry)["ProjectionMode"] = paniniEnabled(baseSystem)
                ? std::string("rectilinear")
                : std::string("panini");
        }

        int findMirrorIndex(const MirrorContext& mirror, const std::string& name) {
            for (int i = 0; i < static_cast<int>(mirror.mirrors.size()); ++i) {
                if (mirror.mirrors[static_cast<size_t>(i)].name == name) return i;
            }
            return -1;
        }

        bool setActiveMirror(BaseSystem& baseSystem, const std::string& name) {
            if (!baseSystem.mirror) return false;
            MirrorContext& mirror = *baseSystem.mirror;
            const int index = findMirrorIndex(mirror, name);
            if (index < 0) return false;
            if (mirror.activeMirrorIndex != index) {
                mirror.activeMirrorIndex = index;
                mirror.expanded = false;
                mirror.expandedMirrorIndex = -1;
            }
            mirror.deviceMirrorIndex[-1] = index;
            return true;
        }

        void invalidateUiCaches(BaseSystem& baseSystem) {
            if (baseSystem.ui) baseSystem.ui->buttonCacheBuilt = false;
            if (baseSystem.font) baseSystem.font->textCacheBuilt = false;
            if (baseSystem.daw) baseSystem.daw->uiCacheBuilt = false;
        }

        void clearPauseAction(UIContext& ui) {
            if (ui.pendingActionType != "PauseMenu") return;
            ui.pendingActionType.clear();
            ui.pendingActionKey.clear();
            ui.pendingActionValue.clear();
            ui.actionDelayFrames = 0;
        }

        void restorePreviousMirror(BaseSystem& baseSystem, int previousMirrorIndex) {
            if (!baseSystem.mirror) return;
            MirrorContext& mirror = *baseSystem.mirror;
            if (previousMirrorIndex < 0 || previousMirrorIndex >= static_cast<int>(mirror.mirrors.size())) {
                previousMirrorIndex = 0;
            }
            if (previousMirrorIndex < 0 || previousMirrorIndex >= static_cast<int>(mirror.mirrors.size())) return;
            if (mirror.activeMirrorIndex != previousMirrorIndex) {
                mirror.activeMirrorIndex = previousMirrorIndex;
                mirror.expanded = false;
                mirror.expandedMirrorIndex = -1;
            }
            mirror.deviceMirrorIndex[-1] = previousMirrorIndex;
        }

        void closePauseMenu(BaseSystem& baseSystem, bool restoreMirror) {
            if (!baseSystem.ui) return;
            UIContext& ui = *baseSystem.ui;
            const int previousMirror = ui.pausePreviousMirrorIndex;
            ui.pauseMenuActive = false;
            ui.pauseMenuScreen = "main";
            ui.pausePreviousMirrorIndex = -1;
            ui.active = false;
            ui.fullscreenActive = false;
            ui.activeWorldIndex = -1;
            ui.activeInstanceID = -1;
            ui.uiLeftDown = false;
            ui.uiLeftPressed = false;
            ui.uiLeftReleased = false;
            clearPauseAction(ui);
            if (restoreMirror) {
                restorePreviousMirror(baseSystem, previousMirror);
            }
            invalidateUiCaches(baseSystem);
        }

        void openPauseMenu(BaseSystem& baseSystem) {
            if (!baseSystem.ui || !baseSystem.mirror) return;
            UIContext& ui = *baseSystem.ui;
            MirrorContext& mirror = *baseSystem.mirror;
            const int pauseMirror = findMirrorIndex(mirror, "PauseMirror");
            if (pauseMirror < 0) return;
            ui.pauseMenuActive = true;
            ui.pauseMenuScreen = "main";
            ui.pausePreviousMirrorIndex = mirror.activeMirrorIndex;
            ui.active = true;
            ui.fullscreenActive = false;
            ui.activeWorldIndex = -1;
            ui.activeInstanceID = -1;
            ui.consumeClick = true;
            ui.uiLeftDown = false;
            ui.uiLeftPressed = false;
            ui.uiLeftReleased = false;
            mirror.activeMirrorIndex = pauseMirror;
            mirror.expanded = false;
            mirror.expandedMirrorIndex = -1;
            mirror.deviceMirrorIndex[-1] = pauseMirror;
            invalidateUiCaches(baseSystem);
        }

        void showPauseScreen(BaseSystem& baseSystem, const std::string& screen) {
            if (!baseSystem.ui) return;
            UIContext& ui = *baseSystem.ui;
            const std::string mirrorName = (screen == "settings") ? "PauseSettingsMirror" : "PauseMirror";
            if (!setActiveMirror(baseSystem, mirrorName)) return;
            ui.pauseMenuScreen = screen;
            ui.active = true;
            ui.fullscreenActive = false;
            ui.activeWorldIndex = -1;
            ui.activeInstanceID = -1;
            invalidateUiCaches(baseSystem);
        }

        void updatePaniniLabel(BaseSystem& baseSystem) {
            if (!baseSystem.level) return;
            const bool enabled = paniniEnabled(baseSystem);
            const std::string label = enabled ? "PANINI: ON" : "PANINI: OFF";
            bool changed = false;
            for (Entity& world : baseSystem.level->worlds) {
                if (world.name != "PauseSettingsWorld") continue;
                for (EntityInstance& inst : world.instances) {
                    if (inst.controlId != "pause_settings_panini" || inst.controlRole != "label") continue;
                    if (inst.text != label) {
                        inst.text = label;
                        changed = true;
                    }
                }
            }
            if (changed && baseSystem.font) {
                baseSystem.font->textCacheBuilt = false;
            }
        }

        void processPauseAction(BaseSystem& baseSystem, PlatformWindowHandle win) {
            if (!baseSystem.ui) return;
            UIContext& ui = *baseSystem.ui;
            if (ui.actionDelayFrames != 0 || ui.pendingActionType != "PauseMenu") return;

            const std::string action = ui.pendingActionKey;
            clearPauseAction(ui);

            if (action == "settings") {
                showPauseScreen(baseSystem, "settings");
            } else if (action == "settings_back") {
                showPauseScreen(baseSystem, "main");
            } else if (action == "toggle_panini") {
                togglePaniniProjection(baseSystem);
                updatePaniniLabel(baseSystem);
            } else if (action == "quit_to_title") {
                closePauseMenu(baseSystem, true);
                if (baseSystem.ui) {
                    baseSystem.ui->levelSwitchPending = true;
                    baseSystem.ui->levelSwitchTarget = "menu";
                }
            } else if (action == "quit_game") {
                closePauseMenu(baseSystem, false);
                PlatformInput::SetWindowShouldClose(win, true);
            }
        }
    }

    void UpdatePauseMenu(BaseSystem& baseSystem, std::vector<Entity>& prototypes, float dt, PlatformWindowHandle win) {
        (void)prototypes;
        (void)dt;
        if (!baseSystem.ui || !win) return;
        UIContext& ui = *baseSystem.ui;

        const bool escapeDown = PlatformInput::IsKeyDown(win, PlatformInput::Key::Escape);
        const bool escapePressed = escapeDown && !ui.pauseEscapeDownLast;
        ui.pauseEscapeDownLast = escapeDown;

        if (isMenuLevel(baseSystem)) {
            if (escapePressed) {
                PlatformInput::SetWindowShouldClose(win, true);
            }
            return;
        }

        if (ui.pauseMenuActive) {
            ui.active = true;
            ui.fullscreenActive = false;
            ui.activeWorldIndex = -1;
            ui.activeInstanceID = -1;

            if (escapePressed) {
                closePauseMenu(baseSystem, true);
                return;
            }

            processPauseAction(baseSystem, win);
            if (ui.pauseMenuActive && ui.pauseMenuScreen == "settings") {
                updatePaniniLabel(baseSystem);
            }
            return;
        }

        if (escapePressed && !ui.active && !ui.loadingActive) {
            openPauseMenu(baseSystem);
        }
    }
}
